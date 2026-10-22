//
// Copyright (c) 2020-2022 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//
//
#include "ampere/mesh/mesh_node.h"

namespace ampere::mesh {

MeshNode::MeshNode(const sc_core::sc_module_name& name,
                   unsigned x,
                   unsigned y,
                   unsigned z,
                   unsigned ports_cnt,
                   uint8_t socket_id,
                   uint8_t die_id)
    : MeshModule{name}, x{x}, y{y}, z{z}, socket_id{socket_id}, die_id{die_id}
{
    static const char dir_tag[] = "senwud";
    size_t port_num = 0;
    int nreq = two_request_channels ? 2 : 1;

    ports.init(DIR_COUNT + ports_cnt, [nreq](const char* name, size_t index) {
        return new MeshPort(name, (index >= DIR_COUNT) ? 1 : nreq, 2, 1, (index >= DIR_COUNT) ? 1 : 2);
    });

    for (auto& port : ports) {
        bool device_port = port_num >= DIR_COUNT;
        std::string suffix;

        if (device_port)
            suffix = fmt::format("p{}", port_num - DIR_COUNT);
        else
            suffix = dir_tag[port_num];

        port.rx().req[0].set_credit_pool(make_credit_pool("req", suffix));
        port.rx().snp[0].set_credit_pool(make_credit_pool("snp", suffix));
        port.rx().rsp[0].set_credit_pool(make_credit_pool("rsp0", suffix));
        port.rx().dat[0].set_credit_pool(make_credit_pool("dat0", suffix));

        if (port.nreq == 2)
            port.rx().req[1].set_credit_pool(make_credit_pool("req1", suffix));

        if (port.nrsp == 2)
            port.rx().rsp[1].set_credit_pool(make_credit_pool("rsp1", suffix));

        if (port.ndat == 2)
            port.rx().dat[1].set_credit_pool(make_credit_pool("dat1", suffix));

        ++port_num;
    }

    if (die_to_die_lrt_param.val().is_array()) {
        for (auto& item : die_to_die_lrt_param.val()) {
            auto die_id = item["die_id"].get<uint8_t>();
            for (auto& entry : item["entries"]) {
                LocalRoutingId lrid;
                entry.get_to(lrid);
                die_to_die_lrt[die_id].push_back(lrid);
            }
        }
    }
}

CreditPool* MeshNode::make_credit_pool(const char* name, const std::string& suffix)
{
    credit_pools.push_back(std::make_unique<CreditPool>(fmt::format("{}_{}", name, suffix).c_str()));
    return credit_pools.back().get();
}

void MeshNode::end_of_elaboration()
{
    auto any_in_bundle_are_bound = [](auto& bundle) -> bool {
        for (size_t i = 0; i < bundle.size(); ++i) {
            if (bundle.is_bound(i))
                return true;
        }
        return false;
    };

    auto any_are_bound = [&](auto& port) -> bool {
        return (any_in_bundle_are_bound(port.rx().req) || any_in_bundle_are_bound(port.rx().rsp)
                || any_in_bundle_are_bound(port.rx().snp) || any_in_bundle_are_bound(port.rx().dat)
                || any_in_bundle_are_bound(port.tx().req) || any_in_bundle_are_bound(port.tx().rsp)
                || any_in_bundle_are_bound(port.tx().snp) || any_in_bundle_are_bound(port.tx().dat));
    };

    have_up_or_down = any_are_bound(ports[UP]) || any_are_bound(ports[DOWN]);
}

bool MeshNode::run()
{
    depart();
    arrive();

    return !(req_q.empty() && req1_q.empty() && snp_q.empty() && rsp0_q.empty() && rsp1_q.empty() && dat0_q.empty()
             && dat1_q.empty());
}

void MeshNode::arrive()
{
    auto port_index = start_port;

    do {
        auto& port = ports[port_index];
        bool from_device = port_index >= DIR_COUNT;

        if (port.rx().snp.is_bound(0))
            arrive_from(port.rx().snp[0], snp_q, 0, ev_recv_snp, port_index, snp_decode_latency);
        if (port.rx().rsp.is_bound(0))
            arrive_from(port.rx().rsp[0], rsp0_q, 0, ev_recv_rsp, port_index, rsp_decode_latency);
        if (port.rx().rsp.is_bound(1))
            arrive_from(port.rx().rsp[1], rsp1_q, 1, ev_recv_rsp1, port_index, rsp_decode_latency);

        if (from_device) {
            int req_chan = choose_req(port.rx().req[0]);

            if (port.rx().req.is_bound(0)) {
                arrive_from(port.rx().req[0],
                            req_chan == 1 ? req1_q : req_q,
                            req_chan,
                            req_chan == 1 ? ev_recv_req1 : ev_recv_req,
                            port_index,
                            req_decode_latency);
            }

            int dat_chan = choose_dat(port.rx().dat[0]);
            if (port.rx().dat.is_bound(0)) {
                arrive_from(port.rx().dat[0],
                            dat_chan == 1 ? dat1_q : dat0_q,
                            dat_chan,
                            dat_chan == 1 ? ev_recv_dat1 : ev_recv_dat,
                            port_index,
                            dat_decode_latency);
            }
        } else {
            if (port.rx().req.is_bound(0))
                arrive_from(port.rx().req[0], req_q, 0, ev_recv_req, port_index, req_decode_latency);
            if (port.rx().req.is_bound(1))
                arrive_from(port.rx().req[1], req1_q, 1, ev_recv_req1, port_index, req_decode_latency);
            if (port.rx().dat.is_bound(0))
                arrive_from(port.rx().dat[0], dat0_q, 0, ev_recv_dat, port_index, dat_decode_latency);
            if (port.rx().dat.is_bound(1))
                arrive_from(port.rx().dat[1], dat1_q, 1, ev_recv_dat1, port_index, dat_decode_latency);
        }

        port_index = next_port(port_index);

    } while (port_index != start_port);

    start_port = next_port(start_port);
}

int MeshNode::choose_req(InWithCreditOpt<MeshReq>& port)
{
    if (two_request_channels) {
        if (port.bind_count() && port->can_take_with_credit()) {
            auto& pkt = port->peek();

            if (pkt->Opcode == RequestOpcode::Evict || pkt->Opcode == RequestOpcode::WriteEvictFull)
                return 1;
        }
    }

    return 0;
}

int MeshNode::choose_dat(InWithCreditOpt<MeshDat>& port)
{
    if (port.bind_count() && port->can_take_with_credit()) {
        auto& pkt = port->peek();

        return config->channel(pkt->TgtID);
    }

    return 0;
}

template<typename T>
void MeshNode::arrive_from(
    InWithCreditOpt<T>& port, Queue<T>& q, int port_num, Event& recv_event, int port_index, unsigned decode_latency)
{
    if (port->can_take_with_credit()) {
        auto [pkt, credit] = port->take_with_credit();
        auto tgt_nid = pkt->TgtID;
        Cycle ready = get_cycle() + 1;
        bool from_device = port_index >= DIR_COUNT;

        if (from_device) {
            /* Assign LRID to the pkt */
            if (tgt_nid.nodeid_type() == NodeId::NodeId_Type::Global) {
                if (tgt_nid.socket_id() == socket_id && tgt_nid.die_id() == die_id) {
                    if (local_lut.val().contains(tgt_nid.agent_id())) {
                        pkt->LRID = local_lut.val().at(tgt_nid.agent_id());
                    } else {
                        pkt->LRID.set_x(tgt_nid.get_x());
                        pkt->LRID.set_y(tgt_nid.get_y());
                        pkt->LRID.set_z(tile_z);
                        pkt->LRID.set_port(0);
                        pkt->LRID.set_device(0);
                    }
                } else {
                    try {
                        pkt->LRID = cross_die_route(tgt_nid, pkt->TxnID, port_index - DIR_COUNT);
                    } catch (const std::out_of_range& exc) {
                        AMPERE_ERROR("could not find cross die route in RTR {} from {} -> {}",
                                     this->name(),
                                     pkt->SrcID,
                                     tgt_nid);
                    }
                }
            } else {
                LocalRoutingId lrid = LocalRoutingId{tgt_nid.x(), tgt_nid.y(), tgt_nid.port(), tgt_nid.device()};
                pkt->LRID = lrid;
            }

            ready += decode_latency;
        }

        auto [out_port, out_index] = output_port(pkt, port_num, port_index);
        q.emplace_back(pkt, std::move(credit), ready, &port, out_port, out_index);
        recv_event(pkt,
                   pkt->Opcode,
                   evd_srcid(pkt->SrcID.id()),
                   evd_tgtid(pkt->TgtID.id()),
                   evd_lrid(pkt->LRID.id()),
                   evd_txnid(pkt->TxnID),
                   evd_rtr_port(port_index));
    }
}

void MeshNode::depart()
{
    depart_to(req_q, ev_send_req, ev_req_stall);
    depart_to(req1_q, ev_send_req1, ev_req_stall);
    depart_to(snp_q, ev_send_snp, ev_snp_stall);
    depart_to(rsp0_q, ev_send_rsp, ev_rsp0_stall);
    depart_to(rsp1_q, ev_send_rsp1, ev_rsp1_stall);
    depart_to(dat0_q, ev_send_dat, ev_dat0_stall);
    depart_to(dat1_q, ev_send_dat1, ev_dat1_stall);
}

template<typename T>
void MeshNode::depart_to(Queue<T>& q, Event& send_event, Event& stall_event)
{
    auto now = get_cycle();
    bool sent_something = false;

    // This vector is used essentially to cache results from can_put() on the
    // port. This is so we get better accounting of "out_of_credit" events
    // since we might be checking the same output port again and again in a
    // single cycle.
    //
    //    -1 ==> we don't know the status of this port, need to call can_put()
    //     0 ==> we cannot put
    //     1 ==> we can put
    //
    std::vector<int> can_put_to_port(ports.size(), -1);

    for (auto it = q.begin(); it != q.end();) {
        auto& entry = *it;

        if (entry.ready <= now && can_put_to_port[entry.to_port] == -1)
            can_put_to_port[entry.to_port] = (*entry.to)->can_put() ? 1 : 0;

        if (entry.ready <= now && can_put_to_port[entry.to_port]) {
            if ((*entry.from)->can_put_credit()) {
                auto pkt = entry.data;
                (*entry.to)->put(pkt);
                can_put_to_port[entry.to_port] = 0;
                (*entry.from)->put_credit(std::move(entry.credit));
                send_event(pkt,
                           pkt->Opcode,
                           evd_srcid(pkt->SrcID.id()),
                           evd_tgtid(pkt->TgtID.id()),
                           evd_lrid(pkt->LRID.id()),
                           evd_txnid(pkt->TxnID),
                           evd_rtr_port(entry.to_port));
                sent_something = true;
                it = q.erase(it);
            } else {
                ev_out_of_credit_return();
                ++it;
            }
        } else {
            ++it;
        }
    }

    if (!q.empty() && !sent_something)
        stall_event();
}

/** Choose an output port for an incoming packet.
 *
 *  General routing rules:
 *
 *      1. If a packet arrives from the NORTH or SOUTH, it will continue in the
 *         same direction until its "y" coordinate matches.
 *      2. If a packet arrives from the EAST or WEST, it will continue in the same
 *         direction until its "x" coordinate matches.
 *      3. If a packet arrives from UP or DOWN, it will continue in the same
 *         direction until its "z" coordinate matches.
 *      4. If a packet reaches one of the matching conditions in 1-3, then we
 *         need to make a decision on where to go next:
 *          a) Check the routing order (e.g., "xyz", "yxz", etc) to find the
 *             first coordinate which does not match and send the packet in
 *             the appropriate direction.
 *          b) If all coordinates match, send the packet toward its "port".
 *      5. If a packet arrives from a device port, we need to determine where
 *         to send it:
 *          a) If its coordinates already match this node, send the packet toward
 *             its "port".
 *          b) If the target exists in the LRT and it has an routing override,
 *             use the routing override order and find the first mismatching
 *             coordinate to determine the output port.
 *          c) If the target does not have an override, use the default routing
 *             order in this node to find the first mismatching coordinate and
 *             thus the output.
 */
template<typename T>
std::pair<OutOpt<T>*, int> MeshNode::output_port(T& pkt, int port_num, int port_index)
{
    bool from_device = port_index >= DIR_COUNT;
    int out_index = -1;
    bool override_xy = false;

    if (!from_device) {
        // First, handle "intertial routing" rules 1-3.
        switch (port_index) {
        case NORTH:
            if (pkt->LRID.y() != y)
                out_index = SOUTH;
            break;
        case SOUTH:
            if (pkt->LRID.y() != y)
                out_index = NORTH;
            break;
        case EAST:
            if (pkt->LRID.x() != x)
                out_index = WEST;
            break;
        case WEST:
            if (pkt->LRID.x() != x)
                out_index = EAST;
            break;
        case UP:
            if (pkt->LRID.x() != x || pkt->LRID.y() != y) {
                if (pkt->LRID.t_z() != z)
                    out_index = DOWN;
            } else {
                if (pkt->LRID.z() != z)
                    out_index = DOWN;
            }
            break;
        case DOWN:
            if (pkt->LRID.x() != x || pkt->LRID.y() != y) {
                if (pkt->LRID.t_z() != z)
                    out_index = UP;
            } else {
                if (pkt->LRID.z() != z)
                    out_index = UP;
            }
            break;
        default:
            break;
        }
    } else {
        auto LRID_without_device = LocalRoutingId{pkt->LRID.x(), pkt->LRID.y(), pkt->LRID.z(), pkt->LRID.port(), 0};

        override_xy = ((node_xy_overrides.val().contains(OverrideXYEntry{pkt->SrcID, pkt->LRID}))
                       || (from_device && tgt_xy_overrides.val().contains(LRID_without_device)));
        pkt->LRID.set_xyoverride(override_xy);

        if (assign_transport_z) {
            uint8_t transport_z;
            if ((pkt->SrcID.agent_id() & 0x40) || (pkt->TgtID.agent_id() & 0x40)) // if Src or Tgt is CHP/HBM
                transport_z = pkt->LRID.z();
            else
                transport_z = (pkt->SrcID.get_y() + pkt->TgtID.get_x()) % 4;
            pkt->LRID.set_t_z(transport_z);
        }
    }

    if (out_index == -1) {
        // We either matched some coordinate while routing intertially, or the packet has
        // just arrived from a device port. Inspect our routing order to find out where to
        // go next.
        //
        for (auto dir : routing_order.val()) {
            if (pkt->LRID.xyoverride()) {
                if (dir == 'x')
                    dir = 'y';
                else if (dir == 'y')
                    dir = 'x';
            }

            if (dir == 'x' && pkt->LRID.x() != x) {
                out_index = pkt->LRID.x() > x ? EAST : WEST;
                break;
            } else if (dir == 'y' && pkt->LRID.y() != y) {
                out_index = pkt->LRID.y() > y ? NORTH : SOUTH;
                break;
            } else if (dir == 'z' && pkt->LRID.z() != z) {
                out_index = pkt->LRID.z() > z ? UP : DOWN;
                break;
            } else if (dir == 't' && (pkt->LRID.x() != x || pkt->LRID.y() != y) && pkt->LRID.t_z() != z) {
                out_index = pkt->LRID.t_z() > z ? UP : DOWN;
                break;
            }
        }
    }

    if (out_index == -1) {
        // If we still don't have an output, then we must have matched x, y, and z. We
        // are the destination router, so send the packet to its port.
        //
        AMPERE_ASSERT(pkt->LRID.x() == x && pkt->LRID.y() == y && pkt->LRID.z() == z,
                      "{}: RTR ({},{},{}): expected to match x,y,z but did not for {}",
                      name(),
                      x,
                      y,
                      z,
                      pkt->LRID);
        out_index = DIR_COUNT + pkt->LRID.port();
    }

    AMPERE_ASSERT(
        out_index != -1, "{}: RTR ({},{},{}): could not determine an output port for {}", name(), x, y, z, pkt->LRID);

    if ((size_t)port_num >= tx_size<T>(ports.at(out_index)))
        port_num = 0;

    OutOpt<T>* p = tx_get<T>(ports.at(out_index), port_num);

    AMPERE_ASSERT(
        p->bind_count() > 0, "{}: RTR ({},{},{}): packet going to {} sent to unbound port", name(), x, y, z, pkt->LRID);

    return {p, out_index};
}

/** Given a port index, advance to the next port.
 *
 *  This method is used to provide fairness on ingress ports. It also provides
 *  the illusion that the UP and DOWN ports don't even exist in cases where the
 *  router is not connected on those dimensions. This is not necessary for
 *  functionality, but unfortunately I've encountered test failures due to the
 *  lack of backward compatibility with the old behavior. I believe these tests
 *  are flaky (i.e., overly sensitive to timing), but it was easier to support
 *  the old behavior exactly here rather than figure out how to make the tests
 *  more robust.
 */
size_t MeshNode::next_port(size_t port_index) const
{
    ++port_index;

    if (port_index == UP && !have_up_or_down)
        ++port_index;

    if (port_index == DOWN && !have_up_or_down)
        ++port_index;

    if (port_index == ports.size())
        port_index = 0;

    return port_index;
}

LocalRoutingId MeshNode::cross_die_route(NodeId tgt_id, uint16_t txn_id, int node_port)
{
    unsigned index;

    if (lrt_index_fixed->empty())
        index =
            lrt_index_with_tgt_id_2_1 ? (tgt_id.agent_id() >> 1) : (lrt_index_with_tgt_id ? tgt_id.agent_id() : txn_id);
    else
        index = lrt_index_fixed->at(node_port);

    return die_to_die_lrt.at(tgt_id.die_id()).at(index & 0x3);
}

static std::ostream& operator<<(std::ostream& out, const MeshReq& req)
{
    fmt::print(out,
               "Opcode={} Addr=0x{} SrcID={} LRID={} TxnID={}",
               req->Opcode,
               req->Addr,
               req->SrcID,
               req->LRID,
               req->TxnID);
    return out;
}

template<typename T>
void MeshNode::dump_queue(std::ostream& out, const char* name, const Queue<T>& queue) const
{
    out << name << ":\n";

    for (auto& entry : queue)
        out << entry.data << '\n';
}

void MeshNode::dump(std::ostream& out) const
{
    out << name() << ":\n";
    dump_queue(out, "req_q", req_q);
    dump_queue(out, "snp_q", snp_q);
    dump_queue(out, "rsp0_q", rsp0_q);
    dump_queue(out, "rsp1_q", rsp1_q);
    dump_queue(out, "dat0_q", dat0_q);
    dump_queue(out, "dat1_q", dat1_q);
}

} // namespace ampere::mesh
