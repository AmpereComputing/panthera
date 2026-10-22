//
// Copyright (c) 2019 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//
//
// Copyright (c) 2018, Ampere Computing
//
#pragma once

#include "ampere/sc/fifo/delay.h"

namespace ampere::sc {

template<typename T>
class DelayBW : public Delay<T>
{
public:
    DelayBW(const char* name, Clock& clock, CycleDelta latency, size_t bandwidth);

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

protected:
    size_t bandwidth;

    Cycle last_rd_cycle;
    Cycle last_wr_cycle;

    unsigned int rd_count;
    unsigned int wr_count;

    void update_read_cycle();
    void update_write_cycle();
    void cant_take_error();
};

template<typename T>
DelayBW<T>::DelayBW(const char* name, Clock& clock, CycleDelta latency, size_t bandwidth)
    : Delay<T>(name, clock, latency, bandwidth * latency * 2),
      bandwidth(bandwidth),
      last_rd_cycle(0),
      last_wr_cycle(0),
      rd_count(0),
      wr_count(0)
{
}

template<typename T>
bool DelayBW<T>::can_take() const
{
    return Delay<T>::can_take() && (last_rd_cycle != this->clock.get_cycle() || rd_count < bandwidth);
}

template<typename T>
T DelayBW<T>::take()
{
    if (!can_take())
        cant_take_error();

    update_read_cycle();
    return Delay<T>::take_internal();
}

template<typename T>
const T& DelayBW<T>::peek(uint64_t index) const
{
    if (!can_take())
        throw std::runtime_error{std::string{"cannot peek from empty fifo "} + this->name()};

    AMPERE_ASSERT(index < num_ready(), "Tried to peek at a nonexistent entry");
    return this->queue[index].data;
}

template<typename T>
uint64_t DelayBW<T>::num_ready() const
{
    uint64_t ready{0};
    for (auto& entry : this->queue) {
        if (entry.ready_cycle <= this->clock.get_cycle())
            ++ready;
        else
            return ready;
    }

    return ready;
}

template<typename T>
const T& DelayBW<T>::get(uint64_t index) const
{
    AMPERE_ASSERT(index < this->queue.size(), "Tried to get a nonexistent entry");
    return this->queue[index].data;
}

template<typename T>
uint64_t DelayBW<T>::size() const
{
    return this->queue.size();
}

template<typename T>
bool DelayBW<T>::can_put() const
{
    if (this->queue.empty())
        return true;

    this->check_for_stale_data();
    return last_wr_cycle != this->clock.get_cycle() || wr_count < bandwidth;
}

template<typename T>
void DelayBW<T>::put(T x)
{
    if (!can_put())
        throw std::runtime_error{std::string{"write bandwidth exceeded in fifo "} + this->name()};

    update_write_cycle();

    this->queue.push_back({this->clock.get_cycle() + this->latency, std::move(x)});
    this->schedule_targets(this->latency);
}

template<typename T>
void DelayBW<T>::cant_take_error()
{
    if (!Delay<T>::can_take())
        throw std::runtime_error{std::string{"cannot take from empty fifo "} + this->name()};
    else
        throw std::runtime_error{std::string{"read bandwidth exceeded in fifo "} + this->name()};
}

template<typename T>
void DelayBW<T>::update_read_cycle()
{
    if (this->clock.get_cycle() == last_rd_cycle) {
        ++rd_count;
    } else {
        last_rd_cycle = this->clock.get_cycle();
        rd_count = 1;
    }
}

template<typename T>
void DelayBW<T>::update_write_cycle()
{
    if (this->clock.get_cycle() == last_wr_cycle) {
        ++wr_count;
    } else {
        last_wr_cycle = this->clock.get_cycle();
        wr_count = 1;
    }
}

} // namespace ampere::sc

/*
 * vim: sw=4:ts=4:et:si:cindent:cino=\:0,(0,g0,t0:
 */
