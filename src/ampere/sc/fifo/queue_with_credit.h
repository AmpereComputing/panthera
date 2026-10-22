//
// Copyright (c) 2019-2021 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//
#pragma once

#include <cstddef>
#include <cstdint>
#include <fmt/core.h>
#include <deque>
#include <functional>
#include <string>
#include <utility>

#include "ampere/sc/clock.h"
#include "ampere/sc/credit.h"
#include "ampere/sc/cycle.h"
#include "ampere/sc/fifo/credit.h"
#include "ampere/sc/fifo/fifo.h"
#include "ampere/sc/report.h"
#include "ampere/sc/runnable.h"

namespace ampere::sc {

template<typename T>
class QueueWithCredit : public FifoWithCredit<T>, public Runnable
{
public:
    QueueWithCredit(const char* name, Clock& clock, CycleDelta latency, CycleDelta return_latency, size_t capacity);
    ~QueueWithCredit();

    bool can_take() const override;
    T take() override;
    bool can_take_with_credit() const override;
    std::pair<T, Credit> take_with_credit() override;

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

    bool can_put_credit() const override;
    void put_credit(Credit credit) override;

    bool run() override;
    bool is_channel() override { return true; }

protected:
    struct Entry
    {
        Cycle ready_cycle;
        T data;
        Credit credit;
    };

    CycleDelta latency;
    size_t capacity;

    std::deque<Entry> queue;
    QueueCredits<Credit> credits;

    CreditPoolIfc* internal_credit_pool{nullptr};

    std::pair<T, Credit> take_internal();
    void collect_returned_credits();
};

template<typename T>
QueueWithCredit<T>::QueueWithCredit(
    const char* name, Clock& clock, CycleDelta latency, CycleDelta return_latency, size_t capacity)
    : FifoWithCredit<T>{name, clock},
      latency{latency},
      capacity{capacity},
      credits(clock, capacity, return_latency, capacity > 0)
{
    clock.add_runnable(this);

    if (capacity > 0) {
        internal_credit_pool =
            new CreditPool{CreditPool::explicit_size{}, fmt::format("{}_credit_pool", name).c_str(), capacity};
        this->target_pool_ptr = &internal_credit_pool;
    }

    this->validate_latency = latency;
    this->validate_return_latency = return_latency;
    this->validate_credits = [this]() -> size_t {
        return this->get_target_pool()->get_size();
    };
    this->validate_credits_pool_name = [this]() -> std::string {
        return this->get_target_pool()->pool_name();
    };
}

template<typename T>
QueueWithCredit<T>::~QueueWithCredit()
{
    if (internal_credit_pool) {
        for (auto& x : queue)
            x.credit->release();

        delete internal_credit_pool;
    }
}

template<typename T>
bool QueueWithCredit<T>::can_take() const
{
    if (queue.empty())
        return false;

    return queue.front().ready_cycle <= this->clock.get_cycle();
}

template<typename T>
T QueueWithCredit<T>::take()
{
    AMPERE_ASSERT(can_take_with_credit(), "cannot take from empty fifo {}", this->name());
    auto [data, credit] = take_internal();
    put_credit(std::move(credit));
    return std::move(data);
}

template<typename T>
bool QueueWithCredit<T>::can_take_with_credit() const
{
    // This is the same as `can_take()`. If we decide to impose credit-return bandwidth
    // limits, then `can_take()` would be slightly different, as it requires the credit
    // return to be possible in the same cycle.
    //
    return can_take();
}

template<typename T>
std::pair<T, Credit> QueueWithCredit<T>::take_with_credit()
{
    AMPERE_ASSERT(can_take_with_credit(), "cannot take from empty fifo {}", this->name());
    return take_internal();
}

template<typename T>
const T& QueueWithCredit<T>::peek(uint64_t index) const
{
    AMPERE_ASSERT(can_take(), "cannot peek from empty fifo {}", this->name());
    AMPERE_ASSERT(index < num_ready(),
                  "cannot peek past fifo {} size (num_ready={}, index={})",
                  this->name(),
                  num_ready(),
                  index);
    return queue[index].data;
}

template<typename T>
uint64_t QueueWithCredit<T>::num_ready() const
{
    uint64_t count = 0;
    Cycle current_cycle = this->clock.get_cycle();

    for (auto& entry : queue) {
        if (entry.ready_cycle <= current_cycle)
            ++count;
        else
            break;
    }

    return count;
}

template<typename T>
const T& QueueWithCredit<T>::get(uint64_t index) const
{
    AMPERE_ASSERT(!queue.empty(), "cannot get from empty fifo {}", this->name());
    AMPERE_ASSERT(
        index < queue.size(), "cannot get past fifo {} size (size={}, index={})", this->name(), queue.size(), index);
    return queue[index].data;
}

template<typename T>
uint64_t QueueWithCredit<T>::size() const
{
    return queue.size();
}

template<typename T>
std::pair<T, Credit> QueueWithCredit<T>::take_internal()
{
    auto data = std::move(queue.front().data);
    auto credit = std::move(queue.front().credit);
    queue.pop_front();

    return {std::move(data), std::move(credit)};
}

template<typename T>
bool QueueWithCredit<T>::can_put() const
{
    const_cast<QueueWithCredit<T>*>(this)->credits.collect();
    return this->get_target_pool()->can_acquire()
           && (queue.empty() || queue.back().ready_cycle < (this->clock.get_cycle() + latency));
}

template<typename T>
void QueueWithCredit<T>::put(T x)
{
    AMPERE_ASSERT(can_put(), "cannot put to a full fifo {}", this->name());

    if (queue.empty())
        this->clock.schedule(this, 0);

    queue.push_back({this->clock.get_cycle() + latency, std::move(x), this->get_target_pool()->acquire()});
}

template<typename T>
bool QueueWithCredit<T>::can_put_credit() const
{
    // No bandwidth limit on credit returns for now.
    return true;
}

template<typename T>
void QueueWithCredit<T>::put_credit(Credit credit)
{
    AMPERE_ASSERT(can_put_credit(), "credit return bandwidth exceeded in {}", this->name());
    credits.add(std::move(credit));
}

template<typename T>
void QueueWithCredit<T>::clear()
{
    auto just_added_cutoff = this->clock.get_cycle() + this->latency;
    while (!queue.empty()) {
        auto e = queue.begin();
        if (e->ready_cycle < just_added_cutoff) {
            invoke_fifo_clear(e->data);
            e->credit->release();
            queue.erase(e);
        } else {
            break;
        }
    }

    credits.clear();
}

template<typename T>
void QueueWithCredit<T>::clear_if(const std::function<bool(const T&)>& pred)
{
    auto just_added_cutoff = this->clock.get_cycle() + this->latency;
    if (!queue.empty()) {
        queue.erase(std::remove_if(queue.begin(),
                                   queue.end(),
                                   [&](auto& e) {
                                       bool remove = e.ready_cycle < just_added_cutoff && pred(e.data);
                                       if (remove) {
                                           invoke_fifo_clear(e.data);
                                           e.credit.release();
                                       }
                                       return remove;
                                   }),
                    queue.end());
    }

    credits.clear();
}

template<typename T>
bool QueueWithCredit<T>::run()
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
