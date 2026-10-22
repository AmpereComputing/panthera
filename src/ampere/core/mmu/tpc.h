//
// Copyright (c) 2019-2020 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//

#pragma once

#include <array>
#include <cstdint>
#include <optional>

#include "ampere/core/mmu/pwt.h"
#include "ampere/uarch_common/cache.h"

namespace ampere::core {

using namespace ampere::uarch_common;

class TPCTag
{
public:
    TPCTag(uint16_t l0index, uint16_t l1index, uint16_t l2index, uint8_t granule_size, uint16_t asid, bool using_ttbr0)
        : indices{l0index, l1index, l2index}, granule_size{granule_size}, asid{asid}, using_ttbr0{using_ttbr0}
    {
    }

    TPCTag(const PWTEntry& entry, uint16_t asid, bool using_ttbr0)
        : indices{entry.index(0), entry.index(1), entry.index(2)},
          granule_size{entry.granule_size},
          asid{asid},
          using_ttbr0{using_ttbr0}
    {
    }

    inline bool operator==(const TPCTag& other) const
    {
        return indices == other.indices && granule_size == other.granule_size && asid == other.asid
               && using_ttbr0 == other.using_ttbr0;
    }
    inline bool operator!=(const TPCTag& other) const { return !this->operator==(other); }
    inline bool operator<(const TPCTag& other) const
    {
        if (indices[0] != other.indices[0])
            return indices[0] < other.indices[0];
        if (indices[1] != other.indices[1])
            return indices[1] < other.indices[1];
        if (indices[2] != other.indices[2])
            return indices[2] < other.indices[2];
        if (asid != other.asid)
            return asid < other.asid;
        if (granule_size != other.granule_size)
            return granule_size < other.granule_size;
        if (using_ttbr0 != other.using_ttbr0)
            return using_ttbr0;
        return false;
    }
    inline bool operator>(const TPCTag& other) const { return other.operator<(*this); }
    inline bool operator<=(const TPCTag& other) const { return !(this->operator>(other)); }
    inline bool operator>=(const TPCTag& other) const { return !(this->operator<(other)); }

    uint16_t l0_index() const { return indices[0]; }
    uint16_t l1_index() const { return indices[1]; }
    uint16_t l2_index() const { return indices[2]; }
    bool granule_64k() const { return granule_size == 16 /* 64k */; }

    std::optional<uint8_t> match_level(const TPCTag& other) const
    {
        if (this->asid != other.asid || this->using_ttbr0 != other.using_ttbr0
            || this->granule_64k() != other.granule_64k() || this->l0_index() != other.l0_index())
        {
            return {};
        }

        if (this->l1_index() != other.l1_index()) {
            // L0 index is empty for 64k granule, so dfferent L1 tags means
            // there is no match at all
            if (this->granule_64k())
                return {};
            else
                return {0};
        }

        if (this->l2_index() != other.l2_index()) {
            return {1};
        }

        return {2};
    }

private:
    std::array<uint16_t, 3> indices;
    uint8_t granule_size;
    uint16_t asid;
    bool using_ttbr0;
};

class TPC : public ManagedCache<TPCTag, PWTEntry, TPCTag>
{
public:
    explicit TPC(const sc_core::sc_module_name& name) : ManagedCache<TPCTag, PWTEntry, TPCTag>(name)
    {
        if (get_environment()->parameters.help_requested())
            return;
        if (num_sets != 1)
            throw std::runtime_error("TPC only supports one set");
    }

    std::optional<PWTEntry> refill(Txn transaction, TPCTag address, const PWTEntry& value)
    {
        std::optional<PWTEntry> victim_value;
        size_t index = compute_set(address);
        auto& set = find_set(address);
        size_t way = this->num_ways;

        if (this->invalidate_duplicates_before_fill)
            this->invalidate(transaction, address);

        if (this->set_full(set) || crp_victim_always) {
            auto victim = find_victim(index);
            if (victim == set.end()) {
                AMPERE_ASSERT(crp_victim_always, "Victimized way not valid, but crp_victim_always was not set");
                way = this->crp->victim(index);
            } else {
                way = victim->second.way;
                this->ev_eviction(transaction);
                victim_value = victim->second.value;
                crp->invalidate(index, way);
                set.erase(victim);
            }
        } else {
            way = this->find_empty_way(index);
        }

        if (this->invalidate_duplicates_after_fill)
            this->invalidate(transaction, address);

        // There should never be duplicate tags, but it only counts as a duplicate if it matches all levels (0-2)
        auto result = this->find_entry_level(address);
        if (result && result->second == 2)
            AMPERE_ERROR("TPC set {} contains a duplicate", index);

        crp->touch(index, way, CacheReplacementPolicy::TouchOp::REFILL);
        ev_refill(transaction, way_data(way));
        set.emplace(address, Entry(way, value));
        return victim_value;
    }

    CacheResult<PWTEntry> lookup(Txn transaction, TPCTag address)
    {
        auto result = this->find_entry_level(address);

        ev_access(transaction, hit_data((!enable_antiperfect_mode && (enable_perfect_mode || (bool)result))));
        if (enable_antiperfect_mode)
            return std::nullopt;

        if (result) {
            auto& entry = result->first->second;
            auto level = result->second;
            s1tpc_hit_level(transaction, hit_level_data(level));
            crp->touch(compute_set(address), entry.way, CacheReplacementPolicy::TouchOp::LOOKUP);
            return CacheResult<PWTEntry>(entry);
        } else if (perfect_hit(address)) {
            // Only fall back on perfect mode if necessary so we can return the actual refilled value when resident
            return CacheResult<PWTEntry>(*perfect_value);
        }

        return std::nullopt;
    }

    std::optional<PWTEntry> invalidate(Txn transaction, TPCTag address, bool suppress_event = false)
    {
        std::optional<PWTEntry> invalidated_value;
        auto result = find_entry_level(address);

        // Only invalidate an exact match, which matches levels 0-2 of the walk
        if (result && result->second == 2) {
            auto set_iter = result->first;
            auto& entry = set_iter->second;

            auto index = compute_set(address);
            auto& set = this->get_set(index);

            invalidated_value = entry.value;
            this->crp->invalidate(index, entry.way);
            set.erase(set_iter);
        }

        return invalidated_value;
    }

protected:
    Event s1tpc_hit_level{"s1tpc_hit_level", "S1TPC Hit Level"};
    EventData<uint8_t> hit_level_data{"hit_level", "Level of TPC Hit"};

    std::optional<std::pair<typename TPC::Set::iterator, uint8_t>> find_entry_level(TPCTag address)
    {
        std::optional<std::pair<typename TPC::Set::iterator, uint8_t>> result;
        Set& set = find_set(address);

        for (auto it = set.begin(); it != set.end(); it++) {
            auto match_level = address.match_level(it->first);
            if (match_level && (!result || (match_level.value() > result->second))) {
                result = std::make_pair(it, match_level.value());

                if (match_level.value() == 2)
                    break;
            }
        }

        return result;
    }

    Index compute_set(TPCTag tag) const override { return (0); }

    TPCTag compute_tag(TPCTag tag) const override { return tag; }
};

} // namespace ampere::core

/*
 * vim: sw=4:ts=4:et:si:cindent:cino=\:0,(0,g0,t0:
 */
