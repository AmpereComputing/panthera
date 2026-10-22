//
// Copyright (c) 2025 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//

#pragma once

#include <cstddef>
#include <functional>
#include <utility>
#include <boost/circular_buffer.hpp>

#include "ampere/sc/clock.h"
#include "ampere/sc/credit.h"
#include "ampere/sc/cycle.h"
#include "ampere/sc/fifo/credit.h"
#include "ampere/sc/fifo/fifo.h"
#include "ampere/sc/fifo/zdelay_with_credit.h"
#include "ampere/sc/report.h"
#include "ampere/sc/runnable.h"

namespace ampere::sc {

template<typename T>
class ZQueueWithCredit : public ZDelayWithCredit<T>
{
    size_t bandwidth;
    QueueCredits<Credit> credits;
    size_t wr_count{0};

public:
    ZQueueWithCredit(const char* name,
                     Clock& clock,
                     CycleDelta return_latency,
                     size_t capacity,
                     size_t bandwidth,
                     bool zero_delay_self_connection_allowed = false)
        : ZDelayWithCredit<T>(name, clock, capacity + bandwidth * return_latency, zero_delay_self_connection_allowed),
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
bool ZQueueWithCredit<T>::can_put() const
{
    const_cast<ZQueueWithCredit<T>*>(this)->credits.collect();
    return (this->valid_cycle != this->clock.get_cycle() || wr_count < bandwidth) && ZDelayWithCredit<T>::can_put();
}

template<typename T>
void ZQueueWithCredit<T>::put(T x)
{
    AMPERE_ASSERT(can_put(), "cannot put to full zqueue {}", this->name());

    if (this->valid_cycle != this->clock.get_cycle())
        wr_count = 0;
    ++wr_count;
    this->valid_cycle = this->clock.get_cycle();

    ZDelayWithCredit<T>::put_internal(std::move(x));
}

template<typename T>
bool ZQueueWithCredit<T>::can_take() const
{
    return !this->entries.empty();
}

template<typename T>
T ZQueueWithCredit<T>::take()
{
    AMPERE_ASSERT(this->can_take(), "cannot take from empty zqueue {}", this->name());

    credits.collect();

    auto x = std::move(this->entries.front());
    this->entries.pop_front();
    credits.return_credit(std::move(x.credit));

    return x.data;
}

template<typename T>
void ZQueueWithCredit<T>::clear()
{
    credits.collect();
    auto just_added_cutoff = this->clock.get_cycle();
    for (auto i = this->entries.size(); !this->entries.empty(); --i) {
        auto& e = this->entries.front();
        if (this->valid_cycle != just_added_cutoff || i > wr_count) {
            invoke_fifo_clear(e.data);
            e.credit->release();
            this->entries.pop_front();
        } else {
            return;
        }
    }
}

template<typename T>
void ZQueueWithCredit<T>::clear_if(const std::function<bool(const T&)>& pred)
{
    credits.collect();
    auto just_added_cutoff = this->clock.get_cycle();
    auto i = this->entries.size();

    this->entries.erase(std::remove_if(this->entries.begin(),
                                       this->entries.end(),
                                       [&](auto& e) {
                                           bool remove =
                                               (this->valid_cycle != just_added_cutoff || i > wr_count) && pred(e.data);
                                           if (remove) {
                                               e.credit->release();
                                               invoke_fifo_clear(e.data);
                                           }
                                           --i;
                                           return remove;
                                       }),
                        this->entries.end());
}

} // namespace ampere::sc
