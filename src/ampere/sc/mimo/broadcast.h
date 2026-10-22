//
// Copyright (c) 2019 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//

#ifndef PANTHERA_BROADCAST_H
#define PANTHERA_BROADCAST_H

#include <boost/circular_buffer.hpp>

#include "ampere/sc/clock.h"
#include "ampere/sc/mimo/mimo.h"

namespace ampere::sc {

template<typename T>
class Broadcast : public Mimo<T>
{
public:
    Broadcast(const char* name, Clock& clock, CycleDelta latency, bool zero_cycle_self_connection_allowed = false)
        : Broadcast(name, clock, latency, latency + 1, zero_cycle_self_connection_allowed)
    {
    }

    bool can_read() const override;
    T read() override;

    bool can_put() const override;
    void put(T x) override;

    const T& get(uint64_t index = 0) const override { return queue.at(index).data; }
    uint64_t size() const override { return queue.size(); }

    void add_target(Runnable* to, Runnable* from) override;

protected:
    Broadcast(
        const char* name, Clock& clock, CycleDelta latency, size_t queue_size, bool zero_cycle_self_connection_allowed);

    struct Entry
    {
        Cycle ready_cycle;
        T data;
    };

    CycleDelta latency;
    boost::circular_buffer<Entry> queue;
    bool zero_cycle_self_connection_allowed = false;

    void check_for_stale_data();
    T read_internal();
};

template<typename T>
Broadcast<T>::Broadcast(
    const char* name, Clock& clock, CycleDelta latency, size_t queue_size, bool zero_cycle_self_connection_allowed)
    : Mimo<T>(name, clock),
      latency(latency),
      queue(queue_size),
      zero_cycle_self_connection_allowed(zero_cycle_self_connection_allowed)
{
}

template<typename T>
bool Broadcast<T>::can_read() const
{
    const_cast<Broadcast<T>*>(this)->check_for_stale_data();

    if (queue.empty())
        return false;

    return queue.front().ready_cycle == this->clock.get_cycle();
}

template<typename T>
T Broadcast<T>::read()
{
    if (!can_read())
        throw std::runtime_error{std::string{"cannot read from empty fifo "} + this->name()};

    return read_internal();
}

template<typename T>
T Broadcast<T>::read_internal()
{
    return queue.front().data;
}

template<typename T>
bool Broadcast<T>::can_put() const
{
    const_cast<Broadcast<T>*>(this)->check_for_stale_data();

    if (queue.empty())
        return true;

    return queue.back().ready_cycle < (this->clock.get_cycle() + latency);
}

template<typename T>
void Broadcast<T>::put(T x)
{
    if (!can_put())
        throw std::runtime_error{std::string{"cannot put to a full broadcast channel "} + this->name()};

    queue.push_back({this->clock.get_cycle() + latency, x});
    this->schedule_targets(latency);
}

template<typename T>
void Broadcast<T>::check_for_stale_data()
{
    while (!queue.empty() && queue.front().ready_cycle < this->clock.get_cycle())
        queue.pop_front();
}

template<typename T>
void Broadcast<T>::add_target(Runnable* to, Runnable* from)
{
    if (latency != 0 || !zero_cycle_self_connection_allowed || to != from)
        Connection::add_target(to, from);
    if (latency == 0 && (!zero_cycle_self_connection_allowed || to != from))
        this->clock.add_zdelay_connection(from, to);
}

} // namespace ampere::sc

#endif // PANTHERA_BROADCAST_H
