/*
 * Copyright (c) 2024-2025 Ampere Computing. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#pragma once

#include <iostream>
#include <nlohmann/json.hpp>
#include <optional>
#include <string>

#include <fmt/format.h>
#include <fmt/ostream.h>
#include "location.h"
#include "lynx.enum.h"
#include "types.h"

namespace ampere::lynx {

using json = nlohmann::json;

class Component
{
public:
    Component(ComponentType type, Location loc, std::optional<std::string_view> tag = {}) : m_type{type}, m_loc{loc}
    {
        if (tag.has_value())
            m_tag = *tag;
    }
    Component(const Component&) = delete;
    Component(Component&&);

    virtual ~Component() = default;

    ComponentType type() const { return m_type; }
    Location loc() const { return m_loc; }

    u8 die_id() const { return m_loc.die_id; }
    u8 x() const { return m_loc.x; }
    u8 y() const { return m_loc.y; }
    i8 port() const { return m_loc.port; }
    i8 didx() const { return m_loc.didx; }

    const std::string& tag() const { return m_tag; }

    static Component from_json(const json& j);

private:
    ComponentType m_type;
    Location m_loc;
    std::string m_tag{};
};

std::ostream& operator<<(std::ostream& os, const Component& comp);

} // namespace ampere::lynx

template<>
struct fmt::formatter<ampere::lynx::Component> : fmt::ostream_formatter
{};

// JSON serialization/deserialization for Component is not as straightforward
// as writing "to_json/from_json" routines because Component is not a default
// constructible type. Following the instructions in the documentation
// (https://github.com/nlohmann/json?tab=readme-ov-file#serialization--deserialization)
// I am defining the routines inside a namespace where an alternate resolution
// can happen.
//
namespace nlohmann {
template<>
struct adl_serializer<ampere::lynx::Component>
{
    static ampere::lynx::Component from_json(const json& j)
    {
        ampere::lynx::ComponentType type = j["type"].get<ampere::lynx::ComponentType>();
        ampere::lynx::Location loc;

        j.get_to(loc);

        std::optional<std::string> tag;

        if (j.contains("tag"))
            tag = j["tag"].get<std::string>();

        return ampere::lynx::Component(type, loc, tag);
    }

    static void to_json(json& j, const ampere::lynx::Component& comp)
    {
        j["type"] = comp.type();
        ampere::lynx::to_json(j, comp.loc());

        if (!comp.tag().empty())
            j["tag"] = comp.tag();
    }
};
} // namespace nlohmann
