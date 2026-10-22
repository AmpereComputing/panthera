//
// Copyright (c) 2025 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//

#include <sysc/kernel/sc_object.h>
#include <sysc/kernel/sc_simcontext.h>
#include <sysc/kernel/sc_time.h>
#include "ampere/core/backend_prefetcher.h"
#include "ampere/event_stream/event_stream.pb.h"
#include "ampere/event_stream/event_stream_broadcast.h"
#include "ampere/event_stream/event_stream_recorder.h"
#include "ampere/sc/connect.h"
#include "ampere/sc/event.h"
#include "ampere/sc/module.h"
#include "ampere/sc/param.h"
#include "ampere/sc/submodule.h"
#include "ampere/sc/sc.h"
#include "ampere/sc/debug_registry.h"
#include "ampere/core/prefetcher.h"
#include "ampere/sc/taker.h"
#include "ampere/sc/transaction.h"
#include "ampere/uarch_common/memop.h"
#include "ampere/uarch_common/memreq.h"

#include "ampere/event_stream/event_stream_reader.h"

#include <string>
#include <variant>
#include <boost/program_options.hpp>
#include <cstdint>
#include <cstdlib>
#include <memory>
#include <optional>
#include <sstream>
#include <type_traits>
#include <unordered_map>
#include <unordered_set>
#include <vector>
#include <fmt/core.h>

class EventDefinition
{
    std::string name;
    int definition_id{-1};

public:
    EventDefinition(const std::string &prefix, const std::string &name)
        : name(prefix.empty() ? name : prefix + "." + name)
    {
    }
    EventDefinition(const std::string &prefix, const std::optional<std::string> &name)
        : name(name ? (prefix.empty() ? *name : prefix + "." + *name) : "")
    {
    }

    bool enabled() const { return !name.empty(); }
    bool has_definition() const { return definition_id != -1; }
    int id() const { return definition_id; }
    explicit operator bool() const { return !name.empty(); }

    void associate(const ampere::event_stream_proto::Definition &definition)
    {
        if (enabled() && definition.name() == name)
            definition_id = definition.id();
    }

    template<typename T>
    bool matches(const T &record) const
    {
        return enabled() && record.definition_id() == definition_id;
    }
};

class ValueDefinitionBase : public EventDefinition
{
public:
    ValueDefinitionBase(const std::string &prefix, const std::string &name) : EventDefinition(prefix, name) {}
    ValueDefinitionBase(std::vector<ValueDefinitionBase *> &all, const std::string &prefix, const std::string &name)
        : ValueDefinitionBase(prefix, name)
    {
        all.emplace_back(this);
    }

    virtual ~ValueDefinitionBase() = default;

    virtual bool populate(const event_stream_proto::Value &value) = 0;
};

template<typename T>
class ValueDefinition : public ValueDefinitionBase
{
    T last_value{};

public:
    ValueDefinition(const std::string &prefix, const std::string &name) : ValueDefinitionBase(prefix, name) {}
    ValueDefinition(std::vector<ValueDefinitionBase *> &all, const std::string &prefix, const std::string &name)
        : ValueDefinitionBase(all, prefix, name)
    {
    }

    bool populate(const event_stream_proto::Value &value) override
    {
        if (!matches(value))
            return false;

        if constexpr (std::is_same_v<T, bool>) {
            if (value.uint_value() == 255 || value.uint_value() == 1)
                last_value = true;
            else if (value.uint_value() == 254 || value.uint_value() == 0)
                last_value = false;
        } else if constexpr (std::is_unsigned_v<T>) {
            last_value = value.uint_value();
        } else if constexpr (std::is_integral_v<T>) {
            last_value = value.int_value();
        } else {
            last_value = T{value.uint_value()};
        }

        return true;
    }

    const T &operator*() const { return last_value; }
};

struct PrefetcherEvent : sc_core::sc_module
{
    sc::Param<std::optional<std::string>> event_name;

    struct
    {
        sc::Event replay{"replay", "Action was replayed"};
        sc::Event matched{"matched", "Same action was matched"};
    } event;

    EventDefinition definition;

    struct
    {
        mutable uint64_t replayed{0};
        uint64_t triggered{0};
        uint64_t matched{0};
    } counts;

    PrefetcherEvent(const std::string &scope, const sc_core::sc_module_name &name, bool required = false)
        : sc_core::sc_module(name),
          event_name("event_name",
                     fmt::format("{} event name", name),
                     required ? std::optional<std::string>(name) : std::nullopt),
          definition(scope, event_name.val())
    {
    }

    explicit PrefetcherEvent(std::vector<PrefetcherEvent *> &all,
                             const std::string &scope,
                             const sc_core::sc_module_name &name,
                             bool required = false)
        : PrefetcherEvent(scope, name, required)
    {
        all.emplace_back(this);
    }

    void verify() const
    {
        AMPERE_ASSERT(
            !definition.enabled() || definition.has_definition(), "{} event definition was not found", name());
    }

    template<typename T>
    bool matches(const T &record) const
    {
        auto matched = definition.matches(record);
        if (matched)
            ++counts.replayed;
        return matched;
    }

    template<typename T>
    bool matches(const T &record, const sc::Txn &tr)
    {
        auto matched = matches(record);
        if (matched)
            event.replay(tr);
        return matched;
    }

    void triggered() { ++counts.triggered; }
    void matched(const sc::Txn &tr)
    {
        event.matched(tr);
        ++counts.matched;
    }

    void dump(std::ostream &os) const override
    {
        if (definition.enabled() && counts.replayed > 0) {
            os << name() << ": replayed ";
            if (counts.matched > 0)
                os << counts.matched << " / ";
            os << counts.replayed;

            if (counts.triggered > 0) {
                os << ", triggered ";
                if (counts.matched > 0)
                    os << counts.matched << " / ";
                os << counts.triggered;
            }
            os << std::endl;
        }
    }
};

template<typename T, typename PT = core::Prefetcher<T>>
class PrefetcherValidator : sc::Module
{
    using addr_t = typename PT::addr_t;
    sc::Submodule<PT> prefetcher{"prefetcher"};
    sc::In<T> prefetcher_request;

    sc::Param<std::string> event_scope{"scope", "Prefetcher event scope", ""};

    std::vector<PrefetcherEvent *> all_events;
    PrefetcherEvent train{all_events, *event_scope, "train", true};
    PrefetcherEvent fill{all_events, *event_scope, "fill"};
    PrefetcherEvent launch{all_events, *event_scope, "launch"};
    PrefetcherEvent drop{all_events, *event_scope, "drop"};
    PrefetcherEvent overflow{all_events, *event_scope, "overflow"};
    PrefetcherEvent evict{all_events, *event_scope, "evict"};

    sc::Param<std::vector<std::string>> print_events{
        "print_events", "Print events from scope", std::vector<std::string>{}};
    sc::Param<std::vector<std::string>> dump_on_events{
        "dump_on_events", "Dump prefetcher state on event", std::vector<std::string>{}};
    sc::Param<std::vector<std::string>> start_at_events{
        "start_at_events", "Start training after event seen", std::vector<std::string>{}};

    enum {
        WAIT_TILL_TRAIN,
        WAIT_TILL_LAUNCH,
        NO_WAIT
    } phase;

    std::unordered_set<int> start_events;
    std::unordered_set<int> listened_events;

    std::unordered_map<int, std::string> print_event_definitions;
    std::unordered_map<int, std::string> data_names;
    std::unordered_set<std::string> dump_event_names;

    std::unordered_set<addr_t> filled;
    std::unordered_set<addr_t> launched;

    sc::Param<bool> train_is_prefetch{"train_is_prefetch", "Train is prefetch", true};
    sc::Param<bool> train_is_demand{"train_is_demand", "Train is demand", false};
    sc::Param<bool> fill_is_prefetch{"fill_is_prefetch", "Fill is prefetch", false};

    sc::Param<std::string> addr_data_name{"addr_data_name", "Address data name", "virt_addr"};
    sc::Param<std::string> hit_data_name{"hit_data_name", "Hit data name", "hit"};

    std::vector<ValueDefinitionBase *> all_data;
    ValueDefinition<addr_t> addr{all_data, *event_scope, *addr_data_name};
    ValueDefinition<bool> hit{all_data, *event_scope, *hit_data_name};

    event_stream::EventStreamRecorder *recorder;

    inline static const auto *replay_src = "[REPLAY]";
    inline static const auto *events_src = "[EVENTS]";

    std::string attach_scope(const std::string &name) const
    {
        if (event_scope->empty())
            return name;
        return *event_scope + "." + name;
    }

    std::string strip_scope(const std::string &name) const
    {
        return (!event_scope->empty() && name.find(*event_scope) == std::string::npos)
                   ? name
                   : name.substr(event_scope->size() + 1);
    }

    std::string strip_prefix(const std::string &name) const
    {
        if (auto dot = name.rfind('.'); dot != std::string::npos)
            return name.substr(dot + 1);
        return name;
    }

    template<typename... Args>
    void debug_print(const char *source, const char *fmt_str, Args &&...args) const
    {
        if (!debug_enabled())
            return;

        fmt::print("{} {}.", source, name());
        fmt::print(fmt::runtime(fmt_str), std::forward<Args>(args)...);
    }

    T request();

    void prefetcher_event(const event_stream::EventStreamRecorder::RecordedEvent &event)
    {
        std::vector<std::string> values;
        for (const auto &[id, value] : event.data) {
            auto name = strip_prefix(event.data_name(id));
            if (name == "txid")
                continue;
            if (const std::string *v = std::get_if<std::string>(&value))
                values.emplace_back(fmt::format("{}={}", name, *v));
            else if (const int64_t *v = std::get_if<int64_t>(&value))
                values.emplace_back(fmt::format("{}={}", name, *v));
            else if (const uint64_t *v = std::get_if<uint64_t>(&value))
                values.emplace_back(fmt::format("{}={}", name, *v));
        }

        debug_print(replay_src, "{} {}\n", strip_prefix(event.name()), fmt::join(values, ", "));

        if (debug_enabled() && dump_event_names.find(event.name()) != dump_event_names.end()) {
            debug_print(replay_src, "dump state:\n");
            prefetcher->dump();
        }
    }

public:
    explicit PrefetcherValidator(const sc_core::sc_module_name &name)
        : Module(name), phase(start_at_events->empty() ? NO_WAIT : WAIT_TILL_TRAIN)
    {
        sc::connect(prefetcher->prefetch_request, prefetcher_request).name("prefetch_request").bandwidth(64);

        auto *es = static_cast<event_stream::EventStreamBroadcast *>(get_environment()->event_stream.get());
        recorder = es->emplace<event_stream::EventStreamRecorder>();
        recorder->callback(
            [this](const event_stream::EventStreamRecorder::RecordedEvent &event) { this->prefetcher_event(event); });

        for (const auto &event : *print_events)
            recorder->record(fmt::format("{}.{}", prefetcher->name(), strip_scope(event)));

        for (const auto &event : *dump_on_events) {
            auto name = fmt::format("{}.{}", prefetcher->name(), strip_scope(event));
            recorder->record(name);
            dump_event_names.emplace(name);
        }
    }

    void definition(const ampere::event_stream_proto::Definition &definition)
    {
        for (auto *event : all_events)
            event->definition.associate(definition);

        for (auto *value : all_data)
            value->associate(definition);

        for (const auto &name : *print_events)
            if (definition.name() == attach_scope(name))
                print_event_definitions[definition.id()] = strip_scope(name);

        for (const auto &name : *start_at_events)
            if (definition.name() == attach_scope(name))
                start_events.emplace(definition.id());

        if (definition.kind() == event_stream_proto::Kind::VALUE && definition.name() != "txid"
            && (event_scope->empty() || definition.name().find(*event_scope) != std::string::npos))
            data_names[definition.id()] = strip_scope(definition.name());
    }

    void start()
    {
        for (auto *event : all_events) {
            event->verify();
            if (event->definition && event->definition.id() >= 0)
                listened_events.emplace(event->definition.id());
        }

        for (const auto &[id, _] : print_event_definitions)
            listened_events.emplace(id);

        for (const auto &id : start_events)
            listened_events.emplace(id);

        AMPERE_ASSERT(!listened_events.empty(), "No listened events");

        unsigned listened_data = 0;
        for (auto *data : all_data)
            listened_data += *data && data->id() >= 0;

        AMPERE_ASSERT(listened_data > 0, "No listened data");
    }

    void event(const ampere::event_stream_proto::Event &event)
    {
        if (!listened_events.contains(event.definition_id()))
            return;

        for (auto *data : all_data) {
            for (const auto &value : event.values())
                if (data->populate(value))
                    break;
        }

        auto req = request();

        if (fill.matches(event, req)) {
            debug_print(replay_src, "fill addr={}\n", *addr);
            prefetcher->fill(0, req, true, *fill_is_prefetch);
            filled.emplace(*addr);
        } else if (evict.matches(event, req)) {
            debug_print(events_src, "evict addr={}\n", *addr);
            if (filled.erase(*addr))
                evict.matched(req);
        }

        if (phase != NO_WAIT) {
            if (start_events.contains(event.definition_id()))
                phase = phase == WAIT_TILL_TRAIN ? WAIT_TILL_LAUNCH : NO_WAIT;
            else if (phase == WAIT_TILL_TRAIN)
                return;
        }

        if (train.matches(event, req)) {
            debug_print(replay_src, "train addr={}, hit={}\n", *addr, *hit);
            prefetcher->train(0, req, *hit, *train_is_prefetch, *train_is_demand, {});
        } else if (phase > WAIT_TILL_LAUNCH && launch.matches(event, req)) {
            debug_print(events_src, "launch addr={}\n", *addr);
            if (auto it = launched.find(*addr); it != launched.end()) {
                debug_print(replay_src, "matched trigger addr={}\n", *addr);
                launched.erase(it);
                launch.matched(req);
            }
        } else if (auto e = print_event_definitions.find(event.definition_id()); e != print_event_definitions.end()) {
            std::vector<std::string> values;
            for (const auto &value : event.values()) {
                if (auto d = data_names.find(value.definition_id()); d != data_names.end()) {
                    if (value.has_string_value())
                        values.emplace_back(fmt::format("{}={}", d->second, value.string_value()));
                    else if (value.has_int_value())
                        values.emplace_back(fmt::format("{}={}", d->second, value.int_value()));
                    else if (value.has_uint_value())
                        values.emplace_back(fmt::format("{}={}", d->second, value.uint_value()));
                }
            }

            debug_print(events_src, "{} {}\n", e->second, fmt::join(values, ", "));
        }
    }

    bool run() override
    {
        while (prefetcher_request->can_take()) {
            auto req = prefetcher_request->take();
            auto req_addr = core::PrefetcherData<T>::get_addr(req);
            debug_print(replay_src, "trigger addr={}\n", req_addr);

            if (!filled.contains(req_addr)) {
                prefetcher->launch(req);
                launch.triggered();
                launched.emplace(req_addr);
            }
        }

        return true;
    }

    void dump(std::ostream &os) const override { prefetcher->dump(os); }

    void summarize(std::ostream &os) const
    {
        for (const auto *event : all_events)
            event->dump(os);
    }
};

template<>
uarch_common::MemReq PrefetcherValidator<uarch_common::MemReq, core::Prefetcher<uarch_common::MemReq>>::request()
{
    return make<uarch_common::MemReq>(0,
                                      *addr,
                                      uarch_common::MemOp::Requester::LSU,
                                      uarch_common::MemOp::Type::READ,
                                      8,
                                      false,
                                      uarch_common::MemOp::PrefetchType::NONE,
                                      uarch_common::MemOp::BarrierType::BAR_NONE,
                                      uarch_common::MemOp::AtomicType::ATOMIC_NONE,
                                      uarch_common::MemOp::SnoopType::SNOOP_NONE,
                                      archx::PageSize::Size::PS_64KB);
}

struct PrefetcherRunner : sc::Module
{
    PrefetcherValidator<uarch_common::MemReq> l2_prefetcher{"l2c"};

    bool started{false};
    ampere::event_stream::EventStreamReader &es_reader;
    ampere::event_stream_proto::Record record;
    int64_t last_time{0};

    PrefetcherRunner(const sc_core::sc_module_name &name,
                     sc::Clock &clock,
                     sc::Environment &env,
                     ampere::event_stream::EventStreamReader &es_reader)
        : sc::Module(name, &clock, &env), es_reader(es_reader)
    {
    }

    bool run() override
    {
        do {
            if (record.has_definition()) {
                l2_prefetcher.definition(record.definition());
            } else if (record.has_event()) {
                if (!started) {
                    l2_prefetcher.start();
                    started = true;
                }

                if (auto time = record.event().time(); !last_time || time != last_time) {
                    last_time = time;
                    return true;
                }

                l2_prefetcher.event(record.event());
            }

            record.Clear();

        } while (es_reader.read(record));

        sc_core::sc_stop();
        return false;
    }

    void summarize() const
    {
        std::cout << name() << " summary:" << std::endl;
        l2_prefetcher.summarize(std::cout);

        std::cout << std::endl;

        std::cout << name() << " details:" << std::endl;
        l2_prefetcher.dump(std::cout);
    }
};

int sc_main(int argc, char **argv)
{
    sc::Clock clock{"main", {1, sc_core::SC_NS}};
    sc::Environment env{"system"};

    env.parameters.load_from_args(argc, argv);

    sc::Param<std::string> input_event_stream{env, "event_stream", "Input event stream path", ""};
    sc::Param<std::string> output_event_stream{env, "output_event_stream", "Output event stream path", ""};

    sc::Param<int> event_stream_fd{env, "event_stream_fd", "file descriptor for output event stream", -1};
    sc::Param<int> resp_stream_fd{env, "resp_stream_fd", "file descriptor for input response stream", -1};

    ampere::event_stream::EventStreamReader es_reader{*input_event_stream};

    env.event_stream = std::make_unique<event_stream::EventStreamBroadcast>();
    auto *es = static_cast<event_stream::EventStreamBroadcast *>(env.event_stream.get());

    if (resp_stream_fd != -1 && event_stream_fd != -1)
        es->emplace<event_stream::EventStreamProto>(event_stream_fd, resp_stream_fd);
    else if (event_stream_fd != -1)
        es->emplace<event_stream::EventStreamProto>(event_stream_fd);
    else if (!output_event_stream->empty())
        es->emplace<event_stream::EventStreamProto>(*output_event_stream);

    sc::DebugRegistry::setup_main(env);

    PrefetcherRunner runner{"top", clock, env, es_reader};

    if (env.parameters.help_requested()) {
        env.parameters.dump_parameters_verbosely(std::cout);
        sc_core::sc_stop();
        return EXIT_SUCCESS;
    }

    if (!env.parameters.check_all_matched())
        throw std::runtime_error{"Not all parameters were matched. Giving up."};

    if (env.event_stream)
        env.event_stream->enable();
    sc_core::sc_start();

    runner.summarize();

    return EXIT_SUCCESS;
}
