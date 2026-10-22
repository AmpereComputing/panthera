//
// Copyright (c) 2023 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//
#include "ampere/mesh/adi.h"

namespace ampere::mesh {

ADI::ADI(const sc_module_name& name, Clock* clock) : DieConnector{name, clock}
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

    a_to_b_in_flight_data = 0;
    b_to_a_in_flight_data = 0;

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

void ADI::end_of_elaboration()
{
    AMPERE_ASSERT(num_links <= rdi::MAX_LINKS,
                  "number of links ({}) exceeds max ({}); raise MAX_LINKS if needed",
                  num_links.val(),
                  rdi::MAX_LINKS);
}

bool ADI::run()
{
    receive();
    transmit();

    if (a_to_b_in_flight_data) {
        a_to_b.ev_pack(a_to_b.evd_pack_dats(1));
        --a_to_b_in_flight_data;
    } else {
        pack_and_send(a_in,
                      a,
                      a_to_b_credits,
                      a_to_b_return_credits,
                      a_to_b_in_flight_data,
                      a_to_b_links,
                      a_out_of_credits,
                      a_to_b_arbiters,
                      a_to_b);
    }

    if (b_to_a_in_flight_data) {
        b_to_a.ev_pack(b_to_a.evd_pack_dats(1));
        --b_to_a_in_flight_data;
    } else {
        pack_and_send(b_in,
                      b,
                      b_to_a_credits,
                      b_to_a_return_credits,
                      b_to_a_in_flight_data,
                      b_to_a_links,
                      b_out_of_credits,
                      b_to_a_arbiters,
                      b_to_a);
    }

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
bool ADI::work_to_do() const
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

    if (a_to_b_in_flight_data + b_to_a_in_flight_data)
        return true;

    return false;
}

template<typename T>
void ADI::receive_from(InWithCreditOpt<T>& port, InFifo<T>& fifo, EventContainer& ev)
{
    if (port.bind_count() && port->can_take_with_credit()) {
        auto [pkt, credit] = port->take_with_credit();
        fifo.push(pkt, std::move(credit));
        ev.ev_recv(pkt, ev.evd_srcid(pkt->SrcID.id()), ev.evd_tgtid(pkt->TgtID.id()), ev.evd_txnid(pkt->TxnID));
    }
}

void ADI::receive()
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
void ADI::transmit_to(OutOpt<T>& port, OutFifo<T>& fifo, int& credits, EventContainer& ev)
{
    if (!fifo.empty() && port.bind_count() && port->can_put()) {
        auto pkt = fifo.front();
        fifo.pop();
        ++credits;
        port->put(pkt);
        ev.ev_send(pkt, ev.evd_srcid(pkt->SrcID.id()), ev.evd_tgtid(pkt->TgtID.id()), ev.evd_txnid(pkt->TxnID));
    }
}

void ADI::transmit()
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

auto ADI::double_slot_bid(const InFifos& fifos, const SlotContents* pack, const Credits& send_credits) const
    -> std::optional<SlotContents>
{
    SlotContents tot = pack[0] + pack[1];

    bool bid = ((int)fifos.req.size() >= tot.req && send_credits.at(Channel::REQ) >= tot.req
                && (int)fifos.snp.size() >= tot.snp && send_credits.at(Channel::SNP) >= tot.snp
                && (int)fifos.dat.size() >= tot.dat && send_credits.at(Channel::DAT) >= tot.dat
                && (int)fifos.rsp0.size() >= tot.rsp0 && send_credits.at(Channel::RSP0) >= tot.rsp0
                && (int)fifos.rsp1.size() >= tot.rsp1 && send_credits.at(Channel::RSP1) >= tot.rsp1);

    if (bid)
        return tot;
    else
        return {};
}

auto ADI::single_slot_bid(const InFifos& fifos, const SlotContents& pack, const Credits& send_credits) const
    -> std::optional<SlotContents>
{
    bool bid = ((int)fifos.req.size() >= pack.req && send_credits.at(Channel::REQ) >= pack.req
                && (int)fifos.snp.size() >= pack.snp && send_credits.at(Channel::SNP) >= pack.snp
                && (int)fifos.dat.size() >= pack.dat && send_credits.at(Channel::DAT) >= pack.dat
                && (int)fifos.rsp0.size() >= pack.rsp0 && send_credits.at(Channel::RSP0) >= pack.rsp0
                && (int)fifos.rsp1.size() >= pack.rsp1 && send_credits.at(Channel::RSP1) >= pack.rsp1);

    if (bid)
        return pack;
    else
        return {};
}

auto ADI::special_slot2_bid(const InFifos& fifos, const SlotContents& pack, const Credits& send_credits) const
    -> std::optional<std::pair<SlotContents, bool>>
{
    bool bid = ((int)fifos.req.size() >= pack.req && send_credits.at(Channel::REQ) >= pack.req
                && (int)fifos.snp.size() >= pack.snp && send_credits.at(Channel::SNP) >= pack.snp
                && (int)fifos.dat.size() >= pack.dat && send_credits.at(Channel::DAT) >= pack.dat
                && (int)fifos.rsp0.size() >= pack.rsp0 && send_credits.at(Channel::RSP0) >= pack.rsp0
                && (int)fifos.rsp1.size() >= pack.rsp1 && send_credits.at(Channel::RSP1) >= pack.rsp1);

    bool bidrsp0 = false;
    bool bidrsp1 = false;
    if (pack.rsp0 == 1 && pack.rsp1 == 1) {
        // We are currently considering the RSP0+RSP1 pack
        bidrsp0 = (int)fifos.rsp0.size() >= 1 && send_credits.at(Channel::RSP0) >= 1;
        bidrsp1 = (int)fifos.rsp1.size() >= 1 && send_credits.at(Channel::RSP1) >= 1;
    }

    if (bid)
        return std::pair(pack, true);
    if (bidrsp0 || bidrsp1)
        return std::pair(SlotContents{0, 0, 0, (int)bidrsp0, (int)bidrsp1}, false);
    else
        return {};
}

void ADI::pack_and_send(InFifos& fifos,
                        MeshPort& port,
                        Credits& send_credits,
                        Credits& return_credits,
                        int& in_flight_data,
                        std::vector<Link>& links,
                        OutOfCreditEvents& ooc,
                        WeightedRRArbiters& arb,
                        EventContainer& ev)
{
    SlotContents res{0, 0, 0, 0, 0};

    auto slot0_slot1_bid = [&](int idx) -> auto {
        return double_slot_bid(fifos, slot0_slot1_packs[idx], send_credits);
    };

    auto slot0_bid = [&](int idx) -> auto {
        return single_slot_bid(fifos, slot0_packs[idx], send_credits);
    };
    auto slot1_bid = [&](int idx) -> auto {
        return single_slot_bid(fifos, slot1_packs[idx], send_credits);
    };

    auto grant = arb.slot0_slot1_arb.select(slot0_slot1_bid);

    if (grant)
        res = res + *grant;

    if (!grant) {
        auto slot0_grant = arb.slot0_arb.select(slot0_bid);
        auto slot1_grant = arb.slot1_arb.select(slot1_bid);
        if (slot0_grant)
            res = res + *slot0_grant;
        if (slot1_grant)
            res = res + *slot1_grant;
    }

    auto slot2_bid = [&](int idx) -> std::optional<SlotContents> {
        if (auto slot2 = special_slot2_bid(fifos, slot2_packs[idx] + res, send_credits); slot2.has_value()) {
            if (slot2->second)
                return slot2_packs[idx];
            else
                return slot2->first;
        } else {
            return {};
        }
    };

    auto slot2_grant = arb.slot2_arb.select(slot2_bid);

    if (slot2_grant)
        res = res + *slot2_grant;

    auto ready = sc_time_stamp() + delay;
    in_flight_data = res.dat;

    if (res.req || res.snp || res.dat || res.rsp0 || res.rsp1) {
        ev.ev_pack(ev.evd_pack_reqs(res.req),
                   ev.evd_pack_snps(res.snp),
                   ev.evd_pack_dathdrs(res.dat),
                   ev.evd_pack_rsp0(res.rsp0),
                   ev.evd_pack_rsp1(res.rsp1),
                   ev.evd_fifo_size(fifos.rsp0.size()),
                   ev.evd_fifo_size(send_credits.at(Channel::RSP0)));
    }

    while (res.req--) {
        send_packet_on_link(fifos.req, port.rx().req[0], Channel::REQ, links[0], ready, ev);
        --send_credits[Channel::REQ];
    }

    while (res.snp--) {
        send_packet_on_link(fifos.snp, port.rx().snp[0], Channel::SNP, links[0], ready, ev);
        --send_credits[Channel::SNP];
    }

    while (res.dat--) {
        send_packet_on_link(fifos.dat, port.rx().dat[0], Channel::DAT, links[0], ready, ev);
        --send_credits[Channel::DAT];
    }

    while (res.rsp0--) {
        send_packet_on_link(fifos.rsp0, port.rx().rsp[0], Channel::RSP0, links[0], ready, ev);
        --send_credits[Channel::RSP0];
    }

    while (res.rsp1--) {
        send_packet_on_link(fifos.rsp1, port.rx().rsp[1], Channel::RSP1, links[0], ready, ev);
        --send_credits[Channel::RSP1];
    }

    send_return_credits(Channel::REQ, return_credits, ready, links);
    send_return_credits(Channel::SNP, return_credits, ready, links);
    send_return_credits(Channel::DAT, return_credits, ready, links);
    send_return_credits(Channel::RSP0, return_credits, ready, links);
    send_return_credits(Channel::RSP1, return_credits, ready, links);
}

void ADI::send_return_credits(Channel chan, Credits& return_credits, sc_core::sc_time ready, std::vector<Link>& links)
{
    for (int count = 32; count; count = count > 4 ? (count >> 1) : (count - 1)) {
        if (return_credits[chan] >= count) {
            links[0].emplace(NUM_CHAN + chan, ready, return_credits[chan]);
            return_credits[chan] -= count;
            return;
        }
    }
}

template<typename T>
void ADI::send_packet_on_link(
    InFifo<T>& fifo, InWithCreditOpt<T>& port, int tag, Link& link, sc_time ready, EventContainer& ev)
{
    auto [pkt, credit] = std::move(fifo.front());
    link.emplace(tag, ready, pkt);
    fifo.pop();
    port->put_credit(std::move(credit));
    ev.ev_send_link(pkt, ev.evd_srcid(pkt->SrcID.id()), ev.evd_tgtid(pkt->TgtID.id()), ev.evd_txnid(pkt->TxnID));
}

void ADI::recv_and_unpack(std::vector<Link>& links,
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

} // namespace ampere::mesh
