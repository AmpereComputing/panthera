//
// Copyright (c) 2019 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//
//
// Copyright (c) 2018, Ampere Computing
//
#pragma once

#include <boost/circular_buffer.hpp>

#include "ampere/sc/clock.h"
#include "ampere/sc/fifo/fifo.h"

namespace ampere::sc {

template<typename T>
class Delay : public Fifo<T>
{
public:
    Delay(const char* name, Clock& clock, CycleDelta latency) : Delay(name, clock, latency, latency * 2) {}

    bool can_take() const override;
    T take() override;

    const T& peek(uint64_t index = 0) const override;
    uint64_t num_ready() const override;

    const T& unsafe_peek(uint64_t index = 0) const override { return get(index); }
    uint64_t unsafe_size() const override { return size(); }

    bool can_put() const override;
    void put(T x) override;

    const T& get(uint64_t index = 0) const override;
    uint64_t size() const override;

    void clear() override;
    void clear_if(const std::function<bool(const T&)>& pred) override;

protected:
    Delay(const char* name, Clock& clock, CycleDelta latency, size_t queue_size);
    struct Entry
    {
        Cycle ready_cycle;
        T data;
    };

    CycleDelta latency;
    boost::circular_buffer<Entry> queue;

    void check_for_stale_data() const;
    T take_internal();
    void clear_internal();
};

template<typename T>
Delay<T>::Delay(const char* name, Clock& clock, CycleDelta latency, size_t queue_size)
    : Fifo<T>(name, clock), latency(latency), queue(queue_size)
{
}

template<typename T>
bool Delay<T>::can_take() const
{
    if (queue.empty())
        return false;

    check_for_stale_data();

    return queue.front().ready_cycle == this->clock.get_cycle();
}

template<typename T>
T Delay<T>::take()
{
    if (!can_take())
        throw std::runtime_error{std::string{"cannot take from empty fifo "} + this->name()};

    return take_internal();
}

template<typename T>
const T& Delay<T>::peek(uint64_t index) const
{
    if (!can_take())
        throw std::runtime_error{std::string{"cannot peek from empty fifo "} + this->name()};

    AMPERE_ASSERT(index < num_ready(), "Tried to peek at a nonexistent entry");

    return queue[index].data;
}

template<typename T>
uint64_t Delay<T>::num_ready() const
{
    // In the 'delay' case, there can only be 0 or 1 things ready.
    return can_take() ? 1 : 0;
}

template<typename T>
const T& Delay<T>::get(uint64_t index) const
{
    if (queue.empty())
        throw std::runtime_error{std::string{"cannot get from empty fifo "} + this->name()};

    AMPERE_ASSERT(index < queue.size(), "Tried to get a nonexistent entry");

    return queue[index].data;
}

template<typename T>
uint64_t Delay<T>::size() const
{
    return queue.size();
}

template<typename T>
T Delay<T>::take_internal()
{
    T data = std::move(queue.front().data);
    queue.pop_front();

    return data;
}

template<typename T>
bool Delay<T>::can_put() const
{
    if (queue.empty())
        return true;

    check_for_stale_data();

    return queue.back().ready_cycle < (this->clock.get_cycle() + latency);
}

template<typename T>
void Delay<T>::put(T x)
{
    if (!can_put())
        throw std::runtime_error{std::string{"cannot put to a full fifo "} + this->name()};

    queue.push_back({this->clock.get_cycle() + latency, std::move(x)});
    this->schedule_targets(latency);
}

template<typename T>
void Delay<T>::check_for_stale_data() const
{
    if (queue.front().ready_cycle < this->clock.get_cycle())
        throw std::runtime_error{std::string{"stale data in delay fifo "} + this->name()};
}

template<typename T>
void Delay<T>::clear()
{
    if (!queue.empty())
        clear_internal();
}

template<typename T>
void Delay<T>::clear_if(const std::function<bool(const T&)>& pred)
{
    if (queue.empty())
        return;

    check_for_stale_data();
    Cycle just_added_cutoff = this->clock.get_cycle() + this->latency;
    queue.erase(std::remove_if(queue.begin(),
                               queue.end(),
                               [&](auto& e) {
                                   bool remove = just_added_cutoff > e.ready_cycle && pred(e.data);
                                   if (remove)
                                       invoke_fifo_clear(e.data);
                                   return remove;
                               }),
                queue.end());
}

template<typename T>
void Delay<T>::clear_internal()
{
    check_for_stale_data();
    Cycle just_added_cutoff = this->clock.get_cycle() + this->latency;
    while (!this->queue.empty()) {
        auto e = this->queue.begin();
        if (just_added_cutoff > e->ready_cycle) {
            invoke_fifo_clear(e->data);
            this->queue.erase(e);
        } else {
            return;
        }
    }
}

} // namespace ampere::sc

/*
 * vim: sw=4:ts=4:et:si:cindent:cino=\:0,(0,g0,t0:
 */
