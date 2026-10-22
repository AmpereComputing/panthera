//
// Copyright (c) 2025 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//

#pragma once

#include <boost/circular_buffer.hpp>
#include <functional>

#include "ampere/sc/cycle.h"
#include "ampere/sc/fifo/credit.h"
#include "ampere/sc/fifo/fifo.h"
#include "ampere/sc/fifo/zdelay.h"
#include "ampere/sc/report.h"

namespace ampere::sc {

template<typename T>
class ZQueue : public ZDelay<T>
{
    size_t bandwidth;
    QueueCredits<> credits;
    size_t wr_count{0};

public:
    ZQueue(const char* name,
           Clock& clock,
           CycleDelta return_latency,
           size_t capacity,
           size_t bandwidth,
           bool zero_delay_self_connection_allowed = false)
        : ZDelay<T>(name, clock, capacity + bandwidth * return_latency, zero_delay_self_connection_allowed),
          bandwidth(bandwidth),
          credits(clock, capacity, return_latency)
    {
    }

    bool can_put() const override;
    void put(T) override;

    bool can_take() const override;
    T take() override;

    void clear() override;
    void clear_if(const std::function<bool(const T&)>& pred [[maybe_unused]]) override;
};

template<typename T>
bool ZQueue<T>::can_put() const
{
    const_cast<ZQueue<T>*>(this)->credits.collect();
    return (this->valid_cycle != this->clock.get_cycle() || wr_count < bandwidth) && credits.available()
           && this->entries.size() < this->entries.capacity();
}

template<typename T>
void ZQueue<T>::put(T x)
{
    AMPERE_ASSERT(can_put(), "cannot put to full zqueue {}", this->name());

    if (this->valid_cycle != this->clock.get_cycle())
        wr_count = 0;
    ++wr_count;
    this->valid_cycle = this->clock.get_cycle();

    ZDelay<T>::put_internal(std::move(x));
}

template<typename T>
bool ZQueue<T>::can_take() const
{
    return !this->entries.empty();
}

template<typename T>
T ZQueue<T>::take()
{
    AMPERE_ASSERT(this->can_take(), "cannot take from empty zqueue {}", this->name());

    credits.collect();
    credits.return_credit();

    return ZDelay<T>::take();
}

template<typename T>
void ZQueue<T>::clear()
{
    auto just_added_cutoff = this->clock.get_cycle();
    for (auto i = this->entries.size(); !this->entries.empty(); --i) {
        auto& e = this->entries.front();
        if (this->valid_cycle != just_added_cutoff || i > wr_count) {
            invoke_fifo_clear(e);
            this->entries.pop_front();
            credits.return_immediately();
        } else {
            return;
        }
    }

    credits.clear();
}

template<typename T>
void ZQueue<T>::clear_if(const std::function<bool(const T&)>& pred)
{
    auto just_added_cutoff = this->clock.get_cycle();
    auto i = this->entries.size();

    this->entries.erase(std::remove_if(this->entries.begin(),
                                       this->entries.end(),
                                       [&](auto& e) {
                                           bool remove =
                                               (this->valid_cycle != just_added_cutoff || i > wr_count) && pred(e);
                                           if (remove) {
                                               invoke_fifo_clear(e);
                                               credits.return_immediately();
                                           }
                                           --i;
                                           return remove;
                                       }),
                        this->entries.end());

    credits.clear();
}

} // namespace ampere::sc
