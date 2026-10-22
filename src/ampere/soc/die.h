//
// Copyright (c) 2022 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//
#pragma once

#include "ampere/mesh/mesh.h"
#include "ampere/sc/sc.h"
#include "ampere/mesh/sam.h"
#include "ampere/mesh/idt.h"
#include "ampere/mesh/control_module.h"

namespace ampere::soc {

using namespace mesh;
using namespace sc;

class Die : public sc::Module
{
public:
    Die(const sc_core::sc_module_name& name);

    using Type = chi::NodeId::DieType;

    const std::vector<json>& get_devices() const { return devices_cfg.val(); }
    Mesh& get_mesh() { return mesh; }
    Type get_type() { return type; }
    uint8_t get_die_id() const { return die_id; }
    DieConnector* get_rdi_obj(uint8_t x, uint8_t y, uint8_t port)
    {
        auto it = rdi_map.find(std::make_tuple(x, y, 0, port));
        AMPERE_ASSERT(it != rdi_map.end(), "no die connector found at ({}, {}, {})", x, y, port);
        return it->second.get();
    }
    DieConnector* get_rdi_obj(uint8_t x, uint8_t y, uint8_t z, uint8_t port)
    {
        auto it = rdi_map.find(std::make_tuple(x, y, z, port));
        AMPERE_ASSERT(it != rdi_map.end(), "no die connector found at ({}, {}, {}, {})", x, y, z, port);
        return it->second.get();
    }

private:
    Param<std::string> type_param{"type", "type for this die (CPU or IO)", "CPU"};
    Param<std::vector<json>> devices_cfg{"devices", "configuration of devices attached to the mesh", "[]"_json};
    Param<uint8_t> die_id{"die_id", "id for this die", 0};

    Type type;

    std::map<std::tuple<uint8_t, uint8_t, uint8_t, uint8_t>, std::unique_ptr<DieConnector>> rdi_map;

    // this is a dummy variable that we use to do some side-effectful
    // initialization (it sets parameters)---it MUST come before Mesh
    // because building the Mesh will make use of these parameters.
    bool built_local_lut;

    Mesh mesh{"mesh", 0, die_id}; // FIXME: hard-coded socket id

    bool build_local_lut();

    ControlMod controlmod{"controlmod"};

    Type type_from_param();
};

} // namespace ampere::soc
