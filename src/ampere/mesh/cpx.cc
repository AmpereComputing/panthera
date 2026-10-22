//
// Copyright (c) 2022 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//
// The concept: we have a multitude of devices we want to attach to a single mesh
// router port (in reality this is 2 or 4, but we should be able to handle any
// number, including 1). That means we need to mux between the incoming CHI channels
// to send packets further into the mesh, and demux the packets coming from the mesh
// back to the appropriate device.
//
// Response Channels
// =================
//
// A device might have 1 or 2 response channels. The mesh has 2 response channels.
// If a device has 2 response channels, then they map in the straightforward way,
// that is rsp0<->rsp0 and rsp1<->rsp1. If a device only has 1 response channel,
// then that channel MUST be rsp0. Inside the CPX, however, an incoming packet is
// routed to a separate queue for rsp0 and rsp1 based on either:
//
//    (a) a fixed channel parameter, per device (pre-Matrix products)
//    (b) the response opcode (Matrix products)
//
// There is only a single credit pool for all responses if a device has only 1
// response channel, so it's impossible for both rsp0 and rsp1 queues to fill up.
//
// On the way back, the two separate rsp0 and rsp1 queues will both feed into
// the device port for rsp0 if it only has 1 response channel.
//
#include "ampere/mesh/cpx.h"

namespace ampere::mesh {
namespace cpx {

template<typename T>
void receive(InWithCreditOpt<T>& port, CQueue<T>& q)
{
    if (port.bind_count() && port->can_take())
        q.emplace_back(std::move(port->take_with_credit()));
}

template<typename T>
void receive(InOpt<T>& port, Queue<T>& q)
{
    if (port.bind_count() && port->can_take())
        q.emplace_back(port->take());
}

template<typename T>
bool transmit(CQueue<T>& q, OutOpt<T>& port, InWithCreditOpt<T>& credit_return)
{
    if (!q.empty() && port->can_put()) {
        auto [pkt, credit] = std::move(q.front());
        q.pop_front();

        AMPERE_ASSERT(credit_return->can_put_credit(), "must be able to return a credit");
        credit_return->put_credit(std::move(credit));

        port->put(pkt);
        return true;
    }

    return false;
}

template<typename T>
bool transmit(Queue<T>& q, OutOpt<T>& port)
{
    if (!q.empty() && port->can_put()) {
        auto pkt = q.front();
        q.pop_front();
        port->put(pkt);
        return true;
    }

    return false;
}

Device::Device(const sc_core::sc_module_name& name, int nrsp) : MeshModule{name}, port{"port", 1, nrsp, 1, 1}
{
    make_credit_pool("req_in");
    make_credit_pool("rsp0_in");
    make_credit_pool("snp_in");
    make_credit_pool("dat_in");

    if (nrsp == 2)
        make_credit_pool("rsp1_in");
}

void Device::end_of_elaboration()
{
    port.rx().req[0].set_credit_pool(credit_pools["req_in"].get());
    port.rx().snp[0].set_credit_pool(credit_pools["snp_in"].get());
    port.rx().dat[0].set_credit_pool(credit_pools["dat_in"].get());
    port.rx().rsp[0].set_credit_pool(credit_pools["rsp0_in"].get());

    if (port.nrsp == 2)
        port.rx().rsp[1].set_credit_pool(credit_pools["rsp1_in"].get());
}

void Device::make_credit_pool(const char* name)
{
    credit_pools.emplace(name, std::make_unique<CreditPool>(name));
}

void Device::run_device()
{
    receive_from_device();
    transmit_to_device();
}

bool Device::all_queues_empty() const
{
    return (req_in.empty() && rsp0_in.empty() && rsp1_in.empty() && snp_in.empty() && dat_in.empty() && req_out.empty()
            && rsp0_out.empty() && rsp1_out.empty() && snp_out.empty() && dat_out.empty());
}

void Device::receive_from_device()
{
    receive(port.rx().req[0], req_in);
    receive(port.rx().rsp[0], choose_rsp());
    if (port.nrsp == 2)
        receive(port.rx().rsp[1], rsp1_in);
    receive(port.rx().snp[0], snp_in);
    receive(port.rx().dat[0], dat_in);
}

CQueue<MeshRsp>& Device::choose_rsp()
{
    if (port.nrsp == 2)
        return rsp0_in;

    if (port.rx().rsp[0]->can_take()) {
        auto& rsp = port.rx().rsp[0]->peek();

        if (matrix_rsp_routing) {
            // Blech, hard-coded for Matrix.
            switch (rsp->Opcode) {
            case chi::ResponseOpcode::RespLCrdReturn:
                // This one can use both...not sure how to do this?
                if (rsp1_in.size() <= rsp0_in.size())
                    return rsp1_in;
            case chi::ResponseOpcode::SnpResp:
            case chi::ResponseOpcode::CompAck:
            case chi::ResponseOpcode::DBIDResp:
                return rsp1_in;
            default:
                break;
            }
        } else if (config->channel(rsp->TgtID) == 1) {
            return rsp1_in;
        }
    }

    return rsp0_in;
}

void Device::transmit_to_device()
{
    transmit(req_out, port.tx().req[0]);
    transmit(snp_out, port.tx().snp[0]);
    transmit(dat_out, port.tx().dat[0]);

    if (port.nrsp == 2) {
        transmit(rsp0_out, port.tx().rsp[0]);
        transmit(rsp1_out, port.tx().rsp[1]);
    } else {
        // Out of an excess of paranoia, I prioritize selection of the response
        // queues based on a toggle. I don't know if it would really be possible
        // for, say, rsp1 to starve while rsp0 continually fed responses back,
        // but I'd like to avoid that case.
        //
        if (rsp_toggle) {
            transmit(rsp1_out, port.tx().rsp[0]);
            transmit(rsp0_out, port.tx().rsp[0]);
        } else {
            transmit(rsp0_out, port.tx().rsp[0]);
            transmit(rsp1_out, port.tx().rsp[0]);
        }

        rsp_toggle = !rsp_toggle;
    }
}

CPX::CPX(const sc_core::sc_module_name& name)
    : MeshModule{name},
      mesh_req_credit_return{mesh_port.rx().req[0]},
      mesh_rsp0_credit_return{mesh_port.rx().rsp[0]},
      mesh_rsp1_credit_return{mesh_port.rx().rsp[1]},
      mesh_snp_credit_return{mesh_port.rx().snp[0]},
      mesh_dat_credit_return{mesh_port.rx().dat[0]},
      pri_req(n),
      pri_rsp0(n),
      pri_rsp1(n),
      pri_snp(n),
      pri_dat(n)
{
    initialize();
}

CPX::CPX(const sc_core::sc_module_name& name, Clock* clock)
    : MeshModule{name, clock},
      mesh_req_credit_return{mesh_port.rx().req[0]},
      mesh_rsp0_credit_return{mesh_port.rx().rsp[0]},
      mesh_rsp1_credit_return{mesh_port.rx().rsp[1]},
      mesh_snp_credit_return{mesh_port.rx().snp[0]},
      mesh_dat_credit_return{mesh_port.rx().dat[0]},
      pri_req(n),
      pri_rsp0(n),
      pri_rsp1(n),
      pri_snp(n),
      pri_dat(n)
{
    initialize();
}

void CPX::initialize()
{
    device_port.init(n, [this](const char* name, size_t index) { return new MeshPort{name, 1, this->nrsp, 1, 1}; });

    device.init(n, [this](const char* name, size_t index) { return new Device{name, this->nrsp}; });

    for (size_t i = 0; i < n; i++) {
        dev_req_in.push_back(&device[i].req_in);
        dev_rsp0_in.push_back(&device[i].rsp0_in);
        dev_rsp1_in.push_back(&device[i].rsp1_in);
        dev_snp_in.push_back(&device[i].snp_in);
        dev_dat_in.push_back(&device[i].dat_in);

        dev_req_credit_return.push_back(&device[i].port.rx().req[0]);
        dev_rsp0_credit_return.push_back(&device[i].port.rx().rsp[0]);
        if (device[i].port.rx().rsp.size() > 1)
            dev_rsp1_credit_return.push_back(&device[i].port.rx().rsp[1]);
        else
            dev_rsp1_credit_return.push_back(&device[i].port.rx().rsp[0]);
        dev_snp_credit_return.push_back(&device[i].port.rx().snp[0]);
        dev_dat_credit_return.push_back(&device[i].port.rx().dat[0]);

        dev_req_out.push_back(&device[i].req_out);
        dev_rsp0_out.push_back(&device[i].rsp0_out);
        dev_rsp1_out.push_back(&device[i].rsp1_out);
        dev_snp_out.push_back(&device[i].snp_out);
        dev_dat_out.push_back(&device[i].dat_out);

        device[i].port.bind(device_port[i]);
    }

    std::iota(pri_req.begin(), pri_req.end(), 0);
    std::iota(pri_rsp0.begin(), pri_rsp0.end(), 0);
    std::iota(pri_rsp1.begin(), pri_rsp1.end(), 0);
    std::iota(pri_snp.begin(), pri_snp.end(), 0);
    std::iota(pri_dat.begin(), pri_dat.end(), 0);
}

void CPX::end_of_elaboration()
{
    mesh_port.rx().req[0].set_credit_pool(&mesh_req_in);
    mesh_port.rx().rsp[0].set_credit_pool(&mesh_rsp0_in);
    if (mesh_port.nrsp == 2)
        mesh_port.rx().rsp[1].set_credit_pool(&mesh_rsp1_in);
    mesh_port.rx().snp[0].set_credit_pool(&mesh_snp_in);
    mesh_port.rx().dat[0].set_credit_pool(&mesh_dat_in);
}

bool CPX::run()
{
    for (size_t i = 0; i < n; ++i)
        device[i].run_device();

    transmit_to_mesh();
    receive_from_mesh();
    steer();

    return have_work();
}

bool CPX::have_work() const
{
    if (!all_queues_empty())
        return true;

    for (size_t i = 0; i < n; ++i) {
        if (!device[i].all_queues_empty())
            return true;
    }

    return false;
}

bool CPX::all_queues_empty() const
{
    return (mesh_req.empty() && mesh_rsp0.empty() && mesh_rsp1.empty() && mesh_snp.empty() && mesh_dat.empty());
}

template<typename T>
void CPX::transmit_channel(std::vector<CQueue<T>*>& q,
                           OutOpt<T>& port,
                           std::vector<int>& pri,
                           std::vector<InWithCreditOpt<T>*>& credit_return)
{
    for (auto it = pri.begin(); it != pri.end(); ++it) {
        auto i = *it;

        if (transmit(*q[i], port, *credit_return[i])) {
            pri.erase(it);
            pri.push_back(i);
            break;
        }
    }
}

void CPX::transmit_to_mesh()
{
    transmit_channel(dev_req_in, mesh_port.tx().req[0], pri_req, dev_req_credit_return);
    transmit_channel(dev_rsp0_in, mesh_port.tx().rsp[0], pri_rsp0, dev_rsp0_credit_return);
    transmit_channel(dev_rsp1_in, mesh_port.tx().rsp[1], pri_rsp1, dev_rsp1_credit_return);
    transmit_channel(dev_snp_in, mesh_port.tx().snp[0], pri_snp, dev_snp_credit_return);
    transmit_channel(dev_dat_in, mesh_port.tx().dat[0], pri_dat, dev_dat_credit_return);
}

void CPX::receive_from_mesh()
{
    receive(mesh_port.rx().req[0], mesh_req);
    receive(mesh_port.rx().rsp[0], mesh_rsp0);
    receive(mesh_port.rx().rsp[1], mesh_rsp1);
    receive(mesh_port.rx().snp[0], mesh_snp);
    receive(mesh_port.rx().dat[0], mesh_dat);
}

template<typename T>
void CPX::steer_channel(CQueue<T>& mesh_q, std::vector<Queue<T>*>& dev_q, InWithCreditOpt<T>& credit_return)
{
    for (auto it = mesh_q.begin(); it != mesh_q.end();) {
        auto i = it->first->LRID.device();

        if (!dev_q[i]->full()) {
            auto [pkt, credit] = std::move(*it);
            it = mesh_q.erase(it);

            credit_return->put_credit(std::move(credit));
            dev_q[i]->push_back(pkt);
        } else {
            ++it;
        }
    }
}

template<>
void CPX::steer_channel<MeshRsp>(CQueue<MeshRsp>& mesh_q,
                                 std::vector<Queue<MeshRsp>*>& dev_q,
                                 InWithCreditOpt<MeshRsp>& credit_return)
{
    for (auto it = mesh_q.begin(); it != mesh_q.end();) {
        auto i = it->first->LRID.device();

        if (!dev_q[i]->full()) {
            auto [pkt, credit] = std::move(*it);
            it = mesh_q.erase(it);

            credit_return->put_credit(std::move(credit));
            dev_q[i]->push_back(pkt);
        } else {
            ++it;
        }
    }
}

void CPX::steer()
{
    steer_channel(mesh_req, dev_req_out, mesh_req_credit_return);
    steer_channel(mesh_rsp0, dev_rsp0_out, mesh_rsp0_credit_return);
    steer_channel(mesh_rsp1, dev_rsp1_out, mesh_rsp1_credit_return);
    steer_channel(mesh_snp, dev_snp_out, mesh_snp_credit_return);
    steer_channel(mesh_dat, dev_dat_out, mesh_dat_credit_return);
}

void CPX::dump(std::ostream& os) const
{
    fmt::print(os, "{} Device input queues:\n", name());

    dump_cqueue(dev_req_in, "REQ");
    dump_cqueue(dev_rsp0_in, "RSP0");
    dump_cqueue(dev_rsp1_in, "RSP1");
    dump_cqueue(dev_snp_in, "SNP");
    dump_cqueue(dev_dat_in, "DAT");
}

template<typename T>
void CPX::dump_cqueue(const std::vector<CQueue<T>*>& cq, const char* name) const
{
    for (size_t i = 0; i < cq.size(); ++i) {
        fmt::print("Device {} {}:\n", i, name);
        for (const auto& entry : *cq[i]) {
            const auto& pkt = entry.first;
            fmt::print("Opcode={} SrcID={} TgtID={} TxnID={}\n", pkt->Opcode, pkt->SrcID, pkt->TgtID, pkt->TxnID);
        }
    }
}

} // namespace cpx
} // namespace ampere::mesh
