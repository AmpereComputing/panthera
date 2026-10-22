//
// Copyright (c) 2018-2019 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//
#pragma once

#ifdef USE_BOOST_SERIALIZATION
#include <boost/archive/text_oarchive.hpp>
#include <boost/archive/text_iarchive.hpp>
#endif

#include <fmt/base.h>
#include <fmt/ostream.h>

#include <nlohmann/json.hpp>
#include <systemc>
#include <fmt/core.h>
#include "ampere/sc/clock.h"
#include "ampere/sc/environment.h"
#include "ampere/sc/transaction.h"

using json = nlohmann::json;

#define AMPERE_MODULE_THROW(msg) throw_error(msg, __FILE__, __LINE__)

namespace ampere::sc {

class Module : public sc_core::sc_module, public Runnable
{
public:
    Module(const char*) = delete;
    explicit Module(const sc_core::sc_module_name& name) : Module{name, nullptr, nullptr} {}
    Module(const sc_core::sc_module_name& name, Clock* clock_in) : Module{name, clock_in, nullptr} {}
    Module(const sc_core::sc_module_name& name, Clock* clock_in, Environment* env_in);
    ~Module() override;

    bool run() override { return false; }
    Clock* get_clock() { return clock; }
    const Clock* get_clock() const { return clock; }
    Cycle get_cycle() const { return clock->get_cycle(); }
    auto fmt_cycle() const { return fmt::group_digits(get_cycle()); }

    sc_core::sc_time cycles_from_now(CycleDelta delta) const
    {
        return sc_core::sc_time_stamp() + clock->get_period() * delta;
    }
    Environment* get_environment() const { return environment; }
    event_stream::EventStream* get_event_stream() const { return environment->event_stream.get(); }

    void throw_error(std::string message, const char* file, int line);

    template<typename T, typename... Args>
    T make(Args&&... args)
    {
        return sc::trans_make_es<T>(get_event_stream(), std::forward<Args>(args)...);
    }

    template<typename T, typename P, typename... Args>
    T make_child(const transaction_ptr<P>& p, Args&&... args)
    {
        return sc::trans_make_child<T>(get_event_stream(), p, std::forward<Args>(args)...);
    }

    template<typename T, typename P, typename... Args>
    T make_clone(const transaction_ptr<P>& p, Args&&... args)
    {
        return sc::trans_make_clone<T>(get_event_stream(), p, std::forward<Args>(args)...);
    }

    void dump(std::ostream& = std::cout) const override {}

#ifdef USE_BOOST_SERIALIZATION
    virtual void serialize(boost::archive::text_oarchive& oa [[maybe_unused]]) const {}
    virtual void deserialize(boost::archive::text_iarchive& ia [[maybe_unused]]) {}
#endif

protected:
    void end_of_elaboration() override {}
    void start_of_simulation() override {}
    void end_of_simulation() override;

    friend class Clock;
    friend class ClockDummy;

    bool debug_enabled() const
    {
#ifdef NDEBUG
        return false;
#else
        return get_environment()->debug.enabled(*this, get_clock());
#endif
    }
    bool debug_enabled([[maybe_unused]] const char* category) const
    {
#ifdef NDEBUG
        return false;
#else
        return get_environment()->debug.enabled(*this, category, get_clock());
#endif
    }

private:
    Clock* clock;
    Environment* environment;

    Clock* get_clock_from_parent();
    Environment* get_environment_from_parent();

    bool dump_at_end{false};
    bool clock_owner{false};
};

} // namespace ampere::sc

template<>
struct fmt::formatter<sc_core::sc_module_name> : fmt::ostream_formatter
{};

/*
 * vim: sw=4:ts=4:et:si:cindent:cino=\:0,(0,g0,t0:
 */
