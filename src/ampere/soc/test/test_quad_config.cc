//
// Copyright (c) 2020 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//

#include "latency_test.h"

namespace ampere::soc {

using namespace ::testing;

TEST_F(LatencyTest, RogueConfigQuad)
{
    get_environment()->parameters.load(TestModule::testconfig("soc/rg-quad.cfg"), "soc");
    get_environment()->parameters.add_setting("soc.core_12.enable", true, "test bench");
    get_environment()->parameters.load(TestModule::testconfig("inj/prog_chase.cfg"), "soc.core_12.source");

    Soc soc{"soc", clock, get_environment()};

    std::cout << "Instructions commited: " << event_count("soc.core_12.instruction_commit") << std::endl;
    std::cout << "Req to req latency: " << req_to_req_lat() << std::endl;
}

} // namespace ampere::soc

/*
 * vim: sw=4:ts=4:et:si:cindent:cino=\:0,(0,g0,t0:
 */
