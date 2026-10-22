//
// Copyright (c) 2025 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//

#include "ampere/archx/uop_info.h"
#include "ampere/core/fsu/bypass.h"

#include <gtest/gtest.h>
#include <nlohmann/json.hpp>

using nlohmann::json;

struct FSUBypassTest : ::testing::Test
{
    core::fsu_producer_unit producer{archx::FsuUnit::BU, 0, false};
    core::fsu_consumer_src consumer{archx::FsuUnit::BU, 0, ampere::archx::FsuSrcType::NORMAL};
    core::FSUBypassDefinition bypass;

    void SetUp() override
    {
        auto json = R"""({
            "latencies": {
                "BU": {
                    "unit": "BU",
                    "levels": {
                        "LOCAL": 2,
                        "UNROUNDED": 4,
                        "NORMAL": 3
                    }
                },
                "BU3": {
                    "unit": "BU",
                    "unit_suffix": 3,
                    "levels": {
                        "LATE": 2,
                        "LOCAL": 3,
                        "NORMAL": 4
                    }
                },
                "BUSrc2": {
                    "unit": "BU",
                    "source": 2,
                    "levels": {
                        "LOCAL": 5
                    }
                },
                "MA": {
                    "unit": "MA",
                    "levels": {
                        "LOCAL": 5,
                        "NORMAL": 6
                    }
                }
            },
            "locality": {
                "MA": ["MU"]
            }
        })"""_json;

        bypass = json.get<core::FSUBypassDefinition>();
    }
};

TEST_F(FSUBypassTest, ConsumerSameUnit)
{
    EXPECT_EQ(bypass.bypass_latency(producer, consumer), 2);
    EXPECT_EQ(bypass.bypass_latency(producer, {archx::FsuUnit::BU, 0, ampere::archx::FsuSrcType::NO_LOCAL_BYP}), 3);
}

TEST_F(FSUBypassTest, ConsumerSameUnitLate)
{
    EXPECT_EQ(
        bypass.bypass_latency({archx::FsuUnit::BU, 3, false}, {archx::FsuUnit::BU, 0, ampere::archx::FsuSrcType::LATE}),
        2);
}

TEST_F(FSUBypassTest, ConsumerSameUnitUnrounded)
{
    producer.dest_no_unrounded_bypass = true;
    EXPECT_EQ(bypass.bypass_latency(producer, consumer), 4);
}

TEST_F(FSUBypassTest, ConsumerSameUnitSpecificSource)
{
    consumer.src = 0;
    EXPECT_EQ(bypass.bypass_latency(producer, consumer), 2);

    consumer.src = 1;
    EXPECT_EQ(bypass.bypass_latency(producer, consumer), 5);
}

TEST_F(FSUBypassTest, ConsumerDifferentUnit)
{
    consumer.unit = ampere::archx::FsuUnit::MA;
    EXPECT_EQ(bypass.bypass_latency(producer, consumer), 3);

    producer.unit_suffix = 3;
    EXPECT_EQ(bypass.bypass_latency(producer, consumer), 4);
}

TEST_F(FSUBypassTest, ConsumerLocalUnit)
{
    producer.unit = ampere::archx::FsuUnit::MA;
    consumer.unit = ampere::archx::FsuUnit::MU;
    EXPECT_EQ(bypass.bypass_latency(producer, consumer), 5);

    consumer.unit = ampere::archx::FsuUnit::HP;
    EXPECT_EQ(bypass.bypass_latency(producer, consumer), 6);
}
