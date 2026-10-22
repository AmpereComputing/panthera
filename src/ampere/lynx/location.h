/*
 * Copyright (c) 2024-2025 Ampere Computing. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#pragma once

#include <fmt/format.h>
#include <fmt/ostream.h>
#include <nlohmann/json.hpp>

#include "types.h"

namespace ampere::lynx {

using json = nlohmann::json;

struct Location
{
    Location() : die_id{0}, x{0}, y{0}, port{-1}, didx{-1} {}
    Location(u8 die_id, u8 x, u8 y, i8 port = -1, i8 didx = -1) : die_id{die_id}, x{x}, y{y}, port{port}, didx{didx} {}
    Location(const Location&) = default;

    u8 die_id;
    u8 x;
    u8 y;
    i8 port;
    i8 didx;

    bool operator==(const Location& other) const
    {
        return die_id == other.die_id && x == other.x && y == other.y && port == other.port && didx == other.didx;
    }

    bool operator!=(const Location& other) const { return !(*this == other); }
};

static constexpr u8 inc(u8 x)
{
    return x + 1;
}
static constexpr u8 dec(u8 x)
{
    return x - 1;
}

inline auto east(const Location& loc) -> Location
{
    return Location{loc.die_id, inc(loc.x), loc.y};
}

inline auto west(const Location& loc) -> Location
{
    return Location{loc.die_id, dec(loc.x), loc.y};
}

inline auto north(const Location& loc) -> Location
{
    return Location{loc.die_id, loc.x, inc(loc.y)};
}

inline auto south(const Location& loc) -> Location
{
    return Location{loc.die_id, loc.x, dec(loc.y)};
}

std::ostream& operator<<(std::ostream& os, const Location& comp);

inline void to_json(json& j, const Location& loc)
{
    j["die_id"] = loc.die_id;
    j["x"] = loc.x;
    j["y"] = loc.y;

    if (loc.port >= 0)
        j["port"] = loc.port;

    if (loc.didx >= 0)
        j["didx"] = loc.didx;
}

inline void from_json(const json& j, Location& loc)
{
    j["die_id"].get_to(loc.die_id);
    j["x"].get_to(loc.x);
    j["y"].get_to(loc.y);

    if (j.contains("port"))
        j["port"].get_to(loc.port);

    if (j.contains("didx"))
        j["didx"].get_to(loc.didx);
}

} // namespace ampere::lynx

template<>
struct fmt::formatter<ampere::lynx::Location> : fmt::ostream_formatter
{};

template<>
struct std::hash<ampere::lynx::Location>
{
    size_t operator()(const ampere::lynx::Location& loc) const
    {
        uint64_t k = ((int64_t{loc.didx} << 32) | (int64_t{loc.port} << 24) | (uint64_t{loc.y} << 16)
                      | (uint64_t{loc.x} << 8) | (uint64_t{loc.die_id}));
        return std::hash<uint64_t>{}(k);
    }
};
