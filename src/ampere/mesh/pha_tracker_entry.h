//
// Copyright (c) 2022 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//

#pragma once

#include <optional>

#include "ampere/mesh/chi/snoop.h"
#include "ampere/mesh/chi/types.h"
#include "ampere/mesh/mesh_req.h"
#include "ampere/mesh/mesh_rsp.h"
#include "ampere/mesh/mesh_snp.h"
#include "ampere/mesh/pha_fsm.h"
#include "ampere/mesh/snoop_filter.h"
#include "ampere/mesh/tracker.enum.h"
#include "ampere/uarch_common/cache.h"
#include <fmt/core.h>
#include <fmt/ostream.h>

namespace ampere::mesh {

using namespace chi;
using uarch_common::Index;

struct PHATrackerEntrySave;

class PHATrackerEntryState
{
public:
    enum State {
        Invalid,     // A free entry
        Allocated,   // Off the free list, but not doing anything yet
        Blocked,     // Blocked by an address match or other internal conflict
        Waiting,     // Waiting for external input (e.g. a CHI Response)
        Ready,       // Ready to be scheduled, but not yet in the pipe
        Executing,   // Actively being executed in the pipeline
        Deallocating // On the deallocation list, but not yet deallocated
    };

    State get_state() const { return state; }
    void set_state(State new_state);

    bool is_valid() const { return state != Invalid; }
    bool is_allocated() const { return state == Allocated; }
    bool is_blocked() const { return state == Blocked; }
    bool is_waiting() const { return state == Waiting; }
    bool is_ready() const { return state == Ready; }
    bool is_executing() const { return state == Executing; }

    friend std::ostream& operator<<(std::ostream& os, State s)
    {
        static const char* s_names[] = {
            "Invalid", "Allocated", "Blocked", "Waiting", "Ready", "Executing", "Deallocating"};
        os << s_names[static_cast<int>(s)];
        return os;
    }

private:
    State state{Invalid};
};

struct PHATrackerEntry
{
    PHATrackerEntry(uint8_t id) : id{id} { reset(); }

    uint64_t id;  /* PHAT table ID */
    uint64_t age; /* age of entry at allocation time */

    // FIXME: I tried to put the following two fields in std::variant, but I apparently gcc 9 doesn't fully support it?
    MeshReq request; /* Request that created this entry (or nullptr if none did) */
    MeshSnp snoop;   /* Snoop Request that created this entry (or nullptr if none did) */
    MeshRsp rsp;

    uint8_t entry_type; /* Credit of different types allow limiting PHAT entries to specific operations */

    PHATrackerEntryState state;      /* State of this entry, i.e., were it is in its lifetime */
    pha_fsm::PHATrackerEntryAux aux; /* Auxiliary data stores data used in later pipelien passes */

    pha_fsm::PHAInput input;   /* Static, dynamic inputs that drive FSM */
    pha_fsm::PHAOutput output; /* Output, including CHI messages to be sent out from FSM */
    PHATrackerEntry* wake;     /* PHAT Id to wake when this Flow is complete (for address conflict) */
    PHATrackerEntry* sfvwake;  /* PHAT Id to wake when this SF victim Flow is complete */

    NodeId stash_nid; /* Node that was selected for stash */
    NodeId chirp_nid; /* Node that most recently sent a response (or data) */

    bool wait4RR;             /* Wait for ReadReceipt or ReadSepData */
    bool wait4DBID;           /* Wait for DBIDResp (or CompDBIDResp) */
    bool wait4Comp;           /* Wait for Comp (or CompDBIDResp) */
    bool wait4PdbData;        /* Wait for a DAT packet */
    bool wait4AdbData;        /* Wait for atomic packet */
    bool wait4CompAck;        /* Wait for CompAck */
    bool wait4SnpCnt0;        /* Wait for all outstanding snoopu responses to be Recieved */
    bool pipelineDone;        /* Pipeline is complete, could be waiting on messages to deallocate */
    bool request_outstanding; /* We (a PxHA) have a request outstanding to a PmHA */
    int snpCounter;           /* Number of remaining outstanding snoops */
    FinalState pdb_state;     /* Cache state of data packet */
    pha_fsm::AdbState_t adb_state;
    std::optional<uint8_t> retry_credit; /* Set to the credit of we received a pcredit after we got a retry */
    bool pcrdgranted; /* True if subsequent request was retried, retained after re-request unlike retry_credit */

    size_t blocker_count{0};

    SnoopStatus snoop_status;      /* Snoop Filter Access*/
    SnoopStatus next_snoop_status; /* Snoop status for writing back into the SF */

    PhysAddr snoop_address;
    Index sf_set;
    int sf_way;

    uint16_t dbid;

    StVecUpdateOpcode stvec_opcode; /* Operation to perform on the SF StVec */
    NodeId stvec_set_node;          /* NodeId to set in the SF StVec */
    NodeId stvec_clear_node;        /* NodeId to clear in the SF Stvec */

    PstUpdateOpcode pst_opcode; /* Operation to perform on the primary snoop target */
    NodeId pst_set_node;        /* NodeId to set */

    /* Received Packet Data
     * These variables are used to store data from packets we received, these can
     * be used when having to transmit packets */
    uint16_t compack_txnid;
    bool compack_rcvd;

    bool trace; // for debug

    bool pdb_is_unique() const { return pdb_state == FinalState::UC || pdb_state == FinalState::UD; }
    bool pdb_is_dirty() const { return pdb_state == FinalState::UD || pdb_state == FinalState::SD; }

    bool SnpIntrReq;
    RsField rs_status;
    bool MmcRespEn;
    bool LikelyShared;
    bool Excl;
    pha_fsm::ReqTagOp_t DatTagOp;
    bool AdbSelect;

    void update_flags()
    {
        wait4RR = output.Wait4RR;
        wait4DBID = output.Wait4DBID;
        wait4Comp = output.Wait4Comp && !input.CompRcvd;
        wait4PdbData = output.Wait4PdbData;
        wait4AdbData = output.Wait4AdbData;
        wait4CompAck = output.Wait4CompAck && !compack_rcvd;
        wait4SnpCnt0 = output.Wait4SnpCnt0;
        pipelineDone = output.PipelineDone;
    }

    bool ready_to_bid() const
    {
        return !pipelineDone && !wait4RR && !wait4DBID && !wait4Comp && !wait4PdbData && !wait4AdbData && !wait4CompAck
               && !wait4SnpCnt0 && !state.is_executing() && !state.is_blocked();
    }

    bool can_deallocate() const
    {
        return pipelineDone && !wait4RR && !wait4DBID && !wait4Comp && !wait4PdbData && !wait4AdbData && !wait4CompAck
               && !wait4SnpCnt0 && !state.is_executing() && !state.is_blocked();
    }

    void reset()
    {
        request = nullptr;
        snoop = nullptr;
        rsp = nullptr;
        entry_type = 0;
        state.set_state(PHATrackerEntryState::Invalid);
        aux.reset();
        input = {};
        input.PipelinePass = 0;
        input.RasAction = pha_fsm::RasAction_t::RAS_NOP;
        output = pha_fsm::PHAOutput();
        output.TotalSNo = -1;
        wake = nullptr;
        sfvwake = nullptr;
        stash_nid = NodeId(-1);
        wait4RR = false;
        wait4DBID = false;
        wait4Comp = false;
        wait4PdbData = false;
        wait4AdbData = false;
        wait4CompAck = false;
        wait4SnpCnt0 = false;
        pipelineDone = false;
        request_outstanding = false;
        snpCounter = 0;
        pdb_state = FinalState::I;
        adb_state = pha_fsm::AdbState_t::ADB_FREE;
        retry_credit = {};
        snoop_status.reset();
        snoop_address = 0_pa;
        next_snoop_status.reset();
        sf_set = std::numeric_limits<std::uint64_t>::max();
        sf_way = -1;
        dbid = -1;
        stvec_opcode = StVecUpdateOpcode::STVEC_NONE;
        stvec_set_node = NodeId{};
        stvec_clear_node = NodeId{};
        pst_opcode = PstUpdateOpcode::PST_NOUPDATE;
        pst_set_node = NodeId{};
        compack_txnid = 0;
        compack_rcvd = false;
        trace = false;
        SnpIntrReq = false;
        rs_status = RsField::NONE;
        MmcRespEn = false;
        LikelyShared = false;
        Excl = false;
        DatTagOp = pha_fsm::ReqTagOp_t::INV;
        AdbSelect = false;
        blocker_count = 0;
        pcrdgranted = false;
    }

    TransactionId txid() const
    {
        if (request)
            return request->txid();
        else if (snoop)
            return snoop->txid();
        else
            return TransactionId{0};
    }

    PHATrackerEntry& operator=(const PHATrackerEntrySave& save);
};

struct ComparePHATrackerEntry
{
    bool operator()(PHATrackerEntry* const& p1, PHATrackerEntry* const& p2) const { return p1->age < p2->age; };
};

// Used for pipeline rejects, this structure saves those portions of a PHATrackerEntry
// that might be changed during a pipeline pass and that need to be restored on a
// rejection.
struct PHATrackerEntrySave
{
    PHATrackerEntrySave() = default;

    pha_fsm::PHAInput input;   /* Static, dynamic inputs that drive FSM */
    pha_fsm::PHAOutput output; /* Output, including CHI messages to be sent out from FSM */
    bool wait4RR;              /* Wait for ReadReceipt or ReadSepData */
    bool wait4DBID;            /* Wait for DBIDResp (or CompDBIDResp) */
    bool wait4Comp;            /* Wait for Comp (or CompDBIDResp) */
    bool wait4PdbData;         /* Wait for a DAT packet */
    bool wait4AdbData;         /* Wait for atomic packet */
    bool wait4CompAck;         /* Wait for CompAck */
    bool wait4SnpCnt0;         /* Wait for all outstanding snoopu responses to be Recieved */
    bool pipelineDone;         /* Pipeline is complete, could be waiting on messages to deallocate */

    PHATrackerEntrySave& operator=(const PHATrackerEntry& entry)
    {
        input = entry.input;
        output = entry.output;
        wait4RR = entry.wait4RR;
        wait4DBID = entry.wait4DBID;
        wait4Comp = entry.wait4Comp;
        wait4PdbData = entry.wait4PdbData;
        wait4AdbData = entry.wait4AdbData;
        wait4CompAck = entry.wait4CompAck;
        wait4SnpCnt0 = entry.wait4SnpCnt0;
        pipelineDone = entry.pipelineDone;
        return *this;
    }
};

inline PHATrackerEntry& PHATrackerEntry::operator=(const PHATrackerEntrySave& save)
{
    input = save.input;
    output = save.output;
    wait4RR = save.wait4RR;
    wait4DBID = save.wait4DBID;
    wait4Comp = save.wait4Comp;
    wait4PdbData = save.wait4PdbData;
    wait4AdbData = save.wait4AdbData;
    wait4CompAck = save.wait4CompAck;
    wait4SnpCnt0 = save.wait4SnpCnt0;
    pipelineDone = save.pipelineDone;
    return *this;
}

#define AMPERE_ENABLE_PHAT_TRACE
#ifdef AMPERE_ENABLE_PHAT_TRACE
#define TRACE(entry, msg, ...)                                                       \
    if ((entry).trace) {                                                             \
        AMPERE_INFO("[{}] {}: " msg, sc_core::sc_time_stamp(), name(), __VA_ARGS__); \
    }
#else
#define TRACE(entry, msg, ...)
#endif

} // namespace ampere::mesh

template<>
struct fmt::formatter<ampere::mesh::PHATrackerEntryState::State> : fmt::ostream_formatter
{};
