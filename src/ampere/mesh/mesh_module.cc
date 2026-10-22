//
// Copyright (c) 2024 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//
#include "ampere/mesh/mesh_module.h"
#include "ampere/mesh/mesh_device.h"
#include "ampere/soc/die.h"

namespace ampere::mesh {

uint8_t MeshModule::get_die_id() const
{
    if (const auto *const dev = sc::get_nearest<MeshDevice>(); dev) {
        auto node_id = dev->get_node_id();
        return node_id.nodeid_type() == NodeId::NodeId_Type::Global ? node_id.die_id() : 0;
    }

    if (const auto *const die = sc::get_nearest_ancestor<soc::Die>(this); die)
        return die->get_die_id();

    // AMPERE_FATAL("could not determine a die id for {}", name());
    return 0;
}

} // namespace ampere::mesh
