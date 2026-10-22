//
// Copyright(c) 2022 Ampere Computing. All Rights Reserved.
// SPDX-License-Identifier: BSD-3-Clause
//
#include <fcntl.h>
#include <fmt/ostream.h>
#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <string>

#include "ampere/evp/plugin.h"
#include "ampere/evp/plugins/capture.h"

namespace ampere::evp {

AMPERE_EVP_PLUGIN_FROM(TimeSkip, "time_skip", "Capture and adjust time when skipping events", CaptureBase)
{
public:
    TimeSkip(ProcessorIfc & proc_ifc, Args & args);

    static void help(int argc, const char** argv);
    void define_event(const Definition& event_def) override;
    void process_event(const Event& event) override;
    void start_simulation() override;
    void report(MetricTableTimeSeries & ts) override;

private:
    std::string event;
    int def_id;
    uint64_t time_adjustment{0};
    uint64_t last_skip_time_seen{0};

    bool skipping_time{false};
};

TimeSkip::TimeSkip(ProcessorIfc& proc_ifc, Args& args) : CaptureBase(proc_ifc, args)
{
    while (args) {
        if (args.pop("--event", event)) {
            skipping_time = true;
        } else {
            break;
        }
    }

    args.done();
}

void TimeSkip::help(int argc, const char** argv)
{
    print_help(argv[0], "skip_time", "<output filename> [-f|--force] [--event <event>]", R"(Arguments:

    --force, -f                Overwrite output file if it exists
    --event <event>            Skip the time slice when the specified event is seen, subsequent times will be adjusted
    --help, -h                 This help message.
)");
}

void TimeSkip::define_event(const Definition& event_def)
{
    if (skipping_time && event_def.name() == event) {
        def_id = event_def.id();
        return;
    }

    CaptureBase::define_event(event_def);
}

void TimeSkip::start_simulation()
{
    CaptureBase::start_simulation();

    if (skipping_time && def_id == 0)
        throw std::runtime_error("Skip time event not found");
}

Event* adjust_event_time(const Event& event, uint64_t adjustment)
{
    auto* adjusted = new Event(event);
    adjusted->set_time(adjusted->time() - adjustment);
    return adjusted;
}

void TimeSkip::process_event(const Event& event)
{
    if (skipping_time) {
        if (event.definition_id() == def_id) {
            if (!last_skip_time_seen)
                last_skip_time_seen = event.time();
            return;
        }
        if (last_skip_time_seen) {
            if (static_cast<uint64_t>(event.time()) == last_skip_time_seen)
                return;

            time_adjustment += event.time() - last_skip_time_seen;
            last_skip_time_seen = 0;
        }
    }

    const auto* record_event = time_adjustment ? adjust_event_time(event, time_adjustment) : &event;
    CaptureBase::process_event(*record_event);
    if (time_adjustment)
        delete record_event;
}

void TimeSkip::report(MetricTableTimeSeries& ts)
{
    CaptureBase::report(ts);
    if (logging && skipping_time)
        fmt::print(std::cerr, "Skipped a total of {}ps via {}\n", time_adjustment, event);
}

} // namespace ampere::evp
