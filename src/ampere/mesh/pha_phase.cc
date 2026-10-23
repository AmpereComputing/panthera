//
// Copyright (c) 2023 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//

#include "ampere/mesh/pha_phase.h"
#include "ampere/mesh/mesh_module.h"
#include "ampere/sc/sc.h"

using namespace ampere::mesh;

PHAPhase::CommandEntry::CommandEntry(PHATrackerEntry* entry)
    : entry(entry),
      snoop_status{entry->snoop_status},
      core_valid_at_init{entry->snoop_status.core_valid_as_set()},
      node_itr{core_valid_at_init.begin()}
{
    /* Initialize the CommandEntry.  Here we set phases to complete if we do not need to complete them.
     * The PHASE then goes the phases in order of PST, STVEC, Stash. (mostly) */
    snp_pst_complete = !entry->output.TxSnpOpcode || !snoop_status.primary_snoop_target() || !entry->output.PstEn;
    snp_stvec_complete = !entry->output.TxSnpOpcode || snoop_status.count() == 0 || entry->output.PstOnly;
    snp_stash_complete = !entry->output.TxSnpStashOpcode || entry->output.PstOnly;

    update_cmd_state();
}

static bool invalidate_l3_copy_hint(MeshSnp snp, PHATrackerEntry* entry)
{
    if (entry->input.PHA == pha_fsm::PHA_t::Pmha)
        return false;

    switch (snp->Opcode) {
    case SnoopOpcode::SnpOnce:
    case SnoopOpcode::SnpOnceFwd:
        return false;
    case SnoopOpcode::SnpUnique:
    case SnoopOpcode::SnpUniqueFwd:
    case SnoopOpcode::SnpUniqueStash:
    case SnoopOpcode::SnpCleanShared:
    case SnoopOpcode::SnpCleanInvalid:
    case SnoopOpcode::SnpMakeInvalid:
        return true;
    case SnoopOpcode::SnpClean:
    case SnoopOpcode::SnpCleanFwd:
        return !(entry->snoop || entry->pdb_is_unique() || entry->pdb_is_dirty());
    case SnoopOpcode::SnpShared:
    case SnoopOpcode::SnpSharedFwd:
        return !entry->snoop;
    default:
        return false;
    };

    return false;
}

void PHAPhase::CommandEntry::update_cmd_state()
{
    if (!snp_pst_complete)
        state = CommandState::SNP_PST;
    else if (!snp_stvec_complete)
        state = CommandState::SNP_ST_VEC;
    else if (!snp_stash_complete)
        state = CommandState::SNP_STASH;
    else
        state = CommandState::IDLE;
}

MeshSnp PHAPhase::gen_common_snp(CommandEntry& cmd, SnoopOpcode snp_opcode, const NodeId& snoop_node)
{
    MeshSnp snp = nullptr;
    auto snp_id = cmd.entry->id;
    auto snp_addr = cmd.entry->snoop_address;

    snp = make_clone<MeshSnp>(cmd.entry->request, node_id, snoop_node, snp_id, snp_opcode, snp_addr);
    snp->FwdNID = node_id;
    snp->FwdTxnID = snp_id;

    snp->InvL3Copy = invalidate_l3_copy_hint(snp, cmd.entry);
    return snp;
}

std::optional<MeshSnp> PHAPhase::fsm_snp_pst(CommandEntry& cmd)
{
    MeshSnp snp = nullptr;
    auto pst = cmd.snoop_status.primary_snoop_target();

    if (!cmd.entry->output.TxSnpOpcode)
        return {};
    auto snp_opcode = cmd.entry->output.TxSnpOpcode;

    switch (*snp_opcode) {
    case SnoopOpcode::SnpCleanInvalid:
    case SnoopOpcode::SnpMakeInvalid:
    case SnoopOpcode::SnpShared:
    case SnoopOpcode::SnpUnique:
    case SnoopOpcode::SnpOnce:
        snp = gen_common_snp(cmd, *snp_opcode, *pst);
        snp->RetToSrc = cmd.entry->output.PstRetToSrc;
        break;
    case SnoopOpcode::SnpCleanFwd:
    case SnoopOpcode::SnpSharedFwd:
    case SnoopOpcode::SnpUniqueFwd:
    case SnoopOpcode::SnpOnceFwd:
        snp = gen_common_snp(cmd, *snp_opcode, *pst);
        snp->FwdNID = cmd.entry->request->SrcID;
        snp->FwdTxnID = cmd.entry->request->TxnID;
        snp->RetToSrc = cmd.entry->output.PstRetToSrc;
        break;

    default:
        AMPERE_FATAL("Unhandled Generate Snoop: {}", *snp_opcode);
    };

    return snp;
}

std::optional<MeshSnp> PHAPhase::fsm_snp_stvec(CommandEntry& cmd)
{
    MeshSnp snp = nullptr;
    if (!cmd.entry->output.TxSnpOpcode)
        return {};
    auto snp_opcode = cmd.entry->output.TxSnpOpcode;
    NodeId snoop_node = *cmd.node_itr; /* Contains the node we will snoop next */

    // Note: If Stash Node is in stvec and we have stash snoop to perform, the Phase
    // should continue to send snoop to stash node with proper stash opcode.  Then resume
    // through stvec.  This prevents bubble in pipeline for stash node when its in stvec.
    if (!cmd.snp_stash_complete && snoop_node == cmd.entry->stash_nid) {
        auto snp_stash = fsm_snp_stash(cmd);
        if (snp_stash) {
            cmd.snp_stash_complete = true;
            return snp_stash;
        } else {
            return {};
        }
    }

    // do not send snp if requester is in stvec. TODO (exclude cases with Atomics with SnpMe set and PmHA with
    // ReadNoSnp)
    if (cmd.entry->request->SrcID == snoop_node)
        return {};

    // TODO: In SF cluster mode, if PST or requesting node itself is in this
    // the the cluster group we are snooping, we should not send a snoop to the
    // PST, instead we insert a bubble into the pipline and for domino only
    // send out 3 snoops for that group (maybe same for stash node???)

    switch (*snp_opcode) {
    case SnoopOpcode::SnpCleanInvalid:
    case SnoopOpcode::SnpMakeInvalid:
    case SnoopOpcode::SnpShared:
    case SnoopOpcode::SnpUnique:
    case SnoopOpcode::SnpOnce:
        snp = gen_common_snp(cmd, *snp_opcode, snoop_node);
        break;
    case SnoopOpcode::SnpSharedFwd:
    case SnoopOpcode::SnpOnceFwd:
        snp = gen_common_snp(cmd, *snp_opcode, snoop_node);
        snp->FwdNID = cmd.entry->request->SrcID;
        snp->FwdTxnID = cmd.entry->request->TxnID;
        break;
    default:
        AMPERE_FATAL("Unhandled Generate Snoop: {}", *snp_opcode);
    };

    return snp;
}

std::optional<MeshSnp> PHAPhase::fsm_snp_stash(CommandEntry& cmd)
{
    MeshSnp snp = nullptr;
    auto snp_id = cmd.entry->id;
    if (!cmd.entry->output.TxSnpStashOpcode)
        return {};
    auto snp_opcode = cmd.entry->output.TxSnpStashOpcode;

    switch (*snp_opcode) {
    case SnoopOpcode::SnpStashShared:
        snp = gen_common_snp(cmd, *snp_opcode, cmd.entry->stash_nid);
        snp->FwdNID = node_id;
        snp->FwdTxnID = snp_id;
        break;
    case SnoopOpcode::SnpUniqueStash:
        snp = gen_common_snp(cmd, *snp_opcode, cmd.entry->stash_nid);
        snp->FwdNID = node_id;
        snp->FwdTxnID = snp_id;
        break;
    default:
        AMPERE_FATAL("Unhandled Generate Stash Snoop: {}", *snp_opcode);
    };

    return snp;
}

bool PHAPhase::run()
{
    // Processing current command is complete, get next from queue
    if (cmd.state == CommandState::IDLE) {
        if (phase_command_in->can_take()) {
            auto* entry = phase_command_in->take();
            cmd = CommandEntry{entry};
        } else
            return false;
    }

    /* Generate Snoops */
    std::optional<MeshSnp> snp = std::nullopt;

    switch (cmd.state) {
    case CommandState::IDLE:
        AMPERE_FATAL("This state should never be triggered");
        break;
    case CommandState::SNP_PST: {
        snp = fsm_snp_pst(cmd);
        if (snp)
            cmd.snp_pst_complete = true;
        break;
    }
    case CommandState::SNP_ST_VEC: {
        snp = fsm_snp_stvec(cmd);
        if (snp) {
            cmd.node_itr++;
            cmd.snp_stvec_complete = (cmd.node_itr == cmd.core_valid_at_init.end());
        }
        break;
    }
    case CommandState::SNP_STASH:
        snp = fsm_snp_stash(cmd);
        if (snp)
            cmd.snp_stash_complete = true;
        break;
    };

    if (snp) {
        auto snoop = *snp;
        if (phase_snp_out->can_put()) {
            TRACE(*(cmd.entry), "Sending Snoop Opcode={} to {}", snoop->Opcode, snoop->TgtID);
            phase_snp_out->put(snoop);
        } else
            AMPERE_FATAL("We generated a SNP and never sent it out, this should not happen with zdelay ports");
    }

    cmd.update_cmd_state();
    return cmd.state != CommandState::IDLE;
}

void PHAPhase::dump(std::ostream& os) const
{
    os << "= " << name() << " ===================================" << std::endl;
    os << "| Tracker ID | Phase State | SnpOpcode |" << std::endl;
    fmt::print("| {:8} | {:8} |\n", cmd.entry ? cmd.entry->id : -1, s[static_cast<int>(cmd.state)]);
}
