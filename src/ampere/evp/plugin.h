//
// Copyright(c) 2022 Ampere Computing LLC
// SPDX-License-Identifier: BSD-3-Clause
//
#pragma once

#include <memory>
#include <string>

#include "ampere/event_stream/event_stream.pb.h"
#include "args.h"
#include "counter.h"
#include "metric_table.h"
#include "processor_ifc.h"
#include "utils.h"

namespace ampere::evp {

/** Base class for all plugins.
 *
 *  Defines an abstract interface for plugins to implement.
 */
class Plugin
{
public:
    Plugin(ProcessorIfc& proc_ifc) : proc_ifc{proc_ifc} {}
    virtual ~Plugin() = default;

    // An event was defined
    virtual void define_event(const Definition& event_def) {}
    // A value was defined
    virtual void define_value(const Definition& value_def) {}
    // An enumeration was defined
    virtual void define_enumeration(const Enumeration& enum_def) {}
    // A parameter was reported
    virtual void report_parameter(const Parameter& parameter) {}
    // The simulation started
    virtual void start_simulation() {}
    // The simulation ended
    virtual void end_simulation() {}
    // An event was seen that was configured to count
    virtual void process_event(const Event& event) {}
    // Collect metrics from plugin (called every interval)
    virtual void collect(MetricSeries& metrics, uint64_t trigger_time) {}
    // Final report (at the end)
    virtual void report(MetricTableTimeSeries& ts) {};

    // Provide help for plugin
    static void help(int argc, const char** argv) {}
    // Is argument --help
    static bool is_help(const std::string& arg) { return Args::matches_switch(arg, "-h|--help"); }

    // Effectively a singleton, subsequent declarations can be merged
    static bool singular() { return false; }
    // Category/tag of plugin
    static const char* category() { return ""; }

protected:
    class PluginError : public std::runtime_error
    {
    public:
        using std::runtime_error::runtime_error;
    };

    static void print_help(const char* prog, const char* name, const char* arguments, const char* details);

    bool has_definition(int id) const { return proc_ifc.has_definition(id); }
    bool has_definition(const std::string& name) const { return proc_ifc.has_definition(name); }
    bool has_parameter(const std::string& name) const { return proc_ifc.has_parameter(name); }

    const Definition& get_definition(int id) const { return proc_ifc.get_definition(id); }
    const Definition& get_definition(const std::string& name) const { return proc_ifc.get_definition(name); }
    const Parameter& get_parameter(const std::string& name) const { return proc_ifc.get_parameter(name); }

    const char* enumeration_value(int definition_id, int64_t value) const
    {
        return proc_ifc.enumeration_value(definition_id, value);
    }
    const char* enumeration_value_for_definition(const Definition& definition, int64_t value) const
    {
        return proc_ifc.enumeration_value_for_definition(definition, value);
    }

    uint64_t get_current_time() const { return proc_ifc.get_current_time(); }
    uint64_t get_first_event_time() const { return proc_ifc.get_first_event_time(); }

    Counter& get_counter(int counter_id) { return proc_ifc.get_counter(counter_id); }
    const Counter& get_counter(int counter_id) const { return proc_ifc.get_counter(counter_id); }

    void count(const std::string& counter_spec,
               CounterSet* counter_set = nullptr,
               CountAction action = CountAction::ADD)
    {
        proc_ifc.count(this, counter_spec, counter_set, action);
    }

    void on_every(const std::string& trigger, Action action) { proc_ifc.on(this, trigger, action); }

    void accumulate(const std::string& counter_spec, CounterSet* counter_set = nullptr)
    {
        proc_ifc.accumulate(this, counter_spec, counter_set);
    }

    template<typename T>
    void on(const std::string& trigger, void (T::*action)(Counter*, const event_stream_proto::Event&))
    {
        Action bound_action = std::bind_front(action, static_cast<T*>(this));
        proc_ifc.on(this, trigger, bound_action);
    }

    template<typename T>
    void on_every(const std::string& trigger, void (T::*action)(Counter*, const event_stream_proto::Event&))
    {
        Action bound_action = std::bind_front(action, static_cast<T*>(this));
        proc_ifc.on(this, trigger, bound_action);
    }

    template<typename T>
    void at(const std::string& trigger, void (T::*action)(uint64_t current_time, uint64_t expiry))
    {
        TimeAction bound_action = std::bind_front(action, static_cast<T*>(this));
        proc_ifc.at(this, trigger, bound_action);
    }

    template<typename T>
    void at_every(const std::string& trigger, void (T::*action)(uint64_t current_time, uint64_t expiry))
    {
        TimeAction bound_action = std::bind_front(action, static_cast<T*>(this));
        proc_ifc.at_every(this, trigger, bound_action);
    }

    template<typename T>
    void on(const std::string& trigger, void (T::*action)())
    {
        if (is_time_spec(trigger)) {
            TimeAction time_action = [=, this](uint64_t, uint64_t) {
                (static_cast<T*>(this)->*action)();
            };
            proc_ifc.at(this, trigger, time_action);
        } else {
            Action counter_action = [=, this](Counter*, const Event&) {
                (static_cast<T*>(this)->*action)();
            };
            proc_ifc.on(this, trigger, counter_action);
        }
    }

    template<typename T>
    void on_every(const std::string& trigger, void (T::*action)())
    {
        if (is_time_spec(trigger)) {
            TimeAction time_action = [=, this](uint64_t, uint64_t) {
                (static_cast<T*>(this)->*action)();
            };
            proc_ifc.at_every(this, trigger, time_action);
        } else {
            Action counter_action = [=, this](Counter*, const Event&) {
                (static_cast<T*>(this)->*action)();
            };
            proc_ifc.on_every(this, trigger, counter_action);
        }
    }

    void stop(const std::string& msg) { proc_ifc.stop(msg); }

    ProcessorIfc& proc_ifc;
};

using make_plugin_func_t = std::unique_ptr<Plugin> (*)(ProcessorIfc&, Args&);
using plugin_help_func_t = void (*)(int, const char**);
using plugin_singular_func_t = bool (*)();
using plugin_category_func_t = const char* (*)();

template<typename T>
std::unique_ptr<Plugin> make_plugin(ProcessorIfc& proc_ifc, Args& args)
{
    return std::make_unique<T>(proc_ifc, args);
}

struct PluginDefinition
{
    make_plugin_func_t factory;
    const char* description;
    plugin_help_func_t help;
    plugin_singular_func_t signular;
    plugin_category_func_t category;

    template<typename T>
    static PluginDefinition make(const char* description)
    {
        return PluginDefinition{
            &::ampere::evp::make_plugin<T>,
            description,
            &T::help,
            &T::singular,
            &T::category,
        };
    }
};

bool register_plugin(const char* name, PluginDefinition definition);

using PluginRegistry = std::map<std::string, PluginDefinition>;
PluginRegistry& registered_plugins();

/** Convenience macro for defining new plugin classes from some base plugin, as
 * it takes care or registration for you.
 *
 *  Write a new plugin from a base like this:
 *
 *      AMPERE_EVP_PLUGIN_FROM(MyPlugin, "my_plugin", "description of my plugin", BasePlugin)
 *      {
 *          MyPlugin(ProcessorIfc& proc_ifc, Args& args) : BasePlugin{proc_ifc}
 *          {
 *               ...
 *          }
 *
 *          // Implement plugin interface functions...
 *      }
 */
#define AMPERE_EVP_PLUGIN_FROM(klass, name, description, base_class)                                     \
    class klass;                                                                                         \
    static bool _evp_plugin_##klass##_register =                                                         \
        ::ampere::evp::register_plugin(name, ::ampere::evp::PluginDefinition::make<klass>(description)); \
    class klass : public base_class

/** Convenience macro for defining new plugin classes, as it takes care or
 *  registration for you.
 *
 *  Write a new plugin like this:
 *
 *      AMPERE_EVP_PLUGIN(MyPlugin, "my_plugin", "description of my plugin")
 *      {
 *          MyPlugin(ProcessorIfc& proc_ifc, Args& args) : Plugin{proc_ifc}
 *          {
 *               ...
 *          }
 *
 *          // Implement plugin interface functions...
 *      }
 */
#define AMPERE_EVP_PLUGIN(klass, name, description) AMPERE_EVP_PLUGIN_FROM(klass, name, description, Plugin)

} // namespace ampere::evp
