//
// Copyright (c) 2018-2019 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//

#pragma once

#include <string>
#include <gtest/gtest.h>
#include <gmock/gmock.h>
#include <boost/filesystem/operations.hpp>
#include "ampere/event_stream/event_stream_proto.h"
#include "ampere/event_stream/testing/event_stream_dummy.h"

#include "ampere/sc/module.h"
#include "ampere/sc/sc.h"
#include "ampere/sc/testing/test.h"
#include "ampere/sc/testing/clock_dummy.h"
#include "ampere/sc/testing/test_interfaces.h"

#ifndef TEST_FIXTURE_DIRECTORY
#define TEST_FIXTURE_DIRECTORY SOURCE_DIRECTORY "/test"
#endif

namespace bfs = boost::filesystem;

namespace ampere::sc {

/** A utility module for testing.
 *
 *  Sigh. So the intention TestModule was that it acts like a Google Test fixture AND a Module
 *  at the same time. This was to make it possible to write a test where your test is just like
 *  any other parent module and you fill it will child modules and connect them up.
 *
 *  I have finally given up on this idea, because of the following unresolvable conflict:
 *
 *    1) A Google Test must be constructible with no arguments.
 *    2) A SystemC module requires a constructor argument *at the leaf level* (not a parent
 *       class) to work.
 *
 *  To date, we have been limping along with a TestModule that kinda-sorta acted like a Module
 *  but wasn't really. Or, technically, it was a Module instance but not present in the SystemC
 *  module hierarchy like it should be. I finally convinced myself of this while debugging some
 *  new tests on the clock crossing queue.
 *
 *  I moved the SystemC context setup/teardown into the more basic Test class, however, I have
 *  not (yet) tried to replace all instances of TestModule with Test. The reason is because it
 *  does provide some module-like services that Test does not: it creates an event stream, for
 *  example, so you can count events, and it can take a list of parameter settings in its
 *  constructor, and it has replicas of the "make" routines for creating transactions. It's not
 *  clear to me if this should be a default part of Test or if it deserves to stay in its own
 *  class. In either case, "TestModule" is probably the wrong name for this thing since it isn't
 *  really a module anymore.
 */
class TestModule : public Test
{
public:
    TestModule() : TestModule({}) {}

    TestModule(std::initializer_list<std::pair<std::string, sc::json>> settings)
    {
        get_environment()->parameters.add_settings(settings, "unit test");
        get_environment()->event_stream = std::make_unique<event_stream::EventStreamDummy>(nullptr);
    }

    static std::string testfixture(std::string filename)
    {
        return (bfs::path(TEST_FIXTURE_DIRECTORY) / filename).string();
    }

    static std::string testconfig(std::string filename) { return (bfs::path(MODEL_CONFIG_TREE) / filename).string(); }

    std::uint64_t event_count(const char* event_name)
    {
        auto es = dynamic_cast<event_stream::EventStreamDummy*>(get_environment()->event_stream.get());
        return es ? es->get_event_count(event_name) : 0;
    }

    std::unique_ptr<event_stream::EventStreamProto> open_event_stream()
    {
        const ::testing::TestInfo* const test_info = ::testing::UnitTest::GetInstance()->current_test_info();
        auto event_stream_filename = std::string(test_info->name()) + ".es";
        std::replace(event_stream_filename.begin(), event_stream_filename.end(), '/', '_');
        std::cout << event_stream_filename << std::endl;
        return std::make_unique<event_stream::EventStreamProto>(event_stream_filename);
    }

    event_stream::EventStream* get_event_stream() { return get_environment()->event_stream.get(); }

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

    void throw_error(std::string message, const char* file, int line)
    {
        throw std::runtime_error{fmt::format("Error in TestModule: {} at {}:{}", message, file, line)};
    }
};

class IntegrationTest : public ::testing::Test, public event_stream::EventAnnouncer
{
protected:
    IntegrationTest() : IntegrationTest{1_ns} {}

    IntegrationTest(sc_core::sc_time clock_period) : IntegrationTest{clock_period, {}} {}

    IntegrationTest(std::initializer_list<std::pair<std::string, sc::json>> settings) : IntegrationTest{1_ns, settings}
    {
    }

    IntegrationTest(sc_core::sc_time clock_period, std::initializer_list<std::pair<std::string, sc::json>> settings)
        : ::testing::Test()
    {
        old_context = sc_core::sc_curr_simcontext;
        sc_core::sc_curr_simcontext = new_context = new sc_core::sc_simcontext;
        Environment::set_root(std::make_unique<Environment>("testing"));
        clock = new Clock("clock", clock_period);
        get_environment()->parameters.add_settings(settings, "unit test");
        get_environment()->event_stream = std::make_unique<event_stream::EventStreamDummy>(this);
    }

    ~IntegrationTest() override
    {
        Clock::set_root(nullptr);
        Environment::set_root(nullptr);
        delete clock;
        delete new_context;
        sc_core::sc_curr_simcontext = old_context;
    }

    Environment* get_environment() { return Environment::get_root(); }

    void post_event(const std::string& event_name, std::uint64_t time) override {}
    void add_int_data(const std::string& event_name,
                      int id,
                      const std::string& data_type_name,
                      std::int64_t value,
                      std::uint64_t time) override
    {
        if (event_name == "sys.soc_0.core_0.gpc.pmu" && data_type_name == "sys.soc_0.core_0.gpc.event" && value == 34) {
            // 34 is the decimal PMU event counter for mispredicted retired
            // branches, so we use it here to differentiate mispredicted
            // branches from other PMU events posted by the GPC
            mispredicted_branches++;
        }
    }
    void add_uint_data(const std::string& event_name,
                       int id,
                       const std::string& data_type_name,
                       std::uint64_t value) override
    {
        if (event_name == "sys.soc_0.core_0.gpc.start_measurement"
            && data_type_name == "sys.soc_0.core_0.gpc.cycle_count")
        {
            start_cycle = value;
        } else if (event_name == "sys.soc_0.core_0.gpc.stop_measurement"
                   && data_type_name == "sys.soc_0.core_0.gpc.cycle_count")
        {
            stop_cycle = value;
        } else if (event_name == "sys.soc_0.core_0.gpc.stop_max_instruction"
                   && data_type_name == "sys.soc_0.core_0.gpc.cycle_count")
        {
            stop_cycle = value;
        }
    }
    void add_string_data(const std::string& event_name,
                         int id,
                         const std::string& data_type_name,
                         const std::string& value) override
    {
    }

    std::uint64_t event_count(const char* event_name)
    {
        auto es = dynamic_cast<event_stream::EventStreamDummy*>(get_environment()->event_stream.get());
        return es ? es->get_event_count(event_name) : 0;
    }

    void run_for(const sc_core::sc_time& duration)
    {
        if (!get_environment()->parameters.check_all_matched())
            AMPERE_ERROR("Not all parameters were matched. Giving up.");

        sc_core::sc_start(duration);

        if (sc_core::sc_get_status() != sc_core::SC_STOPPED)
            sc_core::sc_stop();
    }

    void warmup_for(const sc_core::sc_time& duration)
    {
        if (!get_environment()->parameters.check_all_matched()) {
            std::cerr << "\nParameters:\n";
            get_environment()->parameters.dump_parameters(std::cerr);
            AMPERE_ERROR("Not all parameters were matched. Giving up.");
        }

        sc_core::sc_start(duration);
    }

    float simulated_ipc()
    {
        auto end_instructions = event_count("sys.soc_0.core_0.gpc.instruction_commit");
        auto cycles = stop_cycle - start_cycle;
        return (float)(end_instructions) / (float)cycles;
    }

    std::uint64_t retired_mispredicted_branches() const { return mispredicted_branches; }

    Clock* clock;

private:
    sc_core::sc_simcontext* old_context{nullptr};
    sc_core::sc_simcontext* new_context{nullptr};
    std::uint64_t start_cycle{0};
    std::uint64_t stop_cycle{0};
    std::uint64_t mispredicted_branches{0};
};

template<typename T>
struct ModuleWrapper : Module
{
    T child;
    template<typename... Args>
    ModuleWrapper(const sc_core::sc_module_name& name, Args&&... args)
        : Module(name), child(std::forward<Args>(args)...)
    {
    }

    const T* operator->() const { return &child; }
    T* operator->() { return &child; }
};

} // namespace ampere::sc

/*
 * vim: sw=4:ts=4:et:si:cindent:cino=\:0,(0,g0,t0:
 */
