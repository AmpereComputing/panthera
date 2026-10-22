//
// Copyright (c) 2018-2023 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//

#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <map>
#include <ostream>
#include <stdexcept>
#include <utility>

#include "ampere/sc/sc.h"
#include "ampere/archx/addr.h"
#include "ampere/core/backend_prefetcher.h"
#include "ampere/core/uop.h"

make_ns_enum(ampere::core, UnableToFillReason,
    ATOMIC_LOAD,
    PREFETCH_LEVEL,
    EVICTION_INCOMPLETE
);

namespace ampere::core {

struct OFBEntry
{
    VirtAddr virt_addr;
    std::optional<std::uint64_t> buffer_id{std::nullopt};
    std::optional<std::uint64_t> pwc_id{std::nullopt};
    Credit opportunistic; // Hardware prefetch
    uint8_t pipe{0};
    Cycle alloc_time{0};
    bool spec_lookup{
        false}; // Parallel L1 lookup for a load handled by store forwarding: treat its cache access as a prefetch
    bool sw_prefetch{false}; // This OFB entry serves a software prefetch
    bool exclusive{false};
    bool l2_miss{false}; // Used for STALL_BACKEND_* pmus - lookup OFB to determine if a LOB entry is waiting for an L2
                         // lookup vs L2 miss
    std::optional<UnableToFillReason> unable_to_fill;
    Uop trigger_uop;                                 /* Prefetches allocating ofb don't have lob_entry.
                                                        Data returning from prefetch needs to have some way to associate with PC. */
    std::optional<PhysAddr> phys_addr{std::nullopt}; /* If OFBEntry is allocated by a PCP prefetch request,
                                                        when PCP prefetch request is completed, we need the
                                                        phys addr to read the data value. */
    PrefetchType prefetch_type{PrefetchType::NONE};

    uint8_t pipe_mask() const { return 1U << pipe; }
    bool speculative() const { return opportunistic || spec_lookup; }

    bool is_demand() const { return !opportunistic; }
    bool is_prefetch() const { return static_cast<bool>(opportunistic); }
    bool was_prefetch() const { return prefetch_type != PrefetchType::NONE; }

    bool completes_uop() const { return !speculative() && buffer_id; }

    bool can_upgrade(const ConstUop& uop) const { return speculative() || (buffer_id && uop->is_older(*buffer_id)); }
    bool upgraded() const { return !opportunistic && was_prefetch(); }

    bool should_fill() const { return !unable_to_fill; }
    void try_to_fill()
    {
        if (unable_to_fill) {
            switch (*unable_to_fill) {
            case UnableToFillReason::ATOMIC_LOAD:
            case UnableToFillReason::PREFETCH_LEVEL:
                break;
            case UnableToFillReason::EVICTION_INCOMPLETE:
                return;
            }

            unable_to_fill.reset();
        }
    }
    void hit(const ConstUop& uop)
    {
        // OFB entry remains marked as SW prefetch only if all uops waiting on
        // it are SW prefetches
        sw_prefetch = sw_prefetch && uop->is_sw_prefetch();
        // OFB entry remains marked as Exclusive if any uop was
        exclusive = exclusive || uop->is_exclusive();
    }
    void demand_hit()
    {
        if (opportunistic)
            release_credit(opportunistic);

        spec_lookup = false;
        sw_prefetch = false;
        try_to_fill();
    }

    void drop(UnableToFillReason reason) { unable_to_fill = reason; }
    void drop_uop(UnableToFillReason reason)
    {
        buffer_id.reset();
        drop(reason);
    }

    OFBEntry(VirtAddr virt_addr,
             std::uint64_t buffer_id,
             uint8_t pipe,
             Cycle time,
             bool spec_lookup = false,
             bool sw_prefetch = false,
             bool exclusive = false,
             std::optional<UnableToFillReason> unable_to_fill = {},
             PrefetchType prefetch_type = PrefetchType::NONE)
        : virt_addr(virt_addr.line_address()),
          buffer_id(buffer_id),
          pipe(pipe),
          alloc_time(time),
          spec_lookup(spec_lookup),
          sw_prefetch(sw_prefetch),
          exclusive(exclusive),
          unable_to_fill(unable_to_fill),
          prefetch_type(prefetch_type)
    {
    }
    OFBEntry(PrefetchType type, const L1PrefetchEntry& prefetch, PhysAddr phys_addr, Cycle time)
        : virt_addr(prefetch.virt_addr.line_address()),
          pipe(prefetch.pipe),
          alloc_time(time),
          trigger_uop(prefetch.trigger_uop),
          phys_addr(phys_addr),
          prefetch_type(type)
    {
    }
    OFBEntry(VirtAddr virt_addr, uint64_t pwc_id, PrefetchType prefetch_type, Cycle time)
        : virt_addr(virt_addr.line_address()),
          pwc_id(prefetch_type != PrefetchType::NONE ? std::make_optional(pwc_id) : std::nullopt),
          alloc_time(time),
          prefetch_type(prefetch_type)
    {
    }
    OFBEntry(const OFBEntry&) = delete;
};

class OFB : public Module
{
    uint64_t reservation = 0;
    std::map<PhysAddr, OFBEntry> entries;
    std::vector<size_t> entries_allocated_per_partition;

    Param<size_t> max_entries{"entries", "number of OFB entries", 12};
    Param<bool> reserve_for_oldest_after_full{
        "reserve_for_oldest_after_full",
        "Reserve an OFB entry (next available) for the oldest after reaching full",
        false};
    Param<bool> almost_full_sleep{"almost_full_sleep", "Sleep load with OFB_FULL if OFB almost full (reserved)", false};
    Param<size_t> allocations_per_partition{
        "allocations_per_partition", "Number of entries a partition can allocate per cycle", 0};

    struct
    {
        Event allocate{"allocate", "OFB allocate"};
        Event deallocate{"deallocate", "OFB deallocate"};
        Event full{"full", "OFB full"};
        EventData<unsigned> used_entries{"used_entries", "Number of entries used in OFB"};
        EventData<bool> prefetch{"prefetch", "1 if prefetch, 0 for demand fetch"};
    } event;

public:
    explicit OFB(const sc_core::sc_module_name& name, size_t partitions);

    bool run() override;

    bool can_allocate() const { return can_allocate(nullptr); }
    bool can_allocate(const L1PrefetchEntry& pfe) const;
    bool can_allocate(const ConstUop& uop) const;

    std::pair<bool, OFBEntry&> allocate(const MemReq& req,
                                        uint64_t pwc_id,
                                        PrefetchType prefetch_type = PrefetchType::NONE);
    std::pair<bool, OFBEntry&> allocate(const MemReq& req,
                                        const ConstUop& uop,
                                        bool needs_data = true,
                                        std::optional<UnableToFillReason> unable_to_fill = {},
                                        PrefetchType prefetch_type = PrefetchType::NONE);
    std::pair<bool, OFBEntry&> allocate(const MemReq& req, const L1PrefetchEntry& prefetch, PrefetchType type);
    void deallocate(PhysAddr phys_addr, Txn resp);

    OFBEntry* find(PhysAddr phys_addr)
    {
        if (auto iter = entries.find(phys_addr.line_address()); iter != entries.end())
            return &iter->second;
        return nullptr;
    }
    OFBEntry& at(PhysAddr phys_addr) { return entries.at(phys_addr.line_address()); }

    OFBEntry* find(VirtAddr virt_addr)
    {
        if (auto iter = std::find_if(entries.begin(),
                                     entries.end(),
                                     [=](const auto& e) { return virt_addr.same_cache_line(e.second.virt_addr); });
            iter != entries.end())
            return &iter->second;
        return nullptr;
    }
    OFBEntry& at(VirtAddr virt_addr)
    {
        if (auto* e = find(virt_addr); e)
            return *e;
        throw std::out_of_range("No entry found for virt-addr");
    }

    auto begin() const { return entries.begin(); }
    auto end() const { return entries.end(); }

    auto size() const { return entries.size(); }
    bool full() const { return size() == max_entries; }

    bool reserved() const { return reservation; }
    bool check_if_should_sleep(const ConstUop& uop, uint64_t non_spec_alloc_id, bool force = false);
    void reset_reservation(uint64_t alloc_id)
    {
        if (reservation && reservation == alloc_id)
            reservation = 0;
    }

    void flush(uint64_t alloc_id);
    void mark_l2_miss(PhysAddr phys_addr);

    void dump(std::ostream& os) const override;
};

} // namespace ampere::core

/*
 * vim: sw=4:ts=4:et:si:cindent:cino=\:0,(0,g0,t0:
 */
