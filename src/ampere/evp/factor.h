//
// Copyright (c) 2019-2022 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//
#pragma once

#include <boost/functional/hash.hpp>
#include <cstdint>
#include <unordered_map>
#include <vector>
#include <ranges>

#include "ampere/event_stream/event_stream.pb.h"
#include "ampere/evp/bounds.h"

namespace ampere::evp {

using FactorKey = std::vector<std::pair<int, uint64_t>>;

inline size_t hash_value(const FactorKey& key)
{
    size_t seed = 0;

    for (const auto& factor : key) {
        boost::hash_combine(seed, factor.first);
        boost::hash_combine(seed, factor.second);
    }

    return seed;
}

using FactoredCountTable = std::unordered_map<FactorKey, int64_t, boost::hash<FactorKey>>;

class FactoredCounts
{
public:
    void add_factor(int definition_id, FactorBounds* bounds);

    void increment(const event_stream_proto::Event& event) { ++counts[to_factor_key(event)]; }

    auto factors() const { return std::views::keys(factor_position); }
    const FactoredCountTable& get_factored_counts() const { return counts; }

    bool operator==(const FactoredCounts& other) const;
    bool operator!=(const FactoredCounts& other) const { return !operator==(other); }

private:
    std::unordered_map<int, FactorBounds> factor_bounds;
    std::unordered_map<int, int> factor_position;
    FactoredCountTable counts;

    FactorKey to_factor_key(const event_stream_proto::Event& event);

    static FactorKey::value_type to_factor_value(const event_stream_proto::Value& value);
    static FactorKey::value_type::second_type adjust_value(const FactorKey::value_type& value,
                                                           const FactorBounds& bounds);
};

} // namespace ampere::evp
