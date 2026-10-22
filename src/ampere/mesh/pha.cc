//
// Copyright (c) 2022 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//

#include "ampere/mesh/pha.h"
#include "ampere/mesh/pha_state_fsm.h"

#include <boost/logic/tribool.hpp>
#include <signal.h>

namespace ampere::mesh {

static void add_comma_separated_numbers_to_set(json numbers, std::set<uint64_t>& set)
{
    size_t pos;

    if (numbers.is_string()) {
        std::string s = numbers.get<std::string>();

        if (!s.empty()) {
            uint64_t txid;

            while ((pos = s.find(",")) != std::string::npos) {
                txid = std::stoull(s.substr(0, pos), nullptr, 0);
                fmt::print("tracing txid {}\n", txid);
                set.insert(txid);
                s.erase(0, pos + 1);
            }

            txid = std::stoull(s, nullptr, 0);
            fmt::print("tracing txid {}\n", txid);
            set.insert(txid);
        }
    } else if (numbers.is_number_integer()) {
        set.insert(numbers.get<std::uint64_t>());
    } else if (!numbers.is_null()) {
        AMPERE_ERROR("unexpected JSON type for tracing");
    }
}

PHA::PHA(const sc_core::sc_module_name& name, sc::Clock* clock) : MeshModule{name, clock}
{
    retry.define_credit_type(TRACKER_POOL_IRON, num_iron_entries);
    retry.define_credit_type(TRACKER_POOL_GOLD, num_gold_entries);
    retry.define_credit_type(TRACKER_POOL_PLATINUM, num_platinum_entries);
    retry.define_credit_type(TRACKER_POOL_SFV, num_sfv_entries);
    retry.define_credit_type(TRACKER_POOL_SNP, num_snp_entries);

    add_comma_separated_numbers_to_set(trace_by_addr_param.val(), trace_by_addr);
    add_comma_separated_numbers_to_set(trace_by_txid_param.val(), trace_by_txid);

    connect(phase_command, phase.phase_command_in).name("pha_phase_cmd").bandwidth(1).latency(1);
    connect(phase.phase_snp_out, phase_snoop_ret).name("pha_phase_snp").bandwidth(1).latency(0);
}

void PHA::end_of_elaboration()
{
    /* Build list of stash nodes we iterate through in round robin fashion */
    if (is_pxha()) {
        if (local_l3) {
            stash_nodes.push_back(node_id);
        } else {
            auto stash_nodes_vec = config->stash_nodes(node_id.val().die_id());
            std::copy(stash_nodes_vec.begin(), stash_nodes_vec.end(), std::back_inserter(stash_nodes));
            AMPERE_ASSERT(stash_nodes.size(), "{}: no stash nodes configured", name());
        }

        stash_node = stash_nodes.begin();

        if (stash_target_table.val().empty()) {
            if (stash_with_hashing) {
                // We're hashing to target stash nodes, but were not given an explicit table.
                // Try to build one out of the stash_nodes list. This can work, but only with
                // some constraints on the number of nodes.
                //
                AMPERE_ASSERT((64 % stash_nodes.size()) == 0,
                              "trying to build an implicit stash node table, but 64 is not divisible by the number of "
                              "reported stash nodes ({})",
                              stash_nodes.size());

                AMPERE_ASSERT((stash_nodes.size() % 4) == 0,
                              "trying to build an implicit stash node table, but the number of reported stash nodes "
                              "({}) is not divisible by 4",
                              stash_nodes.size());

                auto stash_it = stash_nodes.begin();

                for (size_t group_id = 0; group_id < 64; ++group_id) {
                    std::vector<NodeId> group_ids;

                    for (size_t offset_id = 0; offset_id < 4; ++offset_id)
                        group_ids.push_back(*stash_it++);

                    stash_node_table.push_back(group_ids);

                    if (stash_it == stash_nodes.end())
                        stash_it = stash_nodes.begin();
                }
            }
        } else {
            if (core_group_to_agent_id_map.val().empty()) {
                for (const uint8_t portiongrp : stash_target_table.val()) {
                    std::vector<NodeId> group_ids;
                    for (size_t cg = 0; cg < 4; ++cg)
                        group_ids.push_back(NodeId(NodeId::DieType::CPU,
                                                   0,
                                                   node_id->die_id(),
                                                   (portiongrp / 4) + ((portiongrp % 4) * 16) + cg * 4));
                    stash_node_table.push_back(group_ids);
                }
            } else {
                for (const uint8_t portiongrp : stash_target_table.val()) {
                    std::vector<NodeId> group_ids;
                    if (core_group_to_agent_id_map.val().contains(portiongrp)) {
                        for (const auto& agent_id : core_group_to_agent_id_map.val().at(portiongrp))
                            group_ids.push_back(NodeId(NodeId::DieType::CPU, 0, node_id->die_id(), agent_id));
                    } else {
                        AMPERE_ERROR("Entry not present in core_group_to_agent_id_map");
                    }
                }
            }
        }

        // PxHA update cbusy on any pmha response
        cbusy_responders = config->pmha_nodes();
    }
    if (is_pmha()) {
        // PmHA update cbusy on specific response or data opcodes
        cbusy_rsps = {chi::ResponseOpcode::ReadReceipt,
                      chi::ResponseOpcode::RetryAck,
                      chi::ResponseOpcode::PCrdGrant,
                      chi::ResponseOpcode::CompDBIDResp,
                      chi::ResponseOpcode::Comp};
        cbusy_dats = {chi::DataOpcode::CompData};
    }
}

bool PHA::run()
{
    receive_requests();
    receive_snoops();
    receive_responses(0);
    receive_responses(1);
    receive_data();
    main_pipe.run();
    receive_phase_snps();
    transmit();

    auto [dealloc, pcrd_type] = tracker.deallocate_run();
    if (dealloc) {
        ev_dealloc();
        // We are counting this a bit late here but the current structure of the PHA code does not allow counting this
        // earlier due to some deallocations being buried in tracker's pipe_done() call
        cbusy.update_cntr(chi::ReqEvent::ReqCmpEvent, get_tracker_occupancy());
        auto requester = retry.dealloc(pcrd_type);

        if (requester) {
            // We now have a tracker entry available, send back a credit to any previously denied requesters.
            auto rsp = make<MeshRsp>(node_id, requester->src_id, requester->txn_id, ResponseOpcode::PCrdGrant);
            // We deallocated and decremented the credit we are now sending as a PCrdGrant so re-increment outstanding
            cbusy.update_cntr(chi::ReqEvent::ReqRecvEvent, get_tracker_occupancy());
            cbusy.refresh_counters();
            rsp->CBusyS = cbusy.update_cbusy(get_tracker_occupancy(), cbusy.get_dscbusy(), false);
            rsp->PCrdType = requester->type;

            if (rsp->TgtID == node_id) {
                l3rsp.push_back(rsp);
            } else {
                if (get_response_channel(rsp->Opcode, 0) == 0)
                    txrsp0.push_back(rsp);
                else
                    txrsp1.push_back(rsp);
            }
        }
    }

    bool empty = tracker.empty() && txreq.empty() && txrsp0.empty() && txrsp1.empty() && txsnp.empty() && txdat.empty()
                 && tracker.dealloc_empty() && l3rsp.empty() && l3snp.empty() && l3dat.empty();
    return !empty;
}

void PHA::receive_requests()
{
    /* Take requests and allocate a tracker entry if possible. */
    if (mesh_port.rx().req[0]->can_take()) {
        auto req = mesh_port.rx().req[0]->take();
        ev_recv_req(req,
                    req->Addr,
                    req->Opcode,
                    evd_txnid(req->TxnID),
                    evd_srcid(req->SrcID.id()),
                    evd_tgtid(req->TgtID.id()),
                    evd_allow_retry(req->AllowRetry));

        /* Retry module determines if we can process the event, if we can't a response
         * is sent to the requester that we are unable to process the request */
        auto [can_process, pcrd_type] = retry.process_request(req, false);

        if (can_process) {
            auto entry = tracker.allocate(req, pha_type, pcrd_type);
            entry->input.SfCmd = pha_fsm::SfCmd_initialize(entry->input.Opcode, (bool)entry->input.SfVictim);
            ev_alloc(req, req->Opcode);
            if (req->AllowRetry)
                cbusy.update_cntr(chi::ReqEvent::ReqRecvEvent, get_tracker_occupancy());

            /* Initialize Aux data */
            entry->aux.MyNodeID = node_id;
            entry->aux.RequesterID = req->SrcID;
            entry->aux.PhatEntryIdx = entry->id;
            entry->aux.RnType = pha_fsm::RN_CORE_PHA; // get_rntype(req->SrcID) TODO
            entry->aux.TxnID = req->TxnID;
            entry->aux.ReturnTxnID = req->ReturnTxnID;
            entry->aux.ReturnNID = req->ReturnNID;
            entry->aux.CF_SnpAttr_DoDWT_RetToSrc =
                entry->input.CF_SnpAttr_DoDWT; /* TODO: Not sure if the following line correct? */
            entry->aux.MemAttr.Cacheable = (req->MemAttr & 0x4) ? true : false;
            entry->aux.MemAttr.Allocate = (req->MemAttr & 0x8) ? true : false;

            // This is an override added here as done in the RTL for allocation flows
            if (entry->aux.MemAttr.Allocate)
                entry->input.SfCmd = pha_fsm::SfCmd_t::SF_LA;

            /* New requests are always able to be scheduled, assuming they are not
             * waiting on another transaction to be completed. */
            if (entry->ready_to_bid()) {
                tracker.ready_to_schedule(*entry);
            }

            if (trace_all || (trace_by_addr.count(entry->request->Addr.value()))
                || (trace_by_txid.count(entry->request->txid().value)))
            {
                entry->trace = true;
            }

            TRACE(*entry,
                  "Request {} by {} Addr={} is allocated Entry {} in State {}",
                  req->Opcode,
                  req->SrcID,
                  req->Addr,
                  entry->id,
                  entry->state.get_state());
        } else {
            /* Retry module determined we are to busy to process this request,
             * notify the requester */
            if (req->Opcode != RequestOpcode::PrefetchTgt) {
                auto rsp = make_clone<MeshRsp>(req, node_id, req->SrcID, req->TxnID, ResponseOpcode::RetryAck);
                cbusy.refresh_counters();
                rsp->CBusyS = cbusy.update_cbusy(get_tracker_occupancy(), cbusy.get_dscbusy(), true);
                rsp->PCrdType = pcrd_type;

                if (rsp->TgtID == node_id) {
                    l3rsp.push_back(rsp);
                } else {
                    if (get_response_channel(rsp->Opcode, 0) == 0)
                        txrsp0.push_back(rsp);
                    else
                        txrsp1.push_back(rsp);
                }
            }
        }
    }
}

void PHA::receive_snoops()
{
    if (mesh_port.rx().snp[0]->can_take()) {
        auto [can_process, pcrd_type] = retry.process_snoop(mesh_port.rx().snp[0]->peek());

        if (can_process) {
            auto snp = mesh_port.rx().snp[0]->take();

            auto entry = tracker.allocate(snp, pha_type, pcrd_type);
            ev_alloc(snp, snp->Opcode);
            entry->input.SfCmd = (snp->Opcode == chi::SnoopOpcode::SnpUniqueStash && !entry->input.SnpIntrReq)
                                     ? pha_fsm::SfCmd_t::SF_LANE
                                     : pha_fsm::SfCmd_t::SF_LU;
            cbusy.update_cntr(chi::ReqEvent::ReqRecvEvent, get_tracker_occupancy());

            /* Initialize Aux data */
            entry->aux.MyNodeID = node_id;
            entry->aux.RequesterID = snp->SrcID;
            entry->aux.PhatEntryIdx = entry->id;
            entry->aux.RnType = pha_fsm::RN_CORE_PHA; // get_rntype(snp->SrcID); TODO
            entry->aux.TxnID = snp->TxnID;
            entry->aux.CF_SnpAttr_DoDWT_RetToSrc =
                entry->input.CF_SnpAttr_DoDWT; /* TODO: Not sure if the following line correct? */
            entry->aux.FwdNID = snp->FwdNID;

            if (entry->ready_to_bid()) {
                tracker.ready_to_schedule(*entry);
            }

            if (trace_all || (trace_by_addr.count(entry->snoop->Addr.value()))
                || (trace_by_txid.count(entry->snoop->txid().value)))
            {
                entry->trace = true;
            }

            TRACE(*entry,
                  "Snoop {} by {} Addr={} is allocated Entry {} in State {}",
                  snp->Opcode,
                  snp->SrcID,
                  snp->Addr,
                  entry->id,
                  entry->state.get_state());
        }
    }
}

void PHA::receive_responses(int port)
{
    auto& p = port == 0 ? mesh_port.rx().rsp[0] : mesh_port.rx().rsp[1];
    if (p->can_take()) {
        auto rsp = p->take();
        ev_recv_rsp(rsp, rsp->Opcode, evd_txnid(rsp->TxnID), evd_srcid(rsp->SrcID.id()), evd_tgtid(rsp->TgtID.id()));

        uint16_t txn_id;

        if (rsp->Opcode == chi::ResponseOpcode::PCrdGrant)
            txn_id = retry_requester.pop_retry(rsp->PCrdType, rsp->SrcID);
        else
            txn_id = rsp->TxnID;

        auto& entry = tracker.find_by_id(txn_id);

        entry.rsp = rsp;
        entry.chirp_nid = rsp->SrcID;

        TRACE(entry,
              "Entry {} received response {} {} DatPull={} from {} in State {}",
              entry.id,
              rsp->Opcode,
              rsp->Resp,
              rsp->DatPull,
              rsp->SrcID,
              entry.state.get_state());

        if (chi::frees_request(rsp->Opcode)
            && ((!retries_outstanding && !entry.pcrdgranted)
                || (retries_outstanding && (rsp->Opcode != chi::ResponseOpcode::RetryAck))))
        {
            AMPERE_ASSERT(outstanding_requests > 0,
                          "Response handling deallocates more requests than allocated: {}",
                          rsp->Opcode);
            --outstanding_requests;
            ev_dealloc_request(rsp);
        }

        switch (rsp->Opcode) {
        case chi::ResponseOpcode::ReadReceipt:
            AMPERE_ASSERT(entry.wait4RR, "Unexpected Response Opcode: {}", rsp->Opcode);
            entry.wait4RR = false;
            break;
        case chi::ResponseOpcode::CompDBIDResp:
            AMPERE_ASSERT(entry.wait4DBID, "Unexpected Response Opcode: {}", rsp->Opcode);
            entry.input.CompRcvd = true;
            entry.wait4DBID = false;
            entry.wait4Comp = false;
            entry.aux.CompleterID = rsp->SrcID;
            entry.request_outstanding = false;
            entry.dbid = rsp->DBID;
            break;
        case chi::ResponseOpcode::DBIDResp:
            AMPERE_ASSERT(entry.wait4DBID, "Unexpected Response Opcode: {}", rsp->Opcode);
            entry.wait4DBID = false;
            entry.dbid = rsp->DBID;
            break;
        case chi::ResponseOpcode::RespSepData:
            AMPERE_ASSERT(entry.wait4Comp, "Unexpected Response Opcode: {}, txid={}", rsp->Opcode, rsp->txid().value);
            entry.wait4Comp = false;
            entry.input.CompRcvd = true;
            entry.compack_txnid = rsp->DBID;
            entry.input.CF_CompType_WrDataCancel = true;
            entry.aux.CompleterID = rsp->SrcID;
            entry.request_outstanding = false;
            break;
        case chi::ResponseOpcode::Comp:
            AMPERE_ASSERT(entry.wait4Comp, "Unexpected Response Opcode: {}", rsp->Opcode);
            entry.input.CompRcvd = true;
            entry.wait4Comp = false;
            entry.input.CF_CompType_WrDataCancel = false;
            entry.aux.CompleterID = rsp->SrcID;
            entry.compack_txnid = rsp->DBID;
            entry.request_outstanding = false;
            break;
        case chi::ResponseOpcode::CompAck:
            entry.wait4CompAck = false;
            entry.compack_rcvd = true;
            break;
        case chi::ResponseOpcode::SnpRespFwded:
            AMPERE_ASSERT(entry.snpCounter, "Unexpected Response Opcode: {}", rsp->Opcode);
            entry.snpCounter--;
            entry.input.SnpFwded = true;
            break;
        case chi::ResponseOpcode::SnpResp:
            AMPERE_ASSERT(entry.snpCounter, "Unexpected Response Opcode: {}", rsp->Opcode);
            entry.snpCounter--;
            /* Response for this SnpStash was a DataPull, DataPull is the result if the completer accepts the stash */
            if (rsp->DatPull == DataPull::Read) {
                entry.input.StashRead = rsp->DatPull == DataPull::Read;
                entry.aux.CompleterID = rsp->SrcID;
                entry.aux.StashTgtID = rsp->SrcID;
                entry.dbid = rsp->DBID;
            }
            break;
        case chi::ResponseOpcode::RetryAck:
            /* Completer (PmHA, MCU, etc.) was not able to fufill our request */
            retry_requester.push_retry(rsp->PCrdType, rsp->SrcID, txn_id);
            break;
        case chi::ResponseOpcode::PCrdGrant:
            /* We were given a credit and we can not retry our failed request to completer
             * this can come before retry, but we must wait for retry packet to retry*/
            /* TODO: I am generating two requests in a cycle */
            AMPERE_ASSERT(entry.request, "Retrying entry has no request");
            entry.retry_credit = rsp->PCrdType;
            entry.pcrdgranted = true;
            generate_request(entry);
            break;
        default:
            AMPERE_FATAL("Unhandled Received Response: {}", rsp->Opcode);
            break;
        };

        if (cbusy_responders.contains(rsp->SrcID) || cbusy_rsps.contains(rsp->Opcode)) {
            cbusy.update(rsp->CBusyS, outstanding_requests, (rsp->Opcode == chi::ResponseOpcode::RetryAck));
        }

        try {
            if (rsp->Opcode != ResponseOpcode::RetryAck && rsp->Opcode != ResponseOpcode::PCrdGrant) {
                int row = pha_fsm::pha_state_fsm_recv_rsp(entry, rsp);
                ev_process_state_event_rsp(entry.request, evd_fsm_row(row));
                TRACE(entry, "Entry {} match cache state rsp table row={}", entry.id, row);
                update_snoop_status(entry);
            }
        } catch (pha_fsm::PhaCacheStateFsmNoTransition& exc) {
            try {
                AMPERE_FATAL(
                    "Invalid match on pha state fsm for recv rsp for txid={} in {} with entry {}, Opcode={}, "
                    "CacheState={}, rsp opcode={}, Resp={} FwdState={}, DatPull={}\n",
                    entry.request->txid().value,
                    name(),
                    entry.id,
                    entry.request->Opcode,
                    entry.input.CacheState,
                    rsp->Opcode,
                    rsp->Resp,
                    rsp->FwdState,
                    rsp->DatPull);
            } catch (InvalidResponseState& exc) {
                AMPERE_FATAL("{}; this came from a response with opcode={} and txid={}",
                             exc.what(),
                             rsp->Opcode,
                             rsp->txid().value);
            }
        }

        /* If all snoops sent out have been responded to clear wait flags to
         * resume FSM */
        entry.wait4SnpCnt0 = entry.snpCounter != 0;
        tracker.pup_check(entry);

        if (entry.ready_to_bid())
            tracker.ready_to_schedule(entry);
        else if (entry.can_deallocate()) { // Deallocate entry if done
            tracker.deallocate(entry);
        }

        TRACE(entry, "Entry {} is in State {} after processing response", entry.id, entry.state.get_state());
    }
}

void PHA::receive_data()
{
    if (mesh_port.rx().dat[0]->can_take()) {
        auto dat = mesh_port.rx().dat[0]->take();
        ev_recv_dat(dat, dat->Opcode, evd_txnid(dat->TxnID), evd_srcid(dat->SrcID.id()), evd_tgtid(dat->TgtID.id()));

        uint16_t txn_id = dat->TxnID;
        auto& entry = tracker.find_by_id(txn_id);
        entry.chirp_nid = dat->SrcID;
        try {
            entry.pdb_state = dat->Resp.state();
        } catch (InvalidResponseState& exc) {
            AMPERE_FATAL(
                "{}; this came from a dat with opcode={} and txid={}", exc.what(), dat->Opcode, dat->txid().value);
        }
        entry.input.PdToPha = dat->Resp.pass_dirty();

        TRACE(entry,
              "Entry {} received data {} ({}) from {} in State {}",
              entry.id,
              dat->Opcode,
              dat->Resp,
              dat->SrcID,
              entry.state.get_state());

        // Omiiting the case where the Pxha receives a Stash and Merged Data (CompData) from PmHA
        if ((dat->Opcode == chi::DataOpcode::CompData && (retries_outstanding || !entry.pcrdgranted))
            && ((pha_type == pha_fsm::PHA_t::Pxha && entry.request->Opcode != chi::RequestOpcode::SnpUniqueStash)
                || (pha_type == pha_fsm::PHA_t::Pmha)))
        {
            AMPERE_ASSERT(outstanding_requests > 0, "CompData handling deallocates more requests than allocated");
            ev_dealloc_request(dat);
            --outstanding_requests;
        }

        switch (dat->Opcode) {
        case chi::DataOpcode::SnpRespDataFwded:
            AMPERE_ASSERT(entry.snpCounter, "Unexpected Data Opcode: {}", dat->Opcode);
            entry.snpCounter--;
            entry.input.SnpFwded = true;
            break;
        case chi::DataOpcode::SnpRespData:
            AMPERE_ASSERT(entry.snpCounter, "Unexpected Data Opcode: {}", dat->Opcode);
            entry.compack_txnid = dat->DBID;
            entry.input.PdbValid = true;
            entry.input.SnpDataRcvd = true;
            entry.snpCounter--;
            break;
        case chi::DataOpcode::DataSepResp:
            AMPERE_ASSERT(entry.wait4PdbData, "Unexpected Data Opcode: {}", dat->Opcode);
            entry.input.PdbValid = true;
            entry.wait4PdbData = false;
            break;
        case chi::DataOpcode::CompData:
            AMPERE_ASSERT(entry.wait4Comp, "Unexpected Data Opcode: {}", dat->Opcode);
            entry.aux.CompleterID = dat->HomeNID;
            entry.compack_txnid = dat->DBID;
            entry.input.CompRcvd = true;
            entry.input.PdbValid = true;
            entry.wait4Comp = false;
            entry.wait4PdbData = false;
            entry.request_outstanding = false;
            break;
        case chi::DataOpcode::NonCopyBackWrData:
        case chi::DataOpcode::CopyBackWrData:
        case chi::DataOpcode::WriteDataCancel:
            AMPERE_ASSERT(entry.wait4PdbData, "Unexpected Data Opcode: {}", dat->Opcode);
            entry.input.PdbValid = true;
            entry.wait4PdbData = false;
            break;
        default:
            AMPERE_FATAL("Unhandled Received Data: {}", dat->Opcode);
            break;
        };

        if (cbusy_responders.contains(dat->SrcID) || cbusy_dats.contains(dat->Opcode)) {
            cbusy.update(dat->CBusyS, outstanding_requests);
        }

        try {
            int row = pha_fsm::pha_state_fsm_recv_dat(entry, dat);
            ev_process_state_event_dat(entry.request, evd_fsm_row(row));
            TRACE(entry, "Entry {} match cache state dat table row={}", entry.id, row);
            update_snoop_status(entry);
        } catch (pha_fsm::PhaCacheStateFsmNoTransition& exc) {
            try {
                AMPERE_FATAL(
                    "{}: Invalid match on pha state fsm for recv dat for txid={} in {} with entry {}, Opcode={}, "
                    "CachState={}, Dat Opcode={}, RespState={} RespDirty={} FwdState={}, DatPull={}\n",
                    name(),
                    entry.request->txid().value,
                    name(),
                    entry.id,
                    entry.request->Opcode,
                    entry.input.CacheState,
                    dat->Opcode,
                    dat->Resp.state_or(FinalState::I),
                    dat->Resp.pass_dirty(),
                    dat->FwdState.state_or(FinalState::I),
                    dat->DatPull);
            } catch (InvalidResponseState& exc) {
                AMPERE_FATAL(
                    "{}; this came from a data with opcode={} and txid={}", exc.what(), dat->Opcode, dat->txid().value);
            }
        }

        /* If all snoops sent out have been responded to clear wait flags to
         * resume FSM */
        entry.wait4SnpCnt0 = (entry.snpCounter == 0) ? false : true;
        tracker.pup_check(entry);

        if (entry.ready_to_bid()) {
            tracker.ready_to_schedule(entry);
        }

        /* Deallocate Entry if done */
        else if (entry.can_deallocate())
        {
            tracker.deallocate(entry);
        }

        TRACE(entry, "Entry {} is in State {} after processing data", entry.id, entry.state.get_state());
    }
}

void PHA::receive_phase_snps()
{
    /* Return for snoops generated by Phase */
    while (phase_snoop_ret->can_take()) {
        auto snp = phase_snoop_ret->take();

        if (snp->TgtID == node_id)
            l3snp.push_back(snp);
        else
            txsnp.push_back(snp);
    }
}

void PHA::transmit()
{
    if (!txretry.empty() && mesh_port.tx().req[0]->can_put()) {
        transmit_channel<MeshReq>(txretry, mesh_port.tx().req[0], [this](const MeshReq& pkt) {
            ev_send_req(pkt,
                        pkt->Opcode,
                        pkt->Addr,
                        evd_srcid(pkt->SrcID.id()),
                        evd_tgtid(pkt->TgtID.id()),
                        evd_txnid(pkt->TxnID),
                        evd_returntxnid(pkt->ReturnTxnID),
                        evd_allow_retry(pkt->AllowRetry),
                        evd_qos(pkt->QoS.to_level()));
        });
    } else if (!txreq.empty() && mesh_port.tx().req[0]->can_put()) {
        transmit_channel_if<MeshReq>(
            txreq,
            mesh_port.tx().req[0],
            [this](const MeshReq& pkt) -> bool {
                if (!cbusy.enable || (outstanding_requests < cbusy.num_requests()) || !pkt->AllowRetry) {
                    if (pkt->Opcode != chi::RequestOpcode::PrefetchTgt && pkt->AllowRetry) {
                        ++outstanding_requests;
                        ev_alloc_request(pkt);
                    }
                    return true;
                }
                return false;
            },
            [this](const MeshReq& pkt) {
                ev_send_req(pkt,
                            pkt->Opcode,
                            pkt->Addr,
                            evd_srcid(pkt->SrcID.id()),
                            evd_tgtid(pkt->TgtID.id()),
                            evd_txnid(pkt->TxnID),
                            evd_returntxnid(pkt->ReturnTxnID),
                            evd_allow_retry(pkt->AllowRetry),
                            evd_qos(pkt->QoS.to_level()));
            });
    }
    transmit_channel<MeshRsp>(txrsp0, mesh_port.tx().rsp[0], [this](const MeshRsp& pkt) {
        ev_send_rsp(pkt,
                    pkt->Opcode,
                    evd_srcid(pkt->SrcID.id()),
                    evd_tgtid(pkt->TgtID.id()),
                    evd_txnid(pkt->TxnID),
                    evd_dbid(pkt->DBID),
                    pkt->CBusyS,
                    evd_qos(pkt->QoS.to_level()));
    });
    transmit_channel<MeshRsp>(txrsp1, mesh_port.tx().rsp[1], [this](const MeshRsp& pkt) {
        ev_send_rsp(pkt,
                    pkt->Opcode,
                    evd_srcid(pkt->SrcID.id()),
                    evd_tgtid(pkt->TgtID.id()),
                    evd_txnid(pkt->TxnID),
                    evd_dbid(pkt->DBID),
                    pkt->CBusyS,
                    evd_qos(pkt->QoS.to_level()));
    });
    transmit_channel<MeshSnp>(txsnp, mesh_port.tx().snp[0], [this](const MeshSnp& pkt) {
        ev_send_snp(pkt,
                    pkt->Opcode,
                    pkt->Addr,
                    evd_srcid(pkt->SrcID.id()),
                    evd_tgtid(pkt->TgtID.id()),
                    evd_txnid(pkt->TxnID),
                    evd_qos(pkt->QoS.to_level()));
    });
    transmit_channel<MeshDat>(txdat, mesh_port.tx().dat[0], [this](const MeshDat pkt) {
        ev_send_dat(pkt,
                    pkt->Opcode,
                    evd_srcid(pkt->SrcID.id()),
                    evd_tgtid(pkt->TgtID.id()),
                    evd_txnid(pkt->TxnID),
                    evd_dbid(pkt->DBID),
                    pkt->CBusyS,
                    evd_qos(pkt->QoS.to_level()));
    });

    // Local L3 outputs
    transmit_channel<MeshSnp>(l3snp, l3_port.tx().snp[0], [this](const MeshSnp& pkt) {
        ev_send_snp(pkt,
                    pkt->Opcode,
                    pkt->Addr,
                    evd_srcid(pkt->SrcID.id()),
                    evd_tgtid(pkt->TgtID.id()),
                    evd_txnid(pkt->TxnID),
                    evd_qos(pkt->QoS.to_level()));
    });
    transmit_channel<MeshRsp>(l3rsp, l3_port.tx().rsp[0], [this](const MeshRsp& pkt) {
        ev_send_rsp(pkt,
                    pkt->Opcode,
                    evd_srcid(pkt->SrcID.id()),
                    evd_tgtid(pkt->TgtID.id()),
                    evd_txnid(pkt->TxnID),
                    evd_dbid(pkt->DBID),
                    pkt->CBusyS,
                    evd_qos(pkt->QoS.to_level()));
    });
    transmit_channel<MeshDat>(l3dat, l3_port.tx().dat[0], [this](const MeshDat pkt) {
        ev_send_dat(pkt,
                    pkt->Opcode,
                    evd_srcid(pkt->SrcID.id()),
                    evd_tgtid(pkt->TgtID.id()),
                    evd_txnid(pkt->TxnID),
                    evd_dbid(pkt->DBID),
                    pkt->CBusyS,
                    evd_qos(pkt->QoS.to_level()));
    });
}

template<typename T, typename CallEvent>
void PHA::transmit_channel(TrafficBuffer<T>& txq, OutOpt<T>& port, CallEvent ev)
{
    if (!txq.empty() && port->can_put()) {
        auto pkt = txq.front();
        txq.pop_front();
        port->put(pkt);
        ev(pkt);
    }
}

template<typename T, typename CallCondition, typename CallEvent>
void PHA::transmit_channel_if(TrafficBuffer<T>& txq, OutOpt<T>& port, CallCondition cond, CallEvent ev)
{
    if (!txq.empty() && port->can_put() && cond(txq.front())) {
        transmit_channel(txq, port, ev);
    }
}

bool PHA::arbitrate(Pipeline<PipePayload>::Data req)
{
    auto entry = tracker.get_next_ready_entry();

    if (entry) {
        req = {entry, PHATrackerEntrySave{}};
        TRACE(*entry, "Entry {} is in the pipe", entry->id);
        ev_schedule(entry->request, entry->request->Opcode, entry->request->Addr, evd_txnid(entry->request->TxnID));
    }

    return true;
}

bool PHA::access(Pipeline<PipePayload>::Data req)
{
    auto& [entry_ptr, save] = req.get();
    auto& entry = *entry_ptr;
    ev_access(entry.request);

    /* Determine if address requested is in a snoop filter */
    bool schedule, invalidate;
    std::tie(schedule, invalidate) = access_sf(entry);

    if (schedule) {
        // schedule is only set to true, when all ways in the set are locked, so we need to reschedule.
        // going from Executing -> Ready state is not allowed, so we transition to Waiting state to
        // allow us to reschedule the entry
        entry.state.set_state(PHATrackerEntryState::Waiting);
        tracker.ready_to_schedule(entry);
        req.invalidate();
        TRACE(entry, "Entry {} is in state {} after reschedule", entry.id, entry.state.get_state());
        return true;
    }

    if (invalidate) {
        req.invalidate();
        return true;
    }

    save = entry;

    /* Increment the number of times this PHA entry has flowed through the pipeline
     * this is used to determine next set of outputs */
    if (!entry.pipelineDone)
        entry.input.PipelinePass++;

    /* Set L3StashEn */
    entry.input.L3StashEn =
        (!entry.input.CopyBackL2L3 && entry.input.SfHit && entry.input.RnIsPst && !entry.input.Gt1Copy
         && is_copyback(entry.input.Opcode))
        || (is_pmha() && is_write(entry.input.Opcode) && entry.aux.MemAttr.Cacheable && entry.aux.MemAttr.Allocate);

    /* Process TrackerFSM to get next transition state */
    try {
        auto old_sno = entry.output.TotalSNo;
        tracker.process_event(entry);
        auto new_sno = entry.output.TotalSNo;
        ev_process_event(entry.request, evd_old_state(old_sno), evd_new_state(new_sno));
    } catch (pha_fsm::PhaFsmNoTransition& exc) {
        AMPERE_FATAL("{}\n...while processing txid={}, Addr={}, TotalSNo={} in {}\n",
                     exc.what(),
                     entry.request->txid().value,
                     entry.snoop ? entry.snoop->Addr : entry.request->Addr,
                     entry.output.TotalSNo,
                     name());
    }

    if (entry.output.TxReqOpcode && *entry.output.TxReqOpcode == chi::RequestOpcode::Evict) {
        if (enable_silent_evictions) {
            entry.output.TxReqOpcode = std::nullopt;
            entry.output.Wait4Comp = false;
        }
    }

    /* Update flags and set as determined by trackerFSM */
    entry.update_flags();
    write_sf(entry);
    entry.input.DoDST = entry.output.NxtDoDST;
    entry.input.SfCmd = entry.output.NxtSfCmd;
    tracker.pup_check(entry);

    TRACE(entry,
          "Entry {} is in FSM TotalSNo {} after pipeline pass {}",
          entry.id,
          entry.output.TotalSNo,
          entry.input.PipelinePass);

    return true;
}

bool PHA::pass_is_rejected(const PHATrackerEntry& entry)
{
    // NOTE: Don't reject based on TxReqFIFO size; the whole mechanism here is
    // actually different in reality.
    //
    // if (entry.output.TxReqOpcode) {
    //    if (entry.retry_credit && txretry.size() >= txretry_max_size) {
    //        ev_pipeline_reject(entry.request, evd_reject_reason(TxREQFull));
    //        return true;
    //    }
    //    else if (txreq.size() > txreq_max_size) {
    //        ev_pipeline_reject(entry.request, evd_reject_reason(TxREQFull));
    //        return true;
    //    }
    //}

    if (entry.output.TxRsp0Opcode && txrsp0.size() >= txrsp0_max_size) {
        ev_pipeline_reject(entry.request, evd_reject_reason(TxRSP0Full));
        return true;
    }

    if (entry.output.TxRsp1Opcode && txrsp1.size() >= txrsp1_max_size) {
        ev_pipeline_reject(entry.request, evd_reject_reason(TxRSP1Full));
        return true;
    }

    // TODO: Snoops

    if (entry.output.TxDatOpcode) {
        if (entry.output.TxDatTgtID == node_id && l3dat.size() >= txdat_max_size) {
            ev_pipeline_reject(entry.request, evd_reject_reason(TxDATFull));
            return true;
        } else if (txdat.size() >= txdat_max_size) {
            ev_pipeline_reject(entry.request, evd_reject_reason(TxDATFull));
            return true;
        }
    }

    return false;
}

bool PHA::update(Pipeline<PipePayload>::Data req)
{
    auto& [entry_ptr, save] = req.get();
    auto& entry = *entry_ptr;

    if (pass_is_rejected(entry)) {
        entry = save;
    } else {
        /* Create Messages */
        generate_request(entry);
        generate_response(entry, 0);
        generate_response(entry, 1);
        generate_snoops(entry);
        generate_data(entry);

        int row = pha_fsm::pha_state_fsm_pipe(entry);
        if (row != -1) {
            ev_process_state_event(entry.request, evd_fsm_row(row));
            TRACE(entry, "Entry {} match pipeline cache state table row={}", entry.id, row);
            update_snoop_status(entry);
        }
    }

    tracker.pipe_done(entry);
    TRACE(entry,
          "Entry {} is at the end of the pipeline. TotalSNo={}, Pass={}, state={}",
          entry.id,
          entry.output.TotalSNo,
          entry.input.PipelinePass,
          entry.state.get_state());

    return true;
}

bool is_data_src_snp(PHATrackerEntry& entry)
{
    auto snoop_state = entry.snoop_status.get_state();
    if ((snoop_state == chi::FinalState::UC) || (snoop_state == chi::FinalState::UD)
        || (snoop_state == chi::FinalState::SD) || ((snoop_state == chi::FinalState::SC) && !(entry.input.RnIsPst)))
        return true;

    return false;
}

std::pair<bool, bool> PHA::access_sf(PHATrackerEntry& entry)
{
    switch (entry.input.SfCmd) {
    /* No Operation this pipeline pass */
    case pha_fsm::SfCmd_t::SF_NOP:

    /* sf writes happen after the pipeline pass, happens in write_sf(...) */
    case pha_fsm::SfCmd_t::SF_WR:
    case pha_fsm::SfCmd_t::SF_WRRS:
        return {false, false};

    /* Lookup Allocate: Perform a snoop filter lookup and if miss occurred allocate
     * either an empty way or find a valid victim */
    case pha_fsm::SfCmd_t::SF_LA:
        /* Lookup snoop filter to see if address exists */
        if (entry.sf_way == -1) {
            std::tie(entry.sf_set, entry.sf_way) = snoop_filter.lookup(entry.request, entry.request->Addr, entry.trace);
        }

        /* data exists in snoop filter */
        if (entry.sf_way != -1) {
            /* Snoop Filter Hit */
            if (snoop_filter.get_rs(entry.sf_set, entry.sf_way) == RsField::VICTIM) {
                entry.sf_way = -1;
                TRACE(entry,
                      "Entry {}, SF_LA getting rejected, as the SF RS field in VICTIM set={}, way={}",
                      entry.id,
                      entry.sf_set,
                      entry.sf_way);
                return {false, true};
            }
            snoop_filter.set_rs(entry.request, entry.sf_set, entry.sf_way, RsField::IN_USE);
            entry.snoop_status = snoop_filter.get_data(entry.sf_set, entry.sf_way);
            entry.next_snoop_status = entry.snoop_status;
            entry.snoop_address = snoop_filter.get_address(entry.sf_set, entry.sf_way);
            entry.input.SfHit = true;
            entry.input.CacheState = entry.snoop_status.get_state();
            entry.input.Gt1Copy = entry.snoop_status.count() > 0;
            entry.input.PO = entry.snoop_status.proxy_owned();
            auto primary_snoop_target = entry.snoop_status.primary_snoop_target();
            if (primary_snoop_target)
                entry.input.RnIsPst = entry.request->SrcID == primary_snoop_target;
            entry.input.DataSrcSnp = is_data_src_snp(entry);
            TRACE(entry,
                  "Entry {}, SF_LA Access: Hit={} set={}, way={}, RS state={}",
                  entry.id,
                  entry.input.SfHit,
                  entry.sf_set,
                  entry.sf_way,
                  snoop_filter.get_rs(entry.sf_set, entry.sf_way));
            return {false, false};
        }

        /* data doesn't exist in snoop filter, find empty way and populate data */
        entry.sf_way = snoop_filter.find_empty_way(entry.request, entry.sf_set);
        if (entry.sf_way != -1) {
            snoop_filter.set_rs(entry.request, entry.sf_set, entry.sf_way, RsField::IN_USE);
            entry.next_snoop_status.reset();
            entry.input.SfHit = false;
            TRACE(entry,
                  "Entry {}, SF_LA Access: Hit={}, set={}, way={}, RS state={}",
                  entry.id,
                  entry.input.SfHit,
                  entry.sf_set,
                  entry.sf_way,
                  snoop_filter.get_rs(entry.sf_set, entry.sf_way));
            return {false, false};
        }

        /* data doesn't exist in snoop filter, Sf is full, find victim and populate it */
        entry.sf_way = snoop_filter.find_victim(entry.request, entry.sf_set);
        if (entry.sf_way != -1) {
            auto snp_addr = snoop_filter.get_address(entry.sf_set, entry.sf_way);
            auto snp_req = make_child<MeshReq>(entry.request,
                                               node_id,
                                               entry.request->TgtID,
                                               0, /* TxnID of 0 will be replaced after allocation is determined */
                                               RequestOpcode::CleanInvalid,
                                               snp_addr);

            auto [can_process, pcrd_type] = retry.process_request(snp_req, true);

            if (can_process) {
                // Spawn SFV entry
                snoop_filter.set_rs(entry.request, entry.sf_set, entry.sf_way, RsField::VICTIM);
                entry.snoop_status = snoop_filter.get_data(entry.sf_set, entry.sf_way);
                entry.input.SfHit = false;
                entry.snoop_address = snp_addr;
                entry.input.Gt1Copy = entry.snoop_status.count() > 0;
                TRACE(entry,
                      "Entry {}, SF_LA Access: set={}, way={}, RS state={}",
                      entry.id,
                      entry.sf_set,
                      entry.sf_way,
                      snoop_filter.get_rs(entry.sf_set, entry.sf_way));

                auto snp_entry = tracker.allocate(snp_req, entry, pcrd_type);
                entry.next_snoop_status = entry.snoop_status;
                snp_entry->sf_set = entry.sf_set;
                snp_entry->sf_way = entry.sf_way;
                ev_alloc(snp_req, snp_req->Opcode);
                if (snp_req->AllowRetry)
                    cbusy.update_cntr(chi::ReqEvent::ReqRecvEvent, get_tracker_occupancy());
                snp_req->TxnID = entry.id;
                snp_entry->input.SfVictim = true;
                snp_entry->input.EWA = true; // in RTL EWA field in MemAttr is set to 1 for SFV
                snp_entry->input.SfCmd =
                    pha_fsm::SfCmd_initialize(snp_entry->input.Opcode, (bool)snp_entry->input.SfVictim);
                if (snp_entry->ready_to_bid()) {
                    tracker.ready_to_schedule(*snp_entry);
                }
                TRACE(entry,
                      "Entry {} spawned a SFV (Entry {}), is now in state {}",
                      entry.id,
                      snp_entry->id,
                      entry.state.get_state());
                if (trace_all || entry.trace || snoop_filter.get_trace(entry.sf_set, entry.sf_way)
                    || (trace_by_addr.contains(snp_addr.value())
                        || (trace_by_txid.count(snp_entry->request->txid().value))))
                {
                    snp_entry->trace = true;
                }
                TRACE(
                    *snp_entry,
                    "Entry {} is allocated as an SFV in state {} SF set={} SF way={}, spawned by Entry {}, address {}",
                    snp_entry->id,
                    snp_entry->state.get_state(),
                    snp_entry->sf_set,
                    snp_entry->sf_way,
                    entry.id,
                    entry.request->Addr);
                return {false, false};
            } else {
                tracker.schedule_blocked_sfv(entry);
                TRACE(entry, "Entry {} could not spawn a SFV, is now in state {}", entry.id, entry.state.get_state());
                entry.sf_way = -1;
                return {false, true};
            }
        }

        // SF is currently full and all ways are locked.  This PHAT entry can't
        // make forward progress and is waiting on on previous phat entries
        // to complete that alias to the same way
        AMPERE_ASSERT(entry.sf_way == -1, "Entry should not get a valid way");
        TRACE(entry, "Entry {}, Access SF is full with all ways inuse/victim.", entry.id);
        return {true, true};

    case pha_fsm::SfCmd_t::SF_LANE:
        /* Lookup snoop filter to see if address exists */
        if (entry.sf_way == -1) {
            std::tie(entry.sf_set, entry.sf_way) = snoop_filter.lookup(entry.request, entry.request->Addr, entry.trace);
        }

        /* data exists in snoop filter */
        if (entry.sf_way != -1) {
            /* Snoop Filter Hit */
            snoop_filter.set_rs(entry.request, entry.sf_set, entry.sf_way, RsField::IN_USE);
            entry.snoop_status = snoop_filter.get_data(entry.sf_set, entry.sf_way);
            entry.next_snoop_status = entry.snoop_status;
            entry.snoop_address = snoop_filter.get_address(entry.sf_set, entry.sf_way);
            entry.input.SfHit = true;
            entry.input.CacheState = entry.snoop_status.get_state();
            entry.input.Gt1Copy = entry.snoop_status.count() > 0;
            entry.input.PO = entry.snoop_status.proxy_owned();
            auto primary_snoop_target = entry.snoop_status.primary_snoop_target();
            if (primary_snoop_target)
                entry.input.RnIsPst = entry.request->SrcID == primary_snoop_target;
            entry.input.DataSrcSnp = is_data_src_snp(entry);
            TRACE(entry,
                  "Entry {}, SF_LA Access: Hit={} set={}, way={}, RS state={}",
                  entry.id,
                  entry.input.SfHit,
                  entry.sf_set,
                  entry.sf_way,
                  snoop_filter.get_rs(entry.sf_set, entry.sf_way));
            return {false, false};
        }

        /* data doesn't exist in snoop filter, find empty way and populate data */
        entry.sf_way = snoop_filter.find_empty_way(entry.request, entry.sf_set);
        if (entry.sf_way != -1) {
            snoop_filter.set_rs(entry.request, entry.sf_set, entry.sf_way, RsField::IN_USE);
            entry.next_snoop_status.reset();
            entry.snoop_address =
                entry.request->Addr; // This is required because SnpStashShared needs to have a stash address
            entry.input.SfHit = false;
            TRACE(entry,
                  "Entry {}, SF_LA Access: Hit={}, set={}, way={}, RS state={}",
                  entry.id,
                  entry.input.SfHit,
                  entry.sf_set,
                  entry.sf_way,
                  snoop_filter.get_rs(entry.sf_set, entry.sf_way));
            return {false, false};
        }

        entry.input.SfCmd = pha_fsm::SfCmd_t::SF_NOP;
        return {false, false};

    /* Lookup: Only determine if we hit/miss on a snoop filter lookup. */
    case pha_fsm::SfCmd_t::SF_LU:
        /* Lookup snoop filter to see if address exists */
        if (entry.sf_way == -1) {
            std::tie(entry.sf_set, entry.sf_way) = snoop_filter.lookup(entry.request, entry.request->Addr, entry.trace);
        }

        /* data exists in snoop filter */
        if (entry.sf_way != -1) {
            /* Snoop Filter Hit */
            if (entry.SnpIntrReq) {
                TRACE(entry,
                      "Entry {}, SnpIntrReq and SF RS is being saved in entry, set={}, way={}, RS state={}",
                      entry.id,
                      entry.sf_set,
                      entry.sf_way,
                      snoop_filter.get_rs(entry.sf_set, entry.sf_way));
                entry.rs_status = snoop_filter.get_rs(entry.sf_set, entry.sf_way);
                snoop_filter.set_rs(entry.snoop, entry.sf_set, entry.sf_way, RsField::IN_USE);
                return {false, false};
            }
            if (snoop_filter.get_rs(entry.sf_set, entry.sf_way) == RsField::VICTIM) {
                if (entry.snoop) {
                    // This is a special case, where the state before incoming snoop was HEAD->SFV. HEAD is almost about
                    // to complete, so the SNP got in HEAD->SNP->SFV The rs_status should be saved here and restored
                    // later
                    TRACE(entry,
                          "Entry {}, SF RS is being saved in entry, set={}, way={}, RS state={}",
                          entry.id,
                          entry.sf_set,
                          entry.sf_way,
                          snoop_filter.get_rs(entry.sf_set, entry.sf_way));
                    entry.rs_status = snoop_filter.get_rs(entry.sf_set, entry.sf_way);
                    snoop_filter.set_rs(entry.snoop, entry.sf_set, entry.sf_way, RsField::IN_USE);
                } else {
                    TRACE(entry,
                          "Entry {}, SF_LU getting rejected, as the SF RS field in VICTIM set={}, way={}",
                          entry.id,
                          entry.sf_set,
                          entry.sf_way);
                    entry.sf_way = -1;
                    return {false, true};
                }
            }
            // dont assign snoop filter status entries when it is an interrupting snoop, as it was already
            // assigned in the pup_check logic phase.
            if (!entry.SnpIntrReq) {
                entry.snoop_status = snoop_filter.get_data(entry.sf_set, entry.sf_way);
                entry.next_snoop_status = entry.snoop_status;
                entry.snoop_address = snoop_filter.get_address(entry.sf_set, entry.sf_way);
                entry.input.SfHit = true;
                entry.input.CacheState = entry.snoop_status.get_state();
                entry.input.Gt1Copy = entry.snoop_status.count() > 0;
                entry.input.PO = entry.snoop_status.proxy_owned();
                auto primary_snoop_target = entry.snoop_status.primary_snoop_target();
                if (primary_snoop_target)
                    entry.input.RnIsPst = entry.request->SrcID == primary_snoop_target;
                entry.input.DataSrcSnp = is_data_src_snp(entry);
                snoop_filter.set_rs(entry.snoop, entry.sf_set, entry.sf_way, RsField::IN_USE);
            }
        }

        if (entry.sf_way != -1) {
            TRACE(entry,
                  "Entry {}, SF_LU Access: Hit={}, set={}, way={}, RS state={}",
                  entry.id,
                  entry.input.SfHit,
                  entry.sf_set,
                  entry.sf_way,
                  snoop_filter.get_rs(entry.sf_set, entry.sf_way));
        } else
            TRACE(entry,
                  "Entry {}, SF_LU Access: Hit={}, set={}, way={}",
                  entry.id,
                  entry.input.SfHit,
                  entry.sf_set,
                  entry.sf_way);

        return {false, false};

    /* Read out SF entry, this is used for SF victims on the first pipeline pass. */
    case pha_fsm::SfCmd_t::SF_RD:
        entry.input.SfHit = true;
        return {false, false};
    default:
        AMPERE_FATAL("Unknown Snoop filter command:");
        return {false, true};
    };
}

void PHA::update_snoop_status(PHATrackerEntry& entry)
{
    switch (entry.stvec_opcode) {
    case StVecUpdateOpcode::STVEC_NONE:
        break;
    case StVecUpdateOpcode::STVEC_SET_ONLY:
        entry.next_snoop_status.set_stvec(entry.stvec_set_node);
        break;
    case StVecUpdateOpcode::STVEC_CLEAR_ONLY:
        entry.next_snoop_status.clear_stvec(entry.stvec_clear_node);
        break;
    case StVecUpdateOpcode::STVEC_SET_AND_CLEAR:
        entry.next_snoop_status.set_stvec(entry.stvec_set_node);
        entry.next_snoop_status.clear_stvec(entry.stvec_clear_node);
        break;
    case StVecUpdateOpcode::STVEC_CLEAR_ALL:
        entry.next_snoop_status.clear_all();
        break;
    case StVecUpdateOpcode::STVEC_CLEAR_ALL_EXCEPT_RN:
        entry.next_snoop_status.clear_all_except(entry.request->SrcID);
        break;
    };

    switch (entry.pst_opcode) {
    case PST_NOUPDATE:
        /* Don't update PST */
        break;
    case PST_IS_NODEID:
        entry.next_snoop_status.set_primary_snoop_target(entry.pst_set_node);
        break;
    case PST_CALC_NEW:
        entry.next_snoop_status.pop_primary_snoop_target();
        break;
    };

    entry.next_snoop_status.set_state(entry.input.CacheState);
    entry.next_snoop_status.set_proxy_owned(entry.input.PO);
    TRACE(entry, "Entry {}, STVEC Update {} CacheState={}", entry.id, entry.next_snoop_status, entry.input.CacheState);
}

chi::FinalState PHA::calc_wakeid_pdb_state(PHATrackerEntry& entry)
{
    AMPERE_ASSERT(entry.snoop, "Entry should be a Snoop");
    // Only do this if we have something to wake up and it is an interrupting snoop
    switch (entry.snoop->Opcode) {
    case SnoopOpcode::SnpShared:
    case SnoopOpcode::SnpSharedFwd:
    case SnoopOpcode::SnpClean:
    case SnoopOpcode::SnpCleanFwd:
    case SnoopOpcode::SnpOnce:
    case SnoopOpcode::SnpOnceFwd:
        return entry.pdb_state;
    case SnoopOpcode::SnpUnique:
    case SnoopOpcode::SnpUniqueFwd:
    case SnoopOpcode::SnpUniqueStash:
    case SnoopOpcode::SnpCleanInvalid:
    case SnoopOpcode::SnpMakeInvalid:
        return chi::FinalState::I;
    case SnoopOpcode::SnpCleanShared:
        if (entry.pdb_state != chi::FinalState::I)
            return chi::FinalState::SC;
        else
            return entry.pdb_state;
    default:
        return entry.pdb_state;
    }
}

void PHA::restore_intr_req_fields(PHATrackerEntry* interrupted_req, PHATrackerEntry& interrupting_snp)
{
    interrupted_req->input.CacheState = interrupting_snp.input.CacheState;
    interrupted_req->input.PO = interrupting_snp.input.PO;
    interrupted_req->pdb_state =
        interrupting_snp.input.IntrPdbValid ? calc_wakeid_pdb_state(interrupting_snp) : interrupted_req->pdb_state;
    interrupted_req->input.PdToPha = interrupted_req->pdb_is_dirty();
    interrupted_req->input.PdbValid = interrupting_snp.input.IntrPdbValid
                                          ? (calc_wakeid_pdb_state(interrupting_snp) != FinalState::I)
                                          : interrupted_req->input.PdbValid;
    interrupted_req->next_snoop_status = interrupting_snp.next_snoop_status;
    interrupted_req->snoop_status = interrupting_snp.snoop_status;
    interrupted_req->input.Gt1Copy = interrupted_req->snoop_status.count() > 0;
}

void PHA::write_sf(PHATrackerEntry& entry)
{
    switch (entry.input.SfCmd) {
    /* No Operation this pipeline pass */
    case pha_fsm::SfCmd_t::SF_NOP:

    /* sf reads happen before the pipeline pass, happens in access_sf(...) */
    case pha_fsm::SfCmd_t::SF_LA:
    case pha_fsm::SfCmd_t::SF_LANE:
    case pha_fsm::SfCmd_t::SF_LU:
    case pha_fsm::SfCmd_t::SF_RD:
        return;

    /* Write: Update the particular way/address of a snoop filter line */
    case pha_fsm::SfCmd_t::SF_WR:
        // if it is a Snoop that interrupted a request, restore the fields of the wakeid here
        // in this case it doesn't really write anything to Snoop Filter
        if (is_pxha() && entry.wake && entry.input.SnpIntrReq) {
            auto wake = entry.wake;
            restore_intr_req_fields(wake, entry);
            TRACE(entry,
                  "Interrupting Snoop {} restoring fields back to interrupted request {}, Cachestate {} pdb_state {} "
                  "PdbValid {}",
                  entry.id,
                  wake->id,
                  wake->input.CacheState,
                  wake->pdb_state,
                  wake->input.PdbValid);
            return;
        }
        AMPERE_ASSERT(
            entry.sf_way >= 0, "Entry {}, Addr {} has an invalid sf way assigned", entry.id, entry.request->Addr);
        TRACE(entry,
              "Entry {}, SF Write set={} way={} status={}",
              entry.id,
              entry.sf_set,
              entry.sf_way,
              entry.next_snoop_status);
        snoop_filter.fill(
            entry.request, entry.sf_set, entry.sf_way, entry.request->Addr, entry.next_snoop_status, entry.trace);
        snoop_filter.update_lru(entry.sf_set, entry.sf_way);
        if (entry.output.PipelineDone) {
            if (entry.rs_status != RsField::NONE) {
                snoop_filter.set_rs(entry.request, entry.sf_set, entry.sf_way, entry.rs_status);
                entry.rs_status = RsField::NONE;
                TRACE(entry,
                      "Entry {}, SF_WR, Entry has a previous rs_status, RS state={}",
                      entry.id,
                      snoop_filter.get_rs(entry.sf_set, entry.sf_way));
            } else if (entry.input.SfVictim) {
                AMPERE_ASSERT(snoop_filter.get_rs(entry.sf_set, entry.sf_way) == RsField::VICTIM,
                              "RS field in wrong state, address={}",
                              entry.request->Addr);
                snoop_filter.set_rs(entry.request, entry.sf_set, entry.sf_way, RsField::IN_USE);
                TRACE(entry,
                      "Entry {}, SF_WR, SFV completed, RS state={}",
                      entry.id,
                      snoop_filter.get_rs(entry.sf_set, entry.sf_way));
            } else {
                snoop_filter.set_rs(entry.request, entry.sf_set, entry.sf_way, RsField::NONE);
                TRACE(entry, "Entry {}, SF_WR, RS state={}", entry.id, snoop_filter.get_rs(entry.sf_set, entry.sf_way));
            }
        }
        ev_sf_write();
        return;
    case pha_fsm::SfCmd_t::SF_WRRS:
        if (entry.rs_status != RsField::NONE) {
            snoop_filter.set_rs(entry.request, entry.sf_set, entry.sf_way, entry.rs_status);
            entry.rs_status = RsField::NONE;
            TRACE(entry,
                  "Entry {}, SF_WRRS, Entry has a previous rs_status, RS state={}",
                  entry.id,
                  snoop_filter.get_rs(entry.sf_set, entry.sf_way));
        } else if (entry.input.SfVictim) {
            AMPERE_ASSERT(snoop_filter.get_rs(entry.sf_set, entry.sf_way) == RsField::VICTIM,
                          "RS field in wrong state");
            snoop_filter.set_rs(entry.request, entry.sf_set, entry.sf_way, RsField::IN_USE);
            TRACE(entry,
                  "Entry {}, SF_WR, SFV completed, RS state={}",
                  entry.id,
                  snoop_filter.get_rs(entry.sf_set, entry.sf_way));
        } else {
            snoop_filter.set_rs(entry.request, entry.sf_set, entry.sf_way, RsField::NONE);
            TRACE(entry, "Entry {}, SF_WRRS, RS state={}", entry.id, snoop_filter.get_rs(entry.sf_set, entry.sf_way));
        }
        return;
    default:
        AMPERE_FATAL("Unknown Snoop filter command:");
        return;
    };
}

static uint32_t stash_hash(PhysAddr addr)
{
    constexpr auto R = [](uint32_t x) -> uint32_t {
        uint32_t reversed = 0;

        for (size_t i = 0; i < 21; ++i)
            reversed |= ((x >> i) & 1) << (20 - i);

        return reversed;
    };

    constexpr auto xor_bits = [](uint32_t x) -> uint32_t {
        uint32_t bit = 0;
        while (x) {
            bit ^= x & 1;
            x >>= 1;
        }
        return bit;
    };

    // This is the full permutation including L3 bank and set bits, but we
    // only care about generating the upper index [25:18].
    //
    static uint32_t perm[] = {
        R(0b100100100000000000000), // bit  6  -+
        R(0b010010010000000000000), // bit  7   | - bank
        R(0b001001001000000000000), // bit  8  -+
        R(0b000100000000100000000), // bit  9  -+
        R(0b000010000000010000000), // bit 10   |
        R(0b000001000000001000000), // bit 11   |
        R(0b000000100000000100000), // bit 12   |
        R(0b000000010000000010000), // bit 13   | - set
        R(0b000000001000000001000), // bit 14   |
        R(0b000000000100000000100), // bit 15   |
        R(0b000000000010000000010), // bit 16   |
        R(0b000000000001000000001), // bit 17  -+
        R(0b000000010001100000000), // bit 18  -+
        R(0b001010000000010000000), // bit 19   |
        R(0b001000000010001000000), // bit 20   |
        R(0b000100000001000100000), // bit 21   | - index
        R(0b100000001000000110000), // bit 22   |
        R(0b000000100100000001000), // bit 23   |
        R(0b010000100000000000100), // bit 24   |
        R(0b000001000000100000010), // bit 25  -+
        R(0b000000000000000000001), // bit 26
    };

    uint64_t c = addr.line_number();

    constexpr int top_bit_index = 25 - PhysAddr::LINE_BITWIDTH;
    constexpr int bot_bit_index = 18 - PhysAddr::LINE_BITWIDTH;

    uint32_t result = 0;

    for (size_t perm_index = top_bit_index; perm_index >= bot_bit_index; --perm_index) {
        result <<= 1;
        result |= xor_bits(c & perm[perm_index]);
    }

    return result;
}

NodeId PHA::choose_stash_node(NodeId src, PhysAddr addr)
{
    if (stash_with_hashing) {
        auto h = stash_hash(addr);
        auto offset = h & 0b11;
        auto index = h >> 2;
        return stash_node_table.at(index).at(offset);
    } else {
        for (int i = 0; i < 2; ++i) {
            // Round robin selection of caches from available nodes
            if (stash_node == stash_nodes.end())
                stash_node = stash_nodes.begin();

            if (*stash_node != src)
                return *stash_node++;

            ++stash_node;
        }

        // If we get here, it's because we couldn't get a stash node
        // that wasn't the original requester, which we're not allowed
        // to stash to when using round-robin
        //
        AMPERE_ERROR("cannot stash back to requester");
        return NodeId{};
    }
}

void PHA::generate_request(PHATrackerEntry& entry)
{
    auto req_opcode = entry.output.TxReqOpcode;
    auto addr = entry.request->Addr;

    if (req_opcode) {
        NodeId tgt_id;

        if (is_pmha() && use_fixed_mcu)
            tgt_id = fixed_mcu;
        else
            tgt_id = sam->lookup(addr);

        if (req_opcode == chi::RequestOpcode::Evict && send_evictions_on_snoop_channel) {
            AMPERE_ASSERT(!is_pmha(), "Unexpectedly sending Evict from a PmHA");
            MeshSnp snp = make<MeshSnp>(node_id, tgt_id, entry.id, chi::SnoopOpcode::Evict, addr);
            txsnp.push_back(snp);
            return;
        }

        MeshReq req = make_clone<MeshReq>(entry.request, node_id, tgt_id, entry.id, *entry.output.TxReqOpcode, addr);

        /* Track that we have a request outstanding, but only if we're a PxHA */
        entry.request_outstanding = is_pxha();

        /* Indicates that we send out a Copyback Response */
        // entry.input.WrDataNeeded = chi::is_copyback(*req_opcode);
        entry.input.WrDataNeeded = chi::is_write(*req_opcode); // TODO need to check if this is correct behviour

        /* Must clear comp Received bit, sometimes we can send two requests and expect more than one packet received */
        entry.input.CompRcvd = false;

        if (req->ExpCompAck) {
            entry.input.CompAckNeeded = true;
            entry.aux.CompleterID = req->TgtID;
        }

        req->Order = entry.output.TxReqOrder;
        req->LikelyShared = entry.output.TxReqLikelyShared;
        req->SnoopMe = entry.output.TxReqCF_Excl_SnoopMe;
        req->SnpAttr = entry.output.TxReqCF_SnpAttr_DoDWT;
        req->ReturnNID = entry.output.TxReqReturnNID;
        req->ReturnTxnID = entry.output.TxReqReturnTxnID;
        req->QoS = entry.request->QoS;

        // FIXME: This is a workaround for a PHA FSM bug.
        if (is_pmha() && *req_opcode == RequestOpcode::WriteNoSnpFull) {
            req->ReturnNID = req->SrcID;
            req->ReturnTxnID = req->TxnID;
        }

        if (enable_prefetch_target)
            generate_prefetch_target(entry);

        if (entry.retry_credit) {
            req->PCrdType = *entry.retry_credit;
            req->AllowRetry = false;
            entry.retry_credit = std::nullopt;
        }

        tracker.pup_check(entry);
        if (req->AllowRetry) {
            TRACE(entry, "Entry {}, sending Request {} to {}", entry.id, req->Opcode, req->TgtID);
            txreq.push_back(req);
        } else {
            txretry.push_back(req);
        }
    }
}

void PHA::generate_response(PHATrackerEntry& entry, int channel)
{
    auto rsp_opcode = channel == 0 ? entry.output.TxRsp0Opcode : entry.output.TxRsp1Opcode;
    auto tgt_id = channel == 0 ? entry.output.TxRsp0TgtID : entry.output.TxRsp1TgtID;

    /* Flow requires a response */
    if (rsp_opcode && *rsp_opcode != ResponseOpcode::RespLCrdReturn) {
        MeshRsp rsp = make_clone<MeshRsp>(entry.request, node_id, tgt_id, entry.request->TxnID, *rsp_opcode);
        rsp->DBID = entry.id;
        switch (*rsp_opcode) {
        case ResponseOpcode::RespSepData:
            rsp->Resp = RespComp_UC;
            break;
        case ResponseOpcode::CompAck:
            rsp->TxnID = entry.compack_txnid;
            break;
        case ResponseOpcode::CompDBIDResp:
        case ResponseOpcode::DBIDResp:
            // StashRead: We received a DataPull from the stashing node.  Here we need to use the DBID of the stash node
            // since they are servicing the stash.
            if (entry.input.StashRead) {
                rsp->SrcID = entry.stash_nid;
                rsp->DBID = entry.rsp->DBID;
            }
            break;
        case ResponseOpcode::Comp:
            switch (entry.request->Opcode) {
            case chi::RequestOpcode::CleanUnique:
            case chi::RequestOpcode::MakeUnique:
                rsp->Resp = RespComp_UC;
                break;
            case chi::RequestOpcode::WriteEvictFull:
            case chi::RequestOpcode::WriteEvictOrEvict:
            case chi::RequestOpcode::WriteBackFull:
            case chi::RequestOpcode::Evict:
            case chi::RequestOpcode::CleanInvalid:
                rsp->Resp = RespComp_I;
                break;
            default:
                rsp->Resp = ResponseStatus{ResponseStatus::Type::Comp, entry.input.CacheState};
                break;
            };
            break;
        case ResponseOpcode::SnpResp:
        case ResponseOpcode::SnpRespFwded:
            rsp->Resp = ResponseStatus{ResponseStatus::Type::Snoop, entry.input.CacheState, false};
            rsp->TxnID = entry.request->TxnID;
            if (entry.input.StashRead) {
                rsp->DBID = entry.id;
                rsp->DatPull = DataPull::Read;
            }
            break;
        case ResponseOpcode::ReadReceipt:
            rsp->TxnID = entry.request->TxnID;
            rsp->Resp = RespComp_I; // spec says field value should be set to zero
            break;
        default:
            AMPERE_FATAL("Unhandled Generate Response: {}", *rsp_opcode);
            break;
        };

        cbusy.refresh_counters();
        rsp->CBusyS = cbusy.update_cbusy(get_tracker_occupancy(), cbusy.get_dscbusy(), false);

        if (get_response_channel(*rsp_opcode, channel) == 0)
            txrsp0.push_back(rsp);
        else
            txrsp1.push_back(rsp);
    }
}

void PHA::generate_snoops(PHATrackerEntry& entry)
{
    int snoop_cnt = get_snoop_count(&entry);
    if (snoop_cnt) {
        /* If stash is being performed choose stash node */
        if (entry.output.TxSnpStashOpcode) {
            if (pha_type == pha_fsm::PHA_t::Pmha) {
                entry.stash_nid = rsam->lookup(entry.request->Addr);
                entry.snoop_address =
                    entry.request->Addr; // This is required because SnpStashShared needs to have a stash address
            } else {
                /* Stash Opcodes need a stash node to store cache line too */
                entry.stash_nid = choose_stash_node(entry.request->SrcID, entry.request->Addr);
            }
        }

        if (phase_command->can_put()) {
            entry.snpCounter = snoop_cnt;
            phase_command->put(&entry);
        } else
            AMPERE_FATAL("Handle Reject/Replay of pipeline pass for phase");
    } else {
        /* No snoops, let FSM resume */
        entry.wait4SnpCnt0 = false;
    }
}

void PHA::generate_data(PHATrackerEntry& entry)
{
    auto dat_opcode = entry.output.TxDatOpcode;
    NodeId tgt_id;

    /* Flow requires a response */
    if (dat_opcode) {
        tgt_id = entry.output.TxDatTgtID;

        MeshDat dat;
        switch (*dat_opcode) {
        case chi::DataOpcode::NonCopyBackWrData:
        case chi::DataOpcode::CopyBackWrData:
            dat = make_clone<MeshDat>(entry.request, node_id, tgt_id, entry.dbid, *dat_opcode);
            dat->HomeNID = node_id;
            if (is_pmha())
                dat->Resp = chi::ResponseStatus{chi::ResponseStatus::Type::WriteData, entry.pdb_state};
            else
                dat->Resp = data_resp_override_for_po(entry, entry.pdb_state);
            break;
        case chi::DataOpcode::SnpRespData:
            dat = make_clone<MeshDat>(entry.request, node_id, tgt_id, entry.request->TxnID, *dat_opcode);
            dat->DBID = entry.id;
            dat->HomeNID = node_id;
            dat->Resp = snoop_resp_override_for_po(entry, entry.input.CacheState);
            TRACE(entry,
                  "Entry {}, sending SnpRespData with Resp={}, CacheState={} PO={}",
                  entry.id,
                  dat->Resp,
                  entry.input.CacheState,
                  entry.input.PO);
            break;
        case chi::DataOpcode::CompData:
            if (entry.input.StashRead) {
                AMPERE_ASSERT(tgt_id == entry.aux.StashTgtID, "StashTgtID is set incorrectly");
                dat = make_clone<MeshDat>(entry.request, node_id, tgt_id, entry.dbid, *dat_opcode);
                // This is the WriteUniquePtl flow where there are two CompAcks received. Once from the IOR and other
                // from the stasher. CompData is sent to the stasher only after the first CompAck from the IOR is
                // received. Setting the compack_rcvd to false here to wait for another CompAck from the stasher.
                entry.wait4CompAck = true;
                entry.compack_rcvd = false;
            } else {
                dat = make_clone<MeshDat>(entry.request, node_id, tgt_id, entry.request->TxnID, *dat_opcode);
            }
            dat->DBID = entry.id;
            dat->HomeNID = node_id;
            dat->Resp = compdata_resp(entry);
            // This is a data forwarding case where PxHA receives a forward snoop from PmHA and
            // the data is forwarded to the requesting agent directly (DCT)
            if (entry.snoop && chi::is_fwd(entry.snoop->Opcode)) {
                dat->TxnID = entry.snoop->FwdTxnID;
            }
            break;
        default:
            AMPERE_FATAL("Unhandled Generate Data: {}", *dat_opcode);
            break;
        };

        cbusy.refresh_counters();
        dat->CBusyS = cbusy.update_cbusy(get_tracker_occupancy(), cbusy.get_dscbusy(), false);

        if (dat->TgtID == node_id)
            l3dat.push_back(dat);
        else
            txdat.push_back(dat);
    }
}

// Note: I basically transliterated this functionality from RTL.
chi::ResponseStatus PHA::data_resp_override_for_po(const PHATrackerEntry& entry, chi::FinalState state) const
{
    // FIXME: In some cases we should use ADB state rather than PDB state. Also, SnpOnce will never pass dirty
    //
    if (state == chi::FinalState::I) {
        return chi::RespWriteData_I;
    } else if (entry.input.PO) {
        if (entry.pdb_is_dirty())
            return chi::RespWriteData_UD_PD;
        else
            return chi::RespWriteData_UC;
    } else {
        if (entry.pdb_is_dirty())
            return chi::RespWriteData_SD_PD;
        else
            return chi::RespWriteData_SC;
    }
}

chi::ResponseStatus PHA::snoop_resp_override_for_po(const PHATrackerEntry& entry, chi::FinalState state) const
{
    // FIXME: In some cases we should use ADB state rather than PDB state. Also, SnpOnce will never pass dirty
    //
    if (state == chi::FinalState::I) {
        return chi::ResponseStatus{chi::ResponseStatus::Type::Snoop, state, entry.pdb_is_dirty()};
    } else if (entry.input.PO) {
        // PO ==> Unique
        if (entry.pdb_is_dirty())
            return chi::RespSnoop_UC_PD;
        else
            return chi::RespSnoop_UC;
    } else {
        // ! PO ==> Shared
        if (state == chi::FinalState::SD) {
            return chi::RespSnoop_SD;
        } else {
            if (entry.pdb_is_dirty())
                return chi::RespSnoop_SC_PD;
            else
                return chi::RespSnoop_SC;
        }
    }
}

chi::ResponseStatus PHA::compdata_resp(const PHATrackerEntry& entry) const
{
    chi::FinalState state{chi::FinalState::I};

    switch (entry.output.CompDataStateMod) {
    case pha_fsm::CompDataStateMod_t::CDSM_IN:
        state = chi::FinalState::I;
        break;
    case pha_fsm::CompDataStateMod_t::CDSM_NOM:
        if (entry.snoop && entry.snoop->Opcode == chi::SnoopOpcode::SnpSharedFwd
            && (entry.input.IntrPdbValid
                || entry.input
                       .IntrAdbValid) /*&& !(entry.input.CF_LikelyShared_DoNotGoToSD)*/) { // FIXME: field not present
            if (entry.pdb_state == chi::FinalState::UD)
                state = chi::FinalState::UC;
            else if (entry.pdb_state == chi::FinalState::SD)
                state = chi::FinalState::SC;
            else
                state = entry.pdb_state;
        } else {
            state = entry.pdb_state;
        }
        break;
    case pha_fsm::CompDataStateMod_t::CDSM_SC:
        state = chi::FinalState::SC;
        break;
    case pha_fsm::CompDataStateMod_t::CDSM_UC:
        state = chi::FinalState::UC;
        break;
    case pha_fsm::CompDataStateMod_t::CDSM_UN:
        if (entry.pdb_state == chi::FinalState::SD)
            state = chi::FinalState::UD;
        else if (entry.pdb_state == chi::FinalState::SC)
            state = chi::FinalState::UC;
        else
            state = entry.pdb_state;
        break;
    case pha_fsm::CompDataStateMod_t::CDSM_UD_PD:
    case pha_fsm::CompDataStateMod_t::CDSM_SH:
        AMPERE_FATAL("Flow not supported");
        break;
        /* FIXME: missing enums from our FSM
            case pha_fsm::CompDataStateMod_t::CDSM_CL:
                break;
            case pha_fsm::CompDataStateMod_t::CDSM_SH:
                break;
        */
    }

    return chi::ResponseStatus{chi::ResponseStatus::Type::Comp, state};
}

void PxHA::generate_prefetch_target(PHATrackerEntry& entry)
{
    // We know we missed the SF and are generating a Read request to the MCU if at this point since we are generated a
    // read
    auto req_opcode = entry.output.TxReqOpcode;

    if (chi::is_read(*req_opcode)) {
        auto addr = entry.request->Addr;
        NodeId tgt_id = prefetch_tgt_sam->lookup(addr);

        // Only send PrefetchTgt if we are on the same die as the mcu
        if (tgt_id.die_id() != node_id->die_id())
            return;

        auto prefetch_tgt =
            make_clone<MeshReq>(entry.request, node_id, tgt_id, entry.id, chi::RequestOpcode::PrefetchTgt, addr);
        prefetch_tgt->AllowRetry = false;
        txreq.push_back(prefetch_tgt);
    }
}

void PxHA::stash_preload(PhysAddr address,
                         FinalState state,
                         NodeIdToStasher node_id_to_stasher,
                         chi::NodeId requester_node)
{
    auto stash_node = choose_stash_node(requester_node, address);
    snoop_filter.preload(stash_node, address, state, node_id_to_stasher);

    auto stasher = node_id_to_stasher(stash_node);
    stasher->stash_preload(address, state, node_id_to_stasher, requester_node);
}

void PxHA::stash_invalidate(PhysAddr address, NodeId from_node)
{
    auto [set, way] = snoop_filter.lookup(Txn{}, address, false);

    if (way != -1) {
        auto status = snoop_filter.get_data(set, way);
        status.set_state(from_node, FinalState::I, true);
        snoop_filter.fill(Txn{}, set, way, address, status, false);
    }
}

unsigned int PHA::get_tracker_occupancy()
{
    return (num_iron_entries + num_gold_entries + num_platinum_entries) - (retry.get_free());
}

int PHA::get_response_channel(chi::ResponseOpcode opcode, int default_chan)
{
    auto override_rsp = chi_rsp_overrides.val().find(opcode);

    if (override_rsp != chi_rsp_overrides.val().end())
        return override_rsp->second;
    else
        return default_chan;
}

void PHA::dump(std::ostream& out) const
{
    out << name() << ":\n";
    dump_buffers(out);
    cbusy.dump(out);
    out << "outstanding requests: " << outstanding_requests << "\n";
}

void PHA::dump_buffers(std::ostream& out) const
{
    out << name() << ".txreq:\n";
    for (const auto& req : txreq) {
        out << req->Opcode << " " << req->Addr << "\n";
    }
}

} // namespace ampere::mesh
