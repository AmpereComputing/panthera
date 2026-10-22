//
// Copyright(c) 2022 Ampere Computing. All Rights Reserved.
// SPDX-License-Identifier: BSD-3-Clause
//
#include "ampere/evp/bounds.h"
#include "ampere/evp/plugin.h"
#include "ampere/evp/utils.h"

#include <cmath>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>
#include <fmt/core.h>

namespace ampere::evp {

AMPERE_EVP_PLUGIN(Latency, "latency", "Measure time between pairs of events in transaction")
{
public:
    Latency(ProcessorIfc & proc_ifc, Args & args);

    static void help(int argc, const char** argv);
    void define_value(const Definition& value_def) override;
    void collect(MetricSeries & metrics, uint64_t trigger_time) override;

private:
    bool enable{true};

    size_t max_latencies_vector_size{10};

    struct TimeCount
    {
        uint64_t time{0};
        uint64_t count{0};
    };

    struct LatencyHist
    {
        float stdev{0.0};
        uint64_t min_latency{UINT64_MAX};
        std::vector<uint64_t> max_N_latencies;
        std::map<uint64_t, uint64_t> latency_counts;
    };

    using LatencyInfo = std::vector<TimeCount>;
    using LatencyHistInfo = std::vector<LatencyHist>;

    struct LatencyTracker
    {
        LatencyTracker(uint64_t start_time, size_t num_events)
            : time_per_event(num_events), last_event_time{start_time}, last_event_idx{-1}
        {
        }

        LatencyInfo time_per_event;

        uint64_t last_event_time;
        int last_event_idx;

        bool saw_start_event{false};
    };

    std::vector<std::string> events;
    std::vector<std::string> event_aliases;
    std::string name;
    std::unordered_set<std::string> key_data_names{{"txid"}};

    LatencyInfo aggregated_info;
    LatencyHistInfo aggregated_hist_info;

    std::unordered_map<uint64_t, LatencyTracker> trackers;
    using TrackerIter = decltype(trackers)::iterator;

    bool histogram{false};
    std::string histogram_metric_name;
    std::unique_ptr<FactorBounds> histogram_bounds;
    bool factored_histogram{false};

    bool tracking_transactions{true};
    std::unordered_set<int> key_definitions;

    static std::string build_name(const std::vector<std::string>& events);
    void update_latency(Counter * counter, const event_stream_proto::Event& event, int idx);
    void end_transaction(Counter * counter, const event_stream_proto::Event& event);
    void record_latency(LatencyTracker & tracker, uint64_t now, int idx);
    void finalize_latency(const TrackerIter& tracker);
    void aggregate_latency(LatencyTracker & tracker);
    std::optional<uint64_t> get_key(const event_stream_proto::Event& event) const;
    TrackerIter get_tracker_by_key(const event_stream_proto::Event& event);
    void assign_latency_metrics(
        MetricSeries & metrics, const TimeCount& info, const LatencyHist& hist_info, std::string event = {});
};

Latency::Latency(ProcessorIfc& proc_ifc, Args& args) : Plugin{proc_ifc}
{
    std::string prefix{};
    bool ignore_missing{false};

    while (args) {
        std::string event;

        if (args.pop("-n|--name", name)) {
        } else if (args.pop("-p|--prefix", prefix)) {
        } else if (args.pop("-k|--key", event)) {
            if (tracking_transactions) {
                key_data_names.clear();
                tracking_transactions = false;
            }
            key_data_names.emplace(event);
        } else if (args.pop("--ignore-missing")) {
            ignore_missing = true;
        } else if (args.pop("--histogram", event)) {
            histogram = true;
            std::tie(histogram_metric_name, histogram_bounds) = FactorBounds::from_event_spec(event);
        } else if (args.pop("--factored")) {
            factored_histogram = true;
        } else if (args.pop(event)) {
            events.emplace_back(event);
        } else {
            break;
        }
    }

    args.done();

    if (!prefix.empty() && !tracking_transactions) {
        auto original_names = std::move(key_data_names);
        key_data_names.clear();
        for (const auto& key : original_names)
            key_data_names.emplace(fmt::format("{}.{}", prefix, key));
    }

    aggregated_info.resize(events.size());
    aggregated_hist_info.resize(events.size());

    int idx = 0;

    for (auto& event : events) {
        auto pos = event.find("=");

        if (pos != std::string::npos) {
            event_aliases.push_back(event.substr(pos + 1));
            event = event.substr(0, pos);
        } else {
            event_aliases.push_back(event);
        }

        auto event_name = prefix.empty() ? event : fmt::format("{}.{}", prefix, event);

        auto update = [this, idx](Counter* counter, const event_stream_proto::Event& event) {
            this->update_latency(counter, event, idx);
        };

        try {
            on_every(event_name, update);
        } catch (NoSuchDefinition& exc) {
            if (!ignore_missing)
                throw;

            if (idx == 0)
                enable = false;
        }

        ++idx;
    }

    if (enable) {
        if (name.empty())
            name = build_name(events);

        if (tracking_transactions)
            on_every("end_transaction", &Latency::end_transaction);
    }
}

void Latency::help(int argc, const char** argv)
{
    print_help(argv[0], "latency", "[-n|--name <name>] [-p|--prefix <prefix>] <start event> <stop event>", R"(Arguments:

    <start event>          Event to consider beginning of latency measurement
    <end event>            Event to consider end of latency measurement
    -n, --name             Metric name for results
    -p, --prefix <event>   Prefix applied to start and end event
    -k, --key <data-type>  Data to use for event tracking (default: txid)
    --histogram <name>     Emit histogram and use suffix name for results, with optional bucket/bounds settings similar to +count,
                           like: name[min:max:granularity].
    --factored             Emit histogram as factored instead of suffix
    --help, -h             This help message.
)");
}

std::string Latency::build_name(const std::vector<std::string>& events)
{
    std::stringstream nm{};
    bool first{true};
    auto prefix = extract_prefix(events[0]).first;

    for (const auto& event : events) {
        if (!first) {
            nm << "_";
            if (auto [e_prefix, e_name] = extract_prefix(event); e_prefix == prefix) {
                nm << e_name;
            } else {
                auto event_name = event;
                std::replace(event_name.begin(), event_name.end(), '.', '_');
                nm << event_name;
            }
        } else {
            nm << event;
        }
        first = false;
    }

    return nm.str();
}

void Latency::define_value(const Definition& value_def)
{
    if (key_data_names.contains(value_def.name()))
        key_definitions.emplace(value_def.id());
}

void Latency::update_latency(Counter* counter, const event_stream_proto::Event& event, int idx)
{
    if (auto key = get_key(event); key) {
        auto tracker_iter = trackers.find(*key);
        if (tracker_iter == trackers.end())
            std::tie(tracker_iter, std::ignore) = trackers.insert({*key, LatencyTracker(event.time(), events.size())});

        record_latency(tracker_iter->second, event.time(), idx);

        if (!tracking_transactions && static_cast<size_t>(idx + 1) == events.size())
            finalize_latency(tracker_iter);
    }
}

void Latency::record_latency(LatencyTracker& tracker, uint64_t now, int idx)
{
    if (tracker.last_event_idx >= 0) {
        auto latency = now - tracker.last_event_time;
        auto& time_count = tracker.time_per_event[tracker.last_event_idx];
        time_count.time += latency;
        time_count.count += 1;
    }

    tracker.last_event_time = now;
    tracker.last_event_idx = idx;

    if (idx == 0)
        tracker.saw_start_event = true;
}

void Latency::finalize_latency(const TrackerIter& tracker_iter)
{
    aggregate_latency(tracker_iter->second);
    trackers.erase(tracker_iter);
}

void Latency::end_transaction(Counter* counter, const event_stream_proto::Event& event)
{
    if (auto tracker_iter = get_tracker_by_key(event); tracker_iter != trackers.end())
        finalize_latency(tracker_iter);
}

void Latency::aggregate_latency(LatencyTracker& tracker)
{
    if (tracker.saw_start_event && tracker.last_event_idx >= 0) {
        for (size_t i = 0; i < tracker.time_per_event.size(); ++i) {
            const auto& tracker_entry = tracker.time_per_event[i];
            auto& info = aggregated_info[i];
            auto latency = tracker_entry.time;
            info.time += tracker_entry.time;
            info.count += tracker_entry.count;

            if (!latency)
                continue;

            auto& hist_info = aggregated_hist_info[i];
            if (histogram) {
                auto bucket = histogram_bounds ? histogram_bounds->adjust(latency) : latency;
                ++hist_info.latency_counts[bucket];
            }

            if ((hist_info.max_N_latencies.size() >= max_latencies_vector_size)
                && (latency > hist_info.max_N_latencies.back()))
            {
                hist_info.max_N_latencies.pop_back();
                hist_info.max_N_latencies.push_back(latency);
                std::sort(hist_info.max_N_latencies.begin(), hist_info.max_N_latencies.end(), std::greater<uint64_t>());
            } else if (hist_info.max_N_latencies.size() < max_latencies_vector_size) {
                hist_info.max_N_latencies.push_back(latency);
                std::sort(hist_info.max_N_latencies.begin(), hist_info.max_N_latencies.end(), std::greater<uint64_t>());
            }

            if (latency < hist_info.min_latency)
                hist_info.min_latency = latency;
            if (info.count == 1)
                continue;

            hist_info.stdev =
                std::sqrt((info.time * info.time + (info.time * info.time) / info.count) / (info.count - 1));
        }
    }
}

std::optional<uint64_t> Latency::get_key(const event_stream_proto::Event& event) const
{
    for (int i = 0; i < event.values_size(); ++i) {
        const auto& value = event.values(i);
        if (key_definitions.contains(value.definition_id()))
            return value.uint_value();
    }

    return {};
}

Latency::TrackerIter Latency::get_tracker_by_key(const event_stream_proto::Event& event)
{
    if (auto txid = get_key(event); txid)
        return trackers.find(*txid);

    return trackers.end();
}

void Latency::assign_latency_metrics(MetricSeries& metrics,
                                     const TimeCount& info,
                                     const LatencyHist& hist_info,
                                     std::string event)
{
    auto prefix = event.empty() ? name : fmt::format("{}.{}", name, event);
    auto time_metric = fmt::format("{}.sum_latency", prefix);
    auto count_metric = fmt::format("{}.count", prefix);
    auto stdev_metric = fmt::format("{}.stdev", prefix);
    auto max_avg_latency_metric = fmt::format("{}.max_avg_latency", prefix);
    auto max_latency_metric = fmt::format("{}.max_latency", prefix);
    auto min_latency_metric = fmt::format("{}.min_latency", prefix);

    metrics[time_metric] = info.time;
    metrics[count_metric] = info.count;
    metrics[stdev_metric] = hist_info.stdev;

    if (hist_info.max_N_latencies.size() > 0) {
        metrics[max_latency_metric] = hist_info.max_N_latencies.front();
        metrics[min_latency_metric] = hist_info.min_latency;

        uint64_t max_lat_sum = 0;
        for (const auto& lat : hist_info.max_N_latencies)
            max_lat_sum += lat;

        metrics[max_avg_latency_metric] = max_lat_sum / hist_info.max_N_latencies.size();
    }

    if (histogram) {
        for (const auto& [latency, count] : hist_info.latency_counts) {
            auto bucket = factored_histogram ? fmt::format("{}/{}:{}", prefix, histogram_metric_name, latency)
                                             : fmt::format("{}.{}.{}", prefix, histogram_metric_name, latency);
            metrics[bucket] = count;
        }
    }
}

void Latency::collect(MetricSeries& metrics, uint64_t trigger_time)
{
    if (!enable)
        return;

    assign_latency_metrics(metrics, aggregated_info[0], aggregated_hist_info[0]);

    if (events.size() > 2) {
        for (size_t i = 0; i < events.size() - 1; ++i)
            assign_latency_metrics(metrics, aggregated_info[i], aggregated_hist_info[i], event_aliases[i]);
    }
}

} // namespace ampere::evp
