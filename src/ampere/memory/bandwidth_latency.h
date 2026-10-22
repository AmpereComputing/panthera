//
// Copyright (c) 2022 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//

#ifndef AMPERE_BANDWIDTH_LATENCY_H
#define AMPERE_BANDWIDTH_LATENCY_H

#include "ampere/sc/sc.h"
#include <boost/icl/interval_map.hpp>
#include <boost/circular_buffer.hpp>

namespace ampere::memory {

using namespace ampere::sc;

class BandwidthLatency
{
public:
    struct LatencyData
    {
        float latency;
        float read_constraint;
        float write_constraint;
    };
    BandwidthLatency(const json& j,
                     size_t window_count,
                     size_t max_read_request_bytes = default_max_request_bytes,
                     size_t max_write_request_bytes = default_max_request_bytes,
                     float bandwidth_scale_factor = 1.0f,
                     float bandwidth_overuse_penalty = 1.0,
                     float latency_offset = 0.0f,
                     bool infer_request_latency_curve = false);
    void increment_read_bytes(float bytes);
    void increment_write_bytes(float bytes);
    float get_current_latency() { return current_latency_data.latency; }
    float get_current_read_resource_limit() { return current_latency_data.read_constraint; }
    float get_current_write_resource_limit() { return current_latency_data.write_constraint; }
    float get_current_bandwidth_rd() { return current_bandwidth_rd * reference_rate; }
    float get_current_bandwidth_wr() { return current_bandwidth_wr * reference_rate; }
    float get_current_bandwidth() { return current_bandwidth * reference_rate; }
    LatencyData compute_latency(float nanoseconds);

private:
    using BWInterval = boost::icl::continuous_interval<float>;
    using WritePctInterval = boost::icl::continuous_interval<float>;
    using LatencyCurve = boost::icl::interval_map<float, LatencyData>;
    using LatencyCurves = boost::icl::interval_map<float, LatencyCurve>;

    static const size_t default_max_request_bytes;

    size_t window_count;
    size_t max_read_request_bytes;
    size_t max_write_request_bytes;
    LatencyCurves bw_latency_curves;
    boost::circular_buffer<float> read_bytes{window_count, 0.0};
    boost::circular_buffer<float> write_bytes{window_count, 0.0};
    LatencyData current_latency_data{10.0, (float)default_max_request_bytes, (float)default_max_request_bytes};
    float current_bandwidth_rd{0.0};
    float current_bandwidth_wr{0.0};
    float current_bandwidth{0.0};
    float reference_rate{1.0};
    float bandwidth_overuse_penalty{1.0};
};

} // namespace ampere::memory

#endif // AMPERE_BANDWIDTH_LATENCY_H

/*
 * vim: sw=4:ts=4:et:si:cindent:cino=\:0,(0,g0,t0:
 */
