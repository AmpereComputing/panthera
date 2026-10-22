//
// Copyright (c) 2024 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//
// A Kestrel Tile is one cluster of AI cores and an AICE block, together
// with a little bit of routing. It acts as MeshDevice and thus offers
// a MeshPort as an interface to the outside world (in this case to a
// slice).
//
#pragma once

#include <vector>

#include "ampere/mesh/mesh_device.h"
#include "ampere/mesh/mesh_module.h"
#include "ampere/mesh/mesh_node.h"
#include "ampere/mesh/mesh_port.h"
#include "ampere/sc/sc.h"

namespace ampere::kestrel {

using namespace ampere::mesh;

class Tile : public MeshModule
{
public:
    explicit Tile(const sc_core::sc_module_name& name);

    MeshPort& get_mesh_port() { return rtr.ports.at(NORTH); }

private:
    Param<std::vector<json>> devices_cfg{"devices", "configuration of devices in the tile", "[]"_json};
    Param<uint8_t> slice_id{"slice_id", "ID number of this slice", 0};
    Param<uint8_t> tile_id{"tile_id", "ID number of this tile", 0};
    Param<int> agent_id_offset{
        "agent_id_offset", "Offset to add to all agent ids to conver them from local to global", 0};

    MeshNode rtr{"rtr", static_cast<unsigned int>(slice_id * 2), tile_id, 2, 0, 0};
    std::vector<std::unique_ptr<MeshDevice>> devices;

    void build_devices();
    std::vector<MeshDevice*> make_devices(const std::map<uint8_t, json>& devices, uint8_t port);
    MeshDevice* make_device_from_cfg(const json& dev_cfg);
    MeshDevice* make_device_with_node_id(const std::string& type_name, const std::string& dev_name, NodeId node_id);
    template<typename T = MeshDevice>
    T* make_device(const std::string& type_name, const std::string& dev_name);
    std::string gen_device_name(const std::string& prefix, const std::string& type_name) const;
};

} // namespace ampere::kestrel
