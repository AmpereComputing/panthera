//
// Copyright (c) 2018-2020 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//

#include "ampere/core/mmu/otb.h"

#include <cstdint>
#include <functional>
#include <optional>
#include <tuple>
#include <utility>
#include <boost/range/adaptor/reversed.hpp>

#include "ampere/archx/bitfield.h"
#include "ampere/archx/sysreg.h"
#include "ampere/archx/translation.h"
#include "ampere/core/ixu_defs.h"
#include "ampere/core/mmu/op.h"
#include "ampere/core/mmu/req.h"
#include "ampere/sc/module.h"
#include "ampere/sc/transaction.h"

namespace ampere::core {

std::vector<std::string> OTB::section_names(unsigned lsu_sections)
{
    std::vector<std::string> names;
    for (unsigned section = 0; section < lsu_sections; ++section)
        names.emplace_back(fmt::format("LSU_{}", section));

    names.emplace_back("ICF");
    names.emplace_back("Shared");
    return names;
}

OTB::OTB(const sc_core::sc_module_name& name)
    : Module(name), shared_section{total_sections() - 1}, sections{"sections", section_names(lsu_sections())}
{
    connect(deallocation).name("deallocation_delay").latency(deallocation_latency).bandwidth(total_entries);

    if (sectioned) {
        unsigned sectioned_entries = 0;
        for (const auto& section : sections)
            sectioned_entries += section.get_size();

        AMPERE_ASSERT(sectioned_entries == total_entries,
                      "Per-section sizes ({}) do not add up to total ({})",
                      sectioned_entries,
                      *total_entries);
    }
}

void OTB::end_of_elaboration()
{
    // Notify of OTB deallocate only if requested
    notify_availablity = available.get_interface();
}

bool OTB::run()
{
    if (remaining_allocation_lockout_cycles)
        remaining_allocation_lockout_cycles--;

    while (deallocation.can_take()) {
        auto otb_id = deallocation.take();
        deallocate_immediately(entries.at(otb_id));
    }

    if (!empty())
        event.non_empty(event.occupancy(entries.size()));

    return true;
}

std::optional<uint64_t> OTB::allocate_id() const
{
    if (entries.size() < total_entries) {
        for (uint64_t otb_id = 0; otb_id < total_entries; otb_id++) {
            if (!entries.contains(otb_id))
                return otb_id;
        }
    }
    return std::nullopt;
}

std::pair<std::optional<uint64_t>, bool> OTB::try_combine(MMUReq mmureq, const TranslationState& state)
{
    if (!combine_duplicates)
        return {std::nullopt, false};

    bool duplicated = false;
    std::optional<uint64_t> otb_id;

    for (auto& [_, otb_entry] : entries) {
        auto& req = otb_entry.request;
        bool same_page = (_64k_duplicate_check && state.granule_size == 16)
                             ? req->virt_addr.same_page_64k(mmureq->virt_addr)
                             : req->virt_addr.same_page_4k(mmureq->virt_addr);

        if (req->is_instruction == mmureq->is_instruction && same_page && otb_entry.asid == state.asid
            && otb_entry.using_ttbr0() == state.using_ttbr0 && !otb_entry.deallocating && !otb_entry.flushing)
        {
            if (req->is_store != mmureq->is_store) {
                // if its a load or store hit same region, previously we don't remove the duplicates for them
                if (otb_entry.op != MMUOp::DuplicateStall && stall_same_page_duplicates)
                    duplicated = true;
            } else {
                otb_id = otb_entry.otb_id;
                event.duplicate_request(mmureq, event.otb_id(*otb_id), mmureq->virt_addr);
                event.subsuming_request(req, event.otb_id(*otb_id), req->virt_addr);
                break;
            }
        }
    }

    return {otb_id, duplicated};
}

std::optional<uint64_t> OTB::try_allocate(MMUReq mmureq, MMUMRU& mru, const TranslationState& state, bool duplicated)
{
    auto otb_id = allocate_id();
    bool reserved = reservation == mmureq->id;
    bool almost_full = (total_entries - entries.size()) == 1;
    if (otb_id && almost_full && !mmureq->is_instruction && reservation && !reserved) {
        event.almost_full(mmureq, mmureq->virt_addr);
        otb_id.reset();
    }

    Credit section_credit;
    if (otb_id && sectioned) {
        auto section = mmureq->section;
        if (!sections[section].can_acquire())
            section = shared_section;
        if (auto& s = sections[section]; s.can_acquire()) {
            section_credit = s.acquire();
        } else {
            otb_id.reset();
        }
    }

    if (otb_id) {
        auto& entry = entries
                          .emplace(std::piecewise_construct,
                                   std::forward_as_tuple(*otb_id),
                                   std::forward_as_tuple(*otb_id, std::move(section_credit), mmureq, mru, state))
                          .first->second;

        event.allocate(mmureq,
                       mmureq->virt_addr,
                       event.otb_id(*otb_id),
                       event.asid(state.asid),
                       event.occupancy(entries.size()),
                       event.instr(mmureq->is_instruction));

        if (duplicated)
            entry.op = MMUOp::DuplicateStall;
        if (reserved || full())
            reservation = 0;
    } else {
        event.full(mmureq, mmureq->virt_addr);
        if (reserve_for_oldest_after_full && !reservation && mmureq->non_speculative)
            reservation = mmureq->id;
    }

    return otb_id;
}

void OTB::deallocate_only(OTBEntry& otbentry)
{
    if (otbentry.section)
        otbentry.section->release();

    auto& mmu_req = otbentry.request;
    event.deallocate(mmu_req,
                     event.otb_id(otbentry.otb_id),
                     event.page_bits(otbentry.page_bits()),
                     event.flushed(otbentry.flushing));

    if (notify_availablity && available->can_put())
        available->put(true);
}

void OTB::deallocate(OTBEntry& otbentry, bool now)
{
    // This loop for unblocking OTB entries have been stalled because other entry is bringing the page
    if (!otbentry.deallocating && stall_same_page_duplicates) {
        for (auto& [_, otb_entry] : entries) {
            if (otb_entry.op == MMUOp::DuplicateStall) {
                bool same_page = (_64k_duplicate_check && otb_entry.granule_size == 16)
                                     ? otb_entry.request->virt_addr.same_page_64k(otbentry.request->virt_addr)
                                     : otb_entry.request->virt_addr.same_page_4k(otbentry.request->virt_addr);

                if (same_page) {
                    // freeing otb id event
                    otb_entry.op = MMUOp::TLBLkp;
                }
            }
        }
    }

    if (now) {
        deallocate_only(otbentry);
        entries.erase(otbentry.otb_id);
    } else {
        otbentry.deallocating = true;
        deallocation.put(otbentry.otb_id);
    }
}

OTB::otb_iterator OTB::schedule(std::function<bool(const OTBEntry&)> pred, ScheduleInfo& info)
{
    auto to_schedule = entries.end();
    // pick non-speculative if ready and present
    if (prioritize_scheduling_non_spec) {
        to_schedule = std::find_if(entries.begin(), entries.end(), [&](const auto& e) {
            return e.second.non_speculative() && pred(e.second);
        });
    }

    // pick round-robin candidate if ready
    if (round_robin_scheduling && to_schedule == entries.end()) {
        if (auto rr = entries.find(info.round_robin_candidate); rr != entries.end() && pred(rr->second))
            to_schedule = rr;

        info.round_robin_candidate = (info.round_robin_candidate + 1) % total_entries;
    }

    return to_schedule;
}

void OTB::schedule_entries(std::function<bool(OTBEntry&)> func,
                           std::function<bool(const OTBEntry&)> pred,
                           OTB::ScheduleInfo& info)
{
    bool use_last = info.use_last;
    if (auto prioritized = schedule(pred, info); prioritized != entries.end()) {
        func(prioritized->second);
    } else if (alternate_first_last_scheduling) {
        // swap first/last scheduling if prioritized not picked
        info.use_last = !info.use_last;
    }

    if (use_last) {
        for (auto& [_, otb_entry] : boost::adaptors::reverse(entries))
            if (pred(otb_entry) && !func(otb_entry))
                break;
    } else {
        for (auto& [_, otb_entry] : entries)
            if (pred(otb_entry) && !func(otb_entry))
                break;
    }
}

void OTB::flush(Flush flush)
{
    remaining_allocation_lockout_cycles = flush_allocation_lockout_cycles;

    for (auto it = entries.begin(); it != entries.end();) {
        auto& otbentry = it->second;
        otbentry.flushing = true;
        if (!otbentry.executing && otbentry.ready && !otbentry.deallocating) {
            deallocate_only(otbentry);
            it = entries.erase(it);
        } else {
            it++;
        }
    }

    if (flush->alloc_id && flush->alloc_id <= reservation)
        reservation = 0;
}

void OTB::clear()
{
    for (auto it = entries.begin(); it != entries.end();) {
        deallocate_only(it->second);
        it = entries.erase(it);
    }
}

void OTB::dump(std::ostream& os) const
{
    fmt::print(os, "\nOTB size: {}\n", entries.size());

    if (!entries.empty()) {
        fmt::print(os,
                   fmt::runtime("{1:<7} | {2:<{0}} | {3:<{0}} | {5:<16} | {6:<7} | {7:<5} | {8:<{0}} | {9:<16}\n"),
                   10,
                   "OTB ID",
                   "MMUReq id",
                   "is_instr",
                   "virt_addr",
                   "op",
                   "ready",
                   "executing",
                   "phys_addr");
        for (const auto& [_, otb_entry] : entries) {
            fmt::print(os,
                       fmt::runtime("{1:<7} | {2:<{0}} | {3:<{0}} | {5:<16} | {6:<7} | {7:<5} | {8:<{0}} | {9:<16}\n"),
                       10,
                       otb_entry.otb_id,
                       otb_entry.request->id,
                       otb_entry.request->is_instruction,
                       otb_entry.request->virt_addr,
                       otb_entry.op,
                       otb_entry.ready,
                       otb_entry.executing,
                       otb_entry.phys_addr);
        }
    }
}

} // namespace ampere::core

/*
 * vim: sw=4:ts=4:et:si:cindent:cino=\:0,(0,g0,t0:
 */
