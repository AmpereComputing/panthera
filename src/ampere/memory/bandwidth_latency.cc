//
// Copyright (c) 2022 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//

#include "ampere/memory/bandwidth_latency.h"

namespace ampere::memory {

const size_t BandwidthLatency::default_max_request_bytes{64 * 128};

bool operator==(const BandwidthLatency::LatencyData& lhs, const BandwidthLatency::LatencyData& rhs)
{
    return (lhs.latency == rhs.latency) && (lhs.read_constraint == rhs.read_constraint)
           && (lhs.write_constraint == rhs.write_constraint);
}

struct LatencyCurveSpec
{
    float write_percentage;
    std::vector<std::vector<float>> bw_latency;
};
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(LatencyCurveSpec, write_percentage, bw_latency);

BandwidthLatency::BandwidthLatency(const json& j,
                                   size_t window_count,
                                   size_t max_read_request_bytes,
                                   size_t max_write_request_bytes,
                                   float bandwidth_scale_factor,
                                   float bandwidth_overuse_penalty,
                                   float latency_offset,
                                   bool infer_request_latency_curve)
    : window_count(window_count),
      max_read_request_bytes(max_read_request_bytes),
      max_write_request_bytes(max_write_request_bytes),
      bandwidth_overuse_penalty(bandwidth_overuse_penalty)
{
    float write_lower{0};
    for (auto const& it : j) {
        auto spec = it.get<ampere::memory::LatencyCurveSpec>();

        // Construct the LatencyCurve from the JSON LatencyCurveSpec data
        float lower{0};
        bool idle_indicated{false};
        LatencyCurve latency_curve;
        for (auto const& point : spec.bw_latency) {
            BWInterval bw_interval;

            float scaled_bw = bandwidth_scale_factor * point[0];
            float offset_lat = latency_offset + point[1];

            if (infer_request_latency_curve) {
                scaled_bw = scaled_bw * offset_lat;
                if (offset_lat * reference_rate > 1.0) {
                    reference_rate = 1.0 / offset_lat;
                }
            }

            AMPERE_ASSERT(
                offset_lat > 0,
                "Negative latency detected in effective BW/Latency curve for bw: {}, base lat: {}, and lat offset: {}",
                point[0],
                point[1],
                latency_offset);

            AMPERE_ASSERT(scaled_bw == 0.0 || scaled_bw > lower,
                          "Curve has empty or decreasing interval for write_percentage {} at bandwidth {}",
                          write_lower,
                          scaled_bw);

            if (scaled_bw < 1.0e-6f) {
                // If an idle bandwidth is provided use that first
                bw_interval = BWInterval::closed(0.0, 0.0);
                idle_indicated = true;
            } else if (!idle_indicated && (lower < 1.0e-6f)) {
                // Otherwise extend the first finite bandwidth point to 0
                bw_interval = BWInterval::closed(lower, scaled_bw);
            } else {
                bw_interval = BWInterval::left_open(lower, scaled_bw);
            }
            latency_curve.set({bw_interval, {offset_lat, 0.0f, 0.0f}});
            lower = scaled_bw;
        }

        // Add this LatencyCurve to the LatencyCurves interval_map
        // which is indexed by write percentage
        WritePctInterval wp_interval;
        if (write_lower > 1.0e-6f) {
            wp_interval = WritePctInterval::left_open(write_lower, spec.write_percentage);
        } else {
            wp_interval = WritePctInterval::closed(write_lower, spec.write_percentage);
        }
        bw_latency_curves.set(std::make_pair(wp_interval, latency_curve));
        write_lower = spec.write_percentage;
    }

    // Iterate over the fully populated bandwidth latency curve set again to set the resource limits now
    // that we know the full breadth of write percentages so we can choose whether to apply the limit
    // to reads or writes based on the breakdown
    for (auto& wp_interval : bw_latency_curves) {
        float write_lower = wp_interval.first.lower();
        auto& latency_curve = wp_interval.second;

        // Use the last point in the curve to compute a resource limit that enforces the
        // bandwidth at that point at the latency at that point
        auto curve_it = latency_curve.rbegin();
        AMPERE_ASSERT(curve_it != latency_curve.rend(), "Empty BW latency curve for write_percentage {}", write_lower);

        // If we are in bandwidth-latency mode the constrained request bytes are equal to the bandwidth
        // of the current point times the latency of the current point (bytes/sec * sec = bytes). If,
        // however, we are in queue-depth based mode the curve is no longer latency per bandwidth but
        // already latency per outstanding request bytes so the constrained request bytes are
        // definitionally the same as the dependent variable:

        float constrained_request_bytes =
            infer_request_latency_curve ? curve_it->first.upper() : curve_it->first.upper() * curve_it->second.latency;

        // Do a bit of a sneaky trick here to make the resource limitation plateau. Set the
        // last point to the resource limit, then also set the next to last point to the same
        // limit and only start relieving the resource limit at earlier points than that. This
        // ensures that naively extrapolating from the last two points results in a flat line
        // at resource limitation defined by the last point in the curve's bandwidth and latency.

        curve_it->second.read_constraint = constrained_request_bytes;
        curve_it->second.write_constraint = constrained_request_bytes;
        auto limit_start = curve_it;
        // Now apply the same condition to the second to last interval bound and proceed from there
        limit_start++;

        float constrained_read_request_slope =
            (constrained_request_bytes - max_read_request_bytes) / limit_start->first.lower();
        float constrained_write_request_slope =
            (constrained_request_bytes - max_write_request_bytes) / limit_start->first.lower();

        for (auto interval = limit_start; interval != latency_curve.rend(); interval++) {
            interval->second.read_constraint =
                constrained_read_request_slope * interval->first.lower() + max_read_request_bytes;
            interval->second.write_constraint =
                constrained_write_request_slope * interval->first.lower() + max_write_request_bytes;
        }
    }

#ifdef DEBUG_BWLAT
    // interval_map can do unexpected things with iffy data like adding together overlapping
    // sections of intervals; this debug print is useful for verifying the data structure was
    // populated as expected from the input cuve data
    for (auto const& i : bw_latency_curves) {
        for (auto const& j : i.second) {
            std::cout << "BW/latency: " << i.first << " " << j.first << " latency: " << j.second.latency
                      << " ns, request limit: " << j.second.read_constraint
                      << " bytes, write request limit: " << j.second.write_constraint << " bytes" << std::endl;
        }
    }
#endif
}

void BandwidthLatency::increment_read_bytes(float bytes)
{
    read_bytes.back() += bytes;
}

void BandwidthLatency::increment_write_bytes(float bytes)
{
    write_bytes.back() += bytes;
}

BandwidthLatency::LatencyData BandwidthLatency::compute_latency(float nanoseconds)
{
    float bytes_read = std::accumulate(read_bytes.begin(), read_bytes.end(), 0);
    float bytes_write = std::accumulate(write_bytes.begin(), write_bytes.end(), 0);
    float bytes_total = bytes_read + bytes_write;

    float write_percentage{0};
    if (bytes_total > 0.0)
        write_percentage = bytes_write / bytes_total;

    if (nanoseconds > 0.0) {
        current_bandwidth_rd = bytes_read / nanoseconds;
        current_bandwidth_wr = bytes_write / nanoseconds;
        current_bandwidth = bytes_total / nanoseconds;
    }

    auto const& curve = bw_latency_curves.find(write_percentage);
    AMPERE_ASSERT(
        curve != bw_latency_curves.end(), "Can't find BW latency curve for write_percentage {}", write_percentage);

    float lower_bw{0.0};
    float upper_bw{0.0};

    LatencyData upper_data;

    auto const& upper_it = curve->second.find(current_bandwidth);

    if (upper_it == curve->second.end()) {
        // If we are greater than the max bandwith then just use the last entry. This
        // will cause the interpolation code below to extrapolate the latency from the
        // last two points up to the effective bandwidth we measured
        auto const& last_it = curve->second.rbegin();

        AMPERE_ASSERT(last_it != curve->second.rend(),
                      "Empty latency curve for write_percentage {}, BW {}",
                      write_percentage,
                      current_bandwidth);

        lower_bw = last_it->first.lower();
        upper_bw = last_it->first.upper();

        upper_data = last_it->second;
    } else {
        lower_bw = upper_it->first.lower();
        upper_bw = upper_it->first.upper();

        upper_data = upper_it->second;
    }

    auto const& lower_it = curve->second.find(lower_bw);
    AMPERE_ASSERT(lower_it != curve->second.end(),
                  "Can't find latency for write_percentage {}, BW {}",
                  write_percentage,
                  current_bandwidth);

    current_latency_data = upper_data; // Take the upper bound by default in case interpolation fails below

    if (lower_bw != upper_bw) {
        LatencyData lower_data = lower_it->second;

        float lower_latency = lower_data.latency;
        float upper_latency = upper_data.latency;

        float lower_read_resources = lower_data.read_constraint;
        float upper_read_resources = upper_data.read_constraint;
        float lower_write_resources = lower_data.write_constraint;
        float upper_write_resources = upper_data.write_constraint;

        float bw_ratio = (current_bandwidth - lower_bw) / (upper_bw - lower_bw);
        if (current_bandwidth > upper_bw) {
            bw_ratio = 1.0 + (bandwidth_overuse_penalty) * (bw_ratio - 1.0);
        }

        float interpolated_latency = (upper_latency - lower_latency) * bw_ratio + lower_latency;
        float interpolated_read_resources =
            (upper_read_resources - lower_read_resources) * bw_ratio + lower_read_resources;
        float interpolated_write_resources =
            (upper_write_resources - lower_write_resources) * bw_ratio + lower_write_resources;

        // Further adjust the read and write resources to account for read and write bandwidth share:
        if (write_percentage >= 0.5f) {
            interpolated_write_resources = write_percentage * interpolated_write_resources;
            interpolated_read_resources =
                max_read_request_bytes
                - (2.0f * max_read_request_bytes - interpolated_read_resources) * write_percentage;
        } else {
            interpolated_write_resources =
                max_write_request_bytes
                - (2.0f * max_write_request_bytes - interpolated_write_resources) * write_percentage;
            interpolated_read_resources = (1.0f - write_percentage) * interpolated_read_resources;
        }

        current_latency_data =
            LatencyData{interpolated_latency, interpolated_read_resources, interpolated_write_resources};
    }

#ifdef DEBUG_BWLAT
    std::cout << "For wp: " << write_percentage << " and bw: " << current_bandwidth
              << " computed a latency of: " << current_latency_data.latency << std::endl;
#endif

    read_bytes.push_back(0);
    write_bytes.push_back(0);

    return current_latency_data;
}

} // namespace ampere::memory

/*
 * vim: sw=4:ts=4:et:si:cindent:cino=\:0,(0,g0,t0:
 */
