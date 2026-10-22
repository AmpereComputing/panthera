//
// Copyright (c) 2019 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//

#include "ampere/archx/driver.h"
#include "ampere/bobcat/bobcat_bpu.h"
#include "ampere/event_stream/testing/event_stream_dummy.h"
#include "ampere/sc/testing/test_module.h"

namespace bfs = boost::filesystem;

namespace ampere::bobcat {

using namespace archx;
using namespace core;
using namespace sc;

class BobcatIntegration : public IntegrationTest
{
protected:
    BobcatIntegration() : IntegrationTest()
    {
        get_environment()->parameters.load((bfs::path(MODEL_CONFIG_TREE) / "core/furiosa/tage.cfg").string(),
                                           "top.core.bpu");
        get_environment()->parameters.add_setting("top.core.bpu.max_instructions", "10000", "test bench");
        get_environment()->parameters.add_setting("top.core.decoders", "[\"furiosa\"]"_json, "test bench");
        get_environment()->event_stream->enable();

        bobcat = std::make_unique<BobcatTop>("top", clock, get_environment());

        sc_core::sc_report_handler::set_actions(sc_core::SC_ERROR,
                                                sc_core::SC_LOG | sc_core::SC_DISPLAY | sc_core::SC_ABORT);
        sc_core::sc_report_handler::set_verbosity_level(sc_core::SC_DEBUG);

        archx::Instruction::set_decoders({"furiosa"});
    }

    std::unique_ptr<BobcatTop> bobcat;
    std::unique_ptr<archx::Driver> driver;

    void load_test(const char* test_name)
    {
        auto instruction_stream_name = TestModule::testfixture(test_name);
        driver = ampere::archx::makeDriver(instruction_stream_name, 0);
        bobcat->set_driver(*driver);
    }
};

TEST_F(BobcatIntegration, BranchPair)
{
    load_test("../../core/test/branch_pair.is.xz");
    run_for(20000_ns);

    EXPECT_NEAR(event_count("top.core.bpu.br_train"), 5685, 10);
    EXPECT_LE(event_count("top.core.bpu.btc_clear"), 12);
    EXPECT_LE(event_count("top.core.bpu.br_recover"), 20);
}

TEST_F(BobcatIntegration, FlakyBranch)
{
    load_test("../../core/test/flaky_branch.is.xz");
    run_for(20000_ns);

    EXPECT_NEAR(event_count("top.core.bpu.br_train"), 3937, 10);
    EXPECT_LE(event_count("top.core.bpu.btc_clear"), 50);
    EXPECT_LE(event_count("top.core.bpu.br_recover"), 25);
}

TEST_F(BobcatIntegration, ConditionalDirectBranchWithHistory3)
{
    load_test("../../core/test/cbdirect_3.is.xz");
    std::string out;
    run_for(20000_ns);
    bobcat->print_stats(out);

    EXPECT_NEAR(event_count("top.core.bpu.br_train"), 1432, 10);
    EXPECT_LE(event_count("top.core.bpu.btc_clear"), 12);
    EXPECT_LE(event_count("top.core.bpu.br_recover"), 15);
}

TEST_F(BobcatIntegration, RepeatedCalls)
{
    load_test("../../core/test/repeated_calls.is.xz");
    std::string out;
    run_for(20000_ns);
    bobcat->print_stats_csv(out, std::cout);

    EXPECT_NEAR(event_count("top.core.bpu.br_train"), 3296, 10);
    EXPECT_LE(event_count("top.core.bpu.btc_clear"), 50);
    EXPECT_LE(event_count("top.core.bpu.br_recover"), 15);
}

TEST_F(BobcatIntegration, GCC_3_13171_chop)
{
    load_test("../../core/test/502.gcc-3.13171_chop.is.xz");
    std::string out;
    run_for(20000_ns);
    bobcat->print_stats_csv(out, std::cout);

    EXPECT_NEAR(event_count("top.core.bpu.br_train"), 3030, 10);
    EXPECT_LE(event_count("top.core.bpu.btc_clear"), 210);
    EXPECT_LE(event_count("top.core.bpu.br_recover"), 375);
}

} // namespace ampere::bobcat

/*
 * vim: sw=4:ts=4:et:si:cindent:cino=\:0,(0,g0,t0:
 */
