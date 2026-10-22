//
// Copyright (c) 2020 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//
#include <boost/algorithm/string/predicate.hpp>

#include "ampere/sc/testing/test_module.h"
#include "ampere/core/test/core_integration.h"

namespace bfs = boost::filesystem;

using namespace ::testing;

class BranchTest : public SysIntegrationTest
{
protected:
    void add_uint_data(const std::string& event_name,
                       int id,
                       const std::string& data_type_name,
                       std::uint64_t value) override
    {
        IntegrationTest::add_uint_data(event_name, id, data_type_name, value);

        if (boost::algorithm::ends_with(data_type_name, ".br_mispredicted")) {
            branches_retired += 1;
            mispredicts_retired += value;
        }
        if (boost::algorithm::ends_with(data_type_name, ".flush_type")) {
            switch (value) {
            case FlushType::BR_MISPREDICT:
                br_mispredict_flushes++;
                break;
            case FlushType::BRANCH_CHECK_UNCOND_BRANCH:
            case FlushType::BRANCH_CHECK_COND_BRANCH:
            case FlushType::BRANCH_CHECK_CALL_DIRECT:
            case FlushType::BRANCH_CHECK_CALL_INDIRECT:
            case FlushType::BRANCH_CHECK_UNCOND_INDIRECT:
            case FlushType::BRANCH_CHECK_RETURN:
            case FlushType::BRANCH_CHECK_TARGET:
            case FlushType::BRANCH_CHECK_WRONG_TYPE:
                branch_check_flushes++;
                break;
            default:
                other_flushes++;
                break;
            }
        }
    }

    uint64_t branches_retired{0};
    uint64_t mispredicts_retired{0};
    uint64_t br_mispredict_flushes{0};
    uint64_t branch_check_flushes{0};
    uint64_t other_flushes{0};
};

TEST_F(BranchTest, BranchPair)
{
    get_environment()->parameters.load(TestModule::testconfig("sys/furiosa-core-test.cfg"));

    run_instruction_stream(TestModule::testfixture("branch_pair.is.xz"), 20000_ns);

    EXPECT_GE(event_count("sys.soc_0.core_0.gpc.instruction_commit"), 15000);
    EXPECT_GE(branches_retired, 8000);
    EXPECT_LE(mispredicts_retired, 120);
}

TEST_F(BranchTest, FlakyBranch)
{
    get_environment()->parameters.load(TestModule::testconfig("sys/furiosa-core-test.cfg"));

    run_instruction_stream(TestModule::testfixture("flaky_branch.is.xz"), 20000_ns);

    EXPECT_GE(event_count("sys.soc_0.core_0.gpc.instruction_commit"), 20000);
    EXPECT_GE(branches_retired, 8000);
    EXPECT_LE(mispredicts_retired, 80);
}

TEST_F(BranchTest, ConditionalDirectBranchWithHistory3)
{
    get_environment()->parameters.load(TestModule::testconfig("sys/furiosa-core-test.cfg"));

    run_instruction_stream(TestModule::testfixture("cbdirect_3.is.xz"), 10000, 40000_ns);

    EXPECT_NEAR(event_count("sys.soc_0.core_0.gpc.instruction_commit"), 10000, 4);
    EXPECT_NEAR(branches_retired, 1450, 30);
    EXPECT_LE(mispredicts_retired, 200);
}

TEST_F(BranchTest, RepeatedCalls)
{
    get_environment()->parameters.load(TestModule::testconfig("sys/furiosa-core-test.cfg"));

    run_instruction_stream(TestModule::testfixture("repeated_calls.is.xz"), 40000_ns);

    EXPECT_GE(event_count("sys.soc_0.core_0.gpc.instruction_commit"), 15000);
    EXPECT_GE(branches_retired, 5000);
    EXPECT_LE(mispredicts_retired, 70);
}

TEST_F(BranchTest, IndirectCalls)
{
    get_environment()->parameters.load(TestModule::testconfig("sys/furiosa-core-test.cfg"));
    get_environment()->parameters.add_setting("sys.soc_0.core_0.bpu.ittage_delay", 2, "test bench");

    run_instruction_stream(TestModule::testfixture("indirect.is.xz"), 25000_ns);

    EXPECT_GE(event_count("sys.soc_0.core_0.gpc.instruction_commit"), 9999);
    EXPECT_GE(branches_retired, 2000);
    EXPECT_LE(mispredicts_retired, 50);
}

TEST_F(BranchTest, testPerfectBPU)
{
    get_environment()->parameters.load(TestModule::testconfig("sys/furiosa-core-test.cfg"));
    get_environment()->parameters.add_setting("sys.soc_0.core_0.bpu.perfect", "true", "test bench");

    run_instruction_stream(TestModule::testfixture("523.xalancbmk-1.8395_chop.is.xz"), 5000, 40000_ns);

    EXPECT_NEAR(event_count("sys.soc_0.core_0.gpc.instruction_commit"), 5000, 5);
    EXPECT_EQ(mispredicts_retired, 0);
    EXPECT_EQ(br_mispredict_flushes, 0);
    EXPECT_EQ(branch_check_flushes, 0);
    EXPECT_EQ(other_flushes, 0);
}

TEST_F(CoreIntegrationTest, testLoopDetector)
{
    get_environment()->parameters.load(TestModule::testconfig("sys/rogue.cfg"));
    get_environment()->parameters.add_setting("sys.soc_0.core_0.bpu.ld_enabled", "true", "test bench");
    get_environment()->parameters.add_setting("sys.soc_0.core_0.icf_out_to_idr_in_fifo.bandwidth", 32, "test bench");

    run_instruction_stream(TestModule::testfixture("dhrystone.is.xz"), 5000, 30000_ns);

    EXPECT_NEAR(event_count("sys.soc_0.core_0.gpc.instruction_commit"), 5000, 5);
    EXPECT_GE(event_count("sys.soc_0.core_0.icf.loop_replay"), 30);
    EXPECT_GE(event_count("sys.soc_0.core_0.gpc.looped_instruction_commit"), 100);
}
