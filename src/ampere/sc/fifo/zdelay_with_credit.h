//
// Copyright (c) 2019-2024 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//

#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <utility>
#include <boost/circular_buffer.hpp>

#include "ampere/sc/ancestor.h"
#include "ampere/sc/clock.h"
#include "ampere/sc/connection.h"
#include "ampere/sc/credit.h"
#include "ampere/sc/cycle.h"
#include "ampere/sc/fifo/fifo.h"
#include "ampere/sc/report.h"
#include "ampere/sc/runnable.h"
#include "ampere/sc/taker.h"

namespace ampere::sc {

template<typename T>
class ZDelayWithCredit : public FifoWithCredit<T>
{
public:
    ZDelayWithCredit(const char* name,
                     Clock& clock,
                     size_t capacity = 1,
                     bool zero_delay_self_connection_allowed = false);

    bool can_take() const override;
    T take() override;
    bool can_take_with_credit() const override;
    std::pair<T, Credit> take_with_credit() override;

    void clear() override {}
    void clear_if(const std::function<bool(const T&)>& pred [[maybe_unused]]) override {}
    const T& peek(uint64_t index = 0) const override;
    uint64_t num_ready() const override;

    const T& unsafe_peek(uint64_t index = 0) const override { return get(index); }
    uint64_t unsafe_size() const override { return size(); }

    bool can_put() const override;
    void put(T) override;

    const T& get(uint64_t index = 0) const override { return peek(index); }
    uint64_t size() const override;

    void add_target(Runnable* to, Runnable* from) override;

    bool can_put_credit() const override;
    void put_credit(Credit credit) override;

protected:
    struct Entry
    {
        T data;
        Credit credit;
    };
    boost::circular_buffer<Entry> entries;
    Cycle valid_cycle;
    bool zero_delay_self_connection_allowed = false;
    void check_for_stale_data() const;

    Runnable* m_from{nullptr};
    Runnable* m_to{nullptr};

    void register_putter_port(sc_core::sc_port_base& port, const char* port_type [[maybe_unused]]) override
    {
        m_from = get_nearest_ancestor<Runnable>(&port);

        if (m_to)
            this->add_target(m_to, m_from);
    }

    void register_taker_port(sc_core::sc_port_base& port, const char* port_type [[maybe_unused]]) override
    {
        m_to = get_nearest_ancestor<Runnable>(&port);

        if (m_from)
            this->add_target(m_to, m_from);

        if (this->first_port_name.empty())
            this->first_port_name = port.name();

        auto* cpc = dynamic_cast<CreditPoolContainerIfc*>(&port);
        AMPERE_ASSERT(cpc, "port {} is not a CreditPoolContainer", port.name());

        if (!this->target_pool_ptr)
            this->target_pool_ptr = cpc->get_credit_pool_ptr();
    }

    void put_internal(T x);
};

template<typename T>
ZDelayWithCredit<T>::ZDelayWithCredit(const char* name,
                                      Clock& clock,
                                      size_t capacity,
                                      bool zero_delay_self_connection_allowed)
    : FifoWithCredit<T>(name, clock),
      entries{capacity},
      valid_cycle{0},
      zero_delay_self_connection_allowed(zero_delay_self_connection_allowed)
{
}

template<typename T>
bool ZDelayWithCredit<T>::can_put() const
{
    return this->get_target_pool()->can_acquire() && (entries.size() < entries.capacity());
}

template<typename T>
void ZDelayWithCredit<T>::put_internal(T x)
{
    valid_cycle = this->clock.get_cycle();
    entries.push_back({std::move(x), std::move(this->get_target_pool()->acquire())});
    this->schedule_targets(0);
}

template<typename T>
void ZDelayWithCredit<T>::put(T x)
{
    AMPERE_ASSERT(can_put(), "cannot put to full zdelay {}", this->name());
    put_internal(std::move(x));
}

template<typename T>
bool ZDelayWithCredit<T>::can_take() const
{
    if (entries.empty())
        return false;

    check_for_stale_data();
    return true;
}

template<typename T>
T ZDelayWithCredit<T>::take()
{
    AMPERE_ASSERT(can_take(), "cannot take from empty zdelay {}", this->name());

    auto x = std::move(entries.front());
    entries.pop_front();
    x.credit.release();
    return x.data;
}

template<typename T>
const T& ZDelayWithCredit<T>::peek(uint64_t index) const
{
    AMPERE_ASSERT(entries.size(), "cannot peek from empty zdelay {}", this->name());
    AMPERE_ASSERT(index < entries.size(), "Tried to peek at a nonexistent entry");

    return entries[index].data;
}

template<typename T>
uint64_t ZDelayWithCredit<T>::num_ready() const
{
    return entries.size();
}

template<typename T>
void ZDelayWithCredit<T>::check_for_stale_data() const
{
    AMPERE_ASSERT(valid_cycle == this->clock.get_cycle(), "stale data in zdelaywithcredit fifo {}", this->name());
}

template<typename T>
uint64_t ZDelayWithCredit<T>::size() const
{
    return entries.size();
}

template<typename T>
void ZDelayWithCredit<T>::add_target(Runnable* to, Runnable* from)
{
    if (!zero_delay_self_connection_allowed || to != from) {
        Connection::add_target(to, from);
        this->clock.add_zdelay_connection(from, to);
    }
}

template<typename T>
bool ZDelayWithCredit<T>::can_take_with_credit() const
{
    return can_take();
}

template<typename T>
std::pair<T, Credit> ZDelayWithCredit<T>::take_with_credit()
{
    AMPERE_ASSERT(can_take_with_credit(), "cannot take from empty zdelay {}", this->name());

    auto entry = std::move(entries.front());
    entries.pop_front();
    return {std::move(entry.data), std::move(entry.credit)};
}

template<typename T>
bool ZDelayWithCredit<T>::can_put_credit() const
{
    return true;
}

template<typename T>
void ZDelayWithCredit<T>::put_credit(Credit credit)
{
    /* Reverse Path, they gave us the credit back and we free it */
    credit->release();
}

} // namespace ampere::sc
