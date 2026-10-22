/*
 * Copyright (c) 2024 Ampere Computing. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#pragma once

#include <stdexcept>
#include <type_traits>

#include "accumulator.h"
#include "component.h"
#include "error.h"
#include "lynx.enum.h"
#include "model.h"
#include "protocols.h"
#include "routing.h"
#include "rules.h"

namespace ampere::lynx {

class Traffic
{
public:
    Traffic(const ModelBase& model, const Rules& rules, Accumulator& accumulator, float traffic)
        : model{model}, rules{rules}, accumulator{accumulator}, traffic{traffic}
    {
    }

    Traffic(const Traffic&) = default;

    template<typename TgtType, typename ReceiveAction>
    void send(Opcode opcode, const Component* src, TgtType tgt, ReceiveAction receive)
    {
        auto channel = rules.channel_map(opcode);

        if constexpr (std::is_same_v<TgtType, ComponentType>) {
            auto target_paths = paths_by_target(model, rules, src, tgt, channel);

            if (target_paths.empty())
                throw NoRoute{"no targets for {} on channel {}", *src, channel};

            float traffic_per_target = traffic / target_paths.size();

            for (const auto& [tgt, paths] : target_paths)
                distribute_traffic(opcode, channel, src, tgt, paths, traffic_per_target, receive);
        } else {
            auto paths = paths_to_target(model, rules, src, tgt, channel);
            distribute_traffic(opcode, channel, src, tgt, paths, traffic, receive);
        }
    }

    template<typename TgtType>
    void send(Opcode opcode, const Component* src, TgtType tgt)
    {
        send(opcode, src, tgt, nullptr);
    }

    bool dmt_allowed(const Component* src, const Component* tgt) const { return rules.dmt_allowed(src, tgt); }

private:
    const ModelBase& model;
    const Rules& rules;
    Accumulator& accumulator;

    float traffic;

    template<typename ReceiveAction>
    void distribute_traffic(Opcode opcode,
                            Channel channel,
                            const Component* src,
                            const Component* tgt,
                            const PathList& paths,
                            float traffic_per_target,
                            ReceiveAction receive)
    {
        if (paths.empty())
            throw NoRoute{"no paths from {src} -> {tgt} on channel {}", *src, *tgt, channel};

        float traffic_per_path = traffic_per_target / paths.size();

        for (const auto& path : paths) {
            accumulator.accumulate_traffic(channel, path, traffic_per_path, opcode);

            if constexpr (!std::is_null_pointer_v<ReceiveAction>)
                receive(Traffic{model, rules, accumulator, traffic_per_path}, tgt);
        }
    }
};

} // namespace ampere::lynx
