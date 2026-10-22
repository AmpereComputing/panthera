//
// Copyright (c) 2018-2019 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//

#pragma once

#include <cstddef>
#include <type_traits>

#include <boost/circular_buffer.hpp>

#include "ampere/sc/clock.h"
#include "ampere/sc/fifo/fifo.h"
#include "ampere/sc/fifo/credit.h"
#include "ampere/sc/event.h"

namespace ampere::sc {

template<typename T>
class Queue : public Fifo<T>, public Runnable
{
public:
    Queue(const char* name,
          Clock& clock,
          CycleDelta latency,
          CycleDelta return_latency,
          size_t capacity,
          bool lossy = false)
        : Queue(name, clock, latency, return_latency, capacity, return_latency + capacity, lossy)
    {
    }

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

    bool run() override;
    bool is_channel() override { return true; }

protected:
    Queue(const char* name,
          Clock& clock,
          CycleDelta latency,
          CycleDelta return_latency,
          size_t capacity,
          size_t queue_size,
          bool lossy);

    template<typename U>
    struct Entry
    {
        Cycle ready_cycle;
        U data;
    };

    CycleDelta latency;
    size_t capacity;
    QueueCredits<> credits;
    bool lossy{false};

    boost::circular_buffer<Entry<T>> queue;

    void put_internal(T x);
    T take_internal();
    void clear_internal();
};

template<typename T>
Queue<T>::Queue(const char* name,
                Clock& clock,
                CycleDelta latency,
                CycleDelta return_latency,
                size_t capacity,
                size_t queue_size,
                bool lossy)
    : Fifo<T>(name, clock),
      latency(latency),
      capacity(capacity),
      credits(clock, capacity, return_latency),
      lossy(lossy),
      queue(queue_size)
{
    clock.add_runnable(this);
}

template<typename T>
bool Queue<T>::can_put() const
{
    const_cast<Queue<T>*>(this)->credits.collect();
    return (credits.available() || (lossy && can_take()))
           && (queue.empty() || queue.back().ready_cycle < this->clock.get_cycle() + latency);
}

template<typename T>
void Queue<T>::put(T x)
{
    if (!can_put())
        throw std::runtime_error{std::string("cannot put to full queue: ") + this->name()};

    put_internal(std::move(x));
}

template<typename T>
bool Queue<T>::can_take() const
{
    return !queue.empty() && queue.front().ready_cycle <= this->clock.get_cycle();
}

template<typename T>
T Queue<T>::take()
{
    if (!can_take())
        throw std::runtime_error{std::string("cannot take from empty queue: ") + this->name()};

    return take_internal();
}

template<typename T>
const T& Queue<T>::peek(uint64_t index) const
{
    if (!can_take())
        throw std::runtime_error{std::string("cannot peek from empty queue: ") + this->name()};

    AMPERE_ASSERT(index < num_ready(), "Tried to peek at a nonexistent entry");

    return queue[index].data;
}

template<typename T>
uint64_t Queue<T>::num_ready() const
{
    uint64_t ready{0};
    for (auto& entry : queue) {
        if (entry.ready_cycle <= this->clock.get_cycle())
            ++ready;
        else
            return ready;
    }

    return ready;
}

template<typename T>
const T& Queue<T>::get(uint64_t index) const
{
    if (queue.empty())
        throw std::runtime_error{std::string("cannot get from empty queue: ") + this->name()};

    AMPERE_ASSERT(index < queue.size(), "Tried to get a nonexistent entry");

    return queue[index].data;
}

template<typename T>
uint64_t Queue<T>::size() const
{
    return queue.size();
}

template<typename T>
void Queue<T>::put_internal(T x)
{
    if (queue.empty()) {
        this->clock.schedule(this, 0);
    } else if (lossy && credits.full()) {
        if (!can_take())
            throw std::runtime_error{std::string("cannot overwrite queue: ") + this->name()};

        take_internal();
        const_cast<Queue<T>*>(this)->credits.collect();
        if (credits.full())
            throw std::runtime_error{std::string("cannot put to full queue: ") + this->name()};
    }

    queue.push_back({this->clock.get_cycle() + latency, std::move(x)});
    credits.consume();
}

template<typename T>
T Queue<T>::take_internal()
{
    T data = std::move(queue.front().data);
    queue.pop_front();

    credits.collect();
    credits.return_credit();

    return data;
}

template<typename T>
void Queue<T>::clear()
{
    this->clear_internal();
}

template<typename T>
void Queue<T>::clear_if(const std::function<bool(const T&)>& pred)
{
    auto just_added_cutoff = this->clock.get_cycle() + this->latency;
    queue.erase(std::remove_if(queue.begin(),
                               queue.end(),
                               [&](auto& e) {
                                   bool remove = e.ready_cycle < just_added_cutoff && pred(e.data);
                                   if (remove) {
                                       invoke_fifo_clear(e.data);
                                       credits.return_immediately();
                                   }
                                   return remove;
                               }),
                queue.end());

    credits.clear();
}

template<typename T>
void Queue<T>::clear_internal()
{
    auto just_added_cutoff = this->clock.get_cycle() + this->latency;
    while (!queue.empty()) {
        auto& e = queue.front();
        if (just_added_cutoff > e.ready_cycle) {
            invoke_fifo_clear(e.data);
            queue.pop_front();
            credits.return_immediately();
        } else {
            return;
        }
    }

    credits.clear();
}

template<typename T>
bool Queue<T>::run()
{
    if (!queue.empty()) {
        CycleDelta delta = queue.front().ready_cycle - this->clock.get_cycle();
        if (delta <= 0)
            delta = 1;
        this->schedule_targets(delta);
        return true;
    }

    return false;
}

} // namespace ampere::sc

/*
 * vim: sw=4:ts=4:et:si:cindent:cino=\:0,(0,g0,t0:
 */
