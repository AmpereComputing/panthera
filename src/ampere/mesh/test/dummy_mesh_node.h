//
// Copyright (c) 2020 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//

#ifndef AMPERE_MESH_TEST_DUMMY_MESH_NODE_H
#define AMPERE_MESH_TEST_DUMMY_MESH_NODE_H

#include <vector>

#include "ampere/mesh/mesh_port.h"
#include "ampere/sc/sc.h"

namespace ampere::mesh {

using namespace ampere::sc;

class DummyMeshNode : public Module
{
public:
    DummyMeshNode(const sc_core::sc_module_name& name) : Module{name}
    {
        mesh_ports.init(ports, [](const char* name, size_t index) { return new MeshPort(name, 1, 2, 1, 1); });
    }
    Param<size_t> ports{"ports", "number of mesh ports", 2};

    sc_core::sc_vector<MeshPort> mesh_ports{"mesh_port"};

    bool run() override;

    MeshReq req;
    MeshRsp rsp;
    MeshSnp snp;
    MeshDat dat;

    size_t starting_port{0};
};

} // namespace ampere::mesh

#endif // AMPERE_MESH_TEST_DUMMY_MESH_NODE_H
