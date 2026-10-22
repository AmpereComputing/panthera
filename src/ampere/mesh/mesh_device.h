//
// Copyright (c) 2020 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//
#pragma once

#include "ampere/mesh/mesh_port.h"
#include "ampere/sc/event.h"

namespace ampere::mesh {

class SAM;

struct MeshDevice
{
    virtual ~MeshDevice() = default;
    virtual MeshPort& get_mesh_port(int i = 0) = 0;
    virtual NodeId get_node_id(int i = 0) const = 0;
    virtual const SAM* get_sam() const { return nullptr; }
};

struct MeshEvents
{
    Event ev_send_req{"send_req", "send a request to the mesh"};
    Event ev_send_rsp{"send_rsp", "send a request to the mesh"};
    Event ev_send_snp{"send_snp", "send a snoop to the mesh"};
    Event ev_send_dat{"send_dat", "send data to the mesh"};
    Event ev_recv_req{"recv_req", "receive a request from the mesh"};
    Event ev_recv_rsp{"recv_rsp", "receive a response from the mesh"};
    Event ev_recv_snp{"recv_snp", "receive a snoop from the mesh"};
    Event ev_recv_dat{"recv_dat", "receive data from the mesh"};

    EventData<uint16_t> evd_txnid{"txnid", "transaction id"};
    EventData<uint16_t> evd_returntxnid{"returntxnid", "return transaction id"};
    EventData<uint32_t> evd_srcid{"srcid", "source id"};
    EventData<uint32_t> evd_tgtid{"tgtid", "target id"};
    EventData<uint32_t> evd_lrid{"lrid", "local routing id"};
    EventData<uint8_t> evd_state{"state", "state"};
    EventData<uint8_t> evd_dbid{"dbid", "DBID"};
    EventData<uint32_t> evd_qos{"qos", "QOS"};
    EventData<bool> evd_allow_retry{"allow_retry", "allow retry"};

private:
    template<typename T>
    struct GetEvent
    {
        static Event& send(MeshEvents& cls);
        static Event& recv(MeshEvents& cls);
    };

public:
    template<typename T, typename... Args>
    void ev_send(const T& pkt, Args&&... args)
    {
        GetEvent<T>::send (*this)(pkt, std::forward<Args>(args)...);
    }

    template<typename T, typename... Args>
    void ev_recv(const T& pkt, Args&&... args)
    {
        GetEvent<T>::recv (*this)(pkt, std::forward<Args>(args)...);
    }
};

struct Stasher;

using NodeIdToStasher = std::function<Stasher*(NodeId)>;

struct Stasher
{
    virtual bool can_stash() const { return false; }
    virtual void stash_preload(PhysAddr, FinalState, NodeIdToStasher, chi::NodeId requester_node)
    {
        AMPERE_FATAL("stash_preload() not supported");
    }
    virtual void stash_invalidate(PhysAddr, NodeId from_node) { AMPERE_FATAL("stash_invalidate() not supported"); }
};

} // namespace ampere::mesh
