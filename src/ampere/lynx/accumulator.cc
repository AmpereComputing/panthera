/*
 * Copyright (c) 2024-2025 Ampere Computing. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "accumulator.h"
#include <utility>

namespace ampere::lynx {

void Accumulator::accumulate_traffic(Channel channel, const Path& path, float traffic, Opcode opcode)
{
    auto to = path.begin();
    auto from = to++;

    for (; to != path.end(); from = to++)
        accumulated_traffic[std::to_underlying(channel)][{*from, *to}][opcode] += traffic;
}

std::unordered_map<Opcode, float> Accumulator::traffic_on_link(Channel channel, Link link) const
{
    return accumulated_traffic[std::to_underlying(channel)].at(link);
}

void Accumulator::dump_accumulated_traffic(std::ostream& os) const
{
    for (size_t ch = 0; ch < util::enum_size<Channel>(); ++ch) {
        fmt::print(os, "{} Channel:\n", static_cast<Channel>(ch));

        for (const auto& [link, traffic_per_opcode] : accumulated_traffic[ch]) {
            fmt::print(os, "    {} -> {}  ", *link.first, *link.second);
            for (const auto& [opcode, traffic] : traffic_per_opcode) {
                fmt::print(os, " {}:{:.2f}", opcode, traffic);
            }
            fmt::print(os, "\n");
        }
    }
}

} // namespace ampere::lynx
