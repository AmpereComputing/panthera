//
// Copyright (c) 2024-2025 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//
#include "ampere/memory/chp.h"
#include "ampere/mesh/chi/opcodes.enum.h"
#include "ampere/mesh/chi/types.h"
#include "ampere/util/bits.h"

namespace ampere::memory {

using util::narrow_cast;

CHP::CHP(const sc_core::sc_module_name& name) : MeshModule{name}
{
    ev = get_nearest<MeshEvents>();
    if (ev) {
        local_mesh_events = false;
    } else {
        ev = new MeshEvents{};
        local_mesh_events = true;
    }

    for (const auto& [qos_name, count] : *arb_counter_param) {
        ARB arb{ARB::NONE};

        if (qos_name == "platinum")
            arb = ARB::PLATINUM;
        else if (qos_name == "gold")
            arb = ARB::GOLD;
        else if (qos_name == "iron")
            arb = ARB::IRON;
        else if (qos_name == "prefetch")
            arb = ARB::PREFETCH;
        else if (qos_name == "rmw")
            arb = ARB::RMW;
        else
            AMPERE_ERROR("unrecognized QoS level in arb_counters: {}", qos_name);

        arb_counter_reset_values[arb] = count;
    }

    arb_counter = arb_counter_reset_values;
    retry.define_credit_type(0, rrt_size);
    retry.define_credit_type(1, wrt_size);
}

CHP::~CHP()
{
    if (local_mesh_events)
        delete ev;
}

bool CHP::run()
{
    cbusy.refresh_counters();
    if (split_cbusy)
        write_cbusy.refresh_counters();

    send_data_to_mesh();
    send_responses_to_mesh();
    receive_data_from_mesh();
    receive_responses_from_mesh();
    receive_data_from_hif();

    request_pipe.run();
    arbitrate();

    return work_to_do();
}

bool CHP::work_to_do() const
{
    auto trackers_ready = rrt.ready() || wrt.ready() || prt.ready() || rdt.ready();
    auto output_queues_not_empty = !(rspq.empty() && datq.empty());
    return trackers_ready || output_queues_not_empty || !request_pipe.empty();
}

/** Receive a CHI request from the system.
 *
 *  Request packets *always* sink. They are either enqueued if we
 *  have space, or retried if we do not.
 */
bool CHP::receive_request(Pipeline<MeshReq>::Data data)
{
    if (mesh_port.rx().req[0]->can_take()) {
        auto req = mesh_port.rx().req[0]->take();

        ev->ev_recv_req(req,
                        req->Addr,
                        req->Opcode,
                        ev->evd_srcid(req->SrcID.id()),
                        ev->evd_tgtid(req->TgtID.id()),
                        ev->evd_txnid(req->TxnID));

        if (req->Opcode != chi::RequestOpcode::PrefetchTgt) {
            auto [can_process, pcrd_type] = retry.process_request(req);

            if (!can_process) {
                retry_request(req, pcrd_type);
                return true;
            }
        }

        data = req;
    }

    return true;
}

bool CHP::enqueue_request(Pipeline<MeshReq>::Data data)
{
    auto req = data.get();

    if (req->Opcode == chi::RequestOpcode::PrefetchTgt) {
        prt.allocate(req, wrt.match(req->Addr));
        ev_alloc_prt(req);
    } else {
        if (is_read(req->Opcode)) {
            auto [pf_hit, pf_has_data] = prt.match(req->Addr);
            rrt.allocate(req, pf_hit, pf_has_data, wrt.match(req->Addr));
            ev_alloc_rrt(req);
            if (pf_hit)
                ev_hit_prefetch(req);
        } else {
            int id = wrt.allocate(req);
            ev_alloc_wrt(req);
            pull_data(req, id);
        }

        if (req->AllowRetry) {
            if (split_cbusy) {
                if (is_read(req->Opcode))
                    cbusy.update_cntr(ReqEvent::ReqRecvEvent, rrt.occupancy());
                else
                    write_cbusy.update_cntr(ReqEvent::ReqRecvEvent, wrt.occupancy());
            } else {
                cbusy.update_cntr(ReqEvent::ReqRecvEvent, get_occupancy());
            }
        }
    }

    return true;
}

void CHP::pull_data(MeshReq req, int id)
{
    auto rsp = make_clone<MeshRsp>(req, node_id, req->SrcID, req->TxnID, ResponseOpcode::CompDBIDResp);
    rsp->DBID = id;
    if (split_cbusy) {
        if (is_read(req->Opcode))
            rsp->CBusyS = cbusy.update_cbusy(rrt.occupancy());
        else
            rsp->CBusyS = write_cbusy.update_cbusy(wrt.occupancy());
    } else {
        rsp->CBusyS = cbusy.update_cbusy(get_occupancy());
    }
    rspq.push_back(rsp);
}

void CHP::retry_request(MeshReq req, int pcrd_type)
{
    auto rsp = make_clone<MeshRsp>(req, node_id, req->SrcID, req->TxnID, ResponseOpcode::RetryAck);
    rsp->PCrdType = pcrd_type;
    if (split_cbusy) {
        if (is_read(req->Opcode))
            rsp->CBusyS = cbusy.update_cbusy(rrt.occupancy(), chi::CBusyState::None, true);
        else
            rsp->CBusyS = write_cbusy.update_cbusy(wrt.occupancy(), chi::CBusyState::None, true);
    } else {
        rsp->CBusyS = cbusy.update_cbusy(get_occupancy(), chi::CBusyState::None, true);
    }
    rspq.push_back(rsp);
}

void CHP::send_data_to_mesh()
{
    while (!datq.empty() && mesh_port.tx().dat[0]->can_put() && get_cycle() >= datq.front().second) {
        auto [dat, _] = datq.front();
        mesh_port.tx().dat[0]->put(dat);
        datq.pop_front();

        ev->ev_send_dat(dat,
                        dat->Opcode,
                        ev->evd_srcid(dat->SrcID.id()),
                        ev->evd_tgtid(dat->TgtID.id()),
                        ev->evd_txnid(dat->TxnID),
                        ev->evd_dbid(dat->DBID));
    }
}

void CHP::send_responses_to_mesh()
{
    while (!rspq.empty() && mesh_port.tx().rsp[0]->can_put()) {
        auto rsp = rspq.front();
        mesh_port.tx().rsp[0]->put(rsp);
        rspq.pop_front();

        ev->ev_send_rsp(rsp,
                        rsp->Opcode,
                        ev->evd_srcid(rsp->SrcID.id()),
                        ev->evd_tgtid(rsp->TgtID.id()),
                        ev->evd_txnid(rsp->TxnID));
    }
}

void CHP::receive_data_from_mesh()
{
    while (mesh_port.rx().dat[0]->can_take()) {
        auto dat = mesh_port.rx().dat[0]->take();

        ev->ev_recv_dat(dat,
                        dat->Opcode,
                        ev->evd_srcid(dat->SrcID.id()),
                        ev->evd_tgtid(dat->TgtID.id()),
                        ev->evd_txnid(dat->TxnID));

        AMPERE_ASSERT(
            dat->Opcode == chi::DataOpcode::NonCopyBackWrData, "expected NonCopyBackWrData, got {}", dat->Opcode);

        wrt.receive_data(dat->TxnID);
    }
}

void CHP::receive_responses_from_mesh()
{
    while (mesh_port.rx().rsp[0]->can_take()) {
        auto rsp = mesh_port.rx().rsp[0]->take();

        ev->ev_recv_rsp(rsp,
                        rsp->Opcode,
                        ev->evd_srcid(rsp->SrcID.id()),
                        ev->evd_tgtid(rsp->TgtID.id()),
                        ev->evd_txnid(rsp->TxnID));

        AMPERE_ERROR("response {} not supported yet", rsp->Opcode);
    }
}

void CHP::arbitrate()
{
    if (hif_rd_cmd->can_put() && rdt.has_free_entry()) {
        ARB select = ARB::NONE;

        if (rrt.ready(ARB::PLATINUM) && arb_counter[ARB::PLATINUM])
            select = ARB::PLATINUM;
        else if (rrt.ready(ARB::GOLD) && arb_counter[ARB::GOLD])
            select = ARB::GOLD;
        else if (rrt.ready(ARB::IRON) && arb_counter[ARB::IRON])
            select = ARB::IRON;
        else if (prt.ready() && arb_counter[ARB::PREFETCH])
            select = ARB::PREFETCH;
        else if (wrt.ready(ARB::RMW) && arb_counter[ARB::RMW])
            select = ARB::RMW;

        if (select == ARB::NONE) {
            arb_counter = arb_counter_reset_values;
        } else if (select == ARB::RMW) {
            auto [id, req] = wrt.dequeue_rmw();
            // FIXME: Yikes, this id adjustment is getting out of hand.
            bool high_priority = (req->QoS.is_platinum() ? true : false);
            auto hif_cmd = make_clone<HIFCmd>(req, HIFOp::RD, req->Addr, high_priority, id + 2 * rdt_size);
            hif_rd_cmd->put(hif_cmd);
            ev_select_rd(hif_cmd, hif_cmd->addr, evd_hif_id(hif_cmd->id), evd_prefetch(true));
            --arb_counter[select];
        } else if (select == ARB::PREFETCH) {
            auto [id, req] = prt.dequeue();
            // Note that the id is offset to be distinguishable from RDT ids.
            bool high_priority = (req->QoS.is_platinum() ? true : false);
            auto hif_cmd = make_clone<HIFCmd>(req, HIFOp::RD, req->Addr, high_priority, id + rdt_size);
            hif_rd_cmd->put(hif_cmd);
            ev_select_rd(hif_cmd, hif_cmd->addr, evd_hif_id(hif_cmd->id), evd_prefetch(true));
            --arb_counter[select];
        } else {
            auto [id, req, pf_hit] = rrt.dequeue(select);
            if (split_cbusy)
                cbusy.update_cntr(ReqEvent::ReqCmpEvent, rrt.occupancy());
            else
                cbusy.update_cntr(ReqEvent::ReqCmpEvent, get_occupancy());

            if (pf_hit) {
                prt.use_data(*pf_hit);
                generate_data_response(req, DataSource::PrefetchTgtUseful);
            } else {
                int rdt_id = rdt.allocate(req);
                bool high_priority = (req->QoS.is_platinum() ? true : false);
                auto hif_cmd = make_clone<HIFCmd>(req, HIFOp::RD, req->Addr, high_priority, rdt_id);
                hif_rd_cmd->put(hif_cmd);
                ev_select_rd(hif_cmd, hif_cmd->addr, evd_hif_id(hif_cmd->id), evd_prefetch(false));
                --arb_counter[select];
            }

            grant_credit(0);
        }
    }

    if (hif_wr_cmd->can_put() && wrt.ready()) {
        auto [id, req] = wrt.dequeue();
        if (split_cbusy)
            write_cbusy.update_cntr(ReqEvent::ReqCmpEvent, wrt.occupancy());
        else
            cbusy.update_cntr(ReqEvent::ReqCmpEvent, get_occupancy());
        bool high_priority = (req->QoS.is_platinum() ? true : false);
        auto hif_cmd = make_clone<HIFCmd>(req, HIFOp::WR, req->Addr, high_priority, id);
        hif_wr_cmd->put(hif_cmd);
        ev_select_wr(hif_cmd, hif_cmd->addr, evd_hif_id(hif_cmd->id));
        rrt.wake_blocked_on_write(id);
        prt.wake_blocked_on_write(id);
        wrt.wake_blocked_on_write(id);
        grant_credit(1);
    }
}

void CHP::grant_credit(uint8_t pcrd_type)
{
    auto retried_req = retry.dealloc(pcrd_type);

    if (retried_req) {
        auto rsp = make<MeshRsp>(node_id, retried_req->src_id, retried_req->txn_id, ResponseOpcode::PCrdGrant);
        rsp->PCrdType = pcrd_type;
        if (split_cbusy) {
            bool read_retry = (pcrd_type == 0);
            if (read_retry) {
                cbusy.update_cntr(ReqEvent::ReqRecvEvent, rrt.occupancy());
                rsp->CBusyS = cbusy.update_cbusy(rrt.occupancy());
            } else {
                write_cbusy.update_cntr(ReqEvent::ReqRecvEvent, wrt.occupancy());
                rsp->CBusyS = write_cbusy.update_cbusy(wrt.occupancy());
            }
        } else {
            cbusy.update_cntr(ReqEvent::ReqRecvEvent, get_occupancy());
            rsp->CBusyS = cbusy.update_cbusy(get_occupancy());
        }
        rspq.push_back(rsp);
    }
}

void CHP::receive_data_from_hif()
{
    while (hif_dat->can_take()) {
        HIFDat dat_id = hif_dat->take();

        if (dat_id >= narrow_cast<int>(2 * rdt_size)) {
            auto wrt_id = dat_id - narrow_cast<int>(2 * rdt_size);
            auto req = wrt.receive_data_rmw(wrt_id);
            ev_hif_data_return(req);
        } else if (dat_id >= narrow_cast<int>(rdt_size)) {
            auto prt_id = dat_id - narrow_cast<int>(rdt_size);
            auto req = prt.receive_data(prt_id);
            rrt.wake_blocked_on_pft(prt_id);
            ev_hif_data_return(req);
        } else {
            auto req = rdt.receive_data(dat_id);
            ev_hif_data_return(req);

            if (rdt.ready()) {
                auto [rdt_id, req] = rdt.dequeue();
                generate_data_response(req, DataSource::PrefetchTgtNotUseful);
            }
        }
    }
}

void CHP::generate_data_response(MeshReq req, DataSource data_source)
{
    DataOpcode data_op = req->Opcode == RequestOpcode::ReadNoSnpSep ? DataOpcode::DataSepResp : DataOpcode::CompData;

    if (req->Order == RequestOrder::RequestAccepted) {
        auto read_receipt = make_clone<MeshRsp>(req, node_id, req->SrcID, req->TxnID, ResponseOpcode::ReadReceipt);
        if (split_cbusy)
            read_receipt->CBusyS = cbusy.update_cbusy(rrt.occupancy(), chi::CBusyState::None);
        else
            read_receipt->CBusyS = cbusy.update_cbusy(get_occupancy(), chi::CBusyState::None);
        rspq.push_back(read_receipt);
    }

    auto pkts = make_data(req, node_id, req->ReturnNID, req->ReturnTxnID, data_op);

    for (auto& dat : pkts) {
        dat->HomeNID = req->SrcID;
        dat->DBID = req->TxnID;
        dat->QoS = req->QoS;
        dat->Resp = chi::RespComp_UC;
        if (split_cbusy)
            dat->CBusyS = cbusy.update_cbusy(rrt.occupancy(), chi::CBusyState::None);
        else
            dat->CBusyS = cbusy.update_cbusy(get_occupancy(), chi::CBusyState::None);
        dat->DatSource = data_source;
        datq.emplace_back(dat, get_cycle() + data_return_delay);
    }
}

} // namespace ampere::memory
