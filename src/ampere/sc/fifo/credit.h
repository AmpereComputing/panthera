//
// Copyright (c) 2025 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//

#pragma once

#include <sysc/kernel/sc_time.h>
#include <cstddef>
#include <type_traits>

#include <boost/circular_buffer.hpp>
#include "ampere/sc/clock.h"
#include "ampere/sc/credit.h"
#include "ampere/sc/cycle.h"
#include "ampere/sc/report.h"

namespace ampere::sc {

template<typename C, typename T = Cycle>
class QueueCreditsBase
{
protected:
    struct Return
    {
        T ready;
        C credit;
    };

    const Clock& clock;
    unsigned credits;
    T return_latency;
    boost::circular_buffer<Return> credit_return;

public:
    QueueCreditsBase(const Clock& clock, size_t capacity, T return_latency)
        : clock(clock), credits(capacity), return_latency(return_latency), credit_return(return_latency + capacity)
    {
    }

    void add(C credit) { credit_return.push_back({this->clock.get_cycle() + this->return_latency, std::move(credit)}); }

    bool return_ready(const Return& entry) const { return entry.ready <= clock.get_cycle(); }
};

template<typename C>
class QueueCreditsBase<C, sc_core::sc_time>
{
protected:
    struct Return
    {
        uint64_t ready;
        C credit;
    };

    const Clock& clock;
    unsigned credits;
    sc_core::sc_time return_latency;
    std::deque<Return> credit_return;

public:
    QueueCreditsBase(const Clock& clock, size_t capacity, sc_core::sc_time return_latency)
        : clock(clock), credits(capacity), return_latency(return_latency)
    {
    }

    void add(C credit)
    {
        auto now = sc_core::sc_time_stamp().value();
        auto ready_time = now + return_latency.value();
        credit_return.emplace_back(ready_time, std::move(credit));
    }

    bool return_ready(const Return& entry) const { return entry.ready <= sc_core::sc_time_stamp().value(); }
};

template<typename C = size_t, typename T = Cycle>
class QueueCredits : public QueueCreditsBase<C, T>
{
public:
    QueueCredits(const Clock& clock, size_t capacity, T return_latency)
        : QueueCreditsBase<C>(clock, capacity, return_latency)
    {
    }

    bool full() const { return this->credits == 0; }
    unsigned available() const { return this->credits; }

    void consume(C amount = 1)
    {
        AMPERE_ASSERT(this->credits >= amount, "Not enough credits");
        this->credits -= amount;
    }

    void return_immediately(C amount = 1) { this->credits += amount; }
    void return_credit(C amount = 1)
    {
        if (this->return_latency > T{})
            this->add(amount);
        else
            return_immediately(amount);
    }

    void collect()
    {
        while (!this->credit_return.empty()) {
            if (this->return_ready(this->credit_return.front())) {
                this->credits += this->credit_return.front().credit;
                this->credit_return.pop_front();
            } else {
                break;
            }
        }
    }

    void clear()
    {
        while (!this->credit_return.empty()) {
            this->credits += this->credit_return.front().credit;
            this->credit_return.pop_front();
        }
    }
};

template<typename T>
class QueueCredits<Credit, T> : public QueueCreditsBase<Credit, T>
{
    bool release_at_end{true};

public:
    QueueCredits(const Clock& clock, size_t capacity, T return_latency, bool release_at_end = true)
        : QueueCreditsBase<Credit, T>(clock, capacity, return_latency), release_at_end(release_at_end)
    {
    }

    ~QueueCredits()
    {
        for (auto& item : this->credit_return)
            item.credit.release();
    }

    void return_immediately(Credit credit) { credit->release(); }
    void return_credit(Credit credit)
    {
        if (this->return_latency > T{})
            this->add(std::move(credit));
        else
            credit->release();
    }

    void collect()
    {
        while (!this->credit_return.empty()) {
            if (this->return_ready(this->credit_return.front())) {
                this->credit_return.front().credit->release();
                this->credit_return.pop_front();
            } else {
                break;
            }
        }
    }

    void clear()
    {
        while (!this->credit_return.empty()) {
            this->credit_return.front().credit.release();
            this->credit_return.pop_front();
        }
    }
};

} // namespace ampere::sc

/*
 * vim: sw=4:ts=4:et:si:cindent:cino=\:0,(0,g0,t0:
 */
