//
// Copyright (c) 2018-2023 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//

#include "ampere/core/lsu/ofb.h"

#include <fmt/core.h>
#include <cstdint>
#include <ranges>

#include "ampere/archx/addr.h"
#include "ampere/core/backend_prefetcher.h"
#include "ampere/core/uop.h"

namespace ampere::core {

OFB::OFB(const sc_core::sc_module_name& name, size_t partitions)
    : Module(name), entries_allocated_per_partition(partitions, 0)
{
}

bool OFB::run()
{
    std::fill(entries_allocated_per_partition.begin(), entries_allocated_per_partition.end(), 0);

    return true;
}

bool OFB::can_allocate(const L1PrefetchEntry& pfe) const
{
    bool has_alloc_bw = allocations_per_partition
                            // May not be necessarily correct, but handles cases where partitions != pipes
                            ? (entries_allocated_per_partition[pfe.pipe % entries_allocated_per_partition.size()]
                               < allocations_per_partition)
                            : true;

    return has_alloc_bw && can_allocate();
}

bool OFB::can_allocate(const ConstUop& uop) const
{
    bool has_space = !full();
    bool has_alloc_bw = allocations_per_partition && uop
                            ? (entries_allocated_per_partition[uop->sch.partition] < allocations_per_partition)
                            : true;

    if (!has_alloc_bw)
        return false;

    if (reservation && has_space) {
        bool ofb_not_almost_full = (size() + 1) < max_entries;
        return ofb_not_almost_full || (uop && reservation == uop->alloc_id);
    }

    return has_space;
}

std::pair<bool, OFBEntry&> OFB::allocate(const MemReq& req, uint64_t pwc_id, PrefetchType prefetch_type)
{
    auto line_addr = req->phys_addr.line_address();
    auto [iter, added] =
        entries.try_emplace(line_addr, req->virt_addr.line_address(), pwc_id, prefetch_type, get_cycle());

    event.allocate(req, line_addr, event.used_entries(size()), event.prefetch(false));

    return {added, iter->second};
}

std::pair<bool, OFBEntry&> OFB::allocate(const MemReq& req,
                                         const ConstUop& uop,
                                         bool needs_data,
                                         std::optional<UnableToFillReason> unable_to_fill,
                                         PrefetchType prefetch_type)
{
    auto line_addr = req->phys_addr.line_address();
    auto [iter, added] = entries.try_emplace(line_addr,
                                             req->virt_addr.line_address(),
                                             uop->alloc_id,
                                             uop->sch.lsu_scheduler_pipe(),
                                             get_cycle(),
                                             !needs_data,
                                             uop->is_sw_prefetch(),
                                             uop->is_exclusive(),
                                             unable_to_fill,
                                             prefetch_type);

    reset_reservation(uop->alloc_id);
    if (allocations_per_partition && added)
        ++entries_allocated_per_partition[uop->sch.partition];
    event.allocate(req, line_addr, event.used_entries(size()), event.prefetch(false));

    return {added, iter->second};
}

std::pair<bool, OFBEntry&> OFB::allocate(const MemReq& req, const L1PrefetchEntry& prefetch, PrefetchType type)
{
    auto line_addr = req->phys_addr.line_address();
    auto [iter, added] = entries.try_emplace(line_addr, type, prefetch, req->phys_addr, get_cycle());

    if (allocations_per_partition && added)
        ++entries_allocated_per_partition[prefetch.pipe % entries_allocated_per_partition.size()];
    event.allocate(req, line_addr, event.used_entries(size()), event.prefetch(true));

    return {added, iter->second};
}

void OFB::deallocate(PhysAddr phys_addr, Txn resp)
{
    auto line_addr = phys_addr.line_address();
    event.deallocate(resp, line_addr);
    entries.erase(line_addr);
}

void OFB::flush(uint64_t alloc_id)
{
    // Ensure if an OFB entry references a flushed slob_entry
    // it is removed to indicate response to this OFB is flushed
    for (auto& ofb_entry : std::views::values(entries))
        if (ofb_entry.buffer_id && *ofb_entry.buffer_id == alloc_id)
            ofb_entry.buffer_id = std::nullopt;

    reset_reservation(alloc_id);
}

void OFB::mark_l2_miss(PhysAddr phys_addr)
{
    if (auto* ofb_entry = find(phys_addr); ofb_entry)
        ofb_entry->l2_miss = true;
}

bool OFB::check_if_should_sleep(const ConstUop& uop, uint64_t non_spec_alloc_id, bool force)
{
    auto is_full = full();
    if (force || is_full || (almost_full_sleep && reservation)) {
        if (is_full)
            event.full(uop);
        if (reserve_for_oldest_after_full && non_spec_alloc_id && uop->alloc_id == non_spec_alloc_id)
            reservation = non_spec_alloc_id;

        return true;
    }

    return false;
}

void OFB::dump(std::ostream& os) const
{
    uint idx{0};
    fmt::print(os, "\nOFB size: {}\n", size());

    if (!entries.empty()) {
        fmt::print(os,
                   "{1:<{0}} | {2:<{0}} | {3:<{0}} | {4:<{0}} | {5:<{0}}\n",
                   10,
                   "OFB idx",
                   "phys_addr",
                   "virt_addr",
                   "slob_id",
                   "opportunistic");
        for (const auto& ofb_handle : entries) {
            fmt::print(os,
                       "{1:<{0}} | {2:<{0}} | {3:<{0}} | {4:<{0}} | {5:<{0}}\n",
                       10,
                       idx++,
                       ofb_handle.first,
                       ofb_handle.second.virt_addr,
                       (ofb_handle.second.buffer_id ? *ofb_handle.second.buffer_id : 0),
                       (ofb_handle.second.opportunistic ? 1 : 0));
        }
    }
}
} // namespace ampere::core

/*
 * vim: sw=4:ts=4:et:si:cindent:cino=\:0,(0,g0,t0:
 */
