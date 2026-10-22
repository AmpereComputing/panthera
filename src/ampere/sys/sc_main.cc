//
// Copyright (c) 2018-2021 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//
#include "ampere/event_stream/event_stream_proto.h"
#include "ampere/sc/sc.h"
#include "ampere/sc/debug_registry.h"
#include "ampere/sys/sys.h"
#include "ampere/sys/version.h"
#include <fmt/format.h>
#include "sysc/kernel/sc_spawn.h"
#include "sysc/utils/sc_report.h"
#include <optional>

using namespace ampere;
using namespace ampere::sc;

void check_all_bandwidths(const sc_core::sc_object& obj)
{
    std::string conn_kind{"ampere::sc::Connection"};

    if (conn_kind == obj.kind()) {
        auto conn = dynamic_cast<const ampere::sc::Connection*>(&obj);
        if (conn->validate_credits
            && conn->validate_credits() < (unsigned long)conn->validate_latency + conn->validate_return_latency)
            AMPERE_WARNING("{}: {} credits in pool {} might not cover latency {} and return latency {} in FIFO",
                           obj.name(),
                           conn->validate_credits(),
                           conn->validate_credits_pool_name(),
                           conn->validate_latency,
                           conn->validate_return_latency);
    } else {
        for (auto& child : obj.get_child_objects())
            check_all_bandwidths(*child);
    }
}

void dump_clock_info(sc_core::sc_object* obj)
{
    auto mod = dynamic_cast<sc::Module*>(obj);

    if (mod) {
        fmt::print("{}: {} ({})\n", mod->name(), mod->get_clock()->name(), mod->get_clock()->get_period());

        for (auto& child : obj->get_child_objects())
            dump_clock_info(child);
    }
}

#ifdef USE_BOOST_SERIALIZATION
void do_save_for_warmup(const sc_core::sc_object& obj, boost::archive::text_oarchive& oa)
{
    auto mod = dynamic_cast<const Module*>(&obj);

    if (mod)
        mod->serialize(oa);

    for (auto& child : obj.get_child_objects())
        do_save_for_warmup(*child, oa);
}

void setup_save_for_warmup(const std::string& save_for_warmup, sc_core::sc_event& save_for_warmup_event)
{
    auto pos = save_for_warmup.find(':');

    if (pos == std::string::npos)
        AMPERE_FATAL("save_for_warmup option should look like \"<time spec>:<file name>\"");

    auto save_at = from_json_T<sc_core::sc_time>(save_for_warmup.substr(0, pos));
    std::string save_filename = save_for_warmup.substr(pos + 1);

    sc_core::sc_spawn_options opt;
    opt.dont_initialize();
    opt.set_sensitivity(&save_for_warmup_event);

    sc_core::sc_spawn(
        [save_filename]() {
            AMPERE_INFO("Saving warmup information to {}", save_filename);
            std::ofstream of{save_filename};
            boost::archive::text_oarchive oa{of};
            for (auto& top : sc_core::sc_get_top_level_objects()) {
                do_save_for_warmup(*top, oa);
            }
            oa << std::string{"@END"};
        },
        "save_for_warmup_thread",
        &opt);

    save_for_warmup_event.notify(save_at);
}

void do_restore_for_warmup(const std::string& restore_for_warmup)
{
    AMPERE_INFO("Loading warmup information from {}", restore_for_warmup);

    std::ifstream ifs{restore_for_warmup};
    boost::archive::text_iarchive ia{ifs};

    while (true) {
        std::string name;
        ia >> name;

        if (name == "@END")
            break;

        auto obj = sc_core::sc_find_object(name.c_str());
        AMPERE_ASSERT(obj, "could not find object \"{}\" to restore", name);

        auto mod = dynamic_cast<Module*>(obj);
        AMPERE_ASSERT(mod, "restored object \"{}\" is not a Module", name);

        mod->deserialize(ia);
    }
}
#else // ! USE_BOOST_SERIALIZAION
void setup_save_for_warmup(const std::string& save_for_warmup, sc_core::sc_event& save_for_warmup_event)
{
    AMPERE_FATAL("not using Boost::serialization; can't do save-for-warmup");
}

void do_restore_for_warmup(const std::string& restore_for_warmup)
{
    AMPERE_FATAL("not using Boost::serialization; can't do restore-for-warmup");
}
#endif

int sc_main(int argc, char** argv)
{
    // Disable throw for non-debug only
#ifdef NDEBUG
    sc_core::sc_report_handler::set_actions(sc_core::SC_ERROR, sc_core::SC_DISPLAY | sc_core::SC_STOP);
    sc_core::sc_report_handler::set_actions(sc_core::SC_FATAL,
                                            sc_core::SC_DISPLAY | sc_core::SC_STOP | sc_core::SC_ABORT);
#endif
    sc_core::sc_report_handler::set_handler(ampere::sc::report_handler);
    sc_core::sc_event save_for_warmup_event;

    bool version_requested = false;
    for (int c = 0; c < argc; c++) {
        if (!strcmp(argv[c], "-v") || !strcmp(argv[c], "-version") || !strcmp(argv[c], "--version")) {
            version_requested = true;
            break;
        }
    }

    fmt::print(std::cerr, "({}, {}, {})\n", AMPERE_CORE_GIT_SHA, AMPERE_CORE_GIT_REF, AMPERE_CORE_GIT_STATE);
    std::string command_line = fmt::format("{}", fmt::join(argv, argv + argc, " "));

    if (version_requested)
        exit(EXIT_SUCCESS);

    auto& env = *Environment::get_root();
    env.parameters.load_from_args(argc, argv);

    auto& clock = *Clock::get_root();

    Param<sc_core::sc_time> max_time{env, "max_time", "time limit for simulation", sc_core::sc_max_time()};
    Param<std::optional<sc_core::sc_time>> warmup_time{
        env,
        "warmup_time",
        "enable events after this amount of simulation time "
        "(default is to not use warmup time, but instead use cues from a trace)",
        {}};
    Param<std::optional<Cycle>> trap_nanoseconds{env, "trap_nanoseconds", "pause simulation at nanosecond", {}};
    Param<std::optional<Cycle>> trap_cycles{env, "trap_cycles", "pause simulation at cycle", {}};
    Param<int> event_stream_fd{env, "event_stream_fd", "file descriptor for output event stream", -1};
    Param<int> resp_stream_fd{env, "resp_stream_fd", "file descriptor for input response stream", -1};
    Param<std::string> event_stream_name{env, "event_stream_name", "filename for output event stream", ""};
    Param<bool> event_stream_always{
        env, "event_stream_always", "always capture the event stream, even during warmup", false};
    Param<bool> run_always{env, "run_always", "clock always ticks each runnable", false};
    Param<bool> dump_params{env, "dump_params", "Dump parameters (defined by modules), do not simulate", false};
    Param<bool> dump_settings{
        env, "dump_settings", "Dump settings (defined by config files/arguments), do not simulate", false};
    Param<bool> dump_clocks{env, "dump_clocks", "Dump all modules with their clocks", false};
    Param<std::string> save_for_warmup{
        env, "save_for_warmup", "Save state to a file for later warmup (format: \"<time spec>:<filename>\")", ""};
    Param<std::string> restore_for_warmup{env, "restore_for_warmup", "Restore saved state from a file", ""};

    if (resp_stream_fd != -1 && event_stream_fd != -1)
        env.event_stream = std::make_unique<event_stream::EventStreamProto>(event_stream_fd, resp_stream_fd);
    else if (event_stream_fd != -1)
        env.event_stream = std::make_unique<event_stream::EventStreamProto>(event_stream_fd);
    else if (!event_stream_name.val().empty())
        env.event_stream = std::make_unique<event_stream::EventStreamProto>(event_stream_name);

    sc::DebugRegistry::setup_main(env);

    Random::seed(42);

    Sys sys("sys");

    if (env.parameters.help_requested() || dump_params) {
        env.parameters.dump_parameters_verbosely(std::cout);
        exit(1);
    }

    if (dump_settings) {
        env.parameters.dump_settings(std::cout);
        exit(1);
    }

    if (dump_clocks) {
        auto tops = sc_core::sc_get_top_level_objects();

        for (auto& top : tops) {
            dump_clock_info(top);
        }

        exit(1);
    }

    if (!save_for_warmup.val().empty())
        setup_save_for_warmup(save_for_warmup, save_for_warmup_event);

    if (!restore_for_warmup.val().empty())
        do_restore_for_warmup(restore_for_warmup);

    if (!env.parameters.check_all_matched())
        throw std::runtime_error{"Not all parameters were matched. Giving up."};

    env.parameters.values_to_event_stream();
    env.event_stream->set_string_parameter(
        "ampere_core_git_sha", "The SHA hash of the current git head revision", AMPERE_CORE_GIT_SHA);
    env.event_stream->set_string_parameter(
        "ampere_core_git_ref", "The refspec of the current git head revision", AMPERE_CORE_GIT_REF);
    env.event_stream->set_string_parameter(
        "ampere_core_git_state",
        "Whether the repository was clean or dirty (exit code of `git diff-index --quiet HEAD --`)",
        AMPERE_CORE_GIT_STATE);
    env.event_stream->set_string_parameter(
        "ampere_core_command_line", "The original amperesys command-line", command_line);

    env.event_stream->disable();

    if (trap_nanoseconds) {
#if defined(AMPERE_TRAP_ON_CYCLE)
        clock.trap_cycle(*trap_nanoseconds);
#else
        std::cerr << "trap_nanoseconds only supported in debug builds\n";
        exit(1);
#endif
    }
    if (trap_cycles) {
#if defined(AMPERE_TRAP_ON_CYCLE)
        sys.get_clock()->trap_cycle(*trap_cycles);
#else
        std::cerr << "trap_cycles only supported in debug builds\n";
        exit(1);
#endif
    }

    clock.run_always(run_always);

    bool error_occurred = false;
    try {
        sc_core::sc_start(sc_core::SC_ZERO_TIME);
        check_all_bandwidths(sys);
        AMPERE_INFO("End of elaboration. Starting simulation.");

        if (event_stream_always || (warmup_time.val().has_value() && *warmup_time.val() == sc_core::SC_ZERO_TIME)) {
            // If we have requested the event stream to start prior to *online*
            // warmup, start it now. Note that this is intentionally after all
            // definitions should have been written to the event stream and
            // after any *offline* warmup has occurred.
            env.event_stream->enable();
        }

        sc_core::sc_time sim_time = max_time;

        // A 0 time means that we're not using warmup. I used to have this as
        // sc_core::sc_max_time(), but there appears to be a bug in SystemC
        // where if you try to construct an sc_time with the largest possible
        // uint64, you actually end up with the largest int64 time. The end
        // result is that:
        //
        //    Param<sc_time> t{"time", sc_core::sc_max_time()};
        //    t.val() != sc_core::sc_max_time() // Oops!
        //
        if (warmup_time.val().has_value() && *warmup_time.val() != sc_core::SC_ZERO_TIME) {
            AMPERE_ASSERT(max_time.val() >= *warmup_time.val(),
                          "max_time ({}) is less than warmup time ({})",
                          max_time.val(),
                          *warmup_time.val());
            sim_time = max_time.val() - *warmup_time.val();
            sc_core::sc_start(*warmup_time.val());
            if (!env.event_stream->is_enabled()) {
                env.event_stream->enable();
                AMPERE_INFO("Start performance measurement at {}", sc_core::sc_time_stamp());
            }
        }

        sc_core::sc_start(sim_time);
    } catch (std::runtime_error& err) {
        std::cerr << err.what() << std::endl;
        error_occurred = true;
    }

    if (sc_core::sc_get_status() != sc_core::SC_STOPPED)
        sc_core::sc_stop();

    size_t sc_error_count = sc_core::sc_report_handler::get_count(sc_core::SC_ERROR);
    if (sc_error_count > 0)
        return sc_error_count;
    return error_occurred ? 1 : 0;
}

/*
 * vim: sw=4:ts=4:et:si:cindent:cino=\:0,(0,g0,t0:
 */
