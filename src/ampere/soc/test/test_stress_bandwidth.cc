//
// Copyright (c) 2020 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//

#include <unistd.h>
#include <cerrno>
#include <cstring>
#include "fmt/core.h"
#include "latency_test.h"

namespace ampere::soc {

using namespace ::testing;

TEST_F(LatencyTestDomino, BandwidthTest)
{
    get_environment()->parameters.load(TestModule::testconfig("soc/dn-quad1.cfg"), "soc");
    get_environment()->parameters.load(TestModule::testconfig("inj/prog_read.cfg"), "soc.core_0.source");
    get_environment()->parameters.add_setting("~soc\\.core_[0-9]+\\.enable", true, "test bench");
    get_environment()->parameters.add_setting("~soc\\.core_[1-9][0-9]*\\.stash_only", true, "test bench");

    Soc soc{"soc", clock, get_environment()};

    run_for(5000_ns);

    auto result_bw = bw();

    // Don't know what the "right" value is here, but we're just making sure that
    // we got something readonably sane.
    //
    EXPECT_GE(result_bw, 10.0);
    EXPECT_LE(result_bw, 30.0);
}

} // namespace ampere::soc

/*
 * vim: sw=4:ts=4:et:si:cindent:cino=\:0,(0,g0,t0:
 */
