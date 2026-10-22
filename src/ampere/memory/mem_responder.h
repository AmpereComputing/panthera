//
// Copyright (c) 2022 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//
#pragma once

#include <map>

#include "ampere/mesh/mesh_device.h"
#include "ampere/mesh/mesh_module.h"
#include "ampere/sc/sc.h"

namespace ampere::memory {

using namespace mesh;
using namespace sc;

/** MemReponder
 *
 * A simple object that can response to a core's memory requests. This is used
 * in RTL PV configurations to provide fixed latency responses to the core.
 */
class MemResponder : public MeshModule, public implements<MemResponder, MeshDevice>, MeshEvents
{
public:
    MemResponder(const sc_core::sc_module_name& name) : MeshModule{name} {}

    MeshPort& get_mesh_port(int i = 0) override { return mesh_port; }
    NodeId get_node_id(int i = 0) const override { return node_id; }

    void dump(std::ostream& out = std::cout) const override;

private:
    Param<chi::NodeId> node_id{"node_id", "Mesh Node identifier (for routing)", chi::NodeId{0}};
    Param<Cycle> dat_delay{"dat_delay", "delay (in cycles) for data return", 200};
    Param<Cycle> rsp_delay{"rsp_delay", "delay (in cycles) for response return", 60};
    Param<bool> separate_response_and_data{"separate_response_and_data", "separate response and data packets", true};

    MeshPort mesh_port;

    // This transaction tracker is not functionally required, but is useful
    // for inspecting the state of the responder (see, e.g., the dump method).
    std::vector<MeshReq> transactions;

    std::multimap<Cycle, MeshRsp> rsp_queue;
    std::multimap<Cycle, MeshDat> dat_queue;

    std::map<uint16_t, uint8_t> awaiting_data;
    std::map<uint16_t, MeshReq> awaiting_comp_ack;

    bool run() override;

    void receive();
    void send();

    void do_read(MeshReq req, FinalState state);
    void do_evict(MeshReq req);
    void do_makeunique(MeshReq req);
    void do_write_back(MeshReq req);
    void do_comp_ack(MeshRsp rsp);

    uint8_t data_id_to_chunks(uint8_t data_id) const;
    bool all_chunks_received(uint8_t chunks) const;

    void schedule_wakeup();

    void add_transaction(MeshReq req);
    void rem_transaction(uint16_t txnid);
};

} // namespace ampere::memory
