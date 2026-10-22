//
// Copyright (c) 2022 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//
#include <boost/filesystem/path.hpp>
#include "ampere/archx/driver_bs.h"
#include "ampere/archx/testing/mock_driver.h"
#include <gtest/gtest.h>

namespace bfs = boost::filesystem;

namespace ampere {
namespace archx {

class DriverBSTest : public ::testing::Test
{
protected:
    std::string testfixture(std::string filename) { return (bfs::path(TEST_FIXTURE_DIRECTORY) / filename).string(); }

    void SetUp() override { Instruction::set_decoders({"furiosa"}); }
};

TEST_F(DriverBSTest, TestEmptyStream)
{
    ASSERT_THROW(DriverBS driver("/dev/null"), std::runtime_error);
}

TEST_F(DriverBSTest, TestConstructor)
{
    DriverBS driver1(testfixture("memcached-bigdata-EL1.2650_198_20220812_c68825.bs.xz"));
    DriverBS driver2(testfixture("pyperf-sqlite-EL1.21_188_20220803_9dafdd.bs.xz"));
}

TEST_F(DriverBSTest, TestFetchInstructionTypes)
{
    DriverBS driver(testfixture("pyperf-sqlite-EL1.21_188_20220803_9dafdd.bs.xz"));

    // unconditional branch (direct call)
    auto i = driver.fetch(driver.next_pc());
    ASSERT_EQ(ProgramCounter(0xffffa91d5d7fde30), i->program_counter());
    ASSERT_TRUE(i->is_branch());
    ASSERT_TRUE(i->is_taken_branch());
    ASSERT_EQ(BranchType::CALL_DIRECT, i->branch_type());

    for (uint64_t pc = 0xffffa91d5d5663d0; pc < 0xffffa91d5d5663fc; pc += 4) {
        i = driver.fetch(driver.next_pc());
        ASSERT_EQ(ProgramCounter(pc), i->program_counter());
        ASSERT_FALSE(i->is_branch());
    }

    // untaken conditional branch
    i = driver.fetch(driver.next_pc());
    ASSERT_EQ(ProgramCounter(0xffffa91d5d5663fc), i->program_counter());
    ASSERT_TRUE(i->is_branch());
    ASSERT_FALSE(i->is_taken_branch());
    ASSERT_EQ(BranchType::CONDITIONAL_DIRECT, i->branch_type());

    for (uint64_t pc = 0xffffa91d5d566400; pc < 0xffffa91d5d566410; pc += 4) {
        i = driver.fetch(driver.next_pc());
        ASSERT_EQ(ProgramCounter(pc), i->program_counter());
        ASSERT_FALSE(i->is_branch());
    }

    // taken conditional branch
    i = driver.fetch(driver.next_pc());
    ASSERT_EQ(ProgramCounter(0xffffa91d5d566410), i->program_counter());
    ASSERT_TRUE(i->is_branch());
    ASSERT_TRUE(i->is_taken_branch());
    ASSERT_EQ(BranchType::CONDITIONAL_DIRECT, i->branch_type());

    for (uint64_t pc = 0xffffa91d5d56643c; pc < 0xffffa91d5d566440; pc += 4) {
        i = driver.fetch(driver.next_pc());
        ASSERT_EQ(ProgramCounter(pc), i->program_counter());
        ASSERT_FALSE(i->is_branch());
    }
}

TEST_F(DriverBSTest, TestFetchInstructionID)
{
    DriverBS driver1(testfixture("memcached-bigdata-EL1.2650_198_20220812_c68825.bs.xz"));
    for (int i = 1; i < 200; i++) {
        ASSERT_EQ(driver1.fetch(driver1.next_pc())->inum().id(), i);
    }

    DriverBS driver2(testfixture("pyperf-sqlite-EL1.21_188_20220803_9dafdd.bs.xz"));
    for (int i = 1; i < 200; i++) {
        ASSERT_EQ(driver2.fetch(driver2.next_pc())->inum().id(), i);
    }
}

TEST_F(DriverBSTest, TestIndependentNextPCandFetch)
{
    DriverBS driver(testfixture("pyperf-sqlite-EL1.21_188_20220803_9dafdd.bs.xz"));
    std::queue<ProgramCounter> pcs;

    pcs.push(driver.next_pc());
    ASSERT_EQ(ProgramCounter(0xffffa91d5d7fde30), pcs.back());

    pcs.push(driver.next_pc());
    ASSERT_EQ(ProgramCounter(0xffffa91d5d5663d0), pcs.back());

    pcs.push(driver.next_pc());
    ASSERT_EQ(ProgramCounter(0xffffa91d5d5663d4), pcs.back());

    for (int i = 0; i < 3; i++) {
        auto pc = pcs.front();
        pcs.pop();
        ASSERT_EQ(driver.fetch(pc)->inum().id(), i + 1);
    }

    auto pc = driver.next_pc();
    ASSERT_EQ(ProgramCounter(0xffffa91d5d5663d8), pc);
    ASSERT_EQ(driver.fetch(pc)->inum().id(), 4);

    pc = driver.next_pc();
    ASSERT_EQ(driver.fetch(ProgramCounter(0xdeadbeef))->inum().id(), 0);

    ASSERT_EQ(ProgramCounter(0xffffa91d5d5663dc), pc);
    ASSERT_EQ(driver.fetch(pc)->inum().id(), 0);
    driver.recover(4);
    ASSERT_EQ(driver.fetch(pc)->inum().id(), 5);
}

TEST_F(DriverBSTest, TestBranchTargetCalculation)
{
    DriverBS driver(testfixture("memcached-bigdata-EL1.2650_198_20220812_c68825.bs.xz"));

    for (unsigned i = 0; i < 13; i++) {
        auto pc = driver.next_pc();
        auto instruction = driver.fetch(pc);
        ASSERT_EQ(i + 1, instruction->inum().id());
        ASSERT_EQ(pc + 4, instruction->next_program_counter());
    }

    auto instruction = driver.fetch(driver.next_pc());
    ASSERT_TRUE(instruction->is_taken_branch());
    ASSERT_EQ(ProgramCounter(0x4207cc), instruction->next_program_counter());
}

TEST_F(DriverBSTest, TestCommit)
{
    DriverBS driver(testfixture("memcached-bigdata-EL1.2650_198_20220812_c68825.bs.xz"));

    auto pc = driver.next_pc();
    auto instruction = driver.fetch(pc);
    ASSERT_EQ(1, instruction->inum().id());
    ASSERT_EQ(pc + 4, instruction->next_program_counter());
    driver.commit(instruction);

    // Trying to commit the same instruction again will throw an exception
    ASSERT_ANY_THROW(driver.commit(instruction));
}

} // namespace archx
} // namespace ampere

/*
 * vim: sw=4:ts=4:et:si:cindent:cino=\:0,(0,g0,t0:
 */
