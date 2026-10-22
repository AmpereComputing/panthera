//
// Copyright (c) 2020 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//

#ifndef AMPERE_MESH_TRACKER_H
#define AMPERE_MESH_TRACKER_H

#include "ampere/mesh/cbusy.h"
#include "ampere/mesh/home_node_tracker_entry.h"
#include "ampere/mesh/home_node_tracker_fsm.h"
#include "ampere/mesh/mesh_req.h"
#include "ampere/sc/sc.h"

namespace ampere::mesh {

using namespace chi;
using namespace sc;

class HomeNodeTracker : public Module
{
public:
    HomeNodeTracker(const sc_core::sc_module_name& name);

    bool full() const { return free_list.empty(); }
    bool empty() const { return free_list.size() == size; }
    size_t occupancy() const { return size - free_list.size(); }
    size_t free_list_size() const { return free_list.size(); }

    std::pair<HomeNodeTrackerEntry*, bool> allocate(MeshReq req);
    std::pair<HomeNodeTrackerEntry*, bool> allocate_for_clean(HomeNodeTrackerEntry& originator);
    void deallocate(HomeNodeTrackerEntry& entry);

    HomeNodeTrackerEntry& find_by_id(uint8_t id) { return entries.at(id); }
    HomeNodeTrackerEntry* find_by_src(NodeId src, uint8_t txn);

    chi::CBusyState get_cbusy() { return cbusy.update_cbusy(occupancy()); }

    template<class Event>
    void process_event(HomeNodeTrackerEntry& entry, const Event& ev)
    {
        tracker_fsm::process_event(*this, entry, ev);
    }

    void ready_to_schedule(HomeNodeTrackerEntry& entry)
    {
        if (entry.state.wait == 0)
            ready_list.push(&entry);
    }

    HomeNodeTrackerEntry* get_next_ready_entry()
    {
        if (ready_list.empty()) {
            return nullptr;
        } else {
            auto entry = ready_list.front();
            ready_list.pop();
            return entry;
        }
    }

    bool can_dealloc(const HomeNodeTrackerEntry& entry) { return entry.state.idle(); }

    bool is_shadow_entry(const HomeNodeTrackerEntry& entry) const { return entry.id >= size; }

    Param<size_t> size{"size", "Number of entries in the tracker", 64};

    CBusy cbusy{"cbusy"};

protected:
    std::vector<HomeNodeTrackerEntry> entries;
    std::queue<HomeNodeTrackerEntry*> ready_list;
    std::map<PhysAddr, HomeNodeTrackerEntry*> active_lines;
    std::deque<int> free_list;

    std::pair<HomeNodeTrackerEntry*, bool> allocate_entry(MeshReq req, int n);

    void dump(std::ostream& os) const override;
};

} // namespace ampere::mesh

#endif // AMPERE_MESH_TRACKER_H
