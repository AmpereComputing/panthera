//
// Copyright (c) 2018-2020 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//

#include "ampere/mesh/responder/chi_responder.h"
#include <cmath>

namespace ampere::mesh {

CHIResponder::CHIResponder(const sc_core::sc_module_name& name, Clock* clock)
    : MeshModule(name, clock), clock_scale(sc_core::sc_time(1.0, sc_core::SC_NS) / get_clock()->get_period())
{
    if (dump_responder_trace) {
        outfile.open(responder_trace_file_name, std::ofstream::out);
    }
}

bool CHIResponder::run()
{
    send_responses();
    receive_requests();

    // schedule a future wakeup on our next operation completion
    schedule_wakeup();

    return false;
}

void CHIResponder::send_responses()
{
    auto cur_cycle = get_clock()->get_cycle();

    // Send back as many responses as possible from our response fifo
    while (!response_queue.empty()) {
        const auto& head = response_queue.begin();

        if (head->first <= cur_cycle) {
            auto resp = head->second;

            if (mesh_port.tx().rsp[0]->can_put()) {
                mesh_port.tx().rsp[0]->put(resp);
                response_queue.erase(head);
                ev_send_rsp(resp, resp->Opcode, evd_txnid(resp->TxnID));
                // We still had responses and places to put them, so keep trying
                continue;
            } else {
                // We had responses but no place to put them
                responder_response_full_event(resp);
            }
        }

        // Unless we successfully enqueue another response this cycle, leave
        break;
    }

    // Send back as much data as possible from our data fifo
    while (!data_queue.empty()) {
        const auto& head = data_queue.begin();

        if (head->first <= cur_cycle) {
            auto data = head->second;

            if (mesh_port.tx().dat[0]->can_put()) {
                mesh_port.tx().dat[0]->put(data);
                data_queue.erase(head);
                ev_send_dat(data, data->Opcode, evd_txnid(data->TxnID));
                // We still had data and places to put them, so keep trying
                continue;
            } else {
                // We had responses but no place to put them
                responder_data_full_event(data);
            }
        }

        // Unless we successfully enqueue another response this cycle, leave
        break;
    }
}

CycleDelta CHIResponder::response_latency(MeshReq& req)
{
    auto spare_capacity = (int64_t)capacity_param - (int64_t)data_queue.size();
    auto sensitivity = read_sensitivity_param.val();
    auto min_latency = min_read_latency_param.val();
    auto linearity = read_linearity.val();

    auto throughput_induced_latency = linearity * (double)response_queue.size() + sensitivity / (double)spare_capacity;
    auto current_response_latency = min_latency + throughput_induced_latency;

    return (CycleDelta)ceil(clock_scale * current_response_latency);
}

void CHIResponder::receive_requests()
{
    if (dump_responder_trace) {
        outfile.open(responder_trace_file_name, std::ofstream::out | std::ofstream::app);
    }

    while (mesh_port.rx().req[0]->can_take() && (data_queue.size() < max_throughput_param)) {
        auto req = mesh_port.rx().req[0]->take();
        ev_recv_req(req, req->Opcode, req->Addr, occupancy_data(data_queue.size()));

        if (req->Opcode == RequestOpcode::ReadNoSnp) {
            auto pkts = make_data(req, node_id, req->ReturnNID, req->ReturnTxnID, DataOpcode::CompData);
            auto completion_time = get_clock()->get_cycle() + response_latency(req);
            for (auto& dat : pkts) {
                dat->HomeNID = req->SrcID;
                dat->DBID = req->TxnID;
                dat->Resp = chi::RespComp_UC;
                data_queue.emplace(completion_time, dat);
            }
        } else if (req->Opcode == RequestOpcode::WriteNoSnpFull) {
            auto rsp =
                make_clone<MeshRsp>(req, node_id, req->ReturnNID, req->ReturnTxnID, ResponseOpcode::CompDBIDResp);
            response_queue.emplace(get_cycle() + 1, rsp);
        }
    }

    while (mesh_port.rx().dat[0]->can_take()) {
        auto dat = mesh_port.rx().dat[0]->take();
        ev_recv_dat(dat, dat->Opcode);
    }

    if (dump_responder_trace) {
        outfile.close();
    }
}

void CHIResponder::schedule_wakeup()
{
    auto cur_cycle = get_clock()->get_cycle();
    auto next_response = response_queue.begin();
    auto next_data = data_queue.begin();

    Cycle wake_cycle{0};

    if (next_response != response_queue.end()) {
        if (next_data != data_queue.end())
            wake_cycle = std::min(next_response->first, next_data->first);
        else
            wake_cycle = next_response->first;
    } else if (next_data != data_queue.end()) {
        wake_cycle = next_data->first;
    }

    if (wake_cycle != 0) {
        CycleDelta cycle_offset = wake_cycle > cur_cycle ? wake_cycle - cur_cycle : 1;
        get_clock()->schedule(this, cycle_offset);
    }
}

} // namespace ampere::mesh

/*
 * vim: sw=4:ts=4:et:si:cindent:cino=\:0,(0,g0,t0:
 */
