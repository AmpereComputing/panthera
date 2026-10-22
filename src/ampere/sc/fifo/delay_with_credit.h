//
// Copyright (c) 2019-2021 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//
#pragma once

#include <boost/circular_buffer.hpp>

#include "ampere/sc/clock.h"
#include "ampere/sc/fifo/fifo.h"

namespace ampere::sc {

template<typename T>
class DelayWithCredit : public FifoWithCredit<T>
{
public:
    DelayWithCredit(const char* name, Clock& clock, CycleDelta latency, CycleDelta return_latency);

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

protected:
    struct Entry
    {
        Cycle ready_cycle;
        T data;
        Credit credit;
    };

    struct CreditEntry
    {
        Cycle ready_cycle;
        Credit credit;
    };

    CycleDelta latency;
    CycleDelta return_latency;

    boost::circular_buffer<Entry> queue;
    std::deque<CreditEntry> credit_return;

    void check_for_stale_data() const;
    std::pair<T, Credit> take_internal();
    void collect_returned_credits();
};

template<typename T>
DelayWithCredit<T>::DelayWithCredit(const char* name, Clock& clock, CycleDelta latency, CycleDelta return_latency)
    : FifoWithCredit<T>{name, clock}, latency{latency}, return_latency{return_latency}, queue(latency + 1)
{
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
bool DelayWithCredit<T>::can_take() const
{
    if (queue.empty())
        return false;

    check_for_stale_data();
    const_cast<DelayWithCredit<T>*>(this)->collect_returned_credits();

    return queue.front().ready_cycle == this->clock.get_cycle();
}

template<typename T>
T DelayWithCredit<T>::take()
{
    AMPERE_ASSERT(can_take_with_credit(), "cannot take from empty fifo {}", this->name());
    auto [data, credit] = take_internal();
    put_credit(std::move(credit));
    return data;
}

template<typename T>
bool DelayWithCredit<T>::can_take_with_credit() const
{
    if (queue.empty())
        return false;

    check_for_stale_data();
    return queue.front().ready_cycle == this->clock.get_cycle();
}

template<typename T>
std::pair<T, Credit> DelayWithCredit<T>::take_with_credit()
{
    AMPERE_ASSERT(can_take_with_credit(), "cannot take from empty fifo {}", this->name());
    return take_internal();
}

template<typename T>
const T& DelayWithCredit<T>::peek(uint64_t index) const
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
uint64_t DelayWithCredit<T>::num_ready() const
{
    // In the 'delay' case, there can only be 0 or 1 things ready.
    return can_take() ? 1 : 0;
}

template<typename T>
const T& DelayWithCredit<T>::get(uint64_t index) const
{
    AMPERE_ASSERT(!queue.empty(), "cannot get from empty fifo {}", this->name());
    AMPERE_ASSERT(
        index < queue.size(), "cannot get past fifo {} size (size={}, index={})", this->name(), queue.size(), index);
    return queue[index].data;
}

template<typename T>
uint64_t DelayWithCredit<T>::size() const
{
    return queue.size();
}

template<typename T>
std::pair<T, Credit> DelayWithCredit<T>::take_internal()
{
    auto data = queue.front().data;
    auto credit = std::move(queue.front().credit);
    queue.pop_front();

    return {std::move(data), std::move(credit)};
}

template<typename T>
bool DelayWithCredit<T>::can_put() const
{
    if (!queue.empty())
        check_for_stale_data();

    const_cast<DelayWithCredit<T>*>(this)->collect_returned_credits();
    return this->get_target_pool()->can_acquire()
           && (queue.empty() || queue.back().ready_cycle < (this->clock.get_cycle() + latency));
}

template<typename T>
void DelayWithCredit<T>::put(T x)
{
    AMPERE_ASSERT(can_put(), "cannot put to a full fifo {}", this->name());

    queue.push_back({this->clock.get_cycle() + latency, std::move(x), this->get_target_pool()->acquire()});
    this->schedule_targets(latency);
}

template<typename T>
bool DelayWithCredit<T>::can_put_credit() const
{
    const_cast<DelayWithCredit<T>*>(this)->collect_returned_credits();
    return true;
}

template<typename T>
void DelayWithCredit<T>::put_credit(Credit credit)
{
    credit_return.push_back({this->clock.get_cycle() + return_latency, std::move(credit)});
}

template<typename T>
void DelayWithCredit<T>::check_for_stale_data() const
{
    if (queue.front().ready_cycle < this->clock.get_cycle())
        AMPERE_ERROR("stale data in delay fifo {}", this->name());
}

template<typename T>
void DelayWithCredit<T>::clear()
{
    check_for_stale_data();
    Cycle just_added_cutoff = this->clock.get_cycle() + this->latency;

    if (!queue.empty()) {
        while (!this->queue.empty()) {
            auto e = this->queue.begin();
            if (just_added_cutoff > e->ready_cycle) {
                invoke_fifo_clear(e->data);
                e->credit->release();
                this->queue.erase(e);
            } else {
                break;
            }
        }
    }

    if (!credit_return.empty())
        for (auto& c : credit_return)
            c.credit->release();
}

template<typename T>
void DelayWithCredit<T>::clear_if(const std::function<bool(const T&)>& pred)
{
    check_for_stale_data();
    Cycle just_added_cutoff = this->clock.get_cycle() + this->latency;

    if (!queue.empty()) {
        queue.erase(std::remove_if(queue.begin(),
                                   queue.end(),
                                   [&](auto& e) {
                                       bool remove = just_added_cutoff > e.ready_cycle && pred(e.data);
                                       if (remove) {
                                           invoke_fifo_clear(e.data);
                                           e.credit->release();
                                       }
                                       return remove;
                                   }),
                    queue.end());
    }

    if (!credit_return.empty())
        for (auto& c : credit_return)
            c.credit->release();
}

template<typename T>
void DelayWithCredit<T>::collect_returned_credits()
{
    while (!credit_return.empty()) {
        if (credit_return.front().ready_cycle <= this->clock.get_cycle()) {
            credit_return.front().credit->release();
            credit_return.pop_front();
        } else {
            break;
        }
    }
}

} // namespace ampere::sc

/*
 * vim: sw=4:ts=4:et:si:cindent:cino=\:0,(0,g0,t0:
 */
