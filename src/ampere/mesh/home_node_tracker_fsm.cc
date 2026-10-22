//
// Copyright (c) 2020 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//
#include <optional>

#include "ampere/mesh/chi/types.h"
#include "ampere/mesh/home_node_tracker.h"
#include "ampere/mesh/home_node_tracker_entry.h"
#include "ampere/mesh/home_node_tracker_fsm.h"

namespace ampere::mesh::tracker_fsm {

std::pair<std::optional<SnoopOpcode>, std::optional<SnoopOpcode>> request_to_snoop(const MeshReq& request,
                                                                                   const SnoopStatus& snoop_status);

SFOp request_to_sf_update_op(RequestOpcode req_op);

void process_event(HomeNodeTracker& tracker, HomeNodeTrackerEntry& entry, const Initialize& ev)
{
    AMPERE_ASSERT(entry.state.main == MainState::Idle,
                  "Initialize occcured outside of Idle state (state is {})",
                  entry.state.main);

    switch (entry.request->Opcode) {
    case RequestOpcode::ReadUnique:
    case RequestOpcode::MakeUnique:
        entry.sf_op = SFOp::ReadLock;
        entry.cache_op = CacheOp::ReadInvalidate;
        entry.state.main = MainState::Read;
        break;
    case RequestOpcode::ReadNotSharedDirty:
        entry.sf_op = SFOp::ReadLock;
        entry.cache_op = CacheOp::Read;
        entry.state.main = MainState::Read;
        break;
    case RequestOpcode::WriteBackFull:
    case RequestOpcode::WriteEvictFull:
        entry.state.main = MainState::CopyBack;
        break;
    case RequestOpcode::Evict:
        entry.sf_op = SFOp::Invalidate;
        entry.state.main = MainState::Evict;
        break;
    default:
        AMPERE_ERROR("request opcode {} not yet implemented", entry.request->Opcode);
        break;
    }

    if (!ev.blocked)
        tracker.ready_to_schedule(entry);
}

void process_event(HomeNodeTracker& tracker, HomeNodeTrackerEntry& entry, const MainPipeStatus& ev)
{
    switch (entry.state.main) {
    case MainState::Read:
        if (ev.sf_status == SFStatus::Full) {
            auto [clean_entry, blocked] = tracker.allocate_for_clean(entry);
            clean_entry->state.main = MainState::Clean;
            if (!blocked)
                tracker.ready_to_schedule(*clean_entry);

            entry.sf_op = SFOp::ResumeAsMiss;
        } else if (ev.sf_status == SFStatus::Lock) {
            entry.sf_op = SFOp::ReadLock;
            tracker.ready_to_schedule(entry);
        } else {
            if (ev.sf_status == SFStatus::Hit) {
                std::tie(entry.snoop_op, entry.snoop_owner_op) = request_to_snoop(entry.request, entry.snoop_status);
                if (entry.snoop_op || entry.snoop_owner_op)
                    entry.state.wait |= Wait4SnpCnt0;
            }

            entry.state.main = MainState::UpdateSF;
            entry.sf_op = request_to_sf_update_op(entry.request->Opcode);
            if (!entry.got_compack)
                entry.state.wait |= Wait4CompAck;

            if (entry.request->Opcode == RequestOpcode::MakeUnique) {
                entry.response_op = ResponseOpcode::Comp;
                entry.data_rsp_status = RespComp_UC;
            } else {
                if (ev.cache_status == CacheStatus::HitClean) {
                    entry.data_op = DataOpcode::CompData;
                    entry.dbid = entry.id;
                    entry.data_rsp_status = RespComp_UC;
                } else if (ev.cache_status == CacheStatus::HitDirty) {
                    entry.data_op = DataOpcode::CompData;
                    entry.dbid = entry.id;
                    // This is not really correct -- because SLC is a memory-side cache, even if we hit a dirty line
                    // we will send the line to the core as clean. But doing this allows a knob in SLC to choose
                    // whether to send dirty data to core.
                    entry.data_rsp_status = RespComp_UD_PD;
                } else if ((entry.state.wait & Wait4SnpCnt0) == 0) {
                    entry.is_mem_request = true;
                    if (entry.request->LikelyShared) {
                        entry.request_op = RequestOpcode::ReadNoSnp;
                        if (entry.state.wait & Wait4CompAck)
                            entry.state.wait &= ~Wait4CompAck;
                        entry.state.wait |= Wait4Data;
                    } else {
                        entry.request_op = RequestOpcode::ReadNoSnpSep;
                        entry.state.wait |= Wait4RRorRSD;
                        if (!entry.sent_response) {
                            entry.response_op = ResponseOpcode::RespSepData;
                            entry.data_rsp_status = RespComp_UC;
                            entry.sent_response = true;
                        }
                    }
                }
            }
        }
        break;
    case MainState::CopyBack:
        entry.response_op = ResponseOpcode::CompDBIDResp;
        entry.state.main = MainState::FillCache;
        entry.state.wait |= Wait4Data;
        break;
    case MainState::Evict:
        entry.response_op = ResponseOpcode::Comp;
        entry.data_rsp_status = RespComp_I;
        entry.state.main = MainState::Idle;
        break;
    case MainState::ReadMemory:
        entry.is_mem_request = true;
        if (entry.request->LikelyShared) {
            entry.request_op = RequestOpcode::ReadNoSnp;
            if (entry.state.wait & Wait4CompAck)
                entry.state.wait &= ~Wait4CompAck;
            entry.state.wait |= Wait4Data;
        } else {
            entry.request_op = RequestOpcode::ReadNoSnpSep;
            entry.state.main = MainState::UpdateSF;
            entry.state.wait |= Wait4RRorRSD;
            if (!entry.sent_response) {
                entry.response_op = ResponseOpcode::RespSepData;
                entry.data_rsp_status = RespComp_UC;
                entry.sent_response = true;
            }
        }
        break;
    case MainState::FillCache:
        // cache_status here is the state of the fill's victim
        if (ev.cache_status == CacheStatus::HitDirty) {
            entry.request_op = RequestOpcode::WriteNoSnpFull;
            entry.is_mem_request = true;
            entry.state.main = MainState::SendDataToMemory;
            entry.state.wait |= Wait4DBID;
            if (entry.request->LikelyShared) {
                entry.data_op = DataOpcode::CompData;
                entry.dbid = entry.id;
                entry.data_rsp_status = RespComp_SC;
                entry.state.wait |= Wait4CompAck;
            }
        } else {
            if (entry.request->LikelyShared) {
                entry.data_op = DataOpcode::CompData;
                entry.dbid = entry.id;
                entry.data_rsp_status = RespComp_SC;
                entry.state.main = MainState::Dealloc;
                entry.state.wait |= Wait4CompAck;
            }
            entry.state.main = MainState::Idle;
        }
        break;
    case MainState::WriteMemory:
        entry.request_op = RequestOpcode::WriteNoSnpFull;
        entry.is_mem_request = true;
        entry.state.main = MainState::SendDataToMemory;
        entry.state.wait |= Wait4DBID;
        break;
    case MainState::SendDataToMemory:
        entry.data_op = DataOpcode::NonCopyBackWrData;
        entry.state.main = MainState::Idle;
        break;
    case MainState::UpdateSF:
        entry.state.main = MainState::Idle;
        break;
    case MainState::MakeUnique:
        AMPERE_ASSERT(
            ev.sf_status == SFStatus::Hit, "MakeUnique issued for {} but it missed the SF", entry.request->Addr);
        std::tie(entry.snoop_op, entry.snoop_owner_op) = request_to_snoop(entry.request, entry.snoop_status);
        if (entry.snoop_op || entry.snoop_owner_op) {
            entry.state.wait |= Wait4SnpCnt0;
        } else {
            entry.response_op = ResponseOpcode::Comp;
            entry.data_rsp_status = RespComp_UC;
            entry.state.main = MainState::Dealloc;
            entry.state.wait |= Wait4CompAck;
        }
        break;
    case MainState::Clean:
        entry.snoop_op = SnoopOpcode::SnpCleanInvalid;
        entry.snoop_owner_op = SnoopOpcode::SnpCleanInvalid;
        entry.state.main = MainState::Dealloc;
        entry.state.wait |= Wait4SnpCnt0;
        break;
    case MainState::Dealloc:
        entry.state.main = MainState::Idle;
        break;
    default:
        AMPERE_FATAL("{} is not yet implemented", entry.state.main);
    }
}

void process_event(HomeNodeTracker& tracker, HomeNodeTrackerEntry& entry, const SnoopResponse& ev)
{
    AMPERE_ASSERT(entry.state.wait & Wait4SnpCnt0, "Received unexpected snoop response");

    size_t snoops_remaining = entry.snoop_status.count() + (entry.snoop_status.primary_snoop_target() ? 1 : 0);

    if (ev.forward)
        entry.snp_fwd = true;

    if (snoops_remaining == 0) {
        entry.state.wait &= ~Wait4SnpCnt0;

        if (entry.state.main == MainState::Read) {
            if (entry.snp_fwd)
                entry.state.main = MainState::Dealloc;
            else
                entry.state.main = MainState::ReadMemory;
        }

        tracker.ready_to_schedule(entry);
    }
}

void process_event(HomeNodeTracker& tracker, HomeNodeTrackerEntry& entry, const CompAck& ev)
{
    AMPERE_ASSERT(entry.state.wait & Wait4CompAck, "Received unexpected CompAck");

    entry.state.wait &= ~Wait4CompAck;
    entry.got_compack = true;
    tracker.ready_to_schedule(entry);
}

void process_event(HomeNodeTracker& tracker, HomeNodeTrackerEntry& entry, const DataRcvd& ev)
{
    AMPERE_ASSERT(entry.state.wait & Wait4Data, "unexpected data");

    entry.chunk_valid |= ev.chunk_valid;

    if (entry.chunk_valid == 0xf) {
        entry.state.wait &= ~Wait4Data;
        if (!entry.request->LikelyShared)
            entry.sf_op = SFOp::Invalidate;

        if (ev.fill_cache) {
            entry.cache_op = ev.is_dirty ? CacheOp::FillDirty : CacheOp::FillClean;
            entry.state.main = MainState::FillCache;
        } else {
            entry.state.main = MainState::WriteMemory;
        }

        tracker.ready_to_schedule(entry);
    }
}

void process_event(HomeNodeTracker& tracker, HomeNodeTrackerEntry& entry, const DataReq& ev)
{
    AMPERE_ASSERT(entry.state.wait & Wait4DBID, "unexpected DBID");

    entry.state.wait &= ~Wait4DBID;
    entry.dbid = ev.dbid;
    tracker.ready_to_schedule(entry);
}

void process_event(HomeNodeTracker& tracker, HomeNodeTrackerEntry& entry, const RetryAck& ev)
{
    entry.state.wait &= ~(Wait4RRorRSD | Wait4DBID);
    entry.state.wait |= Wait4PCrd;

    switch (entry.state.main) {
    case MainState::UpdateSF:
        entry.state.main = MainState::ReadMemory;
        break;
    case MainState::SendDataToMemory:
        entry.state.main = MainState::WriteMemory;
        break;
    default:
        AMPERE_ERROR("did not expect a RetryAck in {} state", entry.state.main);
        break;
    }
}

void process_event(HomeNodeTracker& tracker, HomeNodeTrackerEntry& entry, const CreditGrant& ev)
{
    AMPERE_ASSERT(entry.state.wait & Wait4PCrd, "unexpected PCrdGrant");

    entry.state.wait &= ~Wait4PCrd;
    entry.retry_credit = ev.pcrdtype;
    entry.has_credit = true;

    // Ugly Hack: We may be waiting for a CompAck because we already sent a response;
    // but we do need to restart this request. So if that's all we're waiting for,
    // temporarily make it look like we're not waiting for that CompAck so we can schedule.
    //
    // If we don't do this, then we can be stuck not scheduling as we wait for a CompAck
    // while the core is stuck waiting for data to come back from memory---which will
    // never happen if we don't get the ReadNoSnp request out again. A deadlock.
    //
    bool awaiting_comp_ack = (entry.state.wait & Wait4CompAck) != 0;
    entry.state.wait &= ~Wait4CompAck;

    tracker.ready_to_schedule(entry);

    if (awaiting_comp_ack)
        entry.state.wait |= Wait4CompAck;
}

void process_event(HomeNodeTracker& tracker, HomeNodeTrackerEntry& entry, const ReadReceipt& ev)
{
    AMPERE_ASSERT(entry.state.wait & Wait4RRorRSD, "unexpected ReadReceipt");

    entry.state.wait &= ~Wait4RRorRSD;
    tracker.ready_to_schedule(entry);
}

std::pair<std::optional<SnoopOpcode>, std::optional<SnoopOpcode>> request_to_snoop(const MeshReq& request,
                                                                                   const SnoopStatus& snoop_status)
{
    // LDID is used here to compare the PST as the request->SrcID might have been updated to node id of IDT
    if (snoop_status.primary_snoop_target() && *snoop_status.primary_snoop_target() == request->LDID) {
        // TODO: self snoop would be recognized here
        return {{}, {}};
    }

    auto req_op = request->Opcode;

    switch (req_op) {
    case RequestOpcode::ReadUnique:
        return {SnoopOpcode::SnpUnique, SnoopOpcode::SnpUniqueFwd};
    case RequestOpcode::MakeUnique:
        return {SnoopOpcode::SnpUnique, SnoopOpcode::SnpUnique};
    case RequestOpcode::ReadShared:
        return {SnoopOpcode::SnpShared, SnoopOpcode::SnpSharedFwd};
    case RequestOpcode::ReadNotSharedDirty:
        return {SnoopOpcode::SnpNotSharedDirty, SnoopOpcode::SnpNotSharedDirtyFwd};
    case RequestOpcode::ReadClean:
        return {SnoopOpcode::SnpClean, SnoopOpcode::SnpCleanFwd};
    case RequestOpcode::ReadOnce:
        return {SnoopOpcode::SnpOnce, SnoopOpcode::SnpOnceFwd};
    case RequestOpcode::ReadOnceCleanInvalid:
    case RequestOpcode::ReadOnceMakeInvalid:
        return {SnoopOpcode::SnpUnique, SnoopOpcode::SnpOnceFwd};
    default:
        AMPERE_FATAL("snoop type for request {} not yet implemented", (int)req_op);
        return {SnoopOpcode::SnpUnique, SnoopOpcode::SnpUniqueFwd};
    }
}

SFOp request_to_sf_update_op(RequestOpcode req_op)
{
    switch (req_op) {
    case RequestOpcode::ReadUnique:
    case RequestOpcode::MakeUnique:
        return SFOp::SetUniqueUnlock;
    case RequestOpcode::ReadShared:
    case RequestOpcode::ReadNotSharedDirty:
        return SFOp::SetSharedUnlock;
    case RequestOpcode::CleanInvalid:
        return SFOp::Reset;
    default:
        AMPERE_FATAL("snoop type for request {} not yet implemented", (int)req_op);
        return SFOp::None;
    }
}

} // namespace ampere::mesh::tracker_fsm
