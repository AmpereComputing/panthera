//
// Copyright (c) 2018-2020 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//

#include "ampere/event_stream/event_stream_proto.h"
#include "ampere/sc.h"
#include "ampere/core/core.h"
#include "ampere/soc/soc.h"
#include "ampere/soc/version.h"
#include <optional>

using namespace ampere;
using namespace ampere::sc;
using namespace ampere::core;
using namespace ampere::soc;

int sc_main(int argc, char** argv)
{
    sc_core::sc_report_handler::set_actions(sc_core::SC_ERROR, sc_core::SC_DISPLAY | sc_core::SC_STOP);
    sc_core::sc_report_handler::set_handler(ampere::sc::report_handler);

    bool version_requested = false;
    for (int c = 0; c < argc; c++) {
        if (!strcmp(argv[c], "-v") || !strcmp(argv[c], "-version") || !strcmp(argv[c], "--version")) {
            version_requested = true;
            break;
        }
    }

    fmt::print(std::cerr, "\n({}, {}, {})\n", AMPERE_SOC_GIT_SHA, AMPERE_SOC_GIT_REF, AMPERE_SOC_GIT_STATE);
    auto command_line = fmt::format("{}", fmt::join(argv, argv + argc, " "));

    if (version_requested)
        exit(EXIT_SUCCESS);

    auto& env = *Environment::get_root();
    env.parameters.load_from_args(argc, argv);

    auto& clock = *Clock::get_root();

    Param<float> max_nanoseconds{env, "max_nanoseconds", "time limit for simulation", FLT_MAX};
    Param<std::optional<Cycle>> trap_nanoseconds{env, "trap_nanoseconds", "pause simulation at nanosecond", {}};
    Param<std::optional<Cycle>> trap_cycles{env, "trap_cycles", "pause simulation at cycle", {}};
    Param<int> event_stream_fd{env, "event_stream_fd", "file descriptor for output event stream", -1};
    Param<int> resp_stream_fd{env, "resp_stream_fd", "file descriptor for input response stream", -1};
    Param<std::string> event_stream_name{env, "event_stream_name", "filename for output event stream", ""};
    Param<bool> event_stream_always{
        env, "event_stream_always", "always capture the event stream, even during warmup", false};
    Param<bool> run_always{env, "run_always", "clock always ticks each runnable", false};

    DebugRegistry::setup_main(env);

    if (resp_stream_fd != -1 && event_stream_fd != -1)
        env.event_stream = std::make_unique<event_stream::EventStreamProto>(event_stream_fd, resp_stream_fd);
    else if (event_stream_fd != -1)
        env.event_stream = std::make_unique<event_stream::EventStreamProto>(event_stream_fd);
    else if (!event_stream_name.val().empty())
        env.event_stream = std::make_unique<event_stream::EventStreamProto>(event_stream_name);

    Random::seed(42);

    Soc soc("soc");

    if (env.parameters.help_requested()) {
        env.parameters.dump_parameters_verbosely(std::cout);
        exit(1);
    }

    if (!env.parameters.check_all_matched())
        throw std::runtime_error{"Not all parameters were matched. Giving up."};

    env.parameters.values_to_event_stream();
    env.event_stream->set_string_parameter(
        "ampere_soc_git_sha", "The SHA hash of the current git head revision", AMPERE_SOC_GIT_SHA);
    env.event_stream->set_string_parameter(
        "ampere_soc_git_ref", "The refspec of the current git head revision", AMPERE_SOC_GIT_REF);
    env.event_stream->set_string_parameter(
        "ampere_soc_git_state",
        "Whether the repository was clean or dirty (exit code of `git diff-index --quiet HEAD --`)",
        AMPERE_SOC_GIT_STATE);
    env.event_stream->set_string_parameter("ampere_soc_command_line", "The original command-line", command_line);

    if (!event_stream_always) {
        env.event_stream->disable();
    }

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
        // core.get_clock()->trap_cycle(*trap_cycles); // TODO
#else
        std::cerr << "trap_cycles only supported in debug builds\n";
        exit(1);
#endif
    }

    clock.run_always(run_always);

    AMPERE_INFO("End of elaboration. Starting simulation.");

    try {
        sc_core::sc_start({max_nanoseconds, sc_core::SC_NS});
    } catch (std::runtime_error& err) {
        std::cerr << err.what() << std::endl;
    }

    if (sc_core::sc_get_status() != sc_core::SC_STOPPED)
        sc_core::sc_stop();

    return sc_core::sc_report_handler::get_count(sc_core::SC_ERROR);
}

/*
 * vim: sw=4:ts=4:et:si:cindent:cino=\:0,(0,g0,t0:
 */

