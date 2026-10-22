//
// Copyright (c) 2018-2020 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//

#include "ampere/util/bits.h"
#include "ampere/memory/memory.h"
#include <cmath>
#include <algorithm>
#include <stdexcept>
#include <variant>

namespace ampere::memory {

using util::narrow_cast;

Memory::Memory(const sc_core::sc_module_name& name)
    : MeshModule(name), clock_scale(sc_core::sc_time(1.0, sc_core::SC_NS) / get_clock()->get_period())
{
    initialize();
}

Memory::Memory(const sc_core::sc_module_name& name, Clock* clock)
    : MeshModule(name, clock), clock_scale(sc_core::sc_time(1.0, sc_core::SC_NS) / get_clock()->get_period())
{
    initialize();
}

void Memory::initialize()
{
    if (use_chp) {
        chp = std::make_unique<CHP>("chp");
        connect(chp->hif_rd_cmd, hif_rd_cmd);
        connect(chp->hif_wr_cmd, hif_wr_cmd);
        connect(hif_dat, chp->hif_dat);

        hif_rd_cmd.set_credit_pool(&hif_rd_credits);
        hif_wr_cmd.set_credit_pool(&hif_wr_credits);
    }

    queue_shrink_factor = 1.0f / bandwidth_scale_factor;
    retry.define_credit_type(0, (unsigned)std::ceil(bandwidth_scale_factor * (float)max_reads_param));
    retry.define_credit_type(1, (unsigned)std::ceil(bandwidth_scale_factor * (float)max_writes_param));
    bw_latency_window = (sc_core::sc_time(bw_latency_window_param.val(), sc_core::SC_NS) / get_clock()->get_period());

    if (dump_memory_trace) {
        outfile.open(memory_trace_file_name, std::ofstream::out);
    }
}

void Memory::end_of_elaboration()
{
    if (dump_memory_trace)
        outfile.open(memory_trace_file_name, std::ofstream::out | std::ofstream::app);
}

void Memory::end_of_simulation()
{
    MeshModule::end_of_simulation();

    if (dump_memory_trace)
        outfile.close();
}

bool Memory::run()
{
    current_cycle = get_cycle();

    if (current_cycle == 0 || (current_cycle >= latency_window_start + bw_latency_window))
        compute_latency();

    if (chp)
        receive_hif_requests();
    else
        receive_chi_requests();

    send_responses();

    // schedule a future wakeup on our next operation completion
    schedule_wakeup();

    return false;
}

void Memory::send_responses()
{
    unsigned cbusy_threshold{cbusy.c_threshold_low};
    if (infer_cbusy_queue_occupancy) {
        float bandwidth = bw_latency.get_current_bandwidth();

        auto scaled_high_bw = high_bandwidth_param * bandwidth_scale_factor;
        auto scaled_low_bw = low_bandwidth_param * bandwidth_scale_factor;

        if (bandwidth >= scaled_high_bw) {
            cbusy_threshold = cbusy.c_threshold_high;
        } else if (bandwidth < scaled_high_bw && bandwidth >= scaled_low_bw) {
            cbusy_threshold = cbusy.c_threshold_mid;
        } else {
            cbusy_threshold = cbusy.c_threshold_low;
        }
    } else {
        cbusy_threshold = (unsigned)(queue_shrink_factor * (float)std::max(outstanding_rd_reqs, outstanding_wr_reqs));
    }

    // Send back as many responses as possible from our response fifo
    while (!response_queue.empty()) {
        const auto& head = response_queue.begin();

        if (head->first <= current_cycle) {
            auto resp = head->second;
            resp->CBusyS =
                cbusy.update_cbusy(cbusy_threshold, chi::CBusyState::None, resp->Opcode == ResponseOpcode::RetryAck);

            if (mesh_port.tx().rsp[0]->can_put()) {
                mesh_port.tx().rsp[0]->put(resp);
                response_queue.erase(head);
                ev_send_rsp(resp,
                            resp->Opcode,
                            evd_txnid(resp->TxnID),
                            evd_srcid(resp->SrcID.id()),
                            evd_tgtid(resp->TgtID.id()));
                // We still had responses and places to put them, so keep trying
                continue;
            } else {
                // We had responses but no place to put them
                ev_mem_response_full(resp);
            }
        }

        // Unless we successfully enqueue another response this cycle, leave
        break;
    }

    // Send back as much data as possible from our data fifo
    while (!data_queue.empty()) {
        const auto& head = data_queue.begin();

        if (head->first <= current_cycle) {
            if (std::holds_alternative<MeshDat>(head->second)) {
                if (send_chi_data(std::get<MeshDat>(head->second), cbusy_threshold)) {
                    data_queue.erase(head);
                    continue;
                }
            } else {
                if (send_hif_data(std::get<std::pair<HIFCmd, Credit>>(head->second).first)) {
                    hif_rd_cmd->put_credit(std::move(std::get<std::pair<HIFCmd, Credit>>(head->second).second));
                    data_queue.erase(head);
                    continue;
                }
            }
        }

        // Unless we successfully enqueue another response this cycle, leave
        break;
    }

    // Deallocate any writes that are ready
    while (!write_queue.empty()) {
        const auto& head = write_queue.begin();

        if (head->first <= current_cycle) {
            if (!use_chp) {
                auto requester = retry.dealloc(1);

                if (requester) {
                    auto rsp = make<MeshRsp>(node_id, requester->src_id, requester->txn_id, ResponseOpcode::PCrdGrant);
                    rsp->PCrdType = 1;
                    response_queue.emplace(get_cycle() + 1, rsp);
                }
            }

            if (use_request_latency_curve) {
                bw_latency.increment_write_bytes(effective_bytes(outstanding_wr_reqs, last_write_rq_cycle));
                last_write_rq_cycle = current_cycle;
            }
            outstanding_wr_reqs--;
            if (std::holds_alternative<uint16_t>(head->second))
                ev_mem_write_dealloc(evd_txnid(std::get<uint16_t>(head->second)));
            else
                hif_wr_cmd->put_credit(std::move(std::get<std::pair<HIFCmd, Credit>>(head->second).second));
            write_queue.erase(head);
        } else {
            break;
        }
    }
}

bool Memory::send_chi_data(MeshDat data, unsigned cbusy_threshold)
{
    data->CBusyS = cbusy.update_cbusy(cbusy_threshold, chi::CBusyState::None, false);

    if (mesh_port.tx().dat[0]->can_put()) {
        mesh_port.tx().dat[0]->put(data);

        ev_send_dat(
            data, data->Opcode, evd_txnid(data->TxnID), evd_srcid(data->SrcID.id()), evd_tgtid(data->TgtID.id()));

        if (data->DataID == 0) {
            auto requester = retry.dealloc(0);

            if (requester) {
                auto rsp = make<MeshRsp>(node_id, requester->src_id, requester->txn_id, ResponseOpcode::PCrdGrant);
                rsp->PCrdType = 0;
                response_queue.emplace(current_cycle + 1, rsp);
            }

            if (use_request_latency_curve) {
                bw_latency.increment_read_bytes(effective_bytes(outstanding_rd_reqs, last_read_rq_cycle));
                last_read_rq_cycle = current_cycle;
            }

            outstanding_rd_reqs--;
        }

        return true;
    } else {
        // We had responses but no place to put them
        ev_mem_data_full(data);
        return false;
    }
}

bool Memory::send_hif_data(HIFCmd cmd)
{
    if (hif_dat->can_put()) {
        hif_dat->put(cmd->id);
        if (use_request_latency_curve) {
            bw_latency.increment_read_bytes(effective_bytes(outstanding_rd_reqs, last_read_rq_cycle));
            last_read_rq_cycle = current_cycle;
        }
        ev_hif_send_dat(cmd);
        outstanding_rd_reqs--;
        return true;
    }

    return false;
}

void Memory::compute_latency()
{
    float bw_region_time = bw_latency_window_count_param * bw_latency_window_param.val();

    if (use_request_latency_curve) {
        // Extend current read and write queue depths to end of current window
        float ns_per_tick = get_clock()->get_period().to_seconds() * 1.0e9;
        float effective_wr_bytes =
            (float)outstanding_wr_reqs * bytes_per_request * (float)(current_cycle - last_write_rq_cycle) * ns_per_tick;
        float effective_rd_bytes =
            (float)outstanding_rd_reqs * bytes_per_request * (float)(current_cycle - last_read_rq_cycle) * ns_per_tick;
        bw_latency.increment_write_bytes(effective_wr_bytes);
        bw_latency.increment_read_bytes(effective_rd_bytes);
        last_write_rq_cycle = current_cycle;
        last_read_rq_cycle = current_cycle;
    }
    bw_latency.compute_latency(bw_region_time);

    float current_latency = bw_latency.get_current_latency();

    ev_latency_update(latency_data(current_latency),
                      bandwidth_rd_data(1000 * bw_latency.get_current_bandwidth_rd()),
                      bandwidth_wr_data(1000 * bw_latency.get_current_bandwidth_wr()),
                      bandwidth_data(1000 * bw_latency.get_current_bandwidth()));

    current_latency_cycles = (sc_core::sc_time(current_latency, sc_core::SC_NS) / get_clock()->get_period());

    if (enforce_bandwidth_limits || enforce_write_bandwidth_limits) {
        // Resource limit is expressed in bytes so divide by our bytes per request
        // to get the limit on outstanding requests
        uint64_t max_read_requests = roundf(bw_latency.get_current_read_resource_limit() / bytes_per_request);
        uint64_t max_write_requests = roundf(bw_latency.get_current_write_resource_limit() / bytes_per_request);
        auto const& read_pool = retry.get_pool_entry(0);
        auto const& write_pool = retry.get_pool_entry(1);

        unsigned int parked_read_credits{0};
        unsigned int parked_write_credits{0};

        if (enforce_bandwidth_limits) {
            if (max_read_requests < read_pool.capacity) {
                parked_read_credits = read_pool.capacity - max_read_requests;
            } else {
                parked_read_credits = 0;
            }
            if (max_write_requests < write_pool.capacity) {
                parked_write_credits = write_pool.capacity - max_write_requests;
            } else {
                parked_write_credits = 0;
            }
        } else if (enforce_write_bandwidth_limits) {
            if (max_write_requests < write_pool.capacity) {
                parked_write_credits = write_pool.capacity - max_write_requests;
            } else {
                parked_write_credits = 0;
            }
        }

        retry.park(0, parked_read_credits);
        retry.park(1, parked_write_credits);
    }

    current_requests = 0;
    latency_window_start = current_cycle;
    get_clock()->schedule(this, bw_latency_window);
}

void Memory::receive_hif_requests()
{
    if (hif_rd_cmd->can_take_with_credit()) {
        auto [cmd, credit] = hif_rd_cmd->take_with_credit();
        ev_hif_recv_rd(cmd);
        auto completion_time = current_cycle + current_latency_cycles;
        data_queue.emplace(std::piecewise_construct,
                           std::forward_as_tuple(completion_time),
                           std::forward_as_tuple(std::in_place_index<1>, cmd, std::move(credit)));

        bw_latency.increment_read_bytes(effective_bytes(outstanding_rd_reqs, last_read_rq_cycle));
        last_read_rq_cycle = current_cycle;
        outstanding_rd_reqs++;
        current_requests++;
    }

    if (hif_wr_cmd->can_take_with_credit()) {
        auto [cmd, credit] = hif_wr_cmd->take_with_credit();
        ev_hif_recv_wr(cmd);
        auto completion_time = current_cycle + current_latency_cycles;
        write_queue.emplace(std::piecewise_construct,
                            std::forward_as_tuple(completion_time),
                            std::forward_as_tuple(std::in_place_index<1>, cmd, std::move(credit)));

        bw_latency.increment_write_bytes(effective_bytes(outstanding_wr_reqs, last_write_rq_cycle));
        last_write_rq_cycle = current_cycle;
        outstanding_wr_reqs++;
        current_requests++;
    }
}

void Memory::receive_chi_requests()
{
    while (mesh_port.rx().req[0]->can_take()) {
        MeshReq req = mesh_port.rx().req[0]->take();
        auto [can_process, pcrd_type] = retry.process_request(req);

        if (!can_process) {
            auto rsp = make_clone<MeshRsp>(req, node_id, req->SrcID, req->TxnID, ResponseOpcode::RetryAck);
            rsp->PCrdType = pcrd_type;
            response_queue.emplace(current_cycle, rsp);
            continue;
        }

        const auto& entry = retry.get_pool_entry(0);
        ev_recv_req(req,
                    req->Opcode,
                    req->Addr,
                    evd_srcid(req->SrcID.id()),
                    evd_tgtid(req->TgtID.id()),
                    occupancy_data(entry.capacity - entry.free));

        if (req->Opcode == RequestOpcode::ReadNoSnp || req->Opcode == RequestOpcode::ReadNoSnpSep) {
            auto data_op = req->Opcode == RequestOpcode::ReadNoSnp ? DataOpcode::CompData : DataOpcode::DataSepResp;
            auto pkts = make_data(req, node_id, req->ReturnNID, req->ReturnTxnID, data_op);
            auto completion_time = current_cycle + current_latency_cycles;

            for (auto& dat : pkts) {
                dat->HomeNID = req->SrcID;
                dat->DBID = req->TxnID;
                dat->Resp = chi::RespComp_UC;

                data_queue.emplace(completion_time, dat);
            }

            bw_latency.increment_read_bytes(effective_bytes(outstanding_rd_reqs, last_read_rq_cycle));
            last_read_rq_cycle = current_cycle;
            outstanding_rd_reqs++;
            current_requests++;

            if (req->Order == RequestOrder::RequestAccepted) {
                auto rsp = make_clone<MeshRsp>(req, node_id, req->SrcID, req->TxnID, ResponseOpcode::ReadReceipt);
                response_queue.emplace(completion_time, rsp);
            }

        } else if (req->Opcode == RequestOpcode::WriteNoSnpFull || req->Opcode == RequestOpcode::WriteNoSnpPtl) {
            auto rsp =
                make_clone<MeshRsp>(req, node_id, req->ReturnNID, req->ReturnTxnID, ResponseOpcode::CompDBIDResp);
            auto completion_time = current_cycle + current_latency_cycles;
            write_queue.emplace(completion_time, req->ReturnTxnID);
            response_queue.emplace(current_cycle + 1, rsp);

            bw_latency.increment_write_bytes(effective_bytes(outstanding_wr_reqs, last_write_rq_cycle));
            last_write_rq_cycle = current_cycle;
            outstanding_wr_reqs++;
            current_requests++;
        } else {
            AMPERE_ERROR("unsupported request opcode {}", req->Opcode);
        }

        if (dump_memory_trace) {
            fmt::print(outfile, "{} 0x{:x} {} 64\n", current_cycle, req->Addr.value(), req->Opcode);
        }
    }

    while (mesh_port.rx().dat[0]->can_take()) {
        auto dat = mesh_port.rx().dat[0]->take();
        ev_recv_dat(dat, dat->Opcode, evd_srcid(dat->SrcID.id()), evd_tgtid(dat->TgtID.id()));
    }
}

void Memory::schedule_wakeup()
{
    auto cur_cycle = get_cycle();
    auto next_response = response_queue.begin();
    auto next_data = data_queue.begin();
    auto next_write = write_queue.begin();

    std::vector<Cycle> wake_cycles;

    if (next_response != response_queue.end()) {
        wake_cycles.push_back(next_response->first);
    }
    if (next_data != data_queue.end()) {
        wake_cycles.push_back(next_data->first);
    }
    if (next_write != write_queue.end()) {
        wake_cycles.push_back(next_write->first);
    }

    if (!wake_cycles.empty()) {
        auto wake_cycle = *std::min_element(std::begin(wake_cycles), std::end(wake_cycles));
        CycleDelta cycle_offset = wake_cycle > cur_cycle ? wake_cycle - cur_cycle : 1;
        get_clock()->schedule(this, cycle_offset);
    }
}

float Memory::effective_bytes(size_t outstanding_reqs, Cycle last_rq_cycle)
{
    float eff_bytes = bytes_per_request;

    if (use_request_latency_curve) {
        const auto period_ns = narrow_cast<float>(get_clock()->get_period().to_seconds() * 1.0e9);

        eff_bytes = (narrow_cast<float>(outstanding_reqs) * bytes_per_request
                     * narrow_cast<float>(get_cycle() - last_rq_cycle) * period_ns);
    }

    return eff_bytes;
}

float Memory::get_latency()
{
    return bw_latency.get_current_latency();
}

void Memory::dump(std::ostream& os) const
{
    fmt::print(os, "= {} ======================================================\n", name());
    fmt::print(os, "Current cycle: {}\n", current_cycle);

    if (!response_queue.empty()) {
        fmt::print(os,
                   "- Response Queue ---------------------------------------------------\n"
                   "| Cycle | Opcode | TgtID | TxnID |\n");

        for (const auto& [cycle, rsp] : response_queue)
            fmt::print(os, "| {} | {} | {} | {} |\n", cycle, rsp->Opcode, rsp->TgtID, rsp->TxnID);
    }

    if (!data_queue.empty()) {
        fmt::print(os, "- Read Data Queue ---------------------------------------------------\n");

        if (use_chp) {
            fmt::print(os, "| Cycle | HIF Id | Addr |\n");
            for (const auto& [cycle, entry] : data_queue) {
                const auto& cmd = std::get<std::pair<HIFCmd, Credit>>(entry).first;
                fmt::print(os, "| {} | {} | {} |\n", cycle, cmd->id, cmd->addr);
            }
        } else {
            fmt::print(os, "| Cycle |\n");
            for (const auto& [cycle, entry] : data_queue)
                fmt::print(os, "| {} |\n", cycle);
        }
    }

    if (!write_queue.empty()) {
        fmt::print(os,
                   "- Write Queue Entries ---------------------------------------------------\n"
                   "| Cycle |\n");

        for (const auto& [cycle, entry] : write_queue)
            fmt::print(os, "| {} |\n", cycle);
    }
}

} // namespace ampere::memory

/*
 * vim: sw=4:ts=4:et:si:cindent:cino=\:0,(0,g0,t0:
 */
