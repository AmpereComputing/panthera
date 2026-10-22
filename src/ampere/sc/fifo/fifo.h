//
// Copyright (c) 2019 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//
//
// Copyright (c) 2018, Ampere Computing
//
#pragma once

#include <memory>
#include <type_traits>
#include <utility>

#include "ampere/sc/connection.h"
#include "ampere/sc/taker.h"
#include "ampere/sc/putter.h"
#include "ampere/sc/report.h"

namespace ampere::sc {

template<typename T>
class Fifo : public Connection, public Taker<T>, public Putter<T>
{
public:
    explicit Fifo(const char* name, Clock& clock);

private:
    void register_taker_port(sc_core::sc_port_base& port, const char* port_name [[maybe_unused]]) override
    {
        this->add_target(get_nearest_ancestor<Runnable>(&port));
        if (this->first_port_name.empty())
            this->first_port_name = port.name();
    }
};

template<typename T>
Fifo<T>::Fifo(const char* name, Clock& clock) : Connection(name, clock)
{
}

template<typename T>
class FifoWithCredit : public Connection, public TakerWithCredit<T>, public Putter<T>
{
public:
    FifoWithCredit(const char* name, Clock& clock) : Connection{name, clock} {}

    CreditPoolIfc* get_target_pool()
    {
        AMPERE_ASSERT(
            !(target_pool_ptr && *target_pool_ptr && alt_target_pool_ptr && *alt_target_pool_ptr),
            "{}: target_pool_ptr was set with internal_credit_pool as well as with {}: InWithCredit credit_pool",
            this->name(),
            (*alt_target_pool_ptr)->pool_name());
        AMPERE_ASSERT(target_pool_ptr, "{}: target_pool_ptr not set", this->name());
        AMPERE_ASSERT(*target_pool_ptr,
                      "target_pool not set on port \"{}\" (alternatively, \"{}\" can be given a non-zero capacity)",
                      this->first_port_name,
                      this->name());
        return *target_pool_ptr;
    }

    const CreditPoolIfc* get_target_pool() const { return const_cast<FifoWithCredit*>(this)->get_target_pool(); }

protected:
    // Annoyingly, the target_pool_ptr has to be a pointer-to-a-pointer to a
    // CreditPoolIfc because we would like to both:
    //
    //    (1) Set a credit pool at construction time (it's convenient)
    //    (2) Set a credit pool post elaboration (because we might need port
    //        binding information to do it right)
    //
    // This means that we have to be able to set the pool on both an unbound
    // and bound port, and then transfer that information to this Fifo.
    //
    // The easiest (!) way to do that is to connect to the *address* of the
    // target pool in whatever port binds to this Fifo. Maybe it's set
    // already, or maybe it hasn't been set yet. Either way we'll find out
    // when somebody calls get_target_pool()---it better be set by then!
    //
    CreditPoolIfc** target_pool_ptr{nullptr};
    // This is the Alternate Target Pool which gets set to the pool ptr if
    // the target_pool_ptr is set by internal_credit_pool. Either target_pool_ptr
    // or alt_target_pool_ptr should be set, but never both after elaboration completes.
    CreditPoolIfc** alt_target_pool_ptr{nullptr};

    void register_taker_port(sc_core::sc_port_base& port, const char* port_name [[maybe_unused]]) override
    {
        this->add_target(get_nearest_ancestor<Runnable>(&port));

        if (this->first_port_name.empty())
            this->first_port_name = port.name();

        auto cpc = dynamic_cast<CreditPoolContainerIfc*>(&port);
        AMPERE_ASSERT(cpc, "port {} is not a CreditPoolContainer", port.name());

        if (target_pool_ptr)
            alt_target_pool_ptr = cpc->get_credit_pool_ptr();
        else
            target_pool_ptr = cpc->get_credit_pool_ptr();
    }
};

template<typename T>
struct fifo_clear_method_is
{
private:
    template<typename U>
    static constexpr auto check_as_member(U*) ->
        typename std::enable_if<std::is_same<decltype(std::declval<U>().fifo_clear()), void>::value,
                                std::true_type>::type;

    template<typename>
    static constexpr std::false_type check_as_member(...);

    template<typename U>
    static constexpr auto check_as_pointer_to_member(U*) ->
        typename std::enable_if<std::is_same<decltype(std::declval<U>()->fifo_clear()), void>::value,
                                std::true_type>::type;

    template<typename>
    static constexpr std::false_type check_as_pointer_to_member(...);

    using as_member = decltype(check_as_member<T>(0));
    using as_pointer_to_member = decltype(check_as_pointer_to_member<T>(0));

public:
    static constexpr bool member = as_member::value;
    static constexpr bool pointer_to_member = as_pointer_to_member::value;
    static constexpr bool nonexistant = !as_member::value && !as_pointer_to_member::value;
};

template<typename T, typename std::enable_if_t<fifo_clear_method_is<T>::member, int> = 0>
void invoke_fifo_clear(T& obj)
{
    obj.fifo_clear();
}

template<typename T, typename std::enable_if_t<fifo_clear_method_is<T>::pointer_to_member, int> = 0>
void invoke_fifo_clear(T& obj)
{
    obj->fifo_clear();
}

template<typename T, typename std::enable_if_t<fifo_clear_method_is<T>::nonexistant, int> = 0>
void invoke_fifo_clear(T& obj [[maybe_unused]])
{
}

} // namespace ampere::sc

/*
 * vim: sw=4:ts=4:et:si:cindent:cino=\:0,(0,g0,t0:
 */
