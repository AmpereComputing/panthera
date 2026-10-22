//
// Copyright (c) 2020 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//

#include "ampere/mesh/home_node_tracker.h"

namespace ampere::mesh {

HomeNodeTracker::HomeNodeTracker(const sc_core::sc_module_name& name) : Module{name}
{
    free_list.resize(size);
    std::iota(free_list.begin(), free_list.end(), 0);

    for (size_t i = 0; i < size * 2; i++)
        entries.emplace_back(i);
}

std::pair<HomeNodeTrackerEntry*, bool> HomeNodeTracker::allocate(MeshReq req)
{
    AMPERE_ASSERT(!full(), "cannot allocate into a full HomeNodeTracker");

    int n = free_list.front();
    free_list.pop_front();

    return allocate_entry(req, n);
}

std::pair<HomeNodeTrackerEntry*, bool> HomeNodeTracker::allocate_for_clean(HomeNodeTrackerEntry& originator)
{
    int n = originator.id + size;

    auto req = make_child<MeshReq>(originator.request,
                                   originator.request->TgtID,
                                   originator.request->TgtID,
                                   n,
                                   RequestOpcode::CleanInvalid,
                                   originator.snoop_address);

    auto [entry_ptr, blocked] = allocate_entry(req, n);
    entry_ptr->sf_set = originator.sf_set;
    entry_ptr->sf_way = originator.sf_way;
    std::swap(entry_ptr->snoop_address, originator.snoop_address);
    std::swap(entry_ptr->snoop_status, originator.snoop_status);
    entry_ptr->wake_after_clean = &originator;

    return {entry_ptr, blocked};
}

std::pair<HomeNodeTrackerEntry*, bool> HomeNodeTracker::allocate_entry(MeshReq req, int n)
{
    auto& entry = entries[n];
    entry.request = req;

    auto other = active_lines[req->Addr.line_address()];
    active_lines[req->Addr.line_address()] = &entry;

    if (other) {
        other->wake = &entry;
        return {&entry, true};
    } else {
        return {&entry, false};
    }
}

void HomeNodeTracker::deallocate(HomeNodeTrackerEntry& entry)
{
    AMPERE_ASSERT(entry.locked_sf == false, "entry {} left snoop filter locked", entry.id);

    if (!is_shadow_entry(entry))
        free_list.push_back(entry.id);

    auto it = active_lines.find(entry.request->Addr.line_address());

    if (it != active_lines.end() && it->second == &entry)
        active_lines.erase(it);

    if (entry.wake)
        ready_to_schedule(*entry.wake);

    if (entry.wake_after_clean)
        ready_to_schedule(*entry.wake_after_clean);

    entry.reset();
}

HomeNodeTrackerEntry* HomeNodeTracker::find_by_src(NodeId src, uint8_t txn)
{
    auto it = std::find_if(entries.begin(), entries.end(), [src, txn](const auto& elem) -> bool {
        return elem.request->SrcID == src && elem.request->TxnID == txn;
    });

    return it == entries.end() ? nullptr : &(*it);
}

void HomeNodeTracker::dump(std::ostream& os) const
{
    os << "\n"
       << name() << ":\n\n"
       << " ID |        Addr        |        ReqOp       |  SrcID    | TxnID |    Main State    | Wait State |Snp "
          "#|Next |Wake |\n";

    for (auto& entry : entries) {
        if (entry.state.main == MainState::Idle)
            continue;

        fmt::print(os,
                   "{:3d} | 0x{:016x} | {:18} | {:5} | {:5} | {:16} | {:10b} | {:3d} | {} | {} |\n",
                   entry.id,
                   entry.request->Addr.value(),
                   entry.request->Opcode,
                   entry.request->SrcID,
                   entry.request->TxnID,
                   entry.state.main,
                   entry.state.wait,
                   entry.snoop_status.count(),
                   entry.wake ? fmt::format("{:3d}", entry.wake->id) : "---",
                   entry.wake_after_clean ? fmt::format("{:3d}", entry.wake_after_clean->id) : "---");
    }
}

} // namespace ampere::mesh
