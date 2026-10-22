//
// Copyright (c) 2018-2025 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//
#include <format>
#include <iostream>
#include <string>
#include "ampere/kestrel/aice-sim/version.h"
#include "ampere/kestrel/aice-sim/aice_sim.h"

using namespace ampere;

int sc_main(int argc, char** argv)
{
    sc::Clock clock("main", {1, sc_core::SC_NS});
    sc_core::sc_report_handler::set_handler(ampere::sc::report_handler);

    sc::Environment env{"system"};
    env.parameters.load_from_args(argc, argv);
    sc::Param<sc_core::sc_time> max_time{env, "max_time", "time limit for simulation", sc_core::sc_max_time()};
    sc_core::sc_time sim_time = max_time;
    sc::Param<int> pdbg{env, "dbg", "Turn on debug messages, bitmask", 0};
    sc::Param<int> event_stream_fd{env, "event_stream_fd", "file descriptor for output event stream", -1};
    sc::Param<int> resp_stream_fd{env, "resp_stream_fd", "file descriptor for input response stream", -1};
    sc::Param<std::string> event_stream_name{env, "event_stream_name", "filename for output event stream", "events.es"};
    sc::Param<bool> event_stream_always{
        env, "event_stream_always", "always capture the event stream, even during warmup", true};
    sc::Param<unsigned> simulation_cycles{env, "simulation_cycles", "Simulation cycles", 10};

    std::cerr << std::format("({}, {}, {})\n", AMPERE_CORE_GIT_SHA, AMPERE_CORE_GIT_REF, AMPERE_CORE_GIT_STATE);
    std::string command_line;
    for (int ii = 0; ii < argc; ++ii) {
        command_line += std::string(" ") + std::string(argv[ii]);
    }

    if (resp_stream_fd != -1 && event_stream_fd != -1) {
        env.event_stream = std::make_unique<event_stream::EventStreamProto>(event_stream_fd, resp_stream_fd);
    } else if (event_stream_fd != -1) {
        env.event_stream = std::make_unique<event_stream::EventStreamProto>(event_stream_fd);
    } else if (!event_stream_name.val().empty()) {
        env.event_stream = std::make_unique<event_stream::EventStreamProto>(event_stream_name);
    }

    ampere::kestrel::aice::Top top("top", &clock, &env);

    // dump knobs with -help
    if (env.parameters.help_requested()) {
        env.parameters.dump_parameters_verbosely(std::cout);
        exit(EXIT_FAILURE);
    }

    env.parameters.values_to_event_stream();
    env.event_stream->set_string_parameter(
        "ampere_core_git_sha", "The SHA hash of the current git head revision", AMPERE_CORE_GIT_SHA);
    env.event_stream->set_string_parameter(
        "ampere_core_git_ref", "The refspec of the current git head revision", AMPERE_CORE_GIT_REF);
    env.event_stream->set_string_parameter(
        "ampere_core_git_state", "Whether the repository was clean or dirty", AMPERE_CORE_GIT_STATE);
    env.event_stream->set_string_parameter(
        "ampere_core_command_line", "The original amperesys command-line", command_line);

    env.event_stream->disable();
    ampere::kestrel::aice::dbg = pdbg;

    bool error_occurred = false;
    try {
        sc_core::sc_start(sc_core::SC_ZERO_TIME);
        AMPERE_INFO("End of elaboration. Starting simulation.");
        if (event_stream_always) {
            env.event_stream->enable();
        }
        sc_core::sc_start(sim_time);
    } catch (std::runtime_error& err) {
        std::cerr << std::format("{}\n", err.what());
        error_occurred = true;
    }
    if (sc_core::sc_get_status() != sc_core::SC_STOPPED) {
        sc_core::sc_stop();
    }
    size_t sc_error_count = sc_core::sc_report_handler::get_count(sc_core::SC_ERROR);
    if (sc_error_count > 0) {
        return sc_error_count;
    }
    return error_occurred ? 1 : 0;
}
