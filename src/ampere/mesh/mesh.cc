//
// Copyright (c) 2020 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//

#include "ampere/mesh/mesh.h"
#include "ampere/mesh/chi/types.h"

namespace ampere::mesh {

Mesh::Mesh(const sc_core::sc_module_name& name, uint8_t socket_id, uint8_t die_id) : MeshModule{name}
{
    // Create Mesh Nodes
    for (unsigned yi = 0; yi < size_y; yi++) {
        std::vector<std::vector<std::unique_ptr<MeshNode>>> row;
        for (unsigned xi = 0; xi < size_x; xi++) {
            unsigned x = xi + off_x;
            unsigned y = yi + off_y;

            std::vector<std::unique_ptr<MeshNode>> z_nodes;

            if (size_z == 1) {
                auto mesh_node = std::make_unique<MeshNode>(
                    fmt::format("rtr_X{}_Y{}", x, y).c_str(), x, y, devices, socket_id, die_id);

                z_nodes.push_back(std::move(mesh_node));
            } else {
                for (unsigned z = 0; z < size_z; z++) {
                    auto mesh_node = std::make_unique<MeshNode>(
                        fmt::format("rtr_X{}_Y{}_Z{}", x, y, z).c_str(), x, y, z, devices, socket_id, die_id);

                    z_nodes.push_back(std::move(mesh_node));
                }
            }
            row.push_back(std::move(z_nodes));
        }
        mesh_nodes.push_back(std::move(row));
    }

    // Connect mesh nodes to neighboring nodes
    for (unsigned yi = 0; yi < size_y; yi++) {
        for (unsigned xi = 0; xi < size_x; xi++) {
            for (unsigned z = 0; z < size_z; ++z) {
                unsigned x = xi + off_x;
                unsigned y = yi + off_y;

                // Connect West
                if (xi != 0) {
                    std::string conn_name;

                    if (size_z > 1)
                        conn_name = fmt::format("X{}_Y{}_to_X{}_Y{}_Z{}", x - 1, y, x, y, z);
                    else
                        conn_name = fmt::format("X{}_Y{}_to_X{}_Y{}", x - 1, y, x, y);

                    mesh_nodes[yi][xi][z]->ports[WEST].connect(mesh_nodes[yi][xi - 1][z]->ports[EAST], conn_name);
                }

                // Connect South
                if (yi != 0) {
                    std::string conn_name;

                    if (size_z > 1)
                        conn_name = fmt::format("X{}_Y{}_to_X{}_Y{}_Z{}", x, y - 1, x, y, z);
                    else
                        conn_name = fmt::format("X{}_Y{}_to_X{}_Y{}", x, y - 1, x, y);

                    mesh_nodes[yi][xi][z]->ports[SOUTH].connect(mesh_nodes[yi - 1][xi][z]->ports[NORTH], conn_name);
                }

                // Connect Down
                if (z != 0) {
                    mesh_nodes[yi][xi][z]->ports[DOWN].connect(
                        mesh_nodes[yi][xi][z - 1]->ports[UP],
                        fmt::format("X{}_Y{}_Z{}_to_X{}_Y{}_Z{}", x, y, z - 1, x, y, z));
                }
            }
        }
    }
}

MeshPort& Mesh::get_device_port(unsigned x, unsigned y, unsigned port)
{
    AMPERE_ASSERT(size_z == 1, "{}: missing z coordinate to get_device_port", name());
    return mesh_nodes.at(y - off_y).at(x - off_x).at(0)->ports.at(DIR_COUNT + port);
}

MeshPort& Mesh::get_device_port(unsigned x, unsigned y, unsigned z, unsigned port)
{
    return mesh_nodes.at(y - off_y).at(x - off_x).at(z)->ports.at(DIR_COUNT + port);
}

} // namespace ampere::mesh

/*
 * vim: sw=4:ts=4:et:si:cindent:cino=\:0,(0,g0,t0:
 */
