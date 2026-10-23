//
// Copyright (c) 2023 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//

#pragma once

#include <cstdint>

namespace ampere::sc {

class SaturatingCounter
{
public:
    explicit SaturatingCounter(int64_t max) : SaturatingCounter{0, max, 0} {}
    explicit SaturatingCounter(int64_t min, int64_t max) : SaturatingCounter{min, max, min} {}

    SaturatingCounter(int64_t min, int64_t max, int64_t initial_count) : min{min}, max{max}, counter{0}
    {
        set_value(initial_count);
    }

    int64_t get_value() const { return counter; }
    int64_t get_bit(int64_t idx) const { return (counter >> idx) & 1ul; }

    void set_value(int64_t val)
    {
        if (val > max)
            counter = max;
        else if (val < min)
            counter = min;
        else
            counter = val;
    }

    void set_to_max() { counter = max; }

    void set_to_min() { counter = min; }

    void set_bit(int64_t idx, int64_t value)
    {
        if (value & 1ul)
            counter |= 1ul << idx;
        else
            counter &= ~(1ul << idx);

        if (counter > max)
            counter = max;

        if (counter < min)
            counter = min;
    }

    void operator++(int)
    {
        if (counter < max)
            counter++;
    }

    void operator--(int)
    {
        if (counter > min)
            counter--;
    }

    void operator++()
    {
        if (counter < max)
            counter++;
    }

    void operator--()
    {
        if (counter > min)
            counter--;
    }

    void operator+=(int64_t n)
    {
        if (n > max - counter)
            counter = max;
        else if (n < min - counter)
            counter = min;
        else
            counter += n;
    }

    void operator-=(int64_t n) { (*this) += -n; }

    bool operator<(const SaturatingCounter& other) const { return get_value() < other.get_value(); }
    bool operator>(const SaturatingCounter& other) const { return get_value() > other.get_value(); }
    bool operator==(const SaturatingCounter& other) const { return get_value() == other.get_value(); }
    bool operator!=(const SaturatingCounter& other) const { return get_value() != other.get_value(); }

private:
    int64_t min;
    int64_t max;
    int64_t counter;
};

} // namespace ampere::sc
