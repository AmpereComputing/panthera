//
// Copyright(c) 2022 Ampere Computing LLC
// SPDX-License-Identifier: BSD-3-Clause
//
#pragma once

#include <cstdint>
#include <map>
#include <memory>
#include <stdexcept>
#include <string>
#include <utility>

#include <fmt/args.h>
#include <fmt/core.h>

#include "ampere/event_stream/event_stream.pb.h"
#include "ampere/evp/utils.h"
#include "ampere/protobuf_utils/protobuf_stream.h"

#include "counter.h"
#include "metric_table.h"
#include "plugin.h"
#include "processor_ifc.h"

namespace ampere::evp {

class CounterSpec;

class Processor : public ProcessorIfc
{
public:
    Processor(int in_fd, int out_fd, const std::map<std::string, std::string>& variables);
    Processor(const std::string& fname, const std::map<std::string, std::string>& variables);

    void initialize();
    void start();
    void process();
    void collect(uint64_t trigger_time = 0);
    void report();

    void skip(uint64_t max_time = 0, CounterSet* including = nullptr);
    void stop_skipping();
    void stop_at_previous_time(bool exiting = false);
    void skip_remaining();

    void add_plugin(std::unique_ptr<Plugin> plugin) { plugins.push_back(std::move(plugin)); }

    //
    // ProcessorIfc implementation
    //

    bool has_definition(int id) const override { return definitions.contains(id); }
    bool has_definition(const std::string& name) const override { return definition_index.contains(name); }
    bool has_parameter(const std::string& name) const override { return parameters.contains(name); }
    bool has_enumeration(int id) const override { return enumerations.contains(id); }

    const Definition& get_definition(int id) const override { return definitions.at(id); }

    const Definition& get_definition(const std::string& name) const override
    {
        try {
            return definitions.at(definition_index.at(name));
        } catch (std::out_of_range&) {
            throw NoSuchDefinition{name};
        }
    }

    const Parameter& get_parameter(const std::string& name) const override { return parameters.at(name); }
    const Enumeration& get_enumeration(int id) const override { return enumerations.at(id); }

    Counter& get_counter(int counter_id) override { return counters.at(counter_id).value(); }

    uint64_t get_current_time() const override { return current_time; }
    uint64_t get_first_event_time() const override { return first_event_time; }

    void count(Plugin* plugin,
               const std::string& counter_spec,
               CounterSet* counter_set = nullptr,
               CountAction action = CountAction::ADD) override;
    void on(Plugin* plugin, const std::string& trigger, Action action, CounterSet* counter_set = nullptr) override;
    void on_every(Plugin* plugin,
                  const std::string& trigger,
                  Action action,
                  CounterSet* counter_set = nullptr) override;
    void at(Plugin* plugin, const std::string& trigger, TimeAction action) override;
    void at_every(Plugin* plugin, const std::string& trigger, TimeAction action) override;
    void accumulate(Plugin* plugin, const std::string& counter_spec, CounterSet* counter_set = nullptr) override;

    template<typename P>
    void at_or_on(Plugin* plugin, const std::string& trigger, P trip, CounterSet* counter_set = nullptr)
    {
        if (is_time_spec(trigger)) {
            TimeAction time_action = [=](uint64_t current_time, uint64_t expiry) {
                trip();
            };
            at(plugin, trigger, time_action);
        } else {
            Action action = [=](Counter* counter, const Event& event) {
                trip();
            };
            on(plugin, trigger, action, counter_set);
        }
    }

    const std::map<std::string, std::string>& vars() override { return variables; }
    fmt::format_args vars_for_fmt() override { return variables_for_fmt; }

    void stop(const std::string& msg) override;

private:
    std::unique_ptr<ProtobufStreamReader> input_stream;
    std::unique_ptr<ProtobufStreamWriter> response_stream;

    event_stream_proto::Record record;

    const size_t DEFERRAL_LIMIT = 50000;
    std::list<event_stream_proto::Record> deferred_events;

    std::map<int, Definition> definitions;
    std::map<std::string, int> definition_index;
    std::map<std::string, Parameter> parameters;
    std::map<int, Enumeration> enumerations;

    std::set<int> all_event_ids;
    std::set<int> active_events;

    std::deque<std::optional<Counter>> counters;
    std::multimap<int, Counter*> event_id_to_counter;

    std::vector<std::unique_ptr<Plugin>> plugins;

    MetricTableTimeSeries ts;

    uint64_t current_time{0};
    uint64_t previous_time{0};
    uint64_t start_time{0};
    uint64_t first_event_time{0};

    bool skipping{false};
    bool stop_processing{false};
    bool stopped_at_previous_time{false};

    struct TimeActionWrapper
    {
        TimeAction action;
        uint64_t delta;

        TimeActionWrapper(TimeAction action, uint64_t delta) : action{action}, delta{delta} {}
    };

    std::multimap<uint64_t, TimeActionWrapper> time_based_actions;

    std::map<std::string, std::string> variables;
    fmt::dynamic_format_arg_store<fmt::format_context> variables_for_fmt;

    bool get_next_record();
    void save_definition(const Definition& definition);
    void save_parameter(const Parameter& parameter);
    void save_enumeration(const Enumeration& enumeration);
    void announce_definitions();
    void report_parameters();
    void start_simulation();
    void end_simulation();
    void run_time_based_actions();
    void ensure_event_record();
    void handle_event(const Event& event);

    void enable_active_events();

    void build_counters(const CounterSpec& spec, Action action, CounterSet* counter_set);
    void build_counters(const CounterSpec& spec, CountAction action, CounterSet* counter_set);
    void build_counter(int event_definition_id,
                       const std::string& event_name,
                       const CounterSpec& spec,
                       Action action,
                       CountAction count_action,
                       CounterSet* counter_set);
    int build_counter(int event_definition_id,
                      const std::string& event_name,
                      const CounterSpec& spec,
                      Action action,
                      CountAction count_action = CountAction::ADD);

    std::pair<int, Counter*> add_counter(Counter counter);
    int remove_counter(Counter counter);
    void add_factors(Counter& counter, const std::string& event_name, const CounterSpec& spec) const;

    void build_data_counters(const CounterSpec& spec, CountAction action, CounterSet* counter_set);
    void build_data_counter(int event_definition_id,
                            const std::string& event_name,
                            const CounterSpec& spec,
                            CountAction action,
                            CounterSet* counter_set);
    int build_data_counter(int event_definition_id,
                           const std::string& event_name,
                           const CounterSpec& spec,
                           CountAction action);
};

} // namespace ampere::evp
