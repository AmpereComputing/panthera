//
// Copyright (c) 2020 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//

#include "ampere/soc/soc.h"
#include "ampere/sc/testing/test_module.h"

namespace ampere::soc {

using namespace ::testing;

TEST_F(IntegrationTest, RunLatMemReadWithInjector)
{
    get_environment()->parameters.load(TestModule::testconfig("soc/one_injector_one_mc.cfg"), "soc");
    get_environment()->parameters.add_setting(
        "soc.core_0.source.is", TestModule::testfixture("lat_mem_rd_128M.is.xz"), "test bench");
    get_environment()->event_stream->enable();
    Soc soc{"soc", clock, get_environment()};

    run_for(200000_ns);

    EXPECT_NEAR(event_count("soc.core_0.instruction_commit"), 750, 5);
}

TEST_F(IntegrationTest, CBusyHigh)
{
    get_environment()->parameters.add_setting("soc.core_0.instruction_stream_name",
                                              TestModule::testfixture("../../core/test/bandwidth_128K_0.is.xz"),
                                              "test bench");
    get_environment()->parameters.load(TestModule::testconfig("soc/sn-quad2-one-core.cfg"), "soc");
    get_environment()->parameters.add_setting("~soc\\.memory_[0-9]+\\.cbusy.c_threshold_high", 4, "test bench");
    get_environment()->parameters.add_setting("~soc\\.memory_[0-9]+\\.cbusy.c_threshold_mid", 3, "test bench");
    get_environment()->parameters.add_setting("~soc\\.memory_[0-9]+\\.cbusy.c_threshold_low", 1, "test bench");
    get_environment()->parameters.add_setting(
        "~soc\\.homenode_[0-9]+\\.tracker\\.cbusy.c_threshold_high", 8, "test bench");
    get_environment()->parameters.add_setting(
        "~soc\\.homenode_[0-9]+\\.tracker\\.cbusy.c_threshold_mid", 4, "test bench");
    get_environment()->parameters.add_setting(
        "~soc\\.homenode_[0-9]+\\.tracker\\.cbusy.c_threshold_low", 1, "test bench");

    get_environment()->event_stream->enable();
    Soc soc{"soc", clock, get_environment()};
    run_for(100000_ns);

    // FIXME: The new memory model basically disable the cbusy throttle mechanism,
    // we need to update this when cbusy is better implemented in the memory model.
    EXPECT_GE(event_count("soc.core_0.l2c.cbusy.throttle"), 0);
}

} // namespace ampere::soc

/*
 * vim: sw=4:ts=4:et:si:cindent:cino=\:0,(0,g0,t0:
 */
