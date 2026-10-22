//
// Copyright (c) 2018-2020 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//

#ifndef AMPERE_RESPONDER_H
#define AMPERE_RESPONDER_H

#include <fstream>
#include <string>

#include "ampere/mesh/mesh_module.h"
#include "ampere/mesh/mesh_device.h"
#include "ampere/sc/sc.h"

namespace ampere::mesh {

using namespace ampere::mesh;
using namespace ampere::sc;
using namespace ampere::archx;

class CHIResponder
    : public MeshModule,
      public implements<CHIResponder, MeshDevice, sc::poly_constructor_default, sc::poly_constructor_clock>,
      MeshEvents
{
public:
    explicit CHIResponder(const sc_core::sc_module_name& name) : CHIResponder{name, nullptr} {}
    CHIResponder(const sc_core::sc_module_name& name, Clock* clock);
    bool run() override;

    MeshPort& get_mesh_port(int i = 0) override { return mesh_port; }
    NodeId get_node_id(int i = 0) const override { return node_id; }

private:
    Param<NodeId> node_id{"node_id", "Mesh Node identifier (for routing)", NodeId{0}};
    Param<size_t> max_throughput_param{"max_throughput", "maximum outstanding responder requests", 32};
    Param<double> capacity_param{"capacity", "scaling capacity of responder", 32.0};
    Param<double> min_read_latency_param{"min_read_latency", "unloaded memory latency", 10.0};
    Param<double> read_sensitivity_param{"read_sensitivity", "sensitivity of latency to throughput increases", 10.0};
    Param<double> min_write_latency_param{"min_write_latency", "unloaded memory latency", 5.0};
    Param<double> write_sensitivity_param{"write_sensitivity", "sensitivity of latency to throughput increases", 10.0};
    Param<double> read_linearity{"read_linearity", "linearity of latency to throughput increases", 0.0};
    Param<bool> dump_responder_trace{"dump_responder_trace", "dump responder trace", false};
    Param<int> num_rsp_ports{"num_rsp_ports", "number of response ports", 2};

    Param<std::string> responder_trace_file_name{"responder_trace_file_name", "responder trace file name", ""};
    std::ofstream outfile;

    MeshPort mesh_port{"mesh_port", 1, num_rsp_ports, 1, 1};

    const double clock_scale;

    void send_responses();
    void receive_requests();
    CycleDelta response_latency(MeshReq& req);
    void schedule_wakeup();

    EventData<unsigned> occupancy_data{"occupancy", "Occupancy request queue at time of request"};

    Event responder_response_full_event{"responder_response_full",
                                        "CHIResponder response port full with at least one outstanding response ready"};
    Event responder_data_full_event{"responder_data_full",
                                    "CHIResponder data port full with at least one outstanding data response ready"};

    std::multimap<Cycle, MeshRsp> response_queue;
    std::multimap<Cycle, MeshDat> data_queue;
};

} // namespace ampere::mesh

#endif // AMPERE_RESPONDER_H

/*
 * vim: sw=4:ts=4:et:si:cindent:cino=\:0,(0,g0,t0:
 */
