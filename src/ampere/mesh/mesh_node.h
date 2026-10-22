//
// Copyright (c) 2020-2022 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//

#pragma once

#include <algorithm>
#include <deque>
#include <unordered_map>
#include <unordered_set>

#include "ampere/mesh/mesh_dat.h"
#include "ampere/mesh/mesh_device.h"
#include "ampere/mesh/mesh_module.h"
#include "ampere/mesh/mesh_port.h"
#include "ampere/mesh/mesh_req.h"
#include "ampere/mesh/mesh_rsp.h"
#include "ampere/mesh/mesh_snp.h"
#include "ampere/sc/connect.h"

namespace ampere::mesh {

using namespace ampere::sc;

enum {
    SOUTH,
    EAST,
    NORTH,
    WEST,
    UP,
    DOWN,
    DIR_COUNT
};

class MeshNode : public MeshModule, MeshEvents
{
private:
    unsigned x;
    unsigned y;
    unsigned z;

public:
    sc_core::sc_vector<MeshPort> ports{"ports"};

    MeshNode(const sc_core::sc_module_name& name,
             unsigned x,
             unsigned y,
             unsigned ports_cnt,
             uint8_t socket_id,
             uint8_t die_id)
        : MeshNode{name, x, y, 0, ports_cnt, socket_id, die_id}
    {
    }

    MeshNode(const sc_core::sc_module_name& name,
             unsigned x,
             unsigned y,
             unsigned z,
             unsigned ports_cnt,
             uint8_t socket_id,
             uint8_t die_id);

    struct OverrideXYEntry
    {
        NodeId src;
        LocalRoutingId tgt;

        OverrideXYEntry() = default;
        OverrideXYEntry(NodeId src, LocalRoutingId tgt) : src{src}, tgt{tgt} {}

        friend void from_json(const json& j, OverrideXYEntry& entry)
        {
            j["src"].get_to(entry.src);
            j["tgt"].get_to(entry.tgt);
        }

        bool operator==(const OverrideXYEntry& rhs) const { return (src == rhs.src && tgt == rhs.tgt); }
    };
    struct OverrideXYEntryHash
    {
        std::size_t operator()(const OverrideXYEntry& rhs) const
        {
            return ((std::hash<uint16_t>()(rhs.src.id()) << 16) | (std::hash<uint16_t>()(rhs.tgt.id())));
        }
    };

    struct LocalRoutingIdHash
    {
        std::size_t operator()(const LocalRoutingId& lrid) const { return std::hash<uint16_t>()(lrid.id()); }
    };

    Param<std::string> routing_order{"routing_order", "Order in which to route dimensions", "txyz"};

    // These two versions of X/Y overrides exist because it's done a little differently in pre-Matrix
    // vs. Matrix. In Matrix we only expect a table of (local) target ids, and they're only applied
    // when a packet arrives from a device port.
    //
    // The old way used both source and target, and is applied whenever a packet is routed. I'm
    // honestly not sure if this old way is *right*, but I didn't want to disturb it for Matrix.
    //
    Param<std::unordered_set<OverrideXYEntry, OverrideXYEntryHash>> node_xy_overrides{
        "xy_overrides", "JSON list of src/dst overrides for this node.", "[]"_json};

    Param<std::unordered_set<LocalRoutingId, LocalRoutingIdHash>> tgt_xy_overrides{
        "tgt_xy_overrides", "X/Y overrides based only on target", "[]"_json};

    Param<std::map<uint8_t, LocalRoutingId>> local_lut{
        "local_lut", "Routing LUT for same-die agents (not normally set by the user)", "[]"_json};
    Param<bool> lrt_index_with_tgt_id{
        "lrt_index_with_tgt_id", "index into the die-to-die LRT with TgtID rather than TxnID", false};
    Param<bool> lrt_index_with_tgt_id_2_1{
        "lrt_index_with_tgt_id_2_1", "index into the die-to-die LRT with TgtID rather than TxnID", false};
    Param<std::vector<int>> lrt_index_fixed{"lrt_index_fixed", "array of LRT indexes to use per node port", "[]"_json};
    Param<json> die_to_die_lrt_param{
        "die_to_die_lrt", "Die-to-Die Local Routing Table (i.e., which RDI port to use)", "[]"_json};

    Param<uint8_t> tile_z{"tile_z", "Z coordinate of Tile in Kestrel mesh", 3};

    Param<unsigned> req_decode_latency{"req_decode_latency", "Latency to decode incoming requests (cycles)", 0};
    Param<unsigned> snp_decode_latency{"snp_decode_latency", "Latency to decode incoming snoops (cycles)", 0};
    Param<unsigned> rsp_decode_latency{"rsp_decode_latency", "Latency to decode incoming responses (cycles)", 0};
    Param<unsigned> dat_decode_latency{"dat_decode_latency", "Latency to decode incoming data (cycles)", 0};

    Param<bool> two_request_channels{"two_request_channels", "Use two request channels", false};
    Param<bool> assign_transport_z{
        "assign_transport_z", "Assign a transport Z upon arrival from a node port (Kestrel)", false};

    bool run() override;
    void dump(std::ostream& = std::cout) const override;

private:
    template<typename T>
    struct Entry
    {
        T data;
        Credit credit;
        Cycle ready;

        InWithCreditOpt<T>* from;
        OutOpt<T>* to;
        int to_port;

        Entry(T data, Credit credit, Cycle ready, InWithCreditOpt<T>* from, OutOpt<T>* to, int to_port)
            : data{std::move(data)}, credit{std::move(credit)}, ready{ready}, from{from}, to{to}, to_port{to_port}
        {
        }
    };

    template<typename T>
    using Queue = std::deque<Entry<T>>;

    Queue<MeshReq> req_q;
    Queue<MeshReq> req1_q;
    Queue<MeshSnp> snp_q;
    Queue<MeshRsp> rsp0_q;
    Queue<MeshRsp> rsp1_q;
    Queue<MeshDat> dat0_q;
    Queue<MeshDat> dat1_q;

    size_t start_port{0};
    bool have_up_or_down{false};

    uint8_t socket_id;
    uint8_t die_id;
    std::map<uint8_t, std::vector<LocalRoutingId>> die_to_die_lrt;

    std::vector<std::unique_ptr<CreditPool>> credit_pools;
    CreditPool* make_credit_pool(const char* name, const std::string& suffix);

    void end_of_elaboration() override;

    void arrive();
    void depart();

    template<typename T>
    void arrive_from(InWithCreditOpt<T>& port,
                     Queue<T>& q,
                     int port_num,
                     Event& recv_event,
                     int port_index,
                     unsigned decode_latency);
    template<typename T>
    void depart_to(Queue<T>& q, Event& send_event, Event& stall_event);

    int choose_req(InWithCreditOpt<MeshReq>& port);
    int choose_dat(InWithCreditOpt<MeshDat>& port);

    template<typename T>
    std::pair<OutOpt<T>*, int> output_port(T& pkt, int port_num, int port_index);

    size_t next_port(size_t port_index) const;

    LocalRoutingId cross_die_route(NodeId tgt_id, uint16_t txn_id, int node_port);

    template<typename T>
    void dump_queue(std::ostream& out, const char* name, const Queue<T>& queue) const;

    Event ev_send_req1{"send_req1", "send a request to the mesh"};
    Event ev_send_rsp1{"send_rsp1", "send a response to the mesh"};
    Event ev_send_dat1{"send_dat1", "send data to the mesh"};
    Event ev_recv_req1{"recv_req1", "receive a request from the mesh"};
    Event ev_recv_rsp1{"recv_rsp1", "receive a response from the mesh"};
    Event ev_recv_dat1{"recv_dat1", "receive data from the mesh"};

    Event ev_req_stall{"req_stall", "had requests to send, but sent none this cycle"};
    Event ev_snp_stall{"snp_stall", "had snoops to send, but sent none this cycle"};
    Event ev_rsp0_stall{"rsp0_stall", "had responses to send, but sent none this cycle"};
    Event ev_rsp1_stall{"rsp1_stall", "had responses to send, but sent none this cycle"};
    Event ev_dat0_stall{"dat0_stall", "had data to send, but sent none this cycle"};
    Event ev_dat1_stall{"dat1_stall", "had data to send, but sent none this cycle"};

    Event ev_out_of_credit_return{"out_of_credit_return", "out of bandwidth for credit returns"};

    EventData<int> evd_rtr_port{"rtr_port",
                                "port RTR is sending on (0=SOUTH, 1=EAST, 2=NORTH, 3=WEST; rest are devices)"};
};

} // namespace ampere::mesh

/*
 * vim: sw=4:ts=4:et:si:cindent:cino=\:0,(0,g0,t0:
 */
