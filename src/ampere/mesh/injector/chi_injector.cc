//
// Copyright (c) 2022 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//
#include <limits>

#include "ampere/mesh/injector/chi_injector.h"
#include "ampere/mesh/injector/uop_source_prog.h"

namespace ampere::mesh {

CHIInjector::CHIInjector(const sc_core::sc_module_name& name, sc::Clock* clock) : MeshModule{name, clock}
{
    generator = std::make_unique<UniformRand>(PhysAddr{start_addr}, PhysAddr{stop_addr}, addr_mask, seed);

    bytes_per_packet = config->data_bus_width() / 8;

    // Each data packet is N bytes. And the window is 64 clock cycles. So this
    // factor is basically bytes/ns (== GB/s) if you multiply by how many data
    // packets were received in the window.
    //
    bw_factor = bytes_per_packet / (get_clock()->get_period().to_seconds() * 1E9 * 64);

    AMPERE_ASSERT(duty_cycle_param.val().size() == 2, "duty cycle should be a two-entry list: [num, den]");
    duty_cycle.num = duty_cycle_param.val()[0];
    duty_cycle.den = duty_cycle_param.val()[1];

    if (duty_cycle.num > duty_cycle.den)
        duty_cycle.num = duty_cycle.den;

    AMPERE_ASSERT(duty_cycle.num > 0 && duty_cycle.den > 0, "duty cycle should be positive integers");

    count = Random::get(0, duty_cycle.den);

    send_data_delay_in_cycles = (send_data_after_compdbid_delay * 1000) / get_clock()->get_period().value();
}

void CHIInjector::start_of_simulation()
{
    if (chance_read < 0.0 || chance_read > 1.0)
        AMPERE_ERROR("invalid value for chance_read ({}) must be >= 0 and <= 1", chance_read.val());

    get_environment()->event_stream->enable();
    Event::enable_all();
}

void CHIInjector::send_data(MeshRsp rsp)
{
    auto& entry = outstanding_reqs.at(rsp->TxnID);
    auto req = entry.req;
    auto pkts = make_data(req,
                          node_id,
                          rsp->SrcID,
                          rsp->DBID,
                          (direct_memory_mode ? chi::DataOpcode::NonCopyBackWrData : chi::DataOpcode::CopyBackWrData));

    for (auto& dat : pkts) {
        dat->Resp = chi::RespWriteData_UC;
        data_q.emplace_back(dat, rsp->TxnID);
    }
}

bool CHIInjector::run()
{
    if (!enable)
        return false;

    if (get_cycle() - last_completion > braindead_limit)
        AMPERE_ERROR("simulation braindead: more than {} cycles since last completed transaction\n", braindead_limit);

    if (heartbeat > 0 && (get_cycle() % heartbeat) == 0) {
        auto bw = (bytes_per_packet * data_received) / sc_core::sc_time_stamp().to_seconds() / 1E9;
        auto lat = accumulated_latency / (data_received / 2);
        fmt::print("{} simulated {} cycles, {} transactions, bw={:2f} lat={:2f} maxreq={}\n",
                   name(),
                   get_cycle(),
                   total_transactions,
                   bw,
                   lat,
                   cbusy->num_requests());
    }

    update_duty_cycle();

    bool can_send = send_this_cycle();

    if (can_send) {
        if (mesh_port.tx().req[0]->can_put()) {
            MeshReq req;

            if (!retry_q.empty()) {
                req = retry_q.front();
                retry_q.pop_front();
                outstanding_reqs.emplace(
                    std::piecewise_construct, std::forward_as_tuple(req->TxnID), std::forward_as_tuple(req));
            } else {
                auto addr = generator->get_addr();
                generator->advance();

                if (paging.is_connected()) {
                    auto region = generator->get_region();
                    if (region.empty())
                        region = name();
                    addr = paging->translate(VirtAddr(addr), region);
                }
                req = make<MeshReq>(node_id, sam->lookup(addr), txn_id, get_opcode(), addr);
                if (large_requests)
                    req->RSVDC =
                        chi::ReserveCustomerUseReq(static_cast<uint32_t>(chi::RsvdcReq::LargeReq) | (0b111 << 7));
                outstanding_reqs.emplace(
                    std::piecewise_construct, std::forward_as_tuple(txn_id), std::forward_as_tuple(req));
                ++txn_id;

                if (is_read(req->Opcode))
                    start_time[req->TxnID] = sc_core::sc_time_stamp().to_seconds() * 1E9;
            }

            mesh_port.tx().req[0]->put(req);
            ev_send_req(req,
                        req->Opcode,
                        req->Addr,
                        evd_srcid(req->SrcID.id()),
                        evd_tgtid(req->TgtID.id()),
                        evd_txnid(req->TxnID));
        } else {
            // What happens if we count an unsendable request as a retry?
            ++rejects;
        }
    }

    if (mesh_port.rx().rsp[0]->can_take()) {
        auto rsp = mesh_port.rx().rsp[0]->take();
        ev_recv_rsp(rsp, rsp->Opcode, evd_txnid(rsp->TxnID), rsp->CBusyS);
        cbusy->update(rsp->CBusyS, outstanding_reqs.size());

        if (rsp->Opcode == chi::ResponseOpcode::RetryAck) {
            retry.push_retry(rsp->PCrdType, rsp->SrcID, rsp->TxnID);
            auto& entry = outstanding_reqs.at(rsp->TxnID);
            auto req = entry.req;
            outstanding_reqs.erase(rsp->TxnID);
            waiting_for_credit[rsp->TxnID] = req;
            ++retries;
        } else if (rsp->Opcode == chi::ResponseOpcode::PCrdGrant) {
            auto retry_txn_id = retry.pop_retry(rsp->PCrdType, rsp->SrcID);
            auto req = waiting_for_credit.at(retry_txn_id);
            req->PCrdType = rsp->PCrdType;
            req->AllowRetry = false;
            waiting_for_credit.erase(retry_txn_id);
            retry_q.push_back(req);
        } else if (rsp->Opcode == chi::ResponseOpcode::RespSepData) {
            outstanding_reqs.at(rsp->TxnID).got_comp = true;

            if (outstanding_reqs[rsp->TxnID].got_data) {
                outstanding_reqs.erase(rsp->TxnID);

                auto compack = make<MeshRsp>(node_id, rsp->SrcID, rsp->DBID, chi::ResponseOpcode::CompAck);
                response_q.push_back(compack);
            }
        } else if (rsp->Opcode == chi::ResponseOpcode::CompDBIDResp) {
            // In Kestrel, the TDMA sends the data back after requesting data from TMEM,
            // So there is a delay in between the CompDBIDResp received and NCBWrData sent.
            send_data_delay_queue.emplace(get_cycle() + send_data_delay_in_cycles,
                                          std::make_tuple(std::bind_front(&CHIInjector::send_data, this), rsp));
        } else {
            AMPERE_ERROR("unexpected response opcode {}\n", rsp->Opcode);
        }
    }

    if (mesh_port.rx().dat[0]->can_take()) {
        auto dat = mesh_port.rx().dat[0]->take();
        ev_recv_dat(dat, dat->Opcode, evd_txnid(dat->TxnID), dat->CBusyS);
        cbusy->update(dat->CBusyS, outstanding_reqs.size());

        if (dat->DataID == 0) {
            auto& entry = outstanding_reqs.at(dat->TxnID);
            entry.got_data = true;

            if (entry.got_comp || dat->Opcode == chi::DataOpcode::CompData) {
                cache.emplace_back(entry.req->Addr, dat->Resp.state());
                outstanding_reqs.erase(dat->TxnID);

                if (!direct_memory_mode) {
                    auto rsp = make<MeshRsp>(node_id, dat->HomeNID, dat->DBID, chi::ResponseOpcode::CompAck);
                    response_q.push_back(rsp);
                }
            }

            last_completion = get_cycle();

            accumulated_latency += (sc_core::sc_time_stamp().to_seconds() * 1E9) - start_time.at(dat->TxnID);
            start_time.erase(dat->TxnID);

            if (max_transactions > 0 && ++total_transactions >= max_transactions)
                sc_core::sc_stop();
        }

        ++data_received;
        ++data_received_this_window;
    }

    if (mesh_port.rx().snp[0]->can_take()) {
        auto snp = mesh_port.rx().snp[0]->take();
        ev_recv_snp(snp, snp->Opcode, snp->Addr, evd_txnid(snp->TxnID));

        if (snp->Opcode == chi::SnoopOpcode::SnpMakeInvalid || snp->Opcode == chi::SnoopOpcode::SnpCleanInvalid) {
            auto rsp = make_clone<MeshRsp>(snp, node_id, snp->FwdNID, snp->FwdTxnID, chi::ResponseOpcode::SnpResp);
            rsp->Resp = chi::RespSnoop_I;
            response_q.push_back(rsp);
        } else {
            AMPERE_ERROR("unexpected snoop opcode {}\n", snp->Opcode);
        }
    }

    if (!response_q.empty()) {
        int chan = 0;

        if (num_rsp_ports == 2 && !response_q.empty()) {
            auto& rsp = response_q.front();
            switch (rsp->Opcode) {
            case chi::ResponseOpcode::SnpResp:
            case chi::ResponseOpcode::CompAck:
            case chi::ResponseOpcode::DBIDResp:
            case chi::ResponseOpcode::ReadReceipt:
            case chi::ResponseOpcode::SnpRespFwded:
                chan = 1;
                break;
            default:
                break;
            }
        }

        if (mesh_port.tx().rsp[chan]->can_put()) {
            auto rsp = response_q.front();
            response_q.pop_front();
            mesh_port.tx().rsp[chan]->put(rsp);
            ev_send_rsp(
                rsp, rsp->Opcode, evd_srcid(rsp->SrcID.id()), evd_tgtid(rsp->TgtID.id()), evd_txnid(rsp->TxnID));
        }
    }

    if (!data_q.empty() && mesh_port.tx().dat[0]->can_put()) {
        auto [dat, txn_id] = data_q.front();
        data_q.pop_front();
        mesh_port.tx().dat[0]->put(dat);
        ev_send_dat(dat, dat->Opcode, evd_srcid(dat->SrcID.id()), evd_tgtid(dat->TgtID.id()), evd_txnid(dat->TxnID));

        if (dat->DataID == 0) {
            outstanding_reqs.erase(txn_id);

            last_completion = get_cycle();

            if (max_transactions > 0 && ++total_transactions >= max_transactions)
                sc_core::sc_stop();
        }

        ++data_sent;
        ++data_sent_this_window;
    }

    if (auto itr = send_data_delay_queue.find(get_cycle()); itr != send_data_delay_queue.end()) {
        (std::get<0>(itr->second))(std::get<1>(itr->second));
        send_data_delay_queue.erase(itr);
    }

    return true;
}

void CHIInjector::update_duty_cycle()
{
    if (!dynamic_duty_cycle)
        return;

    if ((get_cycle() % 64) == 0) {
        // Re-evaluate duty cycle after every 64 cycle window. If we have retries
        // above some threshold, lower the duty cycle by increasing the denominator
        // proportional to the number of retries. If we don't have retries, try to
        // bump up the numerator.
        //
        if ((rejects + retries) > 3) {
            duty_cycle.den += (rejects + retries) / 4;
        } else if ((rejects + retries) == 0) {
            ++duty_cycle.num;
        }

#if 0
        fmt::print("@{}: retries={}, rejects={}, rd bw={}, wr bw={}, duty cycle -> {}/{}\n",
                   get_cycle(),
                   retries,
                   rejects,
                   data_received_this_window*bw_factor,
                   data_sent_this_window*bw_factor,
                   duty_cycle.num, duty_cycle.den);
#endif

        retries = 0;
        rejects = 0;
        data_received_this_window = 0;
        data_sent_this_window = 0;
    }
}

bool CHIInjector::send_this_cycle()
{
    if (count + duty_cycle.num > 0)
        count += duty_cycle.num;

    if (outstanding_reqs.size() < cbusy->num_requests() && count >= duty_cycle.den) {
        count -= duty_cycle.den;
        return true;
    }

    return false;
}

chi::RequestOpcode CHIInjector::get_opcode()
{
    bool choose_write = write_mode || Random::get<bool>(1.0 - chance_read);

    if (choose_write) {
        if (direct_memory_mode)
            return chi::RequestOpcode::WriteNoSnpFull;
        else
            return chi::RequestOpcode::WriteBackFull;
    } else {
        if (direct_memory_mode)
            return chi::RequestOpcode::ReadNoSnp;
        else
            return chi::RequestOpcode::ReadNotSharedDirty;
    }
}

} // namespace ampere::mesh
