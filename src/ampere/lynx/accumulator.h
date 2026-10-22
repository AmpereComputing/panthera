/*
 * Copyright (c) 2024-2025 Ampere Computing. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#pragma once

#include <array>
#include <ostream>
#include <unordered_map>
#include <utility>

#include "lynx.enum.h"
#include "routing.h"

template<>
struct std::hash<ampere::lynx::Link>
{
    size_t operator()(const ampere::lynx::Link& link) const noexcept
    {
        return (reinterpret_cast<std::uint64_t>(link.first) >> 2) ^ (reinterpret_cast<std::uint64_t>(link.second) >> 2);
    }
};

namespace ampere::lynx {

class Accumulator
{
public:
    virtual ~Accumulator() = default;
    virtual void accumulate_traffic(Channel channel, const Path& path, float traffic_per_path, Opcode opcode);
    std::unordered_map<Opcode, float> traffic_on_link(Channel channel, Link link) const;
    const std::unordered_map<Link, std::unordered_map<Opcode, float>>& traffic_on_channel(Channel channel) const
    {
        return accumulated_traffic.at(std::to_underlying(channel));
    }

    void dump_accumulated_traffic(std::ostream& os = std::cout) const;

private:
    std::array<std::unordered_map<Link, std::unordered_map<Opcode, float>>, util::enum_size<Channel>()>
        accumulated_traffic;
};

} // namespace ampere::lynx
