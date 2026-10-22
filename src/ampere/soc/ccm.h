//
// Copyright (c) 2020 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//

#ifndef AMPERE_SOC_CCM_H
#define AMPERE_SOC_CCM_H

#include "ampere/core/core.h"
#include "ampere/mesh/mesh_module.h"
#include "ampere/mesh/mesh_port.h"
#include "ampere/sc/sc.h"

namespace ampere::soc {

using namespace ampere::archx;
using namespace ampere::sc;
using namespace ampere::core;
using namespace ampere::mesh;

class Ccm : public MeshModule, public implements<Ccm, mesh::MeshDevice>, MeshEvents
{
public:
    Ccm(const sc_core::sc_module_name& name);
    MeshPort mesh_port{"mesh_port"};
    mesh::MeshPort& get_mesh_port(int i = 0) override { return mesh_port; }
    NodeId get_node_id(int i = 0) const override { return node_id; }

    Param<chi::NodeId> node_id{"node_id", "Mesh Node identifier (for routing)", chi::NodeId{0}};
    Param<int> txn_outstanding_max{"txn_outstanding_max", "Number of Txns this node can have outstanding", 256};

protected:
    Core core{"core"};

    // Requester ports
    In<MemReq> request{"request"};
    Out<MemResp> response{"response"};
    Out<MemReq> snoop{"snoop"};

    std::deque<int> free_txns;
    std::map<int, std::pair<int, MeshReq>> outstanding_reqs;

    // Queues to store outstanding core reqs/rsps
    std::queue<MemReq> core_reqs;
    std::queue<MemResp> core_rsps;

    // Queues to store outstanding mesh reqs/rsps/snps/dats
    std::queue<MeshReq> mesh_reqs;
    std::queue<MeshRsp> mesh_rsps;
    std::queue<MeshSnp> mesh_snps;
    std::queue<MeshDat> mesh_incoming_data;

    void make_response(MeshSnp snp, chi::ResponseOpcode opcode);

    // Communication with core(s)
    void core_receive_requests();

    // Communication with mesh
    void mesh_receive_traffic();
    void mesh_send_traffic();

    // Processing of core ports
    void core_handle_requests();
    void core_send_responses();

    // Processing of mesh ports
    void mesh_handle_data();
    void mesh_handle_snoops();

    bool run() override;
};

} // namespace ampere::soc

#endif // AMPERE_SOC_CCM_H

/*
 * vim: sw=4:ts=4:et:si:cindent:cino=\:0,(0,g0,t0:
 */
