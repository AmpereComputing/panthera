//
// Copyright (c) 2022 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//
#include "ampere/event_stream/event_stream_proto.h"
#include "ampere/memory/memory.h"
#include "ampere/mesh/mesh_config.h"
#include "ampere/mesh/injector/chi_injector.h"
#include "ampere/mesh/sam.h"
#include "ampere/sys/version.h"
#include <fmt/format.h>

using namespace ampere;
using namespace ampere::sc;
using namespace ampere::mesh;

namespace ampere {

class InjMem : public Module, public MeshConfigIfc
{
public:
    explicit InjMem(const sc_core::sc_module_name& name) : Module{name}
    {
        injector.get_mesh_port().connect(mc.get_mesh_port(), "core_to_mem");
    }

    int data_bus_width() const override { return 256; }
    int channel(const chi::NodeId id) const override { return 0; }

private:
    SystemAddressMap system_address_map{"system_address_map"};
    CHIInjector injector{"core_0"};
    memory::Memory mc{"memory_0"};
};

} // namespace ampere

int sc_main(int argc, char** argv)
{
#ifdef NDEBUG
    sc_core::sc_report_handler::set_actions(sc_core::SC_ERROR, sc_core::SC_DISPLAY | sc_core::SC_STOP);
    sc_core::sc_report_handler::set_actions(sc_core::SC_FATAL,
                                            sc_core::SC_DISPLAY | sc_core::SC_STOP | sc_core::SC_ABORT);
#endif
    sc_core::sc_report_handler::set_handler(ampere::sc::report_handler);

    bool version_requested = false;
    for (int c = 0; c < argc; c++) {
        if (!strcmp(argv[c], "-v") || !strcmp(argv[c], "-version") || !strcmp(argv[c], "--version")) {
            version_requested = true;
            break;
        }
    }

    fmt::print(std::cerr, "({}, {}, {})\n", AMPERE_CORE_GIT_SHA, AMPERE_CORE_GIT_REF, AMPERE_CORE_GIT_STATE);

    if (version_requested)
        exit(EXIT_SUCCESS);

    auto& env = *Environment::get_root();
    env.parameters.load_from_args(argc, argv);

    auto& clock = *Clock::get_root();

    Param<sc_core::sc_time> max_time{env, "max_time", "time limit for simulation", sc_core::sc_max_time()};
    Param<float> trap_nanoseconds{env, "trap_nanoseconds", "pause simulation at nanosecond", -1.0};
    Param<float> trap_cycles{env, "trap_cycles", "pause simulation at cycle", -1.0};
    Param<int> event_stream_fd{env, "event_stream_fd", "file descriptor for output event stream", -1};
    Param<std::string> event_stream_name{env, "event_stream_name", "filename for output event stream", ""};
    Param<bool> event_stream_always{
        env, "event_stream_always", "always capture the event stream, even during warmup", false};
    Param<bool> run_always{env, "run_always", "clock always ticks each runnable", false};

    if (event_stream_fd != -1)
        env.event_stream = std::make_unique<event_stream::EventStreamProto>(event_stream_fd);
    else if (!event_stream_name.val().empty())
        env.event_stream = std::make_unique<event_stream::EventStreamProto>(event_stream_name);

    Random::seed(42);

    env.parameters.add_setting("sys.core_0.sam.system_address_map.connect", "sys.system_address_map", "ampereinjmem");
    env.parameters.add_setting("~.*core_to_mem.*capacity", 8, "ampereinjmem");

    ampere::InjMem sys("sys");

    if (!env.parameters.check_all_matched())
        throw std::runtime_error{"Not all parameters were matched. Giving up."};

    if (!event_stream_always) {
        env.event_stream->disable();
        Event::disable_all();
    }

    if (trap_nanoseconds > 0.0) {
#if defined(AMPERE_TRAP_ON_CYCLE)
        clock.trap_cycle(trap_nanoseconds);
#else
        std::cerr << "trap_nanoseconds only supported in debug builds\n";
        exit(1);
#endif
    }
    if (trap_cycles > 0.0) {
#if defined(AMPERE_TRAP_ON_CYCLE)
        sys.get_clock()->trap_cycle(trap_cycles);
#else
        std::cerr << "trap_cycles only supported in debug builds\n";
        exit(1);
#endif
    }

    clock.run_always(run_always);

    bool error_occurred = false;
    try {
        sc_core::sc_start(max_time);
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
