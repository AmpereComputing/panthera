//
// Copyright (c) 2019-2020 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//

#pragma once

#include <cstdint>

#include "ampere/archx/addr.h"
#include "ampere/archx/page_size.h"
#include "ampere/core/tlb.h"

namespace ampere::core {

using namespace archx;
using namespace uarch_common;

using L1TLBEntry = TLBEntry<PhysAddr>;

template<typename VA>
class L1TLBTag : public TLBTag<VA>
{
public:
    using addr_t = VA;
    L1TLBTag(VA addr, uint16_t asid) : TLBTag<VA>(addr, asid) {}

    inline bool operator==(const L1TLBTag& other) const
    {
        return static_cast<const VA&>(*this) == static_cast<const VA&>(other) && this->asid == other.asid;
    }
    inline bool operator!=(const L1TLBTag& other) const { return !this->operator==(other); }
    inline bool operator<(const L1TLBTag& other) const
    {
        if (static_cast<const VA&>(*this) != static_cast<const VA&>(other))
            return static_cast<const VA&>(*this) < static_cast<const VA&>(other);
        if (this->asid != other.asid)
            return this->asid < other.asid;
        return false;
    }
    inline bool operator>(const L1TLBTag& other) const { return other.operator<(*this); }
    inline bool operator<=(const L1TLBTag& other) const { return !(this->operator>(other)); }
    inline bool operator>=(const L1TLBTag& other) const { return !(this->operator<(other)); }

    // See if 'other' is a match for this tag.
    bool match(const L1TLBTag& other, const L1TLBEntry& entry) const
    {
        return this->same_page(other, entry.page_size) && this->asid == other.asid;
    }

    // TODO VMID
};

template<typename VA, TLBType TYPE>
class L1TLB : public TLB<L1TLBTag<VA>, L1TLBEntry, TYPE>
{
    using Base = TLB<L1TLBTag<VA>, L1TLBEntry, TYPE>;

public:
    explicit L1TLB(const sc_core::sc_module_name& name,
                   const L1TLBEntry& perfect_value = L1TLBEntry{0x0_pa, PageSize::Size::PS_4KB})
        : Base(name, perfect_value)
    {
    }

    using Entry = typename Base::Entry;

    std::optional<L1TLBEntry> invalidate(Txn transaction, L1TLBTag<VA> address, bool suppress_event = false)
    {
        std::optional<L1TLBEntry> invalidated_value;
        auto it = this->find_entry(address);
        if (it) {
            auto index = this->compute_set(address);
            auto& set = this->get_set(index);
            invalidated_value = (*it)->second.value;
            this->crp->invalidate(index, (*it)->second.way);
            set.erase(*it);
        }
        return invalidated_value;
    }

    std::optional<L1TLBEntry> refill(Txn transaction, L1TLBTag<VA> address, const L1TLBEntry& value)
    {
        std::optional<L1TLBEntry> victim_value;
        size_t index = this->compute_set(address);
        auto& set = this->find_set(address);
        size_t way{0};

        // L1 ITLB: fill over top of any entry which matches the tag being filled.
        if (this->invalidate_duplicates_before_fill)
            this->invalidate(transaction, address);

        if (this->set_full(set) || this->crp_victim_always) {
            auto victim = this->find_victim(index);
            if (victim == set.end()) {
                AMPERE_ASSERT(this->crp_victim_always, "Victimized way not valid, but crp_victim_always was not set");
                way = this->crp->victim(index);
            } else {
                way = victim->second.way;
                this->ev_eviction(transaction, this->way_data(way));
                victim_value = victim->second.value;
                this->crp->invalidate(index, way);
                set.erase(victim);
            }
        } else {
            way = this->find_empty_way(index);
        }

        // L1 DTLB: pick a victim way first, and then later invalidate any duplicate tags.
        if (this->invalidate_duplicates_after_fill)
            this->invalidate(transaction, address);

        AMPERE_ASSERT(!this->find_entry(address), "L1TLB set {} contains a duplicate", index);

        this->crp->touch(index, way, CacheReplacementPolicy::TouchOp::REFILL);
        this->ev_refill(transaction,
                        this->way_data(way),
                        this->ev_physaddr(value.phys_addr.value()),
                        this->ev_pagebits(value.page_size.page_bits()),
                        this->ev_asid(address.asid));
        set.emplace(address, Entry(way, value));
        return victim_value;
    }

    CacheResult<L1TLBEntry> lookup(Txn transaction, L1TLBTag<VA> address, bool prefetched = false)
    {
        AMPERE_ASSERT(!this->perfect_hit(address) || this->driver_connected,
                      "Driver unconnected while in perfect mode");
        if (auto hit = this->find_entry(address); hit || !this->perfect_hit(address))
            return lookup_result(transaction, address, prefetched, hit);

        auto result = this->translate(address);
        return perfect_lookup_result(transaction, address, prefetched, result);
    }

    CacheResult<L1TLBEntry> lookup(Txn transaction,
                                   const archx::ArchState& state,
                                   L1TLBTag<VA> address,
                                   bool prefetched = false)
    {
        if (auto hit = this->find_entry(address); hit || !this->perfect_hit(address))
            return lookup_result(transaction, address, prefetched, hit);

        auto result = this->translate(state, address);
        return perfect_lookup_result(transaction, address, prefetched, result);
    }

protected:
    EventData<unsigned> ev_pagebits{"page_bits", "Page size, represented by the number of untranslated bits"};
    EventData<unsigned> ev_asid{"asid", "ASID"};

    std::optional<typename L1TLB::Set::iterator> find_entry(L1TLBTag<VA> address)
    {
        auto& set = this->find_set(address);

        for (auto it = set.begin(); it != set.end(); it++) {
            auto match = address.match(it->first, it->second.value);
            if (match)
                return it;
        }

        return std::nullopt;
    }

    CacheResult<L1TLBEntry> lookup_result(Txn transaction,
                                          L1TLBTag<VA> address,
                                          bool prefetched,
                                          const std::optional<typename L1TLB::Set::iterator>& hit)
    {
        if (hit) {
            auto phys_line = (*hit)->second.value.line_address(address);
            this->ev_access(
                transaction,
                this->hit_data((!this->enable_antiperfect_mode && (this->perfect_hit(address) || (bool)hit))),
                address,
                this->ev_physaddr(phys_line),
                this->ev_prefetch(prefetched),
                this->way_data((*hit)->second.way),
                this->ev_asid(address.asid));
        } else {
            this->ev_access(
                transaction,
                this->hit_data((!this->enable_antiperfect_mode && (this->perfect_hit(address) || (bool)hit))),
                address,
                this->ev_prefetch(prefetched));
        }

        if (this->enable_antiperfect_mode)
            return std::nullopt;

        if (hit) {
            this->crp->touch(this->compute_set(address), (*hit)->second.way, CacheReplacementPolicy::TouchOp::LOOKUP);
            return CacheResult<L1TLBEntry>((*hit)->second);
        } else if (this->perfect_hit(address)) {
            // Only fall back on perfect mode if necessary so we can return the actual refilled value when resident
            return CacheResult<L1TLBEntry>(*this->perfect_value);
        }

        return std::nullopt;
    }

    CacheResult<L1TLBEntry> perfect_lookup_result(Txn transaction,
                                                  L1TLBTag<VA> address,
                                                  bool prefetched,
                                                  const std::optional<L1TLBEntry>& result)
    {
        if (!result)
            return std::nullopt;

        auto phys_line = result->line_address(address);
        this->ev_access(transaction,
                        this->hit_data(true),
                        address,
                        this->ev_physaddr(phys_line),
                        this->ev_prefetch(prefetched),
                        this->ev_asid(address.asid));

        this->perfect_result_entry = *result;
        return CacheResult<L1TLBEntry>(this->perfect_result_entry);
    }
};

} // namespace ampere::core

/*
 * vim: sw=4:ts=4:et:si:cindent:cino=\:0,(0,g0,t0:
 */
