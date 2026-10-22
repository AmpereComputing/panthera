//
// Copyright (c) 2020 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//

#include "latency_test.h"

namespace ampere::soc {

using namespace ::testing;

TEST_F(LatencyTestDomino, SLCMissLatency)
{
    get_environment()->parameters.load(TestModule::testconfig("soc/dn-quad1.cfg"), "soc");
    get_environment()->parameters.load(TestModule::testconfig("inj/prog_chase.cfg"), "soc.core_0.source");
    get_environment()->parameters.add_setting("~soc\\.core_[0-9]+\\.enable", true, "test bench");
    get_environment()->parameters.add_setting("~soc\\.core_[1-9][0-9]*\\.stash_only", true, "test bench");

    Soc soc{"soc", clock, get_environment()};

    run_for(2000_ns);

    // FIXME: I don't know what this number should be now that we've moved to MRDIMM 12800
    // in this config.
    EXPECT_NEAR(req_to_req_lat(), 120.0, 20.0);
}

} // namespace ampere::soc

/*
 * vim: sw=4:ts=4:et:si:cindent:cino=\:0,(0,g0,t0:
 */
