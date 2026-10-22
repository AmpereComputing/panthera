//
// Copyright (c) 2022 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//

#include "ampere/mesh/control_module.h"

namespace ampere::mesh {

ControlMod::ControlMod(const sc_core::sc_module_name& name) : Module{name}
{
    ready = -1;
    processing_node = NodeId(chi::NodeId::NodeId_Type::Global);
}

unsigned ControlMod::get_core_num(NodeId core_id) const
{
    if (core_id.nodeid_type() == NodeId::NodeId_Type::Local) {
        AMPERE_ASSERT(core_id.x() <= 4 && core_id.y() <= 3 && core_id.device() <= 3,
                      "X, Y or Device number of core_id is invalid");
        return (core_id.x() - 1) * 16 + core_id.y() * 4 + core_id.device();
    } else {
        AMPERE_ASSERT(core_id.agent_id() <= 63, "Agent ID is not valid");
        return core_id.agent_id();
    }
}

void ControlMod::op_start(NodeId core_id, uint64_t mask)
{
    ready = mask & ((uint64_t)1 << get_core_num(core_id));
    processing_node = core_id;
}

bool ControlMod::is_running(NodeId core_id) const
{
    if (core_id.nodeid_type() == processing_node.nodeid_type())
        return (core_id == processing_node) ? true : false;
    return false;
}

bool ControlMod::is_ready(NodeId core_id) const
{
    if ((ready & ((uint64_t)1 << get_core_num(core_id))) && !is_running(core_id))
        return true;
    return false;
}

void ControlMod::op_done(NodeId core_id, uint64_t mask)
{
    ready = mask & ~((uint64_t)1 << get_core_num(core_id));
    processing_node = NodeId(core_id.nodeid_type());
}

} // namespace ampere::mesh
