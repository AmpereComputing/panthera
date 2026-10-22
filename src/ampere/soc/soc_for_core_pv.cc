//
// Copyright (c) 2022 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//
#include <fmt/format.h>
#include "ampere/soc/soc_for_core_pv.h"

namespace ampere::soc {

SocForCorePV::SocForCorePV(const sc_core::sc_module_name& name) : SocBase{name}
{
    auto& params = get_environment()->parameters;

    params.add_setting(fmt::format("{}.core_0.node_id", this->name()), chi::NodeId{0}, "soc");
    params.add_setting(fmt::format("{}.core_0.l2c.sam.type", this->name()), "RN_SAM", "soc");
    params.add_setting(fmt::format("{}.core_0.l2c.sam.system_address_map.connect", this->name()),
                       fmt::format("{}.system_address_map", this->name()),
                       "soc");
    params.add_setting(fmt::format("{}.core_0.l2c.sam.tgt_lut", this->name()), "[[0,0,0,1]]"_json, "soc");
    params.add_setting(fmt::format("{}.mem_responder.node_id", this->name()), chi::NodeId{1}, "soc");
    params.add_setting(fmt::format("~.*core_to_mem.*capacity", this->name()), 8, "soc");

    core = std::make_unique<Core>("core_0");
    mem_responder = std::make_unique<memory::MemResponder>("mem_responder");

    core->get_mesh_port().connect(mem_responder->get_mesh_port(), "core_to_mem");
}

} // namespace ampere::soc
