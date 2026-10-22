//
// Copyright (c) 2022 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//
#include <limits>
#include <signal.h>

#include "ampere/mesh/injector/ior_injector.h"
#include "ampere/mesh/injector/uop_source_prog.h"

namespace ampere::mesh {

IORInjector::IORInjector(const sc_core::sc_module_name& name, sc::Clock* clock) : MeshModule{name, clock}
{
    for (unsigned i = 0; i < num_sources; i++) {
        if (enable)
            ioop_source[i]->enable();
        else
            ioop_source[i]->disable();
    }

    tracker_free_list.resize(nrt_size + prt_size); // for NRT and PRT
    std::iota(tracker_free_list.begin(), tracker_free_list.end(), 0);

    for (unsigned i = 0; i < num_sources; i++) {
        in_delay[i] = 0;
    }

    mesh_port.init(2, [this](const char* name, size_t index) { return new MeshPort{name, 1, num_rsp_ports, 0, 1}; });

    request_q.resize(2);
    response_q.resize(2);

    connect(pcie_to_ior_rd).name("pcie_to_ior_rd").latency(pipe_delay).capacity(2 * pipe_delay);
    connect(pcie_to_ior_wr).name("pcie_to_ior_wr").latency(pipe_delay).capacity(2 * pipe_delay);

    connect(ior_to_pcie_rd).name("ior_to_pcie_rd").latency(pipe_delay).capacity(2 * pipe_delay);
    connect(ior_to_pcie_wr).name("ior_to_pcie_wr").latency(pipe_delay).capacity(2 * pipe_delay);
}

bool IORInjector::is_read(RequestOpcode opcode) const
{
    return (opcode == chi::RequestOpcode::ReadNoSnp) || (opcode == chi::RequestOpcode::ReadOnce);
}

bool IORInjector::is_write(RequestOpcode opcode) const
{
    return (opcode == chi::RequestOpcode::WriteNoSnpPtl) || (opcode == chi::RequestOpcode::WriteUniquePtl);
}

IORInjector::Type IORInjector::get_type(MeshReq req)
{
    if (is_read(req->Opcode)) {
        return Type::Read;
    } else if (is_write(req->Opcode)) {
        return Type::Write;
    } else {
        AMPERE_FATAL("Unrecognized opcode");
        return Type::Read;
    }
}

void IORInjector::allocate_io_tracker(int i)
{
    if (in_delay[i] == 0) {
        std::optional<IOop> io;
        if ((io_tracker.get_iord_tracker_size(i) < iord_tracker_size)
            && (io_tracker.get_iowr_tracker_size(i) < iowr_tracker_size))
        {
            io = ioop_source[i]->fetch_io();
            ev_fetch(evd_io(i),
                     evd_outstanding_iord(io_tracker.get_iord_tracker_size(i)),
                     evd_outstanding_iowr(io_tracker.get_iowr_tracker_size(i)));
            if (io.has_value())
                ev_fetch(evd_io(i),
                         evd_outstanding_iord(io_tracker.get_iord_tracker_size(i)),
                         evd_outstanding_iowr(io_tracker.get_iowr_tracker_size(i)));
        } else if (!io.has_value() && io_tracker.get_iord_tracker_size(i) < iord_tracker_size) {
            io = ioop_source[i]->fetch_read_io();
            if (io.has_value())
                ev_read_fetch(evd_io(i), evd_outstanding_iord(io_tracker.get_iord_tracker_size(i)));
        } else if (!io.has_value() && io_tracker.get_iowr_tracker_size(i) < iowr_tracker_size) {
            io = ioop_source[i]->fetch_write_io();
            if (io.has_value())
                ev_write_fetch(evd_io(i), evd_outstanding_iowr(io_tracker.get_iowr_tracker_size(i)));
        }
        if (!io.has_value())
            return;

        auto io_op = *io;
        in_delay[i] =
            io_op.type() == chi::IOType::Read ? (delay_factor - 1) : (io_op.get_tlp_size() * delay_factor - 1);
        if ((io_op.is_barrier() && io_tracker.is_iord_tracker_empty(i) && io_tracker.is_iowr_tracker_empty(i))
            || !io_op.is_barrier())
        {
            if (io_op.type() == chi::IOType::Read) {
                auto io_entry = io_tracker.insert_io(Type::Read, io_op.get_tlp_size(), io_op.address(), i);
                io_tracker.incr_rd_occ(i);
                ev_alloc_io(evd_io(i), evd_iotype((unsigned)Type::Read), evd_io_id(io_entry->id));
                io_entry->state = IOEntryState::Ready;

                auto last_wr_io_entry = io_tracker.get_last_wr_entry(i);
                if (last_wr_io_entry) {
                    io_entry->state = IOEntryState::Blocked;
                    last_wr_io_entry->wake_list.push_back(io_entry);
                }
            } else if (io_op.type() == chi::IOType::Write) {
                auto io_entry = io_tracker.insert_io(Type::Write, io_op.get_tlp_size(), io_op.address(), i);
                io_tracker.incr_wr_occ(i);
                ev_alloc_io(evd_io(i), evd_iotype((unsigned)Type::Write), evd_io_id(io_entry->id));
                io_entry->state = IOEntryState::Ready;
            } else {
                AMPERE_FATAL("No other IO Type supported");
            }
        }
    } else {
        in_delay[i]--;
    }
}

void IORInjector::transmit_rd_pcie_to_ior()
{
    if (pcie_to_ior_rd.can_put()) {
        unsigned rr = 0;
        IOEntry io;
        while (!io && rr != num_sources) {
            io = io_tracker.get_next_ready_io(read_str_id, Type::Read);
            if (io) {
                io->stream_id = read_str_id;
                io->state = IOEntryState::Transmitting;
                pcie_to_ior_rd.put(io);
                ev_pcie_to_ior_rd_tx(evd_io_id(io->id));
                AMPERE_UNCATEGORIZED_DEBUG("PCIR TO IOR Read: ID={}", io->id);
            }
            read_str_id = (read_str_id + 1) % num_sources;
            rr++;
        }
    }
}

void IORInjector::transmit_wr_pcie_to_ior()
{
    if (pcie_to_ior_wr.can_put()) {
        unsigned rr = 0;
        IOEntry io;
        while (!io && rr != num_sources) {
            io = io_tracker.get_next_ready_io(write_str_id, Type::Write);
            if (io) {
                io->stream_id = write_str_id;
                io->state = IOEntryState::Transmitting;
                pcie_to_ior_wr.put(io);
                ev_pcie_to_ior_wr_tx(evd_io_id(io->id));
                AMPERE_UNCATEGORIZED_DEBUG("PCIR TO IOR TX: Write: ID={}", io->id);
            }
            write_str_id = (write_str_id + 1) % num_sources;
            rr++;
        }
    }
}

void IORInjector::receive_rd_pcie_to_ior()
{
    if (!io_rentry.has_value() && pcie_to_ior_rd.can_take()) {
        io_rentry = pcie_to_ior_rd.take();
        rtlp = io_rentry.value()->tlp_size;
        ev_pcie_to_ior_rd_rx(evd_io_id(io_rentry.value()->id));
    }
    if (io_rentry.has_value() && (nrt.size() < nrt_size - rtlp) && nrt_alloc_delay == 0) {
        AMPERE_ASSERT(rtlp > 0, "TLP size cannot be negative");
        auto& ior = io_rentry.value();
        auto entry = allocate(ior, ior->address + 64 * (ior->tlp_size - rtlp));
        nrt_alloc_delay = delay_factor;
        entry->io_entry = io_rentry.value();
        rtlp--;
        if (rtlp == 0) {
            io_rentry.value()->state = IOEntryState::Executing;
            io_rentry = std::nullopt;
        }
    }
    if (nrt_alloc_delay > 0)
        nrt_alloc_delay--;
}

void IORInjector::receive_wr_pcie_to_ior()
{
    if (!io_wentry.has_value() && pcie_to_ior_wr.can_take()) {
        io_wentry = pcie_to_ior_wr.take();
        wtlp = io_wentry.value()->tlp_size;
        AMPERE_UNCATEGORIZED_DEBUG("PCIR TO IOR RX: Write: ID={}", io_wentry.value()->id);
        ev_pcie_to_ior_wr_rx(evd_io_id(io_wentry.value()->id));
    }
    if (io_wentry.has_value() && (prt.size() < prt_size - wtlp) && prt_alloc_delay == 0) {
        AMPERE_ASSERT(wtlp > 0, "TLP size cannot be negative");
        auto& iow = io_wentry.value();
        auto entry = allocate(iow, iow->address + 64 * (iow->tlp_size - wtlp));
        prt_alloc_delay = delay_factor;
        entry->io_entry = io_wentry.value();
        wtlp--;
        if (wtlp == 0) {
            io_wentry.value()->state = IOEntryState::Executing;
            io_wentry = std::nullopt;
        }
    }
    if (prt_alloc_delay > 0)
        prt_alloc_delay--;
}

IORInjector::TrackerEntry IORInjector::allocate(IOEntry& io, PhysAddr addr)
{
    uint8_t memattr;
    chi::RequestOpcode opcode;
    uint64_t n = tracker_free_list.front();
    tracker_free_list.pop_front();
    TrackerEntry entry;

    memattr = (Allocate << 3) | (Cacheable << 2);

    if (io->iotype == Type::Read) {
        opcode = Cacheable ? chi::RequestOpcode::ReadOnce : chi::RequestOpcode::ReadNoSnp;
        entry = make_child<TrackerEntry>(io, n);
        nrt.push_back(entry);
    } else if (io->iotype == Type::Write) {
        opcode = Cacheable ? chi::RequestOpcode::WriteUniquePtl : chi::RequestOpcode::WriteNoSnpPtl;
        entry = make_child<TrackerEntry>(io, n);
        prt.push_back(entry);
    } else {
        AMPERE_FATAL("Only Read and Write IO supported");
        return nullptr;
    }

    AMPERE_ASSERT(entry, "NRT or PRT entry could not be allocated");
    entry->io_id = io->id;
    entry->stream_id = io->stream_id;

    switch (opcode) {
    case chi::RequestOpcode::WriteNoSnpPtl:
    case chi::RequestOpcode::WriteUniquePtl:
        entry->Wait4DBIDResp = true;
        entry->Wait4Comp = true;
        entry->state = TrackerEntryState::Ready;
        entry->req = make<MeshReq>(node_id.val().at(0), sam->lookup(addr), n, opcode, addr);
        entry->req->ExpCompAck = true;
        entry->req->Order = chi::RequestOrder::RequestOrder;
        entry->req->MemAttr = memattr;
        break;
    case chi::RequestOpcode::ReadNoSnp:
        entry->Wait4Data = true;
        entry->state = TrackerEntryState::Ready;
        entry->req = make<MeshReq>(node_id.val().at(1), sam->lookup(addr), n, opcode, addr);
        entry->req->MemAttr = Cacheable << 2;
        break;
    case chi::RequestOpcode::ReadOnce:
        entry->Wait4Data = true;
        entry->Wait4RR = false; // Currently ReadOnce with EndPoint Ordering is not supported. TODO
        entry->state = TrackerEntryState::Ready;
        entry->req = make<MeshReq>(node_id.val().at(1), sam->lookup(addr), n, opcode, addr);
        entry->req->MemAttr = Cacheable << 2;
        break;
    default:
        AMPERE_FATAL("unrecognized opcode");
    }
    ev_alloc(
        entry->req, entry->req->Opcode, evd_num_reads(nrt.size()), evd_num_writes(prt.size()), evd_io(io->stream_id));
    return entry;
}

void IORInjector::make_request()
{
    if (!retry_q.empty()) {
        auto& req = retry_q.front();
        retry_q.pop_front();
        outstanding_reqs[req->TxnID] = req;
        if (is_read(req->Opcode))
            request_q[1].push_back(req);
        else
            request_q[0].push_back(req);
    } else {
        for (auto& entry : nrt) {
            // TODO Cancelled txn should not be really retried here, but not sure about the implementation yet
            // It needs to be brought into Ready state (ensuring all older requests are completed)
            if (entry->state == TrackerEntryState::Ready || entry->state == TrackerEntryState::Cancelled) {
                request_q[1].push_back(entry->req);
                entry->state = TrackerEntryState::Executing;
            }
        }
        for (auto& entry : prt) {
            if (entry->state == TrackerEntryState::Ready || entry->state == TrackerEntryState::Cancelled) {
                request_q[0].push_back(entry->req);
                entry->state = TrackerEntryState::Executing;
            }
        }
    }
}

void IORInjector::send_request(int mp)
{
    if (!request_q[mp].empty()) {
        if (mesh_port[mp].tx().req[0]->can_put()) {
            auto req = request_q[mp].front();
            mesh_port[mp].tx().req[0]->put(req);
            ev_send_req(req,
                        req->Opcode,
                        req->Addr,
                        evd_srcid(req->SrcID.id()),
                        evd_tgtid(req->TgtID.id()),
                        evd_txnid(req->TxnID));
            request_q[mp].pop_front();
            outstanding_reqs[req->TxnID] = req;
        } else {
            ev_reject();
        }
    }
}

void IORInjector::make_data_pkts(TrackerEntry& entry)
{
    if (get_type(entry->req) == Type::Write && !entry->dat_sent) {
        AMPERE_ASSERT(entry->rsp != nullptr, "rsp field of entry is not assigned");
        std::vector<MeshDat> pkts;
        if (get_cycle() - entry->last_rcvd_dbid >= timeout_limit) {
            pkts = make_data(
                entry->rsp, node_id.val().at(0), entry->rsp->SrcID, entry->rsp->DBID, chi::DataOpcode::WriteDataCancel);
            entry->state = TrackerEntryState::Cancelled;
        } else {
            pkts = make_data(entry->rsp,
                             node_id.val().at(0),
                             entry->rsp->SrcID,
                             entry->rsp->DBID,
                             chi::DataOpcode::NonCopyBackWrData);
        }
        for (auto& dat : pkts) {
            dat->Resp = chi::RespWriteData_UC;
            wr_data_q.emplace_back(dat, entry->rsp->TxnID);
        }
    }
}

void IORInjector::make_response(TrackerEntry& entry)
{
    int mp = is_read(entry->req->Opcode) ? 1 : 0;
    if (entry->req->ExpCompAck) {
        auto rsp = make<MeshRsp>(node_id.val().at(mp), entry->tgt_id, entry->txn_id, chi::ResponseOpcode::CompAck);
        AMPERE_ASSERT(entry->Wait4DBIDResp == false, "Sending CompAck before having received DBIDResp");
        AMPERE_ASSERT(entry->Wait4Comp == false, "Sending CompAck before having received Comp");
        entry->state = TrackerEntryState::Deallocating;
        response_q[mp].push_back(rsp);
    }
}

void IORInjector::receive_response(int port, int mp)
{
    auto& p = mesh_port[mp].rx().rsp[port];
    if (p->can_take()) {
        auto rsp = p->take();
        ev_recv_rsp(rsp, rsp->Opcode, evd_txnid(rsp->TxnID), rsp->CBusyS);
        cbusy.update(rsp->CBusyS, outstanding_reqs.size());

        if (rsp->Opcode == chi::ResponseOpcode::RetryAck) {
            retry.push_retry(rsp->PCrdType, rsp->SrcID, rsp->TxnID);
            auto entry = find_entry_by_id(rsp->TxnID);
            auto req = entry->req;
            outstanding_reqs.erase(rsp->TxnID);
            waiting_for_credit[rsp->TxnID] = req;
        } else if (rsp->Opcode == chi::ResponseOpcode::PCrdGrant) {
            auto retry_txn_id = retry.pop_retry(rsp->PCrdType, rsp->SrcID);
            auto req = waiting_for_credit.at(retry_txn_id);
            req->PCrdType = rsp->PCrdType;
            req->AllowRetry = false;
            waiting_for_credit.erase(retry_txn_id);
            retry_q.push_back(req);
        } else if (rsp->Opcode == chi::ResponseOpcode::ReadReceipt) {
            // rsp->Opcode == chi::ResponseOpcode::RespSepData) {
            auto entry = std::find_if(nrt.begin(), nrt.end(), [rsp](auto& e) { return e->id == rsp->TxnID; });
            AMPERE_ASSERT(entry != nrt.end(), "Entry ID not found in NRT during receive_response");
            auto e = *entry;
            AMPERE_ASSERT(e->Wait4RR, "Unexpected Opcode: {}", rsp->Opcode);
            e->Wait4RR = false;
            e->tgt_id = rsp->SrcID;
            e->txn_id = rsp->DBID;
            e->rsp = rsp;
            e->state = !e->Wait4Data ? TrackerEntryState::Deallocating : e->state;
            if (e->state == TrackerEntryState::Deallocating) {
                make_response(e);
                dealloc_rq.push_back(rsp->TxnID);
                rd_last_commit_cycle = get_cycle();
            }
        } else if (rsp->Opcode == chi::ResponseOpcode::CompDBIDResp) {
            auto entry = std::find_if(prt.begin(), prt.end(), [rsp](auto& e) { return e->id == rsp->TxnID; });
            AMPERE_ASSERT(entry != prt.end(), "Entry ID not found in PRT during receive_response");
            auto e = *entry;
            AMPERE_ASSERT(e->Wait4DBIDResp, "Unexpected Opcode: {} with Txid: {}", rsp->Opcode, rsp->TxnID);
            AMPERE_ASSERT(e->Wait4Comp, "Unexpected Opcode: {} with Txid: {}", rsp->Opcode, rsp->TxnID);
            e->Wait4DBIDResp = false;
            e->Wait4Comp = false;
            e->tgt_id = rsp->SrcID;
            e->txn_id = rsp->DBID;
            e->rsp = rsp;
            e->last_rcvd_dbid = get_cycle();
            sched_dealloc_prt(rsp->TxnID);
        } else if (rsp->Opcode == chi::ResponseOpcode::DBIDResp) {
            auto entry = std::find_if(prt.begin(), prt.end(), [rsp](auto& e) { return e->id == rsp->TxnID; });
            AMPERE_ASSERT(entry != prt.end(), "Entry ID not found in PRT during receive_response");
            auto e = *entry;
            AMPERE_ASSERT(e->Wait4DBIDResp, "Unexpected Opcode: {} with Txid: {}", rsp->Opcode, rsp->TxnID);
            e->Wait4DBIDResp = false;
            e->tgt_id = rsp->SrcID;
            e->txn_id = rsp->DBID;
            e->rsp = rsp;
            e->last_rcvd_dbid = get_cycle();
            make_data_pkts(e);
            e->dat_sent = true;
        } else if (rsp->Opcode == chi::ResponseOpcode::Comp) {
            auto entry = std::find_if(prt.begin(), prt.end(), [rsp](auto& e) { return e->id == rsp->TxnID; });
            AMPERE_ASSERT(entry != prt.end(), "Entry ID not found in PRT during receive_response");
            auto e = *entry;
            AMPERE_ASSERT(!e->Wait4DBIDResp, "Unexpected Opcode: {} with Txid: {}", rsp->Opcode, rsp->TxnID);
            AMPERE_ASSERT(e->Wait4Comp, "Unexpected Opcode: {} with Txid: {}", rsp->Opcode, rsp->TxnID);
            e->Wait4Comp = false;
            sched_dealloc_prt(rsp->TxnID);
        } else {
            AMPERE_ERROR("unexpected response opcode {}\n", rsp->Opcode);
        }
    }
}

void IORInjector::receive_data()
{
    if (mesh_port[1].rx().dat[0]->can_take()) {
        auto dat = mesh_port[1].rx().dat[0]->take();
        ev_recv_dat(dat, dat->Opcode, evd_txnid(dat->TxnID), dat->CBusyS);
        ev_ior_refill(dat);
        cbusy.update(dat->CBusyS, outstanding_reqs.size());

        if (dat->DataID == 0) {
            auto entry = std::find_if(nrt.begin(), nrt.end(), [dat](auto& e) { return e->id == dat->TxnID; });
            AMPERE_ASSERT(entry != nrt.end(), "Entry ID not found in NRT during receive_data");
            auto e = *entry;

            if (dat->Opcode == chi::DataOpcode::CompData || dat->Opcode == chi::DataOpcode::DataSepResp) {
                AMPERE_ASSERT(e->Wait4Data, "Unexpected Opcode: {}", dat->Opcode);
                e->Wait4Data = false;
                e->txn_id = dat->DBID;
                e->tgt_id = dat->HomeNID;
                e->state = !e->Wait4RR ? TrackerEntryState::Deallocating : e->state;
                if (e->state == TrackerEntryState::Deallocating) {
                    make_response(e);
                    dealloc_rq.push_back(dat->TxnID);
                    rd_last_commit_cycle = get_cycle();
                }
            }
        }
    }
}

void IORInjector::send_response(int mp)
{
    int chan = 0;

    if (num_rsp_ports == 2 && !response_q[mp].empty()) {
        auto& rsp = response_q[mp].front();
        switch (rsp->Opcode) {
        case chi::ResponseOpcode::CompAck:
        case chi::ResponseOpcode::DBIDResp:
        case chi::ResponseOpcode::SnpRespFwded:
            chan = 1;
            break;
        default:
            break;
        }
    }

    if (!response_q[mp].empty() && mesh_port[mp].tx().rsp[chan]->can_put()) {
        auto rsp = response_q[mp].front();
        mesh_port[mp].tx().rsp[chan]->put(rsp);
        response_q[mp].pop_front();
        ev_send_rsp(rsp, rsp->Opcode, evd_srcid(rsp->SrcID.id()), evd_tgtid(rsp->TgtID.id()), evd_txnid(rsp->TxnID));
    }
}

void IORInjector::send_data()
{
    if (!wr_data_q.empty() && mesh_port[0].tx().dat[0]->can_put()) {
        auto [dat, txn_id] = wr_data_q.front();
        wr_data_q.pop_front();
        mesh_port[0].tx().dat[0]->put(dat);
        ev_send_dat(dat, dat->Opcode, evd_srcid(dat->SrcID.id()), evd_tgtid(dat->TgtID.id()), evd_txnid(dat->TxnID));
    }
}

IORInjector::TrackerEntry IORInjector::find_entry_by_id(uint64_t id)
{
    for (auto itr = nrt.begin(); itr != nrt.end(); itr++) {
        if ((*itr)->id == id)
            return *itr;
    }
    for (auto itr = prt.begin(); itr != prt.end(); itr++) {
        if ((*itr)->id == id)
            return *itr;
    }
    AMPERE_FATAL("Entry ID not found in the tracker");
    return nullptr;
}

void IORInjector::sched_dealloc_prt(uint64_t id)
{
    auto itr = prt.begin();
    bool can_dealloc = true;

    // Iterate till the desired entry location and check if any entry before the desired entry is not completed. If yes
    // then dont dealloc
    while ((*itr)->id != id) {
        if ((*itr)->Wait4DBIDResp || (*itr)->Wait4Comp) {
            can_dealloc = false;
            break;
        }
        itr++;
    }
    // If the entry can be deallocated, then erase it and also erase any consecutive completed entries until you
    // find can incomplete Write request entry.
    if (can_dealloc) {
        for (auto entry = itr; entry != prt.end(); entry++) {
            // Deallocate Write entry that is complete. For Writes the condition is that CompDBIDResp is received so
            // it can send Data pkt and CompAck.
            if (!(*entry)->Wait4DBIDResp && !(*entry)->Wait4Comp) {
                // send NCBWriteData or WriteDataCancel
                make_data_pkts(*entry);
                // send CompAck (since CompDBIDResp is already received)
                make_response(*entry);
                // deallocate the entry if timeout did not happen
                if ((*entry)->state != TrackerEntryState::Cancelled) {
                    dealloc_wq.push_back((*entry)->id);
                    wr_last_commit_cycle = get_cycle();
                }
            } else {
                break;
            }
        }
    }
}

void IORInjector::deallocate_io_tracker(TrackerEntry& entry)
{
    auto id = entry->io_id;
    auto stream_id = entry->stream_id;
    auto io_entry = entry->io_entry;
    AMPERE_ASSERT(id == io_entry->id, "IO Entry ID mismatch {}, {}", id, io_entry->id);
    AMPERE_ASSERT(io_entry != nullptr, "IO entry {} not found in IO Tracker {}", id, stream_id);
    AMPERE_ASSERT(io_entry->state == IOEntryState::Executing, "Incorrect state transition of IO Tracker Entry");
    if (io_entry->iotype == Type::Write) {
        for (auto itr : io_entry->wake_list) {
            AMPERE_ASSERT(itr->state == IOEntryState::Blocked, "Incorrect state change");
            itr->state = IOEntryState::Ready;
            ev_wake(evd_blocking_id(io_entry->id), evd_io_id(itr->id));
        }
    }
    io_tracker.delete_io(id, stream_id, io_entry->iotype);
    if (io_entry->iotype == Type::Read)
        io_tracker.decr_rd_occ(stream_id);
    else if (io_entry->iotype == Type::Write)
        io_tracker.decr_wr_occ(stream_id);
    ev_dealloc_io(evd_io(stream_id), evd_io_id(id));
}

void IORInjector::receive_rd_ior_to_pcie()
{
    if (ior_to_pcie_rd.can_take()) {
        auto entry = ior_to_pcie_rd.take();
        ev_ior_to_pcie_rd_rx(evd_io_id(entry->io_id));
        deallocate_io_tracker(entry);
    }
}

void IORInjector::receive_wr_ior_to_pcie()
{
    if (ior_to_pcie_wr.can_take()) {
        auto entry = ior_to_pcie_wr.take();
        AMPERE_UNCATEGORIZED_DEBUG("IOR TO PCIE RX: ID={}", entry->io_id);
        ev_ior_to_pcie_wr_rx(evd_io_id(entry->io_id));
        deallocate_io_tracker(entry);
    }
}

void IORInjector::deallocate_nrt()
{
    if (!dealloc_rq.empty() && nrt_dealloc_delay == 0) {
        auto id = dealloc_rq.front();
        dealloc_rq.pop_front();
        auto entry = std::find_if(nrt.begin(), nrt.end(), [id](auto& e) { return e->id == id; });
        AMPERE_ASSERT(entry != nrt.end(), "Entry ID not found in NRT during deallocation");
        auto e = *entry;
        AMPERE_ASSERT(is_read(e->req->Opcode), "Write entry being deallocated from dealloc_rq");
        AMPERE_ASSERT(e->state == TrackerEntryState::Deallocating, "Deallocating an incomplete transaction");
        e->io_entry->tracker_count--;
        if (e->io_entry->tracker_count == 0) {
            if (ior_to_pcie_rd.can_put()) {
                ior_to_pcie_rd.put(e);
                ev_ior_to_pcie_rd_tx(evd_io_id(e->io_id));
            } else {
                AMPERE_FATAL("Return Read txn from ior to pcie failed");
            }
        }

        ev_dealloc_nrt(e->req, evd_tracker_size(nrt.size()));
        nrt_dealloc_delay = delay_factor;
        outstanding_reqs.erase(e->req->TxnID);
        tracker_free_list.push_back(e->id);
        nrt.erase(entry);
    }

    if (nrt_dealloc_delay > 0)
        nrt_dealloc_delay--;
}

void IORInjector::deallocate_prt()
{
    if (!dealloc_wq.empty() && prt_dealloc_delay == 0) {
        auto id = dealloc_wq.front();
        dealloc_wq.pop_front();
        auto entry = std::find_if(prt.begin(), prt.end(), [id](auto& e) { return e->id == id; });
        AMPERE_ASSERT(entry != prt.end(), "Entry ID not found in PRT during deallocation");
        auto e = *entry;
        AMPERE_ASSERT(is_write(e->req->Opcode), "Read entry being deallocated from dealloc_wq");
        AMPERE_ASSERT(e->state == TrackerEntryState::Deallocating, "Deallocating an incomplete transaction");
        e->io_entry->tracker_count--;
        if (e->io_entry->tracker_count == 0) {
            if (ior_to_pcie_wr.can_put()) {
                ior_to_pcie_wr.put(e);
                AMPERE_UNCATEGORIZED_DEBUG("IOR TO PCIE TX: ID={}", e->io_id);
                ev_ior_to_pcie_wr_tx(evd_io_id(e->io_id));
            } else {
                AMPERE_FATAL("Return Write txn from ior to pcie failed");
            }
        }

        ev_dealloc_prt(e->req, evd_tracker_size(prt.size()));
        prt_dealloc_delay = delay_factor;
        outstanding_reqs.erase(e->req->TxnID);
        tracker_free_list.push_back(e->id);
        prt.erase(entry);
    }

    if (prt_dealloc_delay > 0)
        prt_dealloc_delay--;
}

bool IORInjector::run()
{
    if (!enable)
        return false;

    for (unsigned i = 0; i < num_sources; i++)
        allocate_io_tracker(i);

    transmit_rd_pcie_to_ior();
    transmit_wr_pcie_to_ior();
    receive_rd_pcie_to_ior();
    receive_wr_pcie_to_ior();

    make_request();
    send_request(0);
    send_request(1);
    receive_response(0, 0);
    receive_response(1, 0);
    receive_response(0, 1);
    receive_response(1, 1);
    receive_data();
    send_data();
    send_response(0);
    send_response(1);

    deallocate_nrt();
    deallocate_prt();
    receive_rd_ior_to_pcie();
    receive_wr_ior_to_pcie();

    check_for_braindeath();

    return true;
}

void IORInjector::check_for_braindeath()
{
    if (get_cycle() - rd_last_commit_cycle > brain_dead_limit)
        AMPERE_ERROR("{}: simulation brain dead", name());
    if (get_cycle() - wr_last_commit_cycle > brain_dead_limit)
        AMPERE_ERROR("{}: simulation brain dead", name());
}

void IORInjector::dump(std::ostream& os) const
{
    for (auto& entry : nrt) {
        if (entry->req == nullptr)
            continue;

        fmt::print(os, "NRT");
        fmt::print(os,
                   "{:3d} | {:12} | {:10} | {},{},{},{} | {:3} | {:3} |\n",
                   entry->id,
                   entry->state,
                   is_read(entry->req->Opcode) ? "Read" : "Write",
                   entry->Wait4DBIDResp ? "T" : "F",
                   entry->Wait4Comp ? "T" : "F",
                   entry->Wait4RR ? "T" : "F",
                   entry->Wait4Data ? "T" : "F",
                   entry->io_id,
                   entry->stream_id);
    }
    for (auto& entry : prt) {
        if (entry->req == nullptr)
            continue;

        fmt::print(os, "PRT");
        fmt::print(os,
                   "{:3d} | {:12} | {:10} | {},{},{},{} | {:3} | {:3} |\n",
                   entry->id,
                   entry->state,
                   is_read(entry->req->Opcode) ? "Read" : "Write",
                   entry->Wait4DBIDResp ? "T" : "F",
                   entry->Wait4Comp ? "T" : "F",
                   entry->Wait4RR ? "T" : "F",
                   entry->Wait4Data ? "T" : "F",
                   entry->io_id,
                   entry->stream_id);
    }

    /*for (unsigned i = 0 ; i < num_sources; i++) {

        fmt::print(os, "IO {:1d}\n", i);

        for (auto& entry : io_tracker.get_io_tracker(i)) {
            fmt::print(os, "{:3d} | {:10} | {:12} | {:10} | |\n",
                       entry.id,
                       (entry.iotype == Type::Read) ? "Read" : "Write",
                       entry.state,
                       entry.tracker_count
            );
        }
    }*/
}

} // namespace ampere::mesh
