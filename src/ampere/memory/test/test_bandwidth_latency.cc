//
// Copyright (c) 2022 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//

#include "ampere/memory/bandwidth_latency.h"
#include <gtest/gtest.h>

#include "ampere/sc/testing/assertions.h"
using ::testing::HasSubstr;

namespace ampere::memory {

TEST(BandwidthLatencyTest, testOneCurveOneLatency)
{
    json spec = "[{\"write_percentage\": 1.0, \"bw_latency\": [[10, 50]]}]"_json;
    BandwidthLatency bw(spec, 1);

    ASSERT_EQ(bw.compute_latency(0).latency, 50.0);
    ASSERT_EQ(bw.compute_latency(10).latency, 50.0);

    bw.increment_read_bytes(10);
    ASSERT_EQ(bw.compute_latency(10).latency, 50.0);

    bw.increment_read_bytes(1000);
    ASSERT_EQ(bw.compute_latency(10).latency, 50.0);

    bw.increment_write_bytes(10);
    ASSERT_EQ(bw.compute_latency(10).latency, 50.0);
}

TEST(BandwidthLatencyTest, testOneCurveOneLatencyWithPositiveLatOffset)
{
    json spec = "[{\"write_percentage\": 1.0, \"bw_latency\": [[10, 50]]}]"_json;
    BandwidthLatency bw(spec, 1, 8192, 8192, 1.0, 1.0, 10.0);

    ASSERT_EQ(bw.compute_latency(0).latency, 60.0);
    ASSERT_EQ(bw.compute_latency(10).latency, 60.0);

    bw.increment_read_bytes(10);
    ASSERT_EQ(bw.compute_latency(10).latency, 60.0);

    bw.increment_read_bytes(1000);
    ASSERT_EQ(bw.compute_latency(10).latency, 60.0);

    bw.increment_write_bytes(10);
    ASSERT_EQ(bw.compute_latency(10).latency, 60.0);
}

TEST(BandwidthLatencyTest, testOneCurveOneLatencyWithValidNegativeLatOffset)
{
    json spec = "[{\"write_percentage\": 1.0, \"bw_latency\": [[10, 50]]}]"_json;
    BandwidthLatency bw(spec, 1, 8192, 8192, 1.0, 1.0, -10.0);

    ASSERT_EQ(bw.compute_latency(0).latency, 40.0);
    ASSERT_EQ(bw.compute_latency(10).latency, 40.0);

    bw.increment_read_bytes(10);
    ASSERT_EQ(bw.compute_latency(10).latency, 40.0);

    bw.increment_read_bytes(1000);
    ASSERT_EQ(bw.compute_latency(10).latency, 40.0);

    bw.increment_write_bytes(10);
    ASSERT_EQ(bw.compute_latency(10).latency, 40.0);
}

TEST(BandwidthLatencyTest, testOneCurveThreeLatencies)
{
    json spec = "[{\"write_percentage\": 1.0, \"bw_latency\": [[10, 50], [20, 100], [30,400]]}]"_json;
    BandwidthLatency bw(spec, 1);

    bw.increment_read_bytes(8);
    ASSERT_EQ(bw.compute_latency(1).latency, 50.0);

    bw.increment_read_bytes(18);
    ASSERT_EQ(bw.compute_latency(1).latency, 90.0);

    bw.increment_read_bytes(28);
    ASSERT_EQ(bw.compute_latency(1).latency, 340.0);

    bw.increment_read_bytes(8);
    bw.increment_write_bytes(1.9);
    ASSERT_EQ(bw.compute_latency(1).latency, 50.0);

    bw.increment_read_bytes(18);
    bw.increment_write_bytes(1);
    ASSERT_EQ(bw.compute_latency(1).latency, 95.0);

    bw.increment_read_bytes(28);
    bw.increment_write_bytes(1);
    ASSERT_EQ(bw.compute_latency(1).latency, 370.0);

    bw.increment_read_bytes(8);
    bw.increment_write_bytes(2);
    ASSERT_EQ(bw.compute_latency(1).latency, 50.0);

    bw.increment_read_bytes(18);
    bw.increment_write_bytes(2);
    ASSERT_EQ(bw.compute_latency(1).latency, 100.0);
}

TEST(BandwidthLatencyTest, testTwoCurvesTwoLatencies)
{
    json spec =
        "[{\"write_percentage\": 0.50, \"bw_latency\": [[10, 100], [20, 200]]},{\"write_percentage\": 1.0, \"bw_latency\": [[10, 300], [20, 500]]}]"_json;
    BandwidthLatency bw(spec, 1);

    // 100% writes
    bw.increment_write_bytes(8);
    ASSERT_EQ(bw.compute_latency(1).latency, 300.0);

    bw.increment_write_bytes(18);
    ASSERT_EQ(bw.compute_latency(1).latency, 460.0);

    // 100% reads
    bw.increment_read_bytes(8);
    ASSERT_EQ(bw.compute_latency(1).latency, 100.0);
    bw.increment_read_bytes(18);
    ASSERT_EQ(bw.compute_latency(1).latency, 180.0);

    // 50% read, 50% writes
    bw.increment_read_bytes(4);
    bw.increment_write_bytes(4);
    ASSERT_EQ(bw.compute_latency(1).latency, 100.0);

    bw.increment_read_bytes(9);
    bw.increment_write_bytes(9);
    ASSERT_EQ(bw.compute_latency(1).latency, 180.0);
}

TEST(BandwidthLatencyTest, testDetectEmptyInterval)
{
    EXPECT_AMPERE_FATAL(
        {
            json spec = "[{\"write_percentage\": 1.0, \"bw_latency\": [[10, 300], [10, 500]]}]"_json;
            BandwidthLatency bw(spec, 1);
        },
        HasSubstr("Curve has empty or decreasing interval for write_percentage"));
}

TEST(BandwidthLatencyTest, testDetectInvertedInterval)
{
    EXPECT_AMPERE_FATAL(
        {
            json spec = "[{\"write_percentage\": 1.0, \"bw_latency\": [[20, 300], [10, 500]]}]"_json;
            BandwidthLatency bw(spec, 1);
        },
        HasSubstr("Curve has empty or decreasing interval for write_percentage"));
}

} // namespace ampere::memory
