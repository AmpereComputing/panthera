//
// Copyright (c) 2019 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//

#pragma once

#include <cstddef>
#include <boost/circular_buffer.hpp>
#include <cstdint>
#include <functional>

#include "ampere/sc/ancestor.h"
#include "ampere/sc/clock.h"
#include "ampere/sc/connection.h"
#include "ampere/sc/cycle.h"
#include "ampere/sc/fifo/fifo.h"
#include "ampere/sc/report.h"
#include "ampere/sc/runnable.h"

namespace ampere::sc {

template<typename T>
class ZDelay : public Fifo<T>
{
public:
    ZDelay(const char* name, Clock& clock, size_t capacity = 1, bool zero_delay_self_connection_allowed = false);

    bool can_take() const override;
    T take() override;

    void clear() override {}
    void clear_if(const std::function<bool(const T&)>& pred [[maybe_unused]]) override {}
    const T& peek(uint64_t index = 0) const override;
    uint64_t num_ready() const override;

    const T& unsafe_peek(uint64_t index = 0) const override { return get(index); }
    uint64_t unsafe_size() const override { return size(); }

    bool can_put() const override;
    void put(T) override;

    const T& get(uint64_t index = 0) const override { return peek(index); }
    uint64_t size() const override;

    void add_target(Runnable* to, Runnable* from) override;

protected:
    boost::circular_buffer<T> entries;
    Cycle valid_cycle;
    bool zero_delay_self_connection_allowed = false;

    Runnable* m_from{nullptr};
    Runnable* m_to{nullptr};

    void put_internal(T x);
    void check_for_stale_data() const;

    void register_putter_port(sc_core::sc_port_base& port, const char* port_type [[maybe_unused]]) override
    {
        m_from = get_nearest_ancestor<Runnable>(&port);

        if (m_to)
            this->add_target(m_to, m_from);
    }

    void register_taker_port(sc_core::sc_port_base& port, const char* port_type [[maybe_unused]]) override
    {
        m_to = get_nearest_ancestor<Runnable>(&port);

        if (m_from)
            this->add_target(m_to, m_from);
    }
};

template<typename T>
ZDelay<T>::ZDelay(const char* name, Clock& clock, size_t capacity, bool zero_delay_self_connection_allowed)
    : Fifo<T>(name, clock),
      entries{capacity},
      valid_cycle{0},
      zero_delay_self_connection_allowed(zero_delay_self_connection_allowed)
{
}

template<typename T>
bool ZDelay<T>::can_put() const
{
    if (entries.size() < entries.capacity())
        return true;

    check_for_stale_data();

    return false;
}

template<typename T>
void ZDelay<T>::put_internal(T x)
{
    valid_cycle = this->clock.get_cycle();
    entries.push_back(std::move(x));
    this->schedule_targets(0);
}

template<typename T>
void ZDelay<T>::put(T x)
{
    AMPERE_ASSERT(can_put(), "cannot put to full zdelay {}", this->name());
    put_internal(std::move(x));
}

template<typename T>
bool ZDelay<T>::can_take() const
{
    if (entries.empty())
        return false;

    check_for_stale_data();

    return true;
}

template<typename T>
T ZDelay<T>::take()
{
    AMPERE_ASSERT(can_take(), "cannot take from empty zdelay {}", this->name());

    auto x = std::move(entries.front());
    entries.pop_front();
    return x;
}

template<typename T>
const T& ZDelay<T>::peek(uint64_t index) const
{
    AMPERE_ASSERT(!entries.empty(), "cannot peek from empty zdelay {}", this->name());
    AMPERE_ASSERT(index < entries.size(), "Tried to peek at a nonexistent entry");

    return entries[index];
}

template<typename T>
uint64_t ZDelay<T>::num_ready() const
{
    return can_take() ? entries.size() : 0;
}

template<typename T>
uint64_t ZDelay<T>::size() const
{
    return entries.size();
}

template<typename T>
void ZDelay<T>::check_for_stale_data() const
{
    AMPERE_ASSERT(valid_cycle == this->clock.get_cycle(), "stale data in zdelay fifo {}", this->name());
}

template<typename T>
void ZDelay<T>::add_target(Runnable* to, Runnable* from)
{
    if (!zero_delay_self_connection_allowed || to != from) {
        Connection::add_target(to, from);
        this->clock.add_zdelay_connection(from, to);
    }
}

} // namespace ampere::sc
