//
// Copyright (c) 2020 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//
#pragma once

#include "ampere/mesh/mesh_config.h"
#include "ampere/mesh/mesh_module.h"
#include "ampere/mesh/mesh_node.h"
#include "ampere/sc/sc.h"

namespace ampere::mesh {

using namespace ampere::sc;

class Mesh : public MeshModule
{
public:
    Mesh(const sc_core::sc_module_name& name, uint8_t socket_id, uint8_t die_id);

    MeshPort& get_device_port(unsigned x, unsigned y, unsigned port);
    MeshPort& get_device_port(unsigned x, unsigned y, unsigned z, unsigned port);

    Param<unsigned> size_x{"size_x", "Number of nodes in mesh x direction", 4};
    Param<unsigned> size_y{"size_y", "Number of nodes in mesh y direction", 5};
    Param<unsigned> size_z{"size_z", "Number of nodes in mesh z direction", 1};
    Param<unsigned> off_x{"off_x", "Starting offset for x coordinates", 0};
    Param<unsigned> off_y{"off_y", "Starting offset for y coordinates", 0};
    Param<unsigned> devices{"devices", "Number of devices that can be connected to a mesh node", 2};

private:
    std::vector<std::vector<std::vector<std::unique_ptr<MeshNode>>>> mesh_nodes;
};

} // namespace ampere::mesh

/*
 * vim: sw=4:ts=4:et:si:cindent:cino=\:0,(0,g0,t0:
 */
