//
// Copyright (c) 2023 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//

#pragma once

#include "ampere/mesh/mesh_module.h"
#include "ampere/mesh/pha_tracker_entry.h"
#include "ampere/sc/sc.h"

namespace ampere::mesh {

using namespace sc;
using namespace chi;

inline int get_snoop_count(PHATrackerEntry* entry)
{
    int cnt = 0;

    if (entry->output.TxSnpOpcode) {
        // One snoop for every entry in st vector
        cnt += entry->snoop_status.count();
        // check if requester is present in the core_valid list, if yes then decrement number of snoops as snoop will
        // not be sent to the requester
        if (entry->snoop_status.core_is_valid(entry->request->SrcID)) {
            auto pst = entry->snoop_status.primary_snoop_target();
            AMPERE_ASSERT(pst ? (entry->request->SrcID != pst.value()) : 1,
                          "one entry cannot be present in both core_valid and pst");
            cnt -= 1;
        }

        if (entry->output.PstEn) {
            if (entry->snoop_status.primary_snoop_target()) {
                cnt++;

                // Send to only PST
                if (entry->output.PstOnly)
                    cnt = 1;
            } else if (entry->output.PstOnly) {
                cnt = 0;
            }
        }
    }

    // Snoop sent to stash node
    if (entry->output.TxSnpStashOpcode)
        cnt++;

    return cnt;
}

class PHAPhase : public Module
{
protected:
    Param<size_t> size{"size", "Snoop Engine Command Queue Depth", 16};

    enum class CommandState {
        IDLE,
        SNP_PST,
        SNP_ST_VEC,
        SNP_STASH
    };
    const char* s[4] = {"Idle", "SnpPst", "SnpStVect", "SnpStash"};

    struct CommandEntry
    {
        CommandEntry() : entry(nullptr), state(CommandState::IDLE) {}
        CommandEntry(PHATrackerEntry* entry);

        void update_cmd_state();

        PHATrackerEntry* entry;
        SnoopStatus snoop_status;
        SnoopStatus::NodeIdSet core_valid_at_init;
        CommandState state;
        bool snp_pst_complete{false};
        bool snp_stvec_complete{false};
        bool snp_stash_complete{false};
        std::set<NodeId>::iterator node_itr;
    } cmd;

    NodeId node_id;

    MeshSnp gen_common_snp(CommandEntry& cmd, SnoopOpcode snp_opcode, const NodeId& snoop_node);
    std::optional<MeshSnp> fsm_snp_pst(CommandEntry& cmd);
    std::optional<MeshSnp> fsm_snp_stvec(CommandEntry& cmd);
    std::optional<MeshSnp> fsm_snp_stash(CommandEntry& cmd);

public:
    In<PHATrackerEntry*> phase_command_in{"phase_command_in"};
    Out<MeshSnp> phase_snp_out{"phase_snoop_out"};

    explicit PHAPhase(const sc_core::sc_module_name& name, NodeId node_id) : Module{name}, node_id{node_id} {}

    bool run() override;
    void dump(std::ostream& os) const override;
};
} // namespace ampere::mesh
