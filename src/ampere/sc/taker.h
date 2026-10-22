//
// Copyright (c) 2019-2021 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//
#pragma once

#include <sysc/communication/sc_interface.h>
#include <sysc/communication/sc_port.h>

#include "ampere/sc/credit.h"
#include "ampere/sc/report.h"

namespace ampere::sc {

template<typename T>
class Taker : public sc_core::sc_interface
{
public:
    virtual bool can_take() const = 0;
    virtual T take() = 0;
    virtual void clear() = 0;
    virtual void clear_if(const std::function<bool(const T&)>& pred) = 0;

    virtual const T& peek(uint64_t index = 0) const = 0;
    virtual uint64_t num_ready() const = 0;

    // Potentially timing unsafe, as latency is ignored
    virtual const T& unsafe_peek(uint64_t index [[maybe_unused]] = 0) const
    {
        throw std::runtime_error{"no unsafe size"};
    }
    virtual uint64_t unsafe_size() const { throw std::runtime_error{"no unsafe peeking"}; }

protected:
    friend sc_core::sc_port_base; // the caller of register_port()

    virtual void register_taker_port(sc_core::sc_port_base& port [[maybe_unused]],
                                     const char* port_type [[maybe_unused]])
    {
    }

    // Proxy this call to another name so that we can distinguish between the Taker
    // and the Putter inside a Fifo.
    void register_port(sc_core::sc_port_base& port, const char* port_type) override
    {
        register_taker_port(port, port_type);
    }
};

struct CreditPoolContainerIfc
{
    virtual CreditPoolIfc** get_credit_pool_ptr() = 0;
};

template<typename T>
class TakerWithCredit : public sc_core::sc_interface
{
public:
    virtual bool can_take() const = 0;
    virtual T take() = 0;
    virtual bool can_take_with_credit() const = 0;
    virtual std::pair<T, Credit> take_with_credit() = 0;
    virtual void clear() = 0;
    virtual void clear_if(const std::function<bool(const T&)>& pred) = 0;
    virtual bool can_put_credit() const = 0;
    virtual void put_credit(Credit) = 0;

    virtual const T& peek(uint64_t index = 0) const = 0;
    virtual uint64_t num_ready() const = 0;

    // Potentially timing unsafe, as latency is ignored
    virtual const T& unsafe_peek(uint64_t index = 0) const = 0;
    virtual uint64_t unsafe_size() const = 0;

protected:
    friend sc_core::sc_port_base; // the caller of register_port()

    virtual void register_taker_port(sc_core::sc_port_base& port [[maybe_unused]],
                                     const char* port_type [[maybe_unused]])
    {
    }

    // Proxy this call to another name so that we can distinguish between the Taker
    // and the Putter inside a Fifo.
    void register_port(sc_core::sc_port_base& port, const char* port_type) override
    {
        register_taker_port(port, port_type);
    }
};

template<typename T>
using In = sc_core::sc_port<Taker<T>>;

template<typename T>
using InOpt = sc_core::sc_port<Taker<T>, 1, sc_core::SC_ZERO_OR_MORE_BOUND>;

template<typename T, int N = 1, sc_core::sc_port_policy P = sc_core::SC_ONE_OR_MORE_BOUND>
struct InWithCredit : public sc_core::sc_port<TakerWithCredit<T>, N, P>, public CreditPoolContainerIfc
{
    explicit InWithCredit(const char* name) : sc_core::sc_port<TakerWithCredit<T>, N, P>(name) {}

    void set_credit_pool(CreditPoolIfc* pool) { credit_pool = pool; }
    CreditPoolIfc* get_credit_pool() const { return credit_pool; }

    CreditPoolIfc** get_credit_pool_ptr() override { return &credit_pool; }

private:
    CreditPoolIfc* credit_pool{nullptr};
};

template<typename T>
using InWithCreditOpt = InWithCredit<T, 1, sc_core::SC_ZERO_OR_MORE_BOUND>;

} // namespace ampere::sc

/*
 * vim: sw=4:ts=4:et:si:cindent:cino=\:0,(0,g0,t0:
 */
