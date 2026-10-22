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

TEST_F(StressTestDomino, Prog2r1w)
{
    initialize("prog_2r1w");
    Soc soc{"soc", clock, get_environment()};
    run_for(5000_ns);
}

} // namespace ampere::soc

/*
 * vim: sw=4:ts=4:et:si:cindent:cino=\:0,(0,g0,t0:
 */
