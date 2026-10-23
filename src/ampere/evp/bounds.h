//
// Copyright (c) 2019-2022 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//
#pragma once

#include <memory>
#include <cmath>
#include <string>
#include <cstdint>
#include <type_traits>
#include <optional>
#include <stdexcept>
#include "ampere/util/bits.h"

namespace ampere::evp {

struct FactorBounds
{
    enum Sequence {
        LINEAR,
        EXPONENTIAL,
    };

    std::optional<int64_t> minimum;
    std::optional<int64_t> maximum;
    unsigned long granularity = 1;
    Sequence sequence = LINEAR;

    FactorBounds(std::optional<int64_t> minimum, std::optional<int64_t> maximum) : minimum(minimum), maximum(maximum) {}
    FactorBounds(std::optional<int64_t> minimum, std::optional<int64_t> maximum, unsigned long granularity)
        : minimum(minimum), maximum(maximum), granularity(granularity)
    {
    }
    FactorBounds(std::optional<int64_t> minimum,
                 std::optional<int64_t> maximum,
                 unsigned long granularity,
                 const std::string& sequence)
        : minimum(minimum), maximum(maximum), granularity(granularity)
    {
        if (sequence == "exp" || sequence == "exponential")
            this->sequence = EXPONENTIAL;
    }

    // spec in form of "[min:max]" or "[min:max:granularity]" or "[min:max:granularity:sequence]"
    // where values can be left out as long as ':' remains (e.g. [::granularity])
    static FactorBounds from_spec(const std::string& spec)
    {
        std::optional<int64_t> minimum;
        std::optional<int64_t> maximum;
        std::string sequence;
        auto min_sep = spec.find(':');
        if (min_sep == std::string::npos)
            throw std::runtime_error{
                "factor bounds missing first ':', should be of form '[min:max:granularity:sequence]'"};
        else if (min_sep > 1)
            minimum = std::stoll(spec.substr(1, min_sep - 1));

        auto max_sep = spec.find(':', min_sep + 1);
        auto end_sep = spec.find(']', min_sep + 1);
        auto granularity_sep = end_sep;
        if (max_sep != std::string::npos) {
            granularity_sep = spec.find(':', max_sep + 1);
            if (granularity_sep != std::string::npos)
                end_sep = spec.find(']', granularity_sep + 1);
            else
                granularity_sep = end_sep;
        }
        if (end_sep == std::string::npos)
            throw std::runtime_error{
                "factor bounds missing end ']', should be of form '[min:max:granularity:sequence]'"};

        max_sep = max_sep == std::string::npos ? end_sep : max_sep;
        if (max_sep > (min_sep + 1))
            maximum = std::stoll(spec.substr(min_sep + 1, max_sep - min_sep - 1));

        if (end_sep <= max_sep + 1) {
            if (!minimum && !maximum)
                throw std::runtime_error{"factor bounds empty, should be of form '[min:max:granularity:sequence]'"};
            return {minimum, maximum};
        }

        unsigned long granularity = 1;
        if (granularity_sep > max_sep + 1)
            granularity = std::stoul(spec.substr(max_sep + 1, granularity_sep - max_sep - 1));
        if (granularity_sep == end_sep)
            return {minimum, maximum, granularity};

        return {minimum, maximum, granularity, spec.substr(granularity_sep + 1, end_sep - granularity_sep - 1)};
    }

    static std::pair<std::string, std::unique_ptr<FactorBounds>> from_event_spec(const std::string& event_spec)
    {
        auto bounds = event_spec.find_first_of('[');
        if (bounds == std::string::npos)
            return {event_spec, nullptr};
        return {event_spec.substr(0, bounds),
                std::make_unique<FactorBounds>(FactorBounds::from_spec(event_spec.substr(bounds)))};
    }

    template<typename T>
    T adjust(T value) const
    {
        auto granularity = static_cast<T>(this->granularity);
        switch (sequence) {
        case LINEAR:
            value = (value / granularity) * granularity;
            break;
        case EXPONENTIAL:
            if (granularity == 2) {
                if constexpr (std::is_unsigned_v<T>) {
                    value = static_cast<T>(1) << util::log2(value);
                } else {
                    value = std::pow(granularity, std::floor(std::log2(value)));
                }
            } else if (granularity == 10) {
                value = std::pow(granularity, std::floor(std::log10(value)));
            } else {
                value = std::pow(granularity, std::floor(std::log(value) / std::log(granularity)));
            }
            break;
        }

        if (minimum)
            value = std::max(static_cast<T>(*minimum), value);
        if (maximum)
            value = std::min(static_cast<T>(*maximum), value);

        return value;
    }
};

} // namespace ampere::evp
