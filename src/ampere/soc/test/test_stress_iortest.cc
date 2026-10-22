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

TEST_F(LatencyTestDomino, IORTest)
{
    get_environment()->parameters.load(TestModule::testconfig("soc/dn-quad1.cfg"), "soc");
    get_environment()->parameters.load(TestModule::testconfig("inj/prog_ior_mixed.cfg"), "soc.iorinjector_0.source_0");
    get_environment()->parameters.add_setting("~soc.iorinjector_0.enable", true, "test bench");
    get_environment()->parameters.add_setting("~soc\\.core_[0-9]+\\.enable", true, "test bench");
    get_environment()->parameters.add_setting("~soc\\.core_[0-9]+\\.stash_only", true, "test bench");

    Soc soc{"soc", clock, get_environment()};

    run_for(5000_ns);
    if (::unlink("debug.es") == -1) {
        fmt::print(stderr, "ERROR: Unable to remove debug.es: errno={} {}\n", errno, std::strerror(errno));
    }
}

} // namespace ampere::soc

/*
 * vim: sw=4:ts=4:et:si:cindent:cino=\:0,(0,g0,t0:
 */
