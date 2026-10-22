//
// Copyright (c) 2018-2019 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//
#pragma once

#include "ampere/sc/fifo/queue.h"

namespace ampere::sc {

template<typename T>
class QueueBW : public Queue<T>
{
public:
    QueueBW(const char* name,
            Clock& clock,
            CycleDelta latency,
            CycleDelta return_latency,
            size_t capacity,
            size_t rd_bandwidth,
            size_t wr_bandwidth,
            bool lossy = false);

    bool can_take() const override;
    T take() override;

    bool can_put() const override;
    void put(T x) override;

protected:
    size_t rd_bandwidth;
    size_t wr_bandwidth;

    Cycle last_rd_cycle;
    Cycle last_wr_cycle;

    unsigned int rd_count;
    unsigned int wr_count;

    void update_write_cycle();
    void update_read_cycle();
    void cant_put_error();
    void cant_take_error();
};

template<typename T>
QueueBW<T>::QueueBW(const char* name,
                    Clock& clock,
                    CycleDelta latency,
                    CycleDelta return_latency,
                    size_t capacity,
                    size_t rd_bandwidth,
                    size_t wr_bandwidth,
                    bool lossy)
    : Queue<T>(name, clock, latency, return_latency, capacity, return_latency * wr_bandwidth + capacity, lossy),
      rd_bandwidth(rd_bandwidth),
      wr_bandwidth(wr_bandwidth),
      last_rd_cycle(0),
      last_wr_cycle(0),
      rd_count(0),
      wr_count(0)
{
}

template<typename T>
bool QueueBW<T>::can_put() const
{
    const_cast<QueueBW<T>*>(this)->credits.collect();
    return (this->credits.available() || (this->lossy && can_take()))
           && (last_wr_cycle != this->clock.get_cycle() || wr_count < wr_bandwidth);
}

template<typename T>
void QueueBW<T>::put(T x)
{
    if (!can_put())
        cant_put_error();

    update_write_cycle();
    this->put_internal(std::move(x));
}

template<typename T>
bool QueueBW<T>::can_take() const
{
    return Queue<T>::can_take() && (last_rd_cycle != this->clock.get_cycle() || rd_count < rd_bandwidth);
}

template<typename T>
T QueueBW<T>::take()
{
    if (!can_take())
        cant_take_error();

    update_read_cycle();
    return this->take_internal();
}

template<typename T>
void QueueBW<T>::update_write_cycle()
{
    if (last_wr_cycle == this->clock.get_cycle()) {
        ++wr_count;
    } else {
        last_wr_cycle = this->clock.get_cycle();
        wr_count = 1;
    }
}

template<typename T>
void QueueBW<T>::update_read_cycle()
{
    if (last_rd_cycle == this->clock.get_cycle()) {
        ++rd_count;
    } else {
        last_rd_cycle = this->clock.get_cycle();
        rd_count = 1;
    }
}

template<typename T>
void QueueBW<T>::cant_put_error()
{
    if (last_wr_cycle == this->clock.get_cycle() && wr_count >= wr_bandwidth)
        throw std::runtime_error{std::string("write bandwidth exceeded: ") + this->name()};
    else
        throw std::runtime_error{std::string("cannot put to full queue: ") + this->name()};
}

template<typename T>
void QueueBW<T>::cant_take_error()
{
    if (last_rd_cycle == this->clock.get_cycle() && rd_count >= rd_bandwidth)
        throw std::runtime_error{std::string("read bandwidth exceeded: ") + this->name()};
    else
        throw std::runtime_error{std::string("cannot take from empty queue: ") + this->name()};
}

} // namespace ampere::sc

/*
 * vim: sw=4:ts=4:et:si:cindent:cino=\:0,(0,g0,t0:
 */
