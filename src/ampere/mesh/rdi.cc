//
// Copyright (c) 2023 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//
#include "ampere/mesh/rdi.h"

namespace ampere::mesh {

RDI::RDI(const sc_module_name& name, Clock* clock) : DieConnector{name, clock}
{
    a.rx().req[0].set_credit_pool(&a_in.req.credit_pool);
    a.rx().rsp[0].set_credit_pool(&a_in.rsp0.credit_pool);
    a.rx().rsp[1].set_credit_pool(&a_in.rsp1.credit_pool);
    a.rx().snp[0].set_credit_pool(&a_in.snp.credit_pool);
    a.rx().dat[0].set_credit_pool(&a_in.dat.credit_pool);

    b.rx().req[0].set_credit_pool(&b_in.req.credit_pool);
    b.rx().rsp[0].set_credit_pool(&b_in.rsp0.credit_pool);
    b.rx().rsp[1].set_credit_pool(&b_in.rsp1.credit_pool);
    b.rx().snp[0].set_credit_pool(&b_in.snp.credit_pool);
    b.rx().dat[0].set_credit_pool(&b_in.dat.credit_pool);

    a_to_b_credits[REQ] = a_out_req_fifo_size;
    a_to_b_credits[RSP0] = a_out_rsp0_fifo_size;
    a_to_b_credits[RSP1] = a_out_rsp1_fifo_size;
    a_to_b_credits[SNP] = a_out_snp_fifo_size;
    a_to_b_credits[DAT] = a_out_dat_fifo_size;

    b_to_a_credits[REQ] = b_out_req_fifo_size;
    b_to_a_credits[RSP0] = b_out_rsp0_fifo_size;
    b_to_a_credits[RSP1] = b_out_rsp1_fifo_size;
    b_to_a_credits[SNP] = b_out_snp_fifo_size;
    b_to_a_credits[DAT] = b_out_dat_fifo_size;

    a_to_b_in_flight_data.fill(0);
    b_to_a_in_flight_data.fill(0);

    a_out_of_credits.out_req_fifo_out_of_credits = &ev_a_out_req_fifo_out_of_credits;
    a_out_of_credits.out_rsp0_fifo_out_of_credits = &ev_a_out_rsp0_fifo_out_of_credits;
    a_out_of_credits.out_rsp1_fifo_out_of_credits = &ev_a_out_rsp1_fifo_out_of_credits;
    a_out_of_credits.out_snp_fifo_out_of_credits = &ev_a_out_snp_fifo_out_of_credits;
    a_out_of_credits.out_dat_fifo_out_of_credits = &ev_a_out_dat_fifo_out_of_credits;

    b_out_of_credits.out_req_fifo_out_of_credits = &ev_b_out_req_fifo_out_of_credits;
    b_out_of_credits.out_rsp0_fifo_out_of_credits = &ev_b_out_rsp0_fifo_out_of_credits;
    b_out_of_credits.out_rsp1_fifo_out_of_credits = &ev_b_out_rsp1_fifo_out_of_credits;
    b_out_of_credits.out_snp_fifo_out_of_credits = &ev_b_out_snp_fifo_out_of_credits;
    b_out_of_credits.out_dat_fifo_out_of_credits = &ev_b_out_dat_fifo_out_of_credits;
}

void RDI::end_of_elaboration()
{
    AMPERE_ASSERT(num_links <= rdi::MAX_LINKS,
                  "number of links ({}) exceeds max ({}); raise MAX_LINKS if needed",
                  num_links.val(),
                  rdi::MAX_LINKS);
}

bool RDI::run()
{
    receive();
    transmit();

    pack_and_send(a_in,
                  a,
                  a_to_b_credits,
                  a_to_b_return_credits,
                  a_to_b_in_flight_data,
                  a_to_b_link_state,
                  a_to_b_links,
                  a_out_of_credits);
    pack_and_send(b_in,
                  b,
                  b_to_a_credits,
                  b_to_a_return_credits,
                  b_to_a_in_flight_data,
                  b_to_a_link_state,
                  b_to_a_links,
                  b_out_of_credits);

    recv_and_unpack(a_to_b_links, b_out, b_to_a_credits, b_out_aux_credit);
    recv_and_unpack(b_to_a_links, a_out, a_to_b_credits, a_out_aux_credit);

    return work_to_do();
}

/* Determine if we have any work to do the next cycle. We check three categories of resources:
 *
 *    1. The input FIFOs, all channels.
 *    2. The link state (data in flight in the PHY layer). NOTE: we could be smarter here, since
 *       there is a potentially long-ish delay, and only schedule for when data is going to be ready,
 *       if the link is the only structure that isn't empty.
 *    3. The return credits, which are kind of like the input FIFOs but in the other direction.
 *
 * If any of these resources is occupied, then we're going to run next cycle. Otherwise we can
 * go quiet until something new arrives.
 */
bool RDI::work_to_do() const
{
    if (!(a_in.req.empty() && a_in.rsp0.empty() && a_in.rsp1.empty() && a_in.snp.empty() && a_in.dat.empty()
          && b_in.req.empty() && b_in.rsp0.empty() && b_in.rsp1.empty() && b_in.snp.empty() && b_in.dat.empty()
          && a_out.req.empty() && a_out.rsp0.empty() && a_out.rsp1.empty() && a_out.snp.empty() && a_out.dat.empty()
          && b_out.req.empty() && b_out.rsp0.empty() && b_out.rsp1.empty() && b_out.snp.empty() && b_out.dat.empty()))
    {
        return true;
    }

    if (!(a_to_b_links.empty() && b_to_a_links.empty()))
        return true;

    // We don't remove entries from the credit maps when they reach 0, so unfortunately for now
    // we need to actually look for non-zero values inside them.
    //
    for (auto [chan, count] : a_to_b_return_credits) {
        if (count)
            return true;
    }

    for (auto [chan, count] : b_to_a_return_credits) {
        if (count)
            return true;
    }

    return false;
}

template<typename T>
void RDI::receive_from(InWithCreditOpt<T>& port, InFifo<T>& fifo, EventContainer& ev)
{
    if (port.bind_count() && port->can_take_with_credit()) {
        auto [pkt, credit] = port->take_with_credit();
        fifo.push(pkt, std::move(credit));
        ev.ev_recv(pkt, ev.evd_srcid(pkt->SrcID.id()), ev.evd_tgtid(pkt->TgtID.id()), ev.evd_txnid(pkt->TxnID));
    }
}

void RDI::receive()
{
    receive_from(a.rx().req[0], a_in.req, a_to_b);
    receive_from(a.rx().rsp[0], a_in.rsp0, a_to_b);
    receive_from(a.rx().rsp[1], a_in.rsp1, a_to_b);
    receive_from(a.rx().snp[0], a_in.snp, a_to_b);
    receive_from(a.rx().dat[0], a_in.dat, a_to_b);

    receive_from(b.rx().req[0], b_in.req, b_to_a);
    receive_from(b.rx().rsp[0], b_in.rsp0, b_to_a);
    receive_from(b.rx().rsp[1], b_in.rsp1, b_to_a);
    receive_from(b.rx().snp[0], b_in.snp, b_to_a);
    receive_from(b.rx().dat[0], b_in.dat, b_to_a);

    while (a_in_aux_credit.bind_count() && a_in_aux_credit->can_take()) {
        auto [tag, count] = a_in_aux_credit->take();
        a_to_b_return_credits[encode_external_credit(tag)] += count;
    }

    while (b_in_aux_credit.bind_count() && b_in_aux_credit->can_take()) {
        auto [tag, count] = b_in_aux_credit->take();
        b_to_a_return_credits[encode_external_credit(tag)] += count;
    }
}

template<typename T>
void RDI::transmit_to(OutOpt<T>& port, OutFifo<T>& fifo, int& credits, EventContainer& ev)
{
    if (!fifo.empty() && port.bind_count() && port->can_put()) {
        auto pkt = fifo.front();
        fifo.pop();
        ++credits;
        port->put(pkt);
        ev.ev_send(pkt, ev.evd_srcid(pkt->SrcID.id()), ev.evd_tgtid(pkt->TgtID.id()), ev.evd_txnid(pkt->TxnID));
    }
}

void RDI::transmit()
{
    transmit_to(a.tx().req[0], a_out.req, a_to_b_return_credits[REQ], b_to_a);
    transmit_to(a.tx().rsp[0], a_out.rsp0, a_to_b_return_credits[RSP0], b_to_a);
    transmit_to(a.tx().rsp[1], a_out.rsp1, a_to_b_return_credits[RSP1], b_to_a);
    transmit_to(a.tx().snp[0], a_out.snp, a_to_b_return_credits[SNP], b_to_a);
    transmit_to(a.tx().dat[0], a_out.dat, a_to_b_return_credits[DAT], b_to_a);

    transmit_to(b.tx().req[0], b_out.req, b_to_a_return_credits[REQ], a_to_b);
    transmit_to(b.tx().rsp[0], b_out.rsp0, b_to_a_return_credits[RSP0], a_to_b);
    transmit_to(b.tx().rsp[1], b_out.rsp1, b_to_a_return_credits[RSP1], a_to_b);
    transmit_to(b.tx().snp[0], b_out.snp, b_to_a_return_credits[SNP], a_to_b);
    transmit_to(b.tx().dat[0], b_out.dat, b_to_a_return_credits[DAT], a_to_b);
}

void RDI::pack_and_send(InFifos& fifos,
                        MeshPort& port,
                        Credits& send_credits,
                        Credits& return_credits,
                        std::array<Cycle, rdi::MAX_LINKS>& in_flight_data,
                        rdi::LinkState& link_state,
                        std::vector<Link>& links,
                        OutOfCreditEvents& ooc)
{
    auto incoming = build_incoming_pack_priority(fifos, send_credits, return_credits, ooc);
    reset_link_state(link_state, in_flight_data);
    auto packing = packer.pack(incoming, link_state);
    link_state = packing.new_state;
    send_packing_list(fifos, send_credits, return_credits, port, packing, links);
    update_in_flight_data(link_state, in_flight_data);
}

rdi::IncomingMsgVec RDI::build_incoming_pack_priority(const InFifos& fifos,
                                                      Credits& send_credits,
                                                      Credits& return_credits,
                                                      OutOfCreditEvents& ooc)
{
    struct PrioInfo
    {
        size_t in_q_size;
        int out_q_credits;

        rdi::MsgType type;
        int tag;

        PrioInfo(size_t in_q_size, int out_q_credits, rdi::MsgType type, int tag)
            : in_q_size{in_q_size}, out_q_credits{out_q_credits}, type{type}, tag{tag}
        {
        }

        bool operator<(const PrioInfo& other) const { return in_q_size < other.in_q_size; }
    };

    std::priority_queue<PrioInfo> pq;

    if (!fifos.dat.empty()) {
        if (send_credits[Channel::DAT])
            pq.emplace(fifos.dat.size(), send_credits[Channel::DAT], rdi::MsgType::DAT, Channel::DAT);
        else
            (*ooc.out_dat_fifo_out_of_credits)();
    }

    if (!fifos.rsp0.empty()) {
        if (send_credits[Channel::RSP0])
            pq.emplace(fifos.rsp0.size(), send_credits[Channel::RSP0], rdi::MsgType::RSP, Channel::RSP0);
        else
            (*ooc.out_rsp0_fifo_out_of_credits)();
    }

    if (!fifos.rsp1.empty()) {
        if (send_credits[Channel::RSP1])
            pq.emplace(fifos.rsp1.size(), send_credits[Channel::RSP1], rdi::MsgType::RSP, Channel::RSP1);
        else
            (*ooc.out_rsp1_fifo_out_of_credits)();
    }

    if (!fifos.snp.empty()) {
        if (send_credits[Channel::SNP])
            pq.emplace(fifos.snp.size(), send_credits[Channel::SNP], rdi::MsgType::SNP, Channel::SNP);
        else
            (*ooc.out_snp_fifo_out_of_credits)();
    }

    // -- add credits --

    std::vector<std::pair<int, int>> sorted_credits;

    for (auto [tag, count] : return_credits) {
        if (count)
            sorted_credits.emplace_back(tag, count);
    }

    std::sort(sorted_credits.begin(), sorted_credits.end(), [](std::pair<int, int>& a, std::pair<int, int>& b) {
        return b.second < a.second;
    });

    for (auto cr : sorted_credits)
        pq.emplace(1, std::numeric_limits<int>::max(), rdi::MsgType::CRD, NUM_CHAN + cr.first);

    // -- done with add credits --

    if (!fifos.req.empty()) {
        if (send_credits[Channel::REQ])
            pq.emplace(fifos.req.size(), send_credits[Channel::REQ], rdi::MsgType::REQ, Channel::REQ);
        else
            (*ooc.out_req_fifo_out_of_credits)();
    }

    rdi::IncomingMsgVec incoming;

    while (!pq.empty()) {
        auto prio = pq.top();
        pq.pop();

        incoming.push_back({prio.type, prio.tag});

        if (--prio.in_q_size && --prio.out_q_credits)
            pq.push(prio);
    }

    return incoming;
}

void RDI::reset_link_state(rdi::LinkState& link_state, std::array<Cycle, rdi::MAX_LINKS>& in_flight_data)
{
    auto cycle = get_cycle();

    for (size_t link = 0; link < num_links; ++link) {
        if (in_flight_data[link] <= cycle) {
            in_flight_data[link] = 0;
            link_state[link] = rdi::OneLinkState{};
        }
    }
}

void RDI::send_packing_list(InFifos& fifos,
                            Credits& send_credits,
                            Credits& return_credits,
                            MeshPort& port,
                            const rdi::Packing& packing,
                            std::vector<Link>& links)
{
    auto ready = sc_time_stamp() + delay;

    for (auto [data, l] : packing.packing_list) {
        auto [type, tag] = data;
        auto& link = links[l];

        switch (tag) {
        case REQ:
            send_packet_on_link(fifos.req, port.rx().req[0], tag, link, ready);
            --send_credits[Channel::REQ];
            break;
        case RSP0:
            send_packet_on_link(fifos.rsp0, port.rx().rsp[0], tag, link, ready);
            --send_credits[Channel::RSP0];
            break;
        case RSP1:
            send_packet_on_link(fifos.rsp1, port.rx().rsp[1], tag, link, ready);
            --send_credits[Channel::RSP1];
            break;
        case SNP:
            send_packet_on_link(fifos.snp, port.rx().snp[0], tag, link, ready);
            --send_credits[Channel::SNP];
            break;
        case DAT:
            send_packet_on_link(fifos.dat, port.rx().dat[0], tag, link, ready);
            --send_credits[Channel::DAT];
            break;
        default:
            link.emplace(tag, ready, return_credits[tag - NUM_CHAN]);
            return_credits[tag - NUM_CHAN] = 0;
            break;
        }
    }
}

template<typename T>
void RDI::send_packet_on_link(InFifo<T>& fifo, InWithCreditOpt<T>& port, int tag, Link& link, sc_time ready)
{
    auto [pkt, credit] = std::move(fifo.front());
    link.emplace(tag, ready, pkt);
    fifo.pop();
    port->put_credit(std::move(credit));
}

void RDI::recv_and_unpack(std::vector<Link>& links,
                          OutFifos& fifos,
                          Credits& credits,
                          OutOpt<std::pair<int, int>>& out_aux_credit)
{
    auto now = sc_time_stamp();

    for (auto& link : links) {
        while (!link.empty()) {
            auto& head = link.front();

            if (head.ready_time > now)
                break;

            switch (head.tag) {
            case REQ:
                fifos.req.push(std::get<MeshReq>(head.payload));
                break;
            case RSP0:
                fifos.rsp0.push(std::get<MeshRsp>(head.payload));
                break;
            case RSP1:
                fifos.rsp1.push(std::get<MeshRsp>(head.payload));
                break;
            case SNP:
                fifos.snp.push(std::get<MeshSnp>(head.payload));
                break;
            case DAT:
                fifos.dat.push(std::get<MeshDat>(head.payload));
                break;
            default:
                if (is_external_credit(head.tag))
                    out_aux_credit->put({decode_external_credit(head.tag), std::get<int>(head.payload)});
                else
                    credits[decode_credit(head.tag)] += std::get<int>(head.payload);
                break;
            }

            link.pop();
        }
    }
}

void RDI::update_in_flight_data(rdi::LinkState& link_state, std::array<Cycle, rdi::MAX_LINKS>& in_flight_data)
{
    auto cycle = get_cycle();

    for (size_t link = 0; link < num_links; ++link) {
        if (link_state[link].current.s.dats > 0 && in_flight_data[link] == 0)
            in_flight_data[link] = cycle + flits_per_dat_pkt;
    }
}

} // namespace ampere::mesh
