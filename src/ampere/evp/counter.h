//
// Copyright (c) 2019-2022 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//
#pragma once

#include <memory>
#include <functional>
#include <optional>

#include "ampere/event_stream/event_stream.pb.h"

#include "ampere/evp/bounds.h"
#include "factor.h"

namespace ampere::evp {

class Counter;

using Action = std::function<void(Counter*, const event_stream_proto::Event& event)>;

class Counter
{
public:
    Counter() = default;
    explicit Counter(int event_definition_id, Action action = nullptr, std::optional<int64_t> trip = {})
        : event_definition_id{event_definition_id}, trip{trip.value_or(-1)}, action{action}
    {
    }
    Counter(int event_definition_id, int data_definition_id)
        : event_definition_id{event_definition_id}, data_definition_id{data_definition_id}
    {
    }
    Counter(const Counter& other);
    Counter(Counter&&) = default;

    void add_factor(int id, FactorBounds* bounds);

    int64_t get_count() const { return count; }
    void set_count(int64_t val) { count = val; }

    int64_t get_trip() const { return trip; }
    void set_trip(int64_t val) { trip = val; }

    bool has_action() const { return action != nullptr; }

    bool factored() const { return static_cast<bool>(factored_counts); }
    auto factors() const { return factored_counts->factors(); }
    const FactoredCountTable* get_factored_counts() const
    {
        return factored_counts ? &factored_counts->get_factored_counts() : nullptr;
    }

    bool matches(const event_stream_proto::Event& event) const { return event_definition_id == event.definition_id(); }

    void increment(const event_stream_proto::Event& event)
    {
        if (factored_counts)
            factored_counts->increment(event);

        if (data_definition_id)
            accumulate_data(event);
        else
            ++count;

        if (count == trip && action)
            action(this, event);
    }

    bool operator==(const Counter& other) const;
    bool operator!=(const Counter& other) const { return !operator==(other); }

    int event_definition_id{0};
    int data_definition_id{0};

private:
    int64_t count{0};
    int64_t trip{-1};

    Action action;

    std::unique_ptr<FactoredCounts> factored_counts;

    void accumulate_data(const event_stream_proto::Event& event);
};

} // namespace ampere::evp
