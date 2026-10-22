//
// Copyright (c) 2022 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//
#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "ampere/sc/testing/test_module.h"
#include "ampere/soc/soc.h"

namespace ampere::soc {

using namespace ampere::mesh;
using namespace ampere::archx;

TEST_F(IntegrationTest, PhaPointerChase)
{
    auto& params = get_environment()->parameters;

    params.load(TestModule::testconfig("soc/domino-pha.cfg"), "soc");
    params.load(TestModule::testconfig("inj/prog_chase.cfg"), "soc.core_0.source");
    params.add_setting("soc.core_0.source.loop_count", 5000, "test bench");
    params.add_setting("soc.core_0.source.enable_events_on_start", true, "test bench");

    Soc soc{"soc", clock, get_environment()};
    run_for(1200000_ns);

    EXPECT_NEAR(event_count("soc.core_0.instruction_commit"), 5000, 5);
}

TEST_F(IntegrationTest, PhaStreamingReads)
{
    auto& params = get_environment()->parameters;

    params.load(TestModule::testconfig("soc/domino-pha.cfg"), "soc");
    params.load(TestModule::testconfig("inj/prog_read.cfg"), "soc.core_0.source");
    params.add_setting("soc.core_0.source.enable_events_on_start", true, "test bench");

    Soc soc{"soc", clock, get_environment()};
    run_for(20000_ns);

    // FIXME: this is going to be flaky; need a better check
    EXPECT_NEAR(event_count("soc.core_0.instruction_commit"), 3351, 500);
}

TEST_F(IntegrationTest, PhaStreamingWrites)
{
    auto& params = get_environment()->parameters;

    params.load(TestModule::testconfig("soc/domino-pha.cfg"), "soc");
    params.load(TestModule::testconfig("inj/prog_write.cfg"), "soc.core_0.source");
    params.add_setting("soc.core_0.source.enable_events_on_start", true, "test bench");

    Soc soc{"soc", clock, get_environment()};
    run_for(20000_ns);

    // FIXME: this is going to be flaky; need a better check
    EXPECT_NEAR(event_count("soc.core_0.instruction_commit"), 3351, 500);
}

TEST_F(IntegrationTest, ReadFromL3MultipleTimes)
{
    auto& params = get_environment()->parameters;

    params.load(TestModule::testconfig("soc/domino-pha.cfg"), "soc");

    // Make eviction easy by dialing injector 0's L2 down to 1 set, 1 way
    params.add_setting("soc.core_0.cache.sets", 1, "test bench");
    params.add_setting("soc.core_0.cache.ways", 1, "test bench");

    // Turn off stashing on injector 2 so only injector 1 will be used for this
    params.add_setting("soc.core_2.enable", false, "test bench");

    params.add_setting("soc.core_0.source.enable_events_on_start", true, "test bench");

    // The program for core_0 must achieve the following:
    //
    //   1. Warm address A into the L3 (core_1's L3 specifically---it's the only choice)
    //   2. ReadShared address A
    //   3. Evict address A (the line should still be in core_1's L3)
    //   4. ReadShared address A again (response should be from core_1)
    //   5. Evict address A a second time (this is to trigger the bug, which manifests when the PxHA
    //      tries to stash the line *again* into core_1)
    //
    params.add_setting("soc.core_0.source.type", "UopSourceProg", "test bench");
    params.add_setting("soc.core_0.source.decoders", "[\"furiosa\"]"_json, "test bench");
    params.add_setting("soc.core_0.source.program",
                       R"(
        {
            "code": [
                {"op": "LDR_64_ldst_immpost", "fields": {"imm9": 0, "Rn": 1, "Rt": 2}, "pa": "0x2000"},
                {"op": "LDR_64_ldst_immpost", "fields": {"imm9": 0, "Rn": 2, "Rt": 3}, "pa": "0x3000"},
                {"op": "LDR_64_ldst_immpost", "fields": {"imm9": 0, "Rn": 3, "Rt": 4}, "pa": "0x2000"},
                {"op": "LDR_64_ldst_immpost", "fields": {"imm9": 0, "Rn": 4, "Rt": 5}, "pa": "0x3000"},
                {"op": "ADD_64_addsub_ext", "fields": {"Rm": 5, "Rn": 5, "Rd": 5, "imm3": 0, "option": 0}, "repeat": 1000}
            ]
        }
    )"_json,
                       "test bench");
    params.add_setting("soc.core_0.source.warmup",
                       R"(
        [
            {"address": "0x2000", "state": "SC"}
        ]
    )"_json,
                       "test bench");

    Soc soc{"soc", clock, get_environment()};
    run_for(20000_ns);

    EXPECT_GE(event_count("soc.core_0.instruction_commit"), 1000);

    // Inj0 should receive data for all 4 loads, 2 of which should come from Inj1's L3
    EXPECT_EQ(event_count("soc.core_0.recv_dat"), 4);
    EXPECT_EQ(event_count("soc.core_1.send_dat"), 2);
}

} // namespace ampere::soc
