//
// Copyright (c) 2020 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//
#pragma once

#include "ampere/sc/clock.h"
#include "ampere/sc/fifo/fifo.h"

namespace ampere::sc {

template<typename T>
class CxQueueWithCredit : public FifoWithCredit<T>, public Runnable
{
public:
    CxQueueWithCredit(const char* name,
                      Clock& src_clock,
                      Clock& tgt_clock,
                      sc_core::sc_time delay,
                      sc_core::sc_time return_delay,
                      size_t capacity);
    ~CxQueueWithCredit();

    bool can_take() const override;
    bool can_take_with_credit() const override;
    T take() override;
    std::pair<T, Credit> take_with_credit() override;
    void clear() override;
    void clear_if(const std::function<bool(const T&)>& pred) override;
    bool can_put_credit() const override;
    void put_credit(Credit) override;

    const T& peek(uint64_t index = 0) const override;
    uint64_t num_ready() const override;

    const T& unsafe_peek(uint64_t index = 0) const override { return get(index); }
    uint64_t unsafe_size() const override { return size(); }

    bool can_put() const override;
    void put(T x) override;

    const T& get(uint64_t index = 0) const override;
    uint64_t size() const override;

    bool run() override;
    bool is_channel() override { return true; }

protected:
    struct Entry
    {
        uint64_t ready_time;
        T data;
        Credit credit;
    };

    std::deque<Entry> queue;
    QueueCredits<Credit, sc_core::sc_time> credits;

    Clock& src_clock;

    sc_core::sc_time delay;

    Cycle last_wr_cycle;
    Cycle last_rd_cycle;

    CreditPoolIfc* internal_credit_pool{nullptr};

    std::pair<T, Credit> take_internal();
    void collect_returned_credits();
};

template<typename T>
CxQueueWithCredit<T>::CxQueueWithCredit(const char* name,
                                        Clock& src_clock,
                                        Clock& tgt_clock,
                                        sc_core::sc_time delay,
                                        sc_core::sc_time return_delay,
                                        size_t capacity)
    : FifoWithCredit<T>(name, tgt_clock),
      credits(src_clock, capacity, return_delay),
      src_clock{src_clock},
      delay{delay},
      last_wr_cycle{-1},
      last_rd_cycle{-1}
{
    tgt_clock.add_runnable(this);

    if (capacity > 0) {
        internal_credit_pool =
            new CreditPool{CreditPool::explicit_size{}, fmt::format("{}_credit_pool", name).c_str(), capacity};
        this->target_pool_ptr = &internal_credit_pool;
    }
}

template<typename T>
CxQueueWithCredit<T>::~CxQueueWithCredit()
{
    delete internal_credit_pool;
}

template<typename T>
bool CxQueueWithCredit<T>::can_put() const
{
    const_cast<CxQueueWithCredit<T>*>(this)->credits.collect();
    return this->get_target_pool()->can_acquire() && last_wr_cycle != src_clock.get_cycle();
}

template<typename T>
void CxQueueWithCredit<T>::put(T x)
{
    if (!can_put())
        throw std::runtime_error{std::string("cannot put to full queue: ") + this->name()};

    if (queue.empty())
        this->clock.schedule(this, 0);

    auto now = sc_core::sc_time_stamp().value();
    auto ready_time = now + delay.value();

    last_wr_cycle = src_clock.get_cycle();
    queue.push_back({ready_time, std::move(x), this->get_target_pool()->acquire()});
}

template<typename T>
bool CxQueueWithCredit<T>::can_take() const
{
    if (queue.empty())
        return false;

    auto now = sc_core::sc_time_stamp().value();

    return queue.front().ready_time <= now && last_rd_cycle != this->clock.get_cycle() && can_put_credit();
}

template<typename T>
T CxQueueWithCredit<T>::take()
{
    if (!can_take())
        throw std::runtime_error{std::string("cannot take from empty queue: ") + this->name()};

    auto [data, credit] = take_internal();
    put_credit(std::move(credit));
    return data;
}

template<typename T>
bool CxQueueWithCredit<T>::can_take_with_credit() const
{
    if (queue.empty())
        return false;

    auto now = sc_core::sc_time_stamp().value();

    return queue.front().ready_time <= now && last_rd_cycle != this->clock.get_cycle();
}

template<typename T>
std::pair<T, Credit> CxQueueWithCredit<T>::take_with_credit()
{
    if (!can_take_with_credit())
        throw std::runtime_error{std::string("cannot take from empty queue: ") + this->name()};

    return take_internal();
}

template<typename T>
bool CxQueueWithCredit<T>::can_put_credit() const
{
    // Should there be a credit bandwidth limit?
    return true;
}

template<typename T>
void CxQueueWithCredit<T>::put_credit(Credit credit)
{
    credits.return_credit(std::move(credit));
}

template<typename T>
const T& CxQueueWithCredit<T>::peek(uint64_t index) const
{
    if (!can_take())
        throw std::runtime_error{std::string{"cannot peek from empty fifo "} + this->name()};

    AMPERE_ASSERT(index < num_ready(), "tried to peek at a nonexistent entry in {}", this->name());
    return queue[index].data;
}

template<typename T>
uint64_t CxQueueWithCredit<T>::num_ready() const
{
    uint64_t count = 0;
    auto now = sc_core::sc_time_stamp().value();

    for (auto& entry : queue) {
        if (entry.ready_time <= now)
            ++count;
        else
            break;
    }

    return count;
}

template<typename T>
const T& CxQueueWithCredit<T>::get(uint64_t index) const
{
    AMPERE_ASSERT(index < queue.size(), "tried to peek at a nonexistent entry in {}", this->name());
    return queue[index].data;
}

template<typename T>
uint64_t CxQueueWithCredit<T>::size() const
{
    return queue.size();
}

template<typename T>
std::pair<T, Credit> CxQueueWithCredit<T>::take_internal()
{
    auto [ready_time, data, credit] = std::move(queue.front());
    queue.pop_front();

    last_rd_cycle = this->clock.get_cycle();
    return {data, std::move(credit)};
}

template<typename T>
void CxQueueWithCredit<T>::clear()
{
    while (!queue.empty()) {
        invoke_fifo_clear(queue.front());
        queue.pop_front();
    }

    credits.clear();
}

template<typename T>
void CxQueueWithCredit<T>::clear_if(const std::function<bool(const T&)>& pred)
{
    if (!queue.empty()) {
        queue.erase(std::remove_if(queue.begin(),
                                   queue.end(),
                                   [&](auto& e) {
                                       bool remove = pred(e.data);
                                       if (remove)
                                           invoke_fifo_clear(e.data);
                                       return remove;
                                   }),
                    queue.end());
    }

    credits.clear();
}

template<typename T>
bool CxQueueWithCredit<T>::run()
{
    if (!queue.empty()) {
        auto next_ready_time = queue.front().ready_time;
        auto now = sc_core::sc_time_stamp().value();

        CycleDelta delta;

        if (next_ready_time <= now) {
            delta = 1;
        } else {
            auto p = this->clock.get_period().value();
            delta = ((next_ready_time - now) + (p - 1)) / p;
        }

        this->schedule_targets(delta);
        return true;
    }

    return false;
}

} // namespace ampere::sc

/*
 * vim: sw=4:ts=4:et:si:cindent:cino=\:0,(0,g0,t0:
 */
