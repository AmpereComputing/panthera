//
// Copyright(c) 2024 Ampere Computing. All Rights Reserved.
// SPDX-License-Identifier: BSD-3-Clause
//
#include "ampere/evp/bounds.h"
#include "ampere/evp/counter.h"
#include "ampere/evp/plugin.h"
#include "ampere/evp/processor_ifc.h"

#include <cstdint>
#include <map>
#include <list>
#include <string>

namespace ampere::evp {

AMPERE_EVP_PLUGIN(Rate, "rate", "Count events with some time-slice")
{
public:
    Rate(ProcessorIfc & proc_ifc, Args & args);

    static void help(int argc, const char** argv);
    void collect_within_event(Counter * counter, const Event& event);
    void collect_within_time(uint64_t current_time, uint64_t expiry);
    void collect(MetricSeries & metrics, uint64_t trigger_time) override;
    void end_simulation() override
    {
        ended = true;
    }

private:
    void collect_within();
    std::string format_event_name(const std::string& event_name, const std::string& name, int64_t rate) const;

    bool ended = false;
    bool factored = false;
    struct Setting
    {
        int64_t previous_value = 0;
        std::unique_ptr<FactorBounds> bounds;
        explicit Setting(std::unique_ptr<FactorBounds>&& bounds) : bounds(std::move(bounds)) {}
    };
    std::list<std::pair<std::string, CounterSet>> counters;
    std::map<int, Setting> settings;
    std::map<int, std::map<int64_t, uint64_t>> histogram;
};

Rate::Rate(ProcessorIfc& proc_ifc, Args& args) : Plugin{proc_ifc}
{
    std::string name;
    std::string interval;
    args.pop(interval);

    while (args) {
        std::string arg;
        if (args.pop("--factored")) {
            factored = true;
        } else if (args.pop("-s|--suffix", name)) {
            counters.emplace_back(name, CounterSet{});
        } else if (args.pop("-e|--event", arg)) {
            if (counters.empty())
                counters.emplace_back("rate", CounterSet{});

            auto [event_name, bounds] = FactorBounds::from_event_spec(arg);
            CounterSet event_counters;
            count(event_name, &event_counters);
            for (auto id : event_counters)
                settings.emplace(id, std::move(bounds));

            counters.back().second.merge(event_counters);
        } else
            break;
    }

    args.done();

    if (is_time_spec(interval))
        at_every(interval, &Rate::collect_within_time);
    else
        on_every(interval, &Rate::collect_within_event);
}

void Rate::collect_within()
{
    for (auto& [counter_id, setting] : settings) {
        const auto& counter = get_counter(counter_id);
        auto current_value = counter.get_count();
        auto within_period = current_value - setting.previous_value;
        if (setting.bounds)
            within_period = setting.bounds->adjust(within_period);

        setting.previous_value = current_value;
        ++histogram[counter_id][within_period];
    }
}

void Rate::collect_within_event(Counter* counter, const Event& event)
{
    collect_within();
}

void Rate::collect_within_time(uint64_t current_time, uint64_t expiry)
{
    collect_within();
}

std::string Rate::format_event_name(const std::string& event_name, const std::string& name, int64_t rate) const
{
    return factored ? fmt::format("{}/{}:{}", event_name, name, rate) : fmt::format("{}.{}.{}", event_name, name, rate);
}

void Rate::collect(MetricSeries& metrics, uint64_t trigger_time)
{
    if (ended)
        collect_within();

    for (auto& [name, name_counters] : counters) {
        for (auto counter_id : name_counters) {
            const auto& counter = get_counter(counter_id);
            const auto& event_name = get_definition(counter.event_definition_id).name();

            for (auto [rate, count] : histogram[counter_id])
                metrics[format_event_name(event_name, name, rate)] = count;
        }
    }
}

void Rate::help(int argc, const char** argv)
{
    print_help(argv[0], "rate", "<interval> [--factored] [-n, --name <name> ] [-e|--event <event>]", R"(Arguments:

    <interval>          Event to consider beginning of rate measurement
    --factored          Emit events as factored instead of suffix
    -n, --name          Event suffix or data-value name for output event
    -e, --event         Metric name for results, with optional bucket/bounds settings similar to +count, like:
                        event_name[min:max:granularity].
    --help, -h          This help message.
)");
}

} // namespace ampere::evp
