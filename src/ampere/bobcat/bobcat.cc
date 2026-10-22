//
// Copyright (c) 2019 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//
#include <boost/filesystem/path.hpp>
#include <limits>

#include "ampere/archx/driver.h"
#include "ampere/bobcat/bobcat_bpu.h"
#include "ampere/bobcat/version.h"
#include "ampere/event_stream/event_stream_proto.h"
#include "ampere/sc/debug_registry.h"

namespace fs = boost::filesystem;
using namespace ampere::sc;

std::string extract_trace_name(fs::path p)
{
    if (p.has_extension()) {
        while (p.extension() != ".is")
            p = p.stem();

        if (p.has_extension())
            p = p.stem();
    }

    return p.native();
}

int sc_main(int argc, char** argv)
{
    Clock clock{"main", {1, sc_core::SC_NS}};
    Environment env{"system"};
    int status = 0;

    fmt::print(std::cerr, "({}, {}, {})\n", AMPERE_BOBCAT_GIT_SHA, AMPERE_BOBCAT_GIT_REF, AMPERE_BOBCAT_GIT_STATE);
    auto command_line = fmt::format("{}", fmt::join(argv, argv + argc, " "));

    env.parameters.load_from_args(argc, argv);

    Param<float> max_nanoseconds{
        env, "max_nanoseconds", "time limit for simulation", std::numeric_limits<float>::max()};
    Param<std::string> instruction_stream_name{env, "is", "path to an instruction stream", ""};
    Param<std::string> branch_stream_name{env, "bs", "path to a branch stream", ""};
    Param<int> event_stream_fd{env, "event_stream_fd", "file descriptor for output event stream", -1};
    Param<int> resp_stream_fd{env, "resp_stream_fd", "file descriptor for input response stream", -1};
    Param<std::string> event_stream_name{env, "event_stream_name", "filename for output event stream", ""};
    Param<bool> event_stream_always{env, "event_stream_always", "Always produce events, even during warm-up", false};
    Param<std::string> csv{env, "csv", "output branch statistics to a CSV file", ""};
    Param<bool> quiet{env, "quiet", "don't print summary (useful for batch mode)", false};
    Param<bool> dump_params{env, "dump_params", "Dump parameters (defined by modules), do not simulate", false};
    Param<bool> dump_settings{
        env, "dump_settings", "Dump settings (defined by config files/arguments), do not simulate", false};

    if (resp_stream_fd != -1 && event_stream_fd != -1)
        env.event_stream = std::make_unique<ampere::event_stream::EventStreamProto>(event_stream_fd, resp_stream_fd);
    else if (event_stream_fd != -1)
        env.event_stream = std::make_unique<ampere::event_stream::EventStreamProto>(event_stream_fd);
    else if (!event_stream_name.val().empty())
        env.event_stream = std::make_unique<ampere::event_stream::EventStreamProto>(event_stream_name);

    if (!event_stream_always)
        env.event_stream->disable();

    sc_core::sc_report_handler::set_actions(sc_core::SC_INFO, sc_core::SC_DISPLAY);

    DebugRegistry::setup_main(env);

    ampere::bobcat::BobcatTop top{"top", &clock, &env};

    if (env.parameters.help_requested() || dump_params) {
        env.parameters.dump_parameters_verbosely(std::cout);
        exit(1);
    }

    if (dump_settings) {
        env.parameters.dump_settings(std::cout);
        exit(1);
    }

    if (!env.parameters.check_all_matched())
        throw std::runtime_error{"Not all parameters were matched. Giving up."};

    if (instruction_stream_name.val().empty() == branch_stream_name.val().empty())
        AMPERE_ERROR("Specify either an instruction or branch stream, but not both (use `-is <file>` or `-bs <file>`)");

    std::unique_ptr<ampere::archx::Driver> driver;
    if (!instruction_stream_name.val().empty())
        driver = ampere::archx::makeDriver(instruction_stream_name, 0);
    else
        driver = ampere::archx::makeBSDriver(branch_stream_name);
    top.set_driver(*driver);

    env.parameters.values_to_event_stream();
    env.event_stream->set_string_parameter(
        "ampere_bobcat_git_sha", "The SHA hash of the current git head revision", AMPERE_BOBCAT_GIT_SHA);
    env.event_stream->set_string_parameter(
        "ampere_bobcat_git_ref", "The refspec of the current git head revision", AMPERE_BOBCAT_GIT_REF);
    env.event_stream->set_string_parameter(
        "ampere_bobcat_git_state",
        "Whether the repository was clean or dirty (exit code of `git diff-index --quiet HEAD --`)",
        AMPERE_BOBCAT_GIT_STATE);
    env.event_stream->set_string_parameter(
        "ampere_bobcat_command_line", "The original bobcat command-line", command_line);

    AMPERE_INFO("End of elaboration. Starting simulation.");

    try {
        sc_core::sc_start({max_nanoseconds, sc_core::SC_NS});
    } catch (std::runtime_error& err) {
        std::cerr << err.what() << std::endl;
        status = -1;
    }

    if (sc_core::sc_get_status() != sc_core::SC_STOPPED)
        sc_core::sc_stop();

    auto trace_name = extract_trace_name(fs::path{instruction_stream_name});

    if (!quiet)
        top.print_stats(trace_name);

    if (!csv.val().empty()) {
        std::ofstream os{csv};
        top.print_stats_csv(trace_name, os);
    }

    return status;
}

/*
 * vim: sw=4:ts=4:et:si:cindent:cino=\:0,(0,g0,t0:
 */
