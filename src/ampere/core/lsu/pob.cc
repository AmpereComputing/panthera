//
// Copyright (c) 2018-2023 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//

#include "ampere/core/lsu/pob.h"

#include <fmt/core.h>
#include <cstdint>
#include "ampere/archx/el.h"

namespace ampere::core {

POB::POB(const sc_core::sc_module_name& name) : Module(name) {}

void POB::el_change(archx::EL old_el, archx::EL new_el)
{
    AMPERE_ASSERT(old_el != new_el, "POB el_change was called but EL didn't change");
    if (flush_on_el_change)
        flush();
}

// Find a prefetch candidate in POB which is ready to replay
const POB::Entry* POB::replay_ready(archx::EL current_el) const
{
    for (const auto& entry : entries) {
        if (entry.can_replay(current_el))
            return &entry;
    }

    return nullptr;
}

// Find a prefetch candidate in POB which is ready to replay
std::optional<POB::Entry> POB::replay(archx::EL current_el)
{
    for (auto pob_it = entries.begin(); pob_it != entries.end(); pob_it++) {
        if (pob_it->can_replay(current_el)) {
            auto entry = *pob_it;
            entries.erase(pob_it);
            event.replay(entry.trigger_uop, entry.virt_addr, entry.type);
            return entry;
        }
    }

    return {};
}

void POB::ofb_dealloc()
{
    // An OFB deallocated, so wake all entries waiting on OFB_FULL
    for (auto& entry : entries)
        if (entry.status == POBState::WAIT_OFB_FULL)
            entry.status = POBState::READY;
}

/* Inputs:
 * req_id: Req ID which was sent to MMU
 * OTB id: returning from MMU
 */
void POB::handle_mmu_ack(uint64_t req_id, std::optional<uint64_t> otb_id)
{
    for (auto pob_it = entries.begin(); pob_it != entries.end();) {
        auto& entry = *pob_it;
        if (entry.mmu_req_id == req_id) {
            if (otb_id.has_value()) {
                entry.status =
                    POBState::WAIT_TRANSLATION; // wait for translation to come back and keep occupying this entry
                entry.otb_id = *otb_id;
            } else { // MMU sent NACK due to full OTB
                entry.status = POBState::OTB_FULL;
                event.dropped_mmu_noack(entry.trigger_uop, VirtAddr(entry.virt_addr.extract_range(52, 0)), entry.type);
                pob_it = entries.erase(pob_it);
                continue;
            }
        }

        ++pob_it;
    }
}

/* Inputs:
 * id: OTB ID contained in MMU response
 * fault: whether the request is faulting
 */
void POB::handle_mmu_response(uint64_t id, bool fault)
{
    // Using the id received in mem response, do associative search against all entries in POB.
    // Erase the POB entries corresponding to faulting response

    for (auto pob_it = entries.begin(); pob_it != entries.end();) {
        auto& entry = *pob_it;
        if (entry.otb_id && *entry.otb_id == id) {
            auto addr = VirtAddr(entry.virt_addr.extract_range(52, 0));
            if (fault) {
                entry.status = POBState::FAULT;
                event.dropped_fault(entry.trigger_uop, addr, entry.type);

                pob_it = entries.erase(pob_it);
                continue;
            } else {
                entry.status = POBState::READY; // replay candidate
                event.promote_replay(entry.trigger_uop, addr, entry.type);
            }
        }

        ++pob_it;
    }
}

bool POB::try_allocate(L1PrefetchEntry prefetch, PrefetchType type, uint64_t mmu_req_id, POBState state)
{
    if (!can_allocate()) {
        event.dropped_full(prefetch.trigger_uop, VirtAddr(prefetch.virt_addr.extract_range(52, 0)));
        return false;
    }

    if (allocate_on_el0_only && prefetch.trigger_uop && prefetch.trigger_uop->exception_level() != archx::EL0) {
        event.dropped_el(prefetch.trigger_uop, VirtAddr(prefetch.virt_addr.extract_range(52, 0)));
        return false;
    }

    allocate(prefetch, type, mmu_req_id, state);
    return true;
}

void POB::allocate(L1PrefetchEntry prefetch, PrefetchType type, uint64_t mmu_req_id, POBState state)
{
    // Check if POB already contains a duplicate prefetch candidate (at the granularity of cacheline addr)
    for (const auto& entry : entries) {
        if (entry.virt_addr.same_cache_line(prefetch.virt_addr)) {
            event.dropped_duplicate(prefetch.trigger_uop, VirtAddr(prefetch.virt_addr.extract_range(52, 0)));
            return;
        }
    }

    entries.emplace_back(prefetch.virt_addr,
                         prefetch.trigger_uop,
                         prefetch.parent_txn,
                         mmu_req_id,
                         prefetch.consumer_pc,
                         state,
                         get_cycle(),
                         type,
                         prefetch.l2_prefetch_type);

    event.insert(prefetch.trigger_uop, VirtAddr(prefetch.virt_addr.extract_range(52, 0)));
}

void POB::dump(std::ostream& os) const
{
    fmt::print(os, "= POB =========================================================\n");
    fmt::print(os,
               "| {1:>{0}} | {2:>{0}} | {3:>{0}} | {4:>{0}} | {5:>{0}} | {6:>{0}} \n",
               16,
               "MMUReqId",
               "Vaddr",
               "Trigger Uop PC",
               "Status",
               "Alloc_cycle",
               "OTB ID");

    for (const auto& e : entries) {
        fmt::print(os,
                   "| {1:>{0}} | {2:>{0}} | {3:>{0}} | {4:>{0}} | {5:>{0}} | {6:>{0}} \n",
                   16,
                   e.mmu_req_id,
                   e.virt_addr,
                   e.trigger_uop->pc(),
                   *e.status,
                   e.alloc_cycle,
                   e.otb_id.value_or(-1));
    }

    fmt::print(os, "\n");
}

} // namespace ampere::core

/*
 * vim: sw=4:ts=4:et:si:cindent:cino=\:0,(0,g0,t0:
 */
