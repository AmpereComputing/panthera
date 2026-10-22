//
// Copyright (c) 2019 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//
#include "factor.h"
#include "bounds.h"

#include <algorithm>

namespace ampere::evp {

void FactoredCounts::add_factor(int definition_id, FactorBounds *bounds)
{
    if (bounds)
        factor_bounds.emplace(definition_id, *bounds);
    factor_position.emplace(definition_id, factor_position.size());
}

bool FactoredCounts::operator==(const FactoredCounts &other) const
{
    return std::ranges::equal(factors(), other.factors());
}

FactorKey FactoredCounts::to_factor_key(const event_stream_proto::Event &event)
{
    FactorKey key(factor_position.size());

    for (int i = 0; i < event.values_size(); i++) {
        const event_stream_proto::Value &value = event.values(i);
        if (auto pos = factor_position.find(value.definition_id()); pos != factor_position.end()) {
            auto key_value = to_factor_value(value);
            if (!factor_bounds.empty()) {
                if (auto b = factor_bounds.find(value.definition_id()); b != factor_bounds.end())
                    key_value.second = adjust_value(key_value, b->second);
            }
            key[pos->second] = std::move(key_value);
        }
    }

    return key;
}

FactorKey::value_type FactoredCounts::to_factor_value(const event_stream_proto::Value &value)
{
    if (value.values_case() == event_stream_proto::Value::kIntValue)
        return {-value.definition_id(), value.int_value()};
    else if (value.values_case() == event_stream_proto::Value::kUintValue)
        return {value.definition_id(), value.uint_value()};
    else
        throw std::runtime_error{"factor value is not integral"};
}

FactorKey::value_type::second_type FactoredCounts::adjust_value(const FactorKey::value_type &value,
                                                                const FactorBounds &bounds)
{
    if (value.first < 0)
        return bounds.adjust<int64_t>(static_cast<int64_t>(value.second));
    return bounds.adjust<uint64_t>(value.second);
}

} // namespace ampere::evp
