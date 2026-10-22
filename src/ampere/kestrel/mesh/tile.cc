//
// Copyright (c) 2024 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//
#include "ampere/kestrel/mesh/tile.h"
#include "ampere/mesh/cpx.h"

namespace ampere::kestrel {

Tile::Tile(const sc_core::sc_module_name& name) : MeshModule{name}
{
    build_devices();
}

void Tile::build_devices()
{
    std::map<uint8_t, std::map<uint8_t, json>> device_map;

    for (const auto& dev_cfg : devices_cfg.val()) {
        auto [port, device] = dev_cfg["position"].get<std::tuple<uint8_t, uint8_t>>();
        device_map[port][device] = dev_cfg;
    }

    for (auto& [port, devices] : device_map) {
        auto& attach_point = rtr.ports.at(DIR_COUNT + port);
        auto num_devices = devices.size();
        auto device_vec = make_devices(devices, port);

        if (num_devices > 1) {
            auto* cpx = make_submod<CPX>("cpx", {{"n", num_devices}});

            for (size_t i = 0; i < device_vec.size(); ++i) {
                device_vec.at(i)->get_mesh_port().connect(cpx->device_port.at(i),
                                                          fmt::format("cpx{}_to_dev{}", port, i));
            }

            cpx->mesh_port.connect(attach_point, fmt::format("cpx_to_rtr_port{}", port));
        } else {
            device_vec[0]->get_mesh_port().connect(attach_point, fmt::format("dev_to_rtr_port{}", port));
        }
    }
}

std::vector<MeshDevice*> Tile::make_devices(const std::map<uint8_t, json>& devices, uint8_t port)
{
    std::vector<MeshDevice*> built_devices;

    built_devices.reserve(devices.size());

    for (const auto& [dev_idx, dev_cfg] : devices)
        built_devices.push_back(make_device_from_cfg(dev_cfg));

    return built_devices;
}

template<typename T>
T* Tile::make_device(const std::string& type_name, const std::string& dev_name)
{
    // Provide an opportunity for the user to override the type with a specific parameter.
    auto full_dev_type_name = fmt::format("{}.{}.type", name(), dev_name);
    auto* setting = get_environment()->parameters.match_setting(full_dev_type_name);

    std::string real_type_name;

    if (setting)
        real_type_name = setting->value().get<std::string>();
    else
        real_type_name = type_name;

    std::unique_ptr<T> dev = make_registered_module<T>(real_type_name, dev_name);
    T* ret = dev.get();
    auto mesh_dev = std::unique_ptr<MeshDevice>(static_cast<MeshDevice*>(dev.release()));
    devices.push_back(std::move(mesh_dev));
    return ret;
}

MeshDevice* Tile::make_device_from_cfg(const json& dev_cfg)
{
    auto type_name = dev_cfg["type"].get<std::string>();
    auto prefix = dev_cfg.contains("prefix") ? dev_cfg["prefix"].get<std::string>() : "";
    auto agent_id = dev_cfg["agent_id"].get<uint8_t>();
    auto dev_name = gen_device_name(prefix, type_name);
    return make_device_with_node_id(type_name, dev_name, NodeId{NodeId::DieType::CPU, 0, 0, agent_id});
}

MeshDevice* Tile::make_device_with_node_id(const std::string& type_name, const std::string& dev_name, NodeId node_id)
{
    auto& params = get_environment()->parameters;
    params.add_setting(fmt::format("{}.{}.node_id", name(), dev_name), node_id, "Soc config");
    return make_device<MeshDevice>(type_name, dev_name);
}

std::string Tile::gen_device_name(const std::string& prefix, const std::string& type_name) const
{
    std::string instance_name_root;

    if (prefix.empty()) {
        instance_name_root = type_name;
        std::transform(instance_name_root.begin(), instance_name_root.end(), instance_name_root.begin(), ::tolower);
    } else {
        instance_name_root = prefix;
    }

    return sc_core::sc_gen_unique_name(instance_name_root.c_str());
}

} // namespace ampere::kestrel
