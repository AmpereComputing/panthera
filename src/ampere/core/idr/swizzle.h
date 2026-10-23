//
// Copyright (c) 2018-2023 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//

#pragma once

#include "ampere/util/enum.h"
#include "ampere/core/idr/idr_load_balancing.h"
#include "ampere/core/idr/idr_defs.h"

#include <cstdint>
#include <functional>
#include <nlohmann/json_fwd.hpp>
#include <string>
#include <string_view>
#include <unordered_map>

using nlohmann::json;

make_ns_enum(ampere::core, SwizzleType,
    NONE,
    MIDDLE,
    FULL,
    ZEROTWO,
    ONETHREE
);

using sc::Param;

namespace ampere::core {

class Swizzle
{
    bool swizzle_history{false};

    struct Input
    {
        unsigned uop_count;
        uint64_t uop_memory;
        bool load_balance_toggle;
        bool swizzle_history;

        bool operator==(const Input& rhs) const
        {
            return (uop_count == rhs.uop_count && uop_memory == rhs.uop_memory
                    && load_balance_toggle == rhs.load_balance_toggle && swizzle_history == rhs.swizzle_history);
        }

        friend void from_json(const json& j, Input& input)
        {
            input.uop_count = j.at(0).get<unsigned>();
            input.uop_memory = std::stoull(j.at(1).get<std::string>(), nullptr, 2);
            input.load_balance_toggle = j.at(2).get<bool>();
            input.swizzle_history = j.at(3).get<bool>();
        }
    };

    struct Output
    {
        enum SwizzleType type {
            SwizzleType::NONE
        };
        bool set_swizzle_history{false};
        bool swizzle_history{false};

        friend void from_json(const json& j, Output& output)
        {
            output.type = j.at(0).get<SwizzleType>();
            output.set_swizzle_history = j.at(1).get<bool>();
            output.swizzle_history = j.at(2).get<bool>();
        }
    };

    struct Hasher
    {
        std::size_t operator()(const Input& k) const
        {
            return ((std::hash<unsigned>()(k.uop_count << ((sizeof(bool) * 2) + sizeof(unsigned))))
                    | (std::hash<unsigned>()(k.uop_memory << (sizeof(bool) * 2)))
                    | (std::hash<unsigned>()(k.load_balance_toggle << sizeof(bool)))
                    | (std::hash<unsigned>()(k.swizzle_history)));
        }
    };

    class Table
    {
        mutable std::unordered_map<Input, Output, Hasher> table;

    public:
        Output get(const Input& input) const { return table[input]; }

        // Swizzle Input {uop cnt, mem uops lanes, load balance, swizzle history},  Swizzle Output {Type, set history}
        friend void from_json(const json& j, Table& table)
        {
            for (const auto& row : j) {
                table.table.emplace(row.at(0).get<Input>(), row.at(1).get<Output>());
            }
        }
    };

    static std::string_view swizzle_uops(DecodedUopGroup& uops, SwizzleType type);

    Param<bool> swizzle{"swizzle", "Swizzle uop groups depending on load/store history", false};
    Param<Table> swizzle_table{"swizzle_table", "Table for swizzles", {}};
    Param<bool> bcopy_swizzle_tweak{
        "bcopy_swizzle_tweak", "Minor tweak to swizzle logic to improve bcopy loop throughput", false};

public:
    explicit operator bool() const { return swizzle; }
    std::string swizzle_uops(DecodedUopGroup& uops,
                             const IDRLoadBalancing& load_balancing,
                             const SchedulerCredits& sch);
};

} // namespace ampere::core

/*
 * vim: sw=4:ts=4:et:si:cindent:cino=\:0,(0,g0,t0:
 */
