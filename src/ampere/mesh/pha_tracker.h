//
// Copyright (c) 2022 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//
#pragma once

#include <unordered_set>
#include <variant>

#include "ampere/mesh/cbusy.h"
#include "ampere/mesh/mesh_req.h"
#include "ampere/mesh/pha_fsm.h"
#include "ampere/mesh/pha_tracker_entry.h"
#include "ampere/sc/sc.h"

namespace ampere::mesh {

using namespace chi;
using namespace sc;

class PHA;

class PHATracker : public Module
{
public:
    PHATracker(const sc_core::sc_module_name& name, size_t size, const PHA* pha);

    bool full() const { return free_list.empty(); }
    bool empty() const { return free_list.size() == size; }
    size_t occupancy() const { return size - free_list.size(); }
    size_t free_list_size() const { return free_list.size(); }

    PHATrackerEntry* allocate(MeshReq req, pha_fsm::PHA_t pha_type, uint8_t entry_type, bool sfv = false);
    PHATrackerEntry* allocate(MeshSnp snp, pha_fsm::PHA_t pha_type, uint8_t entry_type);
    PHATrackerEntry* allocate(MeshReq req, PHATrackerEntry& originator, uint8_t entry_type);

    void pup_check(PHATrackerEntry& entry);
    void block_entry(PHATrackerEntry& entry);
    void unblock_entry(PHATrackerEntry& entry);
    std::optional<PHATrackerEntry*> snp_in_list(PHATrackerEntry& entry);
    void sfv_alloc(PHATrackerEntry& entry, PhysAddr addr);

    std::pair<bool, uint8_t> deallocate_run();
    bool dealloc_empty() { return dealloc_list.empty(); }

    PHATrackerEntry& find_by_id(uint8_t id)
    {
        auto& entry = entries.at(id);
        AMPERE_ASSERT(entry.state.is_valid(), "{}: found invalid entry {}", name(), id);
        return entry;
    }

    // TODO: PHATrackerEntry* find_by_src(NodeId src, uint8_t txn);
    // TODO: chi::CBusyState get_cbusy() { return cbusy.update_cbusy(occupancy()); }

    void process_event(PHATrackerEntry& entry) { pha_fsm::process_transition(entry.input, entry.aux, entry.output); }

    void ready_to_schedule(PHATrackerEntry& entry)
    {
        if (!entry.state.is_ready()) {
            if (entry.input.SfVictim)
                ready_sfv_list.insert(&entry);
            else if (entry.input.FlowType == pha_fsm::FlowType_t::FLOW_SNP)
                ready_snp_list.insert(&entry);
            else if (entry.request->QoS.is_platinum())
                ready_plat_list.insert(&entry);
            else
                ready_list.insert(&entry);

            entry.state.set_state(PHATrackerEntryState::Ready);
        }
    }

    void schedule_blocked_sfv(PHATrackerEntry& entry)
    {
        sfv_blocked.push_back(&entry);
        entry.state.set_state(PHATrackerEntryState::Blocked);
    }

    void deallocate(PHATrackerEntry& entry)
    {
        dealloc_list.push_back(&entry);
        entry.state.set_state(PHATrackerEntryState::Deallocating);
    }
    void temp_set_defaults(PHATrackerEntry& entry);

    PHATrackerEntry* get_next_ready_entry()
    {
        PHATrackerEntry* entry = nullptr;

        bool snoop_available = ready_snp_list.size() > 0;
        bool sfv_available = ready_sfv_list.size() > 0;
        bool plat_available = ready_plat_list.size() > 0;
        bool other_available = ready_list.size() > 0;

        // Every other time schedule a snoop
        if (schedule_snp && snoop_available) {
            entry = *ready_snp_list.begin();
            ready_snp_list.erase(ready_snp_list.begin());
            entry->state.set_state(PHATrackerEntryState::Executing);
            schedule_snp = false;
            return entry;
        }
        schedule_snp = true;

        // IF SFV available we schedule this with higher priority than requests.
        if (sfv_available) {
            entry = *ready_sfv_list.begin();
            ready_sfv_list.erase(ready_sfv_list.begin());
            entry->state.set_state(PHATrackerEntryState::Executing);
            return entry;
        }

        if (schedule_plat && plat_available) {
            entry = *ready_plat_list.begin();
            ready_plat_list.erase(ready_plat_list.begin());
            entry->state.set_state(PHATrackerEntryState::Executing);
            schedule_plat = false;
            return entry;
        }

        schedule_plat = true;
        if (other_available) {
            entry = *ready_list.begin();
            ready_list.erase(ready_list.begin());
            entry->state.set_state(PHATrackerEntryState::Executing);
            return entry;
        }

        return entry;
    }

    void pipe_done(PHATrackerEntry& entry)
    {
        if (!entry.state.is_blocked()) {
            // By default go to Waiting, but the code below might move us to Ready or Deallocating.
            entry.state.set_state(PHATrackerEntryState::Waiting);

            if (entry.ready_to_bid())
                ready_to_schedule(entry);
            else if (entry.can_deallocate())
                deallocate(entry);
        }
    }

    Param<bool> disable_dmt{"disable_dmt", "Disable Direct Memory Transfers", false};
    Param<bool> disable_dct{"disable_dct", "Disable Direct Core Transfers", false};
    Param<bool> disable_dwt{"disable_dwt", "Disable Direct Write Transfers", true};

    Param<std::unordered_set<uint8_t>> dmt_die_disable{
        "dmt_die_disable", "Disable DMT to particular die ids", "[]"_json};

protected:
    size_t size;
    uint64_t tracker_age{0}; /* The current age of an allocated entry */

    /* List of tracker entries to operate on */
    std::vector<PHATrackerEntry> entries;

    /* List of ids associated with entries list above.  Entries in this lists
     * are not currently being used and free */
    std::deque<int> free_list;

    /* List of tracker entries allocated.  These are being operated on and/or
     * we are waiting for responses from the mesh */
    std::set<PHATrackerEntry*, ComparePHATrackerEntry> ready_snp_list;
    std::set<PHATrackerEntry*, ComparePHATrackerEntry> ready_sfv_list;
    std::set<PHATrackerEntry*, ComparePHATrackerEntry> ready_plat_list;
    std::set<PHATrackerEntry*, ComparePHATrackerEntry> ready_list;

    bool schedule_snp{true};
    bool schedule_plat{true};

    /* List of entries that are blocking because tracker is full needs to spawn new SFV entries*/
    std::list<PHATrackerEntry*> sfv_blocked;

    /* List of entries who are ready to be deallocated */
    std::list<PHATrackerEntry*> dealloc_list;

    /* This is a list of active cachelines that we are actively working with,
     * this list is used to track/determine conflicts in with incoming requests
     * to the same address. */
    std::map<PhysAddr, PHATrackerEntry*> active_lines;

    const PHA* pha;

public:
    void dump(std::ostream& os) const override;

protected:
    CBusy cbusy{"cbusy"};

    PHATrackerEntry& get_free_tracker_entry();
    void block_if_active(PHATrackerEntry& entry, PhysAddr addr, bool skip_ahead);
    void remove_from_ready_list(PHATrackerEntry& entry);
    void init_common(PHATrackerEntry& entry, pha_fsm::PHA_t pha_type, uint8_t entry_type);
    void init_from_req(PHATrackerEntry& entry, const MeshReq& req, pha_fsm::PHA_t pha_type);
    void init_from_snp(PHATrackerEntry& entry, const MeshSnp& snp);
};

} // namespace ampere::mesh
