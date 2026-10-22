//
// Copyright (c) 2018-2020 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//

#pragma once

#include <cstddef>
#include <cstdint>
#include <list>
#include <map>
#include <optional>
#include <stdexcept>
#include <type_traits>
#include <vector>

#include "ampere/sc/sc.h"
#include "ampere/sc/value.h"
#include "ampere/uarch_common/indexer.h"
#include "ampere/uarch_common/crp/cache_replacement_policy.h"
#include "ampere/uarch_common/crp/lru.h"

namespace ampere::uarch_common {

using namespace ampere::sc;

class CacheConfiguration : public Module
{
public:
    explicit CacheConfiguration(const sc_core::sc_module_name& name);

    size_t get_num_sets() const { return num_sets; }
    size_t get_num_ways() const { return num_ways; }
    unsigned get_line_bits() const { return line_bits; }
    unsigned get_set_bits() const { return set_bits; }
    unsigned get_index_bits() const { return line_bits + set_bits; }

private:
    Param<std::optional<NonZero<size_t>>> size_param{"size", "cache size in bytes", {}};
    Param<std::optional<sc::PowerOfTwo<size_t>>> sets_param{"sets", "number of sets in the cache", {}};
    Param<sc::NonZero<size_t>> ways_param{"ways", "number of ways in the cache", 4};
    Param<sc::PowerOfTwo<size_t>> line_size_param{"line_size", "cache line size in bytes", 64};

protected:
    size_t size;
    size_t num_sets;
    size_t num_ways;
    size_t line_size;
    unsigned line_bits;
    unsigned set_bits;
};

/*
 * UnmanagedCache handles parameter-parsing, structure-sizing, and very basic
 * set/entry lookup while leaving details such as replacement policy and
 * tag/index computation to derived classes.
 *
 * Note that the parameters specified (line_size, ways, etc.)are not directly
 * used by UnmanagedCache other than to specify the number of sets and ways.
 * Their computed/sanitized values are, however, recorded as protected member
 * variables for use by deriving classes.
 */
template<typename Tag, typename Value>
class UnmanagedCache : public CacheConfiguration
{
public:
    using value_type = Value;
    using Set = std::map<Tag, Value>;

    explicit UnmanagedCache(const sc_core::sc_module_name& name);

protected:
    std::optional<typename Set::iterator> find_entry(Index index, Tag tag);
    std::optional<typename Set::const_iterator> find_entry(Index index, Tag tag) const;

    std::vector<Set>& get_sets() { return sets; }
    const std::vector<Set>& get_sets() const { return sets; }

    unsigned set_number(Index index) const { return index & (num_sets - 1); }
    Set& get_set(Index index);
    const Set& get_set(Index index) const;

    inline bool set_full(const Set& set) const { return set.size() == num_ways; }

private:
    std::vector<Set> sets;
};

namespace CacheEntry {

template<typename Value>
struct Way
{
    Way() = delete;

    explicit Way(const Value& value) : value(value) {}
    explicit Way(Value&& value) : value(std::move(value)) {}

    Way(size_t way, const Value& value) : way{way}, value(value) {}
    Way(size_t way, Value&& value) : way{way}, value(std::move(value)) {}

    Way& operator=(const Value& value)
    {
        this->value = value;
        return *this;
    }
    Way& operator=(Value&& value)
    {
        this->value = std::move(value);
        return *this;
    }

    size_t way{0};
    Value value;
};

template<typename T>
auto default_entry_value()
{
    if constexpr (std::is_default_constructible_v<T>)
        return T{};
    else
        return std::nullopt;
}

} // namespace CacheEntry

template<typename Value>
class CacheResult
{
    using EntryType = CacheEntry::Way<std::remove_cv_t<Value>>;

public:
    using value_type = Value;

    CacheResult() = default;
    CacheResult(std::nullopt_t) {}

    explicit CacheResult(EntryType& value) : _value(&value) {}
    explicit CacheResult(EntryType&& value) : owned(true), _value(new EntryType(std::move(value))) {}

    CacheResult(const CacheResult& other)
        : owned(other.owned), _value(other.owned ? new EntryType(*other._value) : other._value)
    {
    }
    CacheResult(CacheResult&& other) noexcept : owned(other.owned), _value(other._value) { other.owned = false; }

    ~CacheResult() { cleanup(); }

    CacheResult& operator=(const CacheResult& other)
    {
        if (&other != this) {
            cleanup();
            owned = other.owned;
            _value = owned ? new EntryType(*other._value) : other._value;
        }
        return *this;
    }
    CacheResult& operator=(CacheResult&& other) noexcept
    {
        cleanup();
        owned = other.owned;
        other.owned = false;
        _value = other._value;
        return *this;
    }
    CacheResult& operator=(EntryType& value)
    {
        cleanup();
        owned = false;
        _value = value;
        return *this;
    }
    CacheResult& operator=(EntryType&& value)
    {
        cleanup();
        owned = true;
        _value = new EntryType(std::move(value));
        return *this;
    }

    bool has_value() const { return _value; }
    Value& value() const
    {
        if (!has_value())
            throw std::bad_optional_access();
        return _value->value;
    }
    size_t way() const { return _value->way; }

    explicit operator bool() const { return has_value(); }
    Value& operator*() const noexcept { return _value->value; }
    Value* operator->() const noexcept { return &_value->value; }

private:
    bool owned{false};
    EntryType* _value{nullptr};

    void cleanup()
    {
        if (owned)
            delete _value;
    }
};

/*
 * ManagedCache provides a nearly-functional cache, exposed primarily through
 * its lookup() and refill() methods.
 *
 * A user must implement the pure virtual compute_set and compute_tag methods
 * for their particular types (the Cache class provides a implementation usable
 * for many applications which cache based on Address).
 */
template<typename Address, typename Value, typename Tag = uint64_t>
class ManagedCache : public UnmanagedCache<Tag, typename CacheEntry::Way<Value>>
{
public:
    using Entry = typename CacheEntry::Way<Value>;
    using Result = CacheResult<Value>;
    using ConstResult = CacheResult<const Value>;
    using base_type = UnmanagedCache<Tag, Entry>;
    using value_type = Value;

    explicit ManagedCache(const sc_core::sc_module_name& name)
        : ManagedCache(name, CacheEntry::default_entry_value<Value>())
    {
    }

    template<typename U>
    ManagedCache(const sc_core::sc_module_name& name, U&& perfect_value)
        : UnmanagedCache<Tag, Entry>(name), perfect_value{std::forward<U>(perfect_value)}
    {
        crp->init(this->num_sets, this->num_ways);

        AMPERE_ASSERT_ERROR(this->perfect_value || !enable_perfect_mode,
                            "enable_perfect_mode unsupported for '{}' (no perfect value provided)",
                            name);
    }

    using Set = typename UnmanagedCache<Tag, Entry>::Set;

    Submodule<CacheReplacementPolicy, LRU> crp{"cache_replacement_policy"};
    ConstResult find(Address address) const;
    Result lookup(Txn transaction,
                  Address address,
                  bool update_lru = true,
                  bool suppress_event = false,
                  bool prefetched = false,
                  uint64_t event_addr = 0x0);
    void update_value(Txn transaction, Address address, const Value& value, bool suppress_event = false);
    bool would_fill_without_victim(Address address) const;
    ConstResult next_victim(Address address) const;
    Result refill(
        Txn transaction, Address address, const Value& value, bool suppress_event = false, uint64_t event_addr = 0x0);
    Result invalidate(Txn transaction, Address address, bool suppress_event = false);
    void clear();

    // Override compute_set and compute_tag for custom set/tag calculations
    virtual Index compute_set(Address address) const = 0;
    virtual Tag compute_tag(Address address) const = 0;
    std::list<Value> set_contents(Address address) const;

    // FIXME: LSU can access it to override PA tag check for a perfect cache, make separate DL1 and make private
    Param<bool> enable_perfect_mode{"enable_perfect_mode", "Force all lookups to hit", false};

protected:
    Param<bool> crp_victim_always{"crp_victim_always",
                                  "Use the cache replacement policy's chosen victim, even when the the set is not full",
                                  false};
    Param<bool> crp_update_on_fill{
        "crp_update_on_fill", "Call the cache replacement policy's touch() function on a cache fill", true};
    Param<bool> invalidate_duplicates_before_fill{
        "invalidate_duplicates_before_fill", "Fills will overwrite entry with a matching tag", false};
    Param<bool> invalidate_duplicates_after_fill{
        "invalidate_duplicates_after_fill",
        "Fills pick a victim normally, then invalidate any other matching tags",
        false};
    Param<bool> enable_antiperfect_mode{"enable_antiperfect_mode", "Force all lookups to miss", false};
    Param<bool> randomize_initial_fill{"randomize_initial_fill",
                                       "Randomize which way gets filled if there are more than one empty ways in a set",
                                       false};

    using UnmanagedCache<Tag, Entry>::find_entry;
    std::optional<typename base_type::Set::iterator> find_entry(Address address);
    std::optional<typename base_type::Set::const_iterator> find_entry(Address address) const;
    Set& find_set(Address address);
    const Set& find_set(Address address) const;
    typename Set::const_iterator find_victim(Index index) const;
    size_t find_empty_way(Index index) const;
    std::list<Value> set_contents_indexed(Index index) const;
    virtual bool perfect_hit(Address address) const;

    std::optional<CacheEntry::Way<Value>> perfect_value;

    Event ev_access{"access", "Cache access"};
    Event ev_refill{"refill", "Cache refill"};
    Event ev_eviction{"eviction", "Cache eviction"};
    Event ev_fill_empty{"fill", "Cache fill in empty way"};
    Event ev_invalidate{"invalidate", "Invalidate a cache entry"};
    EventData<bool> hit_data{"hit", "Cache hit"};
    EventData<bool> ev_prefetch{"prefetch", "Initiated by prefetcher"};
    EventData<uint64_t> ev_physaddr{"phys_line", "Physical address"};
    EventData<Index> set_data{"set", "Cache set"};
    EventData<unsigned> way_data{"way", "Cache way"};
};

/*
 * Cache defines compute_set and compute_tag methods to be used with Address
 * objects
 */
template<typename Address, typename Value, typename Tag = uint64_t>
class Cache : public ManagedCache<Address, Value, Tag>
{
public:
    using Result = CacheResult<Value>;
    using ConstResult = CacheResult<const Value>;

    Param<size_t> tag_bits_param{"tag_bits", "tag bits used", 0};

    explicit Cache(const sc_core::sc_module_name& name) : Cache(name, CacheEntry::default_entry_value<Value>()) {}

    template<typename U>
    Cache(const sc_core::sc_module_name& name, U&& perfect_value)
        : ManagedCache<Address, Value, Tag>(name, std::forward<U>(perfect_value))
    {
        if (tag_bits_param)
            ms_tag_bit = tag_bits_param + this->set_bits + this->line_bits - 1;
        else
            ms_tag_bit = 63;
    }

    Index compute_set(Address address) const override;
    Tag compute_tag(Address address) const override;

    using ManagedCache<Address, Value, Tag>::get_sets;

protected:
    unsigned ms_tag_bit;
};

unsigned compute_num_address_bits(size_t value);

template<typename Tag, typename Value>
UnmanagedCache<Tag, Value>::UnmanagedCache(const sc_core::sc_module_name& name) : CacheConfiguration(name)
{
    sets.resize(num_sets);
}

template<typename Tag, typename Value>
typename UnmanagedCache<Tag, Value>::Set& UnmanagedCache<Tag, Value>::get_set(Index index)
{
    return sets[set_number(index)];
}

template<typename Tag, typename Value>
const typename UnmanagedCache<Tag, Value>::Set& UnmanagedCache<Tag, Value>::get_set(Index index) const
{
    return sets.at(set_number(index));
}

// Return a std::optional object. Hit/miss can be determined by checking the return value
// in a conditional and if it is a hit then the cache value can be access by dereferencing
// the object.
template<typename Tag, typename Value>
std::optional<typename UnmanagedCache<Tag, Value>::Set::iterator> UnmanagedCache<Tag, Value>::find_entry(Index index,
                                                                                                         Tag tag)
{
    auto& set = this->get_set(index);
    if (auto way = set.find(tag); way != set.end())
        return way;

    return std::nullopt;
}

template<typename Tag, typename Value>
std::optional<typename UnmanagedCache<Tag, Value>::Set::const_iterator> UnmanagedCache<Tag, Value>::find_entry(
    Index index, Tag tag) const
{
    auto& set = this->get_set(index);
    if (auto way = set.find(tag); way != set.end())
        return way;

    return std::nullopt;
}

template<typename Address, typename Value, typename Tag>
std::optional<typename ManagedCache<Address, Value, Tag>::Set::iterator> ManagedCache<Address, Value, Tag>::find_entry(
    Address address)
{
    return this->find_entry(compute_set(address), compute_tag(address));
}

template<typename Address, typename Value, typename Tag>
std::optional<typename ManagedCache<Address, Value, Tag>::Set::const_iterator>
ManagedCache<Address, Value, Tag>::find_entry(Address address) const
{
    return this->find_entry(compute_set(address), compute_tag(address));
}

template<typename Address, typename Value, typename Tag>
CacheResult<const Value> ManagedCache<Address, Value, Tag>::find(Address address) const
{
    if (auto entry = find_entry(address); entry)
        return CacheResult<const Value>(const_cast<CacheEntry::Way<Value>&>((*entry)->second));

    return std::nullopt;
}

template<typename Address, typename Value, typename Tag>
CacheResult<Value> ManagedCache<Address, Value, Tag>::lookup(
    Txn transaction, Address address, bool update_lru, bool suppress_event, bool prefetched, uint64_t event_addr)
{
    if (!enable_antiperfect_mode) {
        auto index = compute_set(address);
        auto tag = compute_tag(address);

        if (auto hit = find_entry(index, tag); hit) {
            if (!suppress_event) {
                ev_access(transaction,
                          hit_data(true),
                          set_data(this->set_number(index)),
                          way_data((*hit)->second.way),
                          address,
                          ev_prefetch(prefetched),
                          ev_physaddr.if_set(event_addr));
            }
            if (update_lru) {
                crp->touch(compute_set(address), (*hit)->second.way, CacheReplacementPolicy::TouchOp::LOOKUP);
            }
            return CacheResult<Value>((*hit)->second);
        } else if (perfect_hit(address)) {
            // Only fall back on perfect mode if necessary so we can return the actual refilled value when resident
            if (!suppress_event)
                ev_access(
                    transaction, hit_data(true), address, ev_prefetch(prefetched), ev_physaddr.if_set(event_addr));
            if constexpr (std::is_copy_constructible_v<Value>) {
                CacheEntry::Way<Value> value(perfect_value->value);
                return CacheResult<Value>(std::move(value));
            } else {
                // Value modifications will be visible across calls, potentially unexpected
                throw std::logic_error("Cannot return mutable reference to perfect value");
            }
        }
    }

    if (!suppress_event)
        ev_access(transaction, hit_data(false), address, ev_prefetch(prefetched), ev_physaddr.if_set(event_addr));

    return std::nullopt;
}

template<typename Address, typename Value, typename Tag>
void ManagedCache<Address, Value, Tag>::update_value(Txn transaction,
                                                     Address address,
                                                     const Value& value,
                                                     bool suppress_event)
{
    auto hit = find_entry(address);

    if (!suppress_event)
        ev_access(transaction, hit_data(perfect_hit(address) || (bool)hit), address);

    if (hit) {
        crp->touch(compute_set(address), (*hit)->second.way, CacheReplacementPolicy::TouchOp::UPDATE);
        (*hit)->second.value = value;
    }
}

template<typename Address, typename Value, typename Tag>
ManagedCache<Address, Value, Tag>::Result ManagedCache<Address, Value, Tag>::invalidate(Txn transaction,
                                                                                        Address address,
                                                                                        bool suppress_event)
{
    Result invalidated_value;

    auto tag = compute_tag(address);
    auto index = compute_set(address);
    auto& set = this->get_set(index);

    if (auto way = set.find(tag); way != set.end()) {
        crp->invalidate(index, way->second.way);
        auto invalidated = way->second;
        invalidated_value = std::move(invalidated);
        if (!suppress_event)
            ev_invalidate(set_data(this->set_number(index)), way_data(way->second.way));

        set.erase(way);
    }

    return invalidated_value;
}

template<typename Address, typename Value, typename Tag>
bool ManagedCache<Address, Value, Tag>::would_fill_without_victim(Address address) const
{
    auto& set = this->get_set(compute_set(address));
    auto way = set.find(compute_tag(address));

    return way != set.end() || !this->set_full(set);
}

template<typename Address, typename Value, typename Tag>
ManagedCache<Address, Value, Tag>::ConstResult ManagedCache<Address, Value, Tag>::next_victim(Address address) const
{
    size_t index = compute_set(address);
    auto& set = this->get_set(index);

    if (this->set_full(set) || crp_victim_always) {
        if (auto victim = find_victim(index); victim != set.end())
            return CacheResult<const Value>(const_cast<CacheEntry::Way<Value>&>(victim->second));
    }

    return {};
}

template<typename Address, typename Value, typename Tag>
ManagedCache<Address, Value, Tag>::Result ManagedCache<Address, Value, Tag>::refill(
    Txn transaction, Address address, const Value& value, bool suppress_event, uint64_t event_addr)
{
    Result victim_value;
    auto tag = compute_tag(address);
    size_t index = compute_set(address);
    auto& set = this->get_set(index);
    size_t way = this->num_ways;

    // Some caches will fill over top of any entry which matches the tag being filled.
    // Here we invalidate any matching entry, and then the check below will see there there
    // is space in this set and use the invalid way.
    if (invalidate_duplicates_before_fill)
        invalidate(transaction, address);

    if (this->set_full(set) || crp_victim_always) {
        auto victim = find_victim(index);

        if (victim == set.end()) {
            AMPERE_ASSERT(crp_victim_always, "Victimized way not valid, but crp_victim_always was not set");
            way = this->crp->victim(index);
        } else {
            way = victim->second.way;

            if (!suppress_event)
                ev_eviction(transaction, set_data(this->set_number(index)), way_data(way));
            auto victimized = victim->second;
            victim_value = std::move(victimized);
            this->crp->invalidate(index, way);
            set.erase(victim);
        }
    } else {
        way = this->find_empty_way(index);
        if (!suppress_event)
            ev_fill_empty(set_data(this->set_number(index)), way_data(way));
    }

    // Some caches pick a victim way first, and then later invalidate any duplicate tags.
    if (invalidate_duplicates_after_fill)
        invalidate(transaction, address);

    // There should never be duplicate tags. We need an exception for perfect mode because find_entry always returns
    // true.
    AMPERE_ASSERT(perfect_hit(address) || !find_entry(index, tag), "set {} contains a duplicate", index);

    if (crp_update_on_fill)
        crp->touch(index, way, CacheReplacementPolicy::TouchOp::REFILL);

    if (!suppress_event)
        ev_refill(transaction, ev_physaddr(event_addr), set_data(this->set_number(index)), way_data(way));

    set.insert_or_assign(tag, Entry(way, value));
    return victim_value;
}

template<typename Address, typename Value, typename Tag>
typename UnmanagedCache<Tag, typename CacheEntry::Way<Value>>::Set& ManagedCache<Address, Value, Tag>::find_set(
    Address address)
{
    return this->get_set(compute_set(address));
}

template<typename Address, typename Value, typename Tag>
const typename UnmanagedCache<Tag, typename CacheEntry::Way<Value>>::Set& ManagedCache<Address, Value, Tag>::find_set(
    Address address) const
{
    return this->get_set(compute_set(address));
}

template<typename Address, typename Value, typename Tag>
std::list<Value> ManagedCache<Address, Value, Tag>::set_contents_indexed(Index index) const
{
    std::list<Value> values;
    auto set = this->get_set(index);

    for (const auto& line : set)
        values.emplace_back(line.second.value);

    return values;
}

template<typename Address, typename Value, typename Tag>
std::list<Value> ManagedCache<Address, Value, Tag>::set_contents(Address address) const
{
    return set_contents_indexed(compute_set(address));
}

template<typename Address, typename Value, typename Tag>
bool ManagedCache<Address, Value, Tag>::perfect_hit(Address address) const
{
    return enable_perfect_mode && perfect_value;
}

template<typename Address, typename Value, typename Tag>
typename ManagedCache<Address, Value, Tag>::Set::const_iterator ManagedCache<Address, Value, Tag>::find_victim(
    Index index) const
{
    const auto& set = this->get_set(index);
    size_t victim_way = this->crp->victim(index);

    for (auto it = set.begin(); it != set.end(); ++it) {
        if (it->second.way == victim_way)
            return it;
    }

    return set.end();
}

template<typename Address, typename Value, typename Tag>
size_t ManagedCache<Address, Value, Tag>::find_empty_way(Index index) const
{
    auto& set = this->get_set(index);
    size_t pick_nth_empty_way = 1;

    if (randomize_initial_fill) {
        pick_nth_empty_way = sc::Random::get((size_t)1, this->num_ways - set.size());
    }

    size_t empty_ways = 0;
    for (size_t way = 0; way < this->num_ways; way++) {
        bool found_way = false;
        for (auto it = set.begin(); it != set.end(); ++it) {
            if (it->second.way == way)
                found_way = true;
        }
        if (!found_way) {
            empty_ways++;
            if (empty_ways == pick_nth_empty_way)
                return way;
        }
    }
    AMPERE_ERROR("Couldn't find an empty way in set {}", index);
    return 0;
}

template<typename Address, typename Value, typename Tag>
void ManagedCache<Address, Value, Tag>::clear()
{
    for (auto& set : this->get_sets())
        set.clear();
}

template<typename Address, typename Value, typename Tag>
Index Cache<Address, Value, Tag>::compute_set(const Address address) const
{
    if (this->set_bits == 0)
        return 0;

    auto index = address.extract_range(this->line_bits + this->set_bits - 1, this->line_bits);
    if (index >= this->num_sets)
        throw std::logic_error("set index is larger than the number of sets");
    return index;
}

template<typename Address, typename Value, typename Tag>
Tag Cache<Address, Value, Tag>::compute_tag(const Address address) const
{
    return address.extract_range(this->ms_tag_bit, this->line_bits + this->set_bits);
}

} // namespace ampere::uarch_common

/*
 * vim: sw=4:ts=4:et:si:cindent:cino=\:0,(0,g0,t0:
 */
