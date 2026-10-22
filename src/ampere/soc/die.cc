//
// Copyright (c) 2022 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//

#include "ampere/soc/die.h"

#include <algorithm>

namespace ampere::soc {

Die::Die(const sc_core::sc_module_name& name)
    : sc::Module{name}, type{type_from_param()}, built_local_lut{build_local_lut()}
{
    using Coord = std::tuple<uint8_t, uint8_t, uint8_t, uint8_t>;
    std::map<Coord, std::map<uint8_t, json>> device_map;

    bool using_z = false;

    for (const auto& dev_cfg : get_devices()) {
        auto num_coords = dev_cfg["position"].size();

        if (num_coords == 4) {
            auto [x, y, port, device] = dev_cfg["position"].get<std::tuple<uint8_t, uint8_t, uint8_t, uint8_t>>();
            device_map[{x, y, 0, port}][device] = dev_cfg;
        } else if (num_coords == 5) {
            auto [x, y, z, port, device] =
                dev_cfg["position"].get<std::tuple<uint8_t, uint8_t, uint8_t, uint8_t, uint8_t>>();
            device_map[{x, y, z, port}][device] = dev_cfg;
            using_z = true;
        } else {
            AMPERE_ERROR("expecting either 4 or 5 coordinates in a \"position\"");
        }
    }

    for (auto& [coord, devices] : device_map) {
        auto [x, y, z, port] = coord;

        auto& dev_cfg = devices.begin()->second;

        if (dev_cfg.contains("via")) {
            std::string conn_name;

            if (using_z)
                conn_name = fmt::format("rdi_X{}_Y{}_Z{}_P{}", x, y, z, port);
            else
                conn_name = fmt::format("rdi_X{}_Y{}_P{}", x, y, port);

            if (dev_cfg["via"].get<std::string>() == "rdi") {
                auto rdi = std::make_unique<RDI>(conn_name.c_str(), this->get_clock());
                rdi_map[{x, y, z, port}] = std::move(rdi);
            } else if (dev_cfg["via"].get<std::string>() == "idt") {
                auto rdi = std::make_unique<IDT>(conn_name.c_str(), this->get_clock());
                rdi_map[{x, y, z, port}] = std::move(rdi);
            }
        }
    }
}

bool Die::build_local_lut()
{
    std::map<uint8_t, LocalRoutingId> lut;
    using Coord = std::tuple<uint8_t, uint8_t, uint8_t, uint8_t>;
    std::map<Coord, std::map<uint8_t, json>> device_map;

    bool using_z = false;

    for (const auto& dev_cfg : get_devices()) {
        uint8_t x{0};
        uint8_t y{0};
        uint8_t z{0};
        uint8_t port{0};
        uint8_t device{0};

        auto num_coords = dev_cfg["position"].size();

        if (num_coords == 4) {
            std::tie(x, y, port, device) = dev_cfg["position"].get<std::tuple<uint8_t, uint8_t, uint8_t, uint8_t>>();
        } else if (num_coords == 5) {
            std::tie(x, y, z, port, device) =
                dev_cfg["position"].get<std::tuple<uint8_t, uint8_t, uint8_t, uint8_t, uint8_t>>();
            using_z = true;
        } else {
            AMPERE_ERROR("expecting either 4 or 5 coordinates in a \"position\"");
        }

        auto lrid = LocalRoutingId{x, y, z, port, device};
        auto agent_id = dev_cfg["agent_id"].get<uint16_t>();
        lut[agent_id] = lrid;
        device_map[{x, y, z, port}][device] = dev_cfg;
    }

    std::string prefix_name;

    if (using_z) {
        prefix_name = fmt::format(R"(~{}\.mesh\.rtr_X[0-9]+_Y[0-9]+_Z[0-9]+)", name());
        get_environment()->parameters.add_setting(prefix_name + "\\.assign_transport_z", true, "die constructor");
    } else {
        prefix_name = fmt::format(R"(~{}\.mesh\.rtr_X[0-9]+_Y[0-9]+)", name());
    }

    get_environment()->parameters.add_setting(prefix_name + "\\.local_lut", lut, "die constructor");

    return true;
}

Die::Type Die::type_from_param()
{
    std::string s = type_param;
    std::ranges::transform(s, s.begin(), [](unsigned char c) { return std::tolower(c); });

    if (s == "cpu")
        return Type::CPU;
    else if (s == "io")
        return Type::IO;

    AMPERE_ERROR("{}.type must be either CPU or IO", name());
    return Type::CPU;
}

} // namespace ampere::soc
