//
// Copyright (c) 2019-2022 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//
#include "counter.h"
#include "bounds.h"

namespace ampere::evp {

Counter::Counter(const Counter& other)
    : event_definition_id(other.event_definition_id),
      data_definition_id(other.data_definition_id),
      count(other.count),
      trip(other.trip)
{
    if (other.factored_counts)
        factored_counts = std::make_unique<FactoredCounts>(*other.factored_counts);
}

void Counter::add_factor(int id, FactorBounds* bounds)
{
    if (!factored_counts)
        factored_counts = std::make_unique<FactoredCounts>();

    factored_counts->add_factor(id, bounds);
}

void Counter::accumulate_data(const event_stream_proto::Event& event)
{
    for (int i = 0; i < event.values_size(); i++) {
        const event_stream_proto::Value& value = event.values(i);
        if (value.definition_id() == data_definition_id) {
            if (value.values_case() == event_stream_proto::Value::kIntValue)
                count += value.int_value();
            else if (value.values_case() == event_stream_proto::Value::kUintValue)
                count += value.uint_value();
            else
                throw std::runtime_error{"data value is not integral"};
        }
    }
}

bool Counter::operator==(const Counter& other) const
{
    return event_definition_id == other.event_definition_id && data_definition_id == other.data_definition_id
           && has_action() == other.has_action() && get_trip() == other.get_trip() && factored() == other.factored()
           && (!factored() || *factored_counts == *other.factored_counts);
}

} // namespace ampere::evp
