//
// Copyright (c) 2019 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//
#include <boost/filesystem/path.hpp>
#include "ampere/archx/driver_is.h"
#include "ampere/archx/testing/mock_driver.h"
#include <gtest/gtest.h>

namespace bfs = boost::filesystem;

namespace ampere {
namespace archx {

class DriverTest : public ::testing::Test
{
protected:
    std::string testfixture(std::string filename) { return (bfs::path(TEST_FIXTURE_DIRECTORY) / filename).string(); }

    void SetUp() override { Instruction::set_decoders({"furiosa"}); }
};

TEST_F(DriverTest, TestEmptyStream)
{
    ASSERT_THROW(DriverIS driver("/dev/null"), std::runtime_error);
}

TEST_F(DriverTest, TestConstructor)
{
    DriverIS driver(testfixture("mix/istream.30638.30638.is.xz"));
}

TEST_F(DriverTest, TestFetchTwoInstructions)
{
    DriverIS driver(testfixture("mix/istream.30638.30638.is.xz"));

    auto i = driver.fetch(driver.next_pc());
    ASSERT_EQ(ProgramCounter(0x400494), i->program_counter());
    ASSERT_EQ(0xd280001d, i->opcode());

    i = driver.fetch(driver.next_pc());
    ASSERT_EQ(ProgramCounter(0x400498), i->program_counter());
    ASSERT_EQ(0xd280001e, i->opcode());
}

TEST_F(DriverTest, TestFetchInstructionID)
{
    DriverIS driver(testfixture("mix/istream.30638.30638.is.xz"));

    ASSERT_EQ(driver.fetch(driver.next_pc())->inum().id(), 1);
    ASSERT_EQ(driver.fetch(driver.next_pc())->inum().id(), 2);
}

TEST_F(DriverTest, TestIndependentNextPCandFetch)
{
    DriverIS driver(testfixture("mix/istream.30638.30638.is.xz"));
    std::queue<ProgramCounter> pcs;

    pcs.push(driver.next_pc());
    ASSERT_EQ(ProgramCounter(0x400494), pcs.back());

    pcs.push(driver.next_pc());
    ASSERT_EQ(ProgramCounter(0x400498), pcs.back());

    pcs.push(driver.next_pc());
    ASSERT_EQ(ProgramCounter(0x40049c), pcs.back());

    for (int i = 0; i < 3; i++) {
        auto pc = pcs.front();
        pcs.pop();
        ASSERT_EQ(driver.fetch(pc)->inum().id(), i + 1);
    }

    auto pc = driver.next_pc();
    ASSERT_EQ(ProgramCounter(0x4004a0), pc);
    ASSERT_EQ(driver.fetch(pc)->inum().id(), 4);

    pc = driver.next_pc();
    ASSERT_EQ(driver.fetch(ProgramCounter(0xdeadbeef))->inum().id(), -1);

    ASSERT_EQ(ProgramCounter(0x4004a4), pc);
    ASSERT_EQ(driver.fetch(pc)->inum().id(), -2);
    driver.recover(4);
    ASSERT_EQ(driver.fetch(pc)->inum().id(), 5);
}

TEST_F(DriverTest, TestBranchTargetCalculation)
{
    DriverIS driver(testfixture("mix/istream.30638.30638.is.xz"));

    for (unsigned i = 0; i < 18; i++) {
        auto pc = driver.next_pc();
        auto instruction = driver.fetch(pc);
        ASSERT_EQ(i + 1, instruction->inum().id());
        ASSERT_EQ(pc + 4, instruction->next_program_counter());
    }

    auto instruction = driver.fetch(driver.next_pc());
    ASSERT_EQ(ProgramCounter(0x4006a8), instruction->next_program_counter());
}

TEST_F(DriverTest, TestCommit)
{
    DriverIS driver(testfixture("mix/istream.30638.30638.is.xz"));

    auto pc = driver.next_pc();
    auto instruction = driver.fetch(pc);
    ASSERT_EQ(1, instruction->inum().id());
    ASSERT_EQ(pc + 4, instruction->next_program_counter());
    driver.commit(instruction);

    // Trying to commit the same instruction again will throw an exception
    ASSERT_ANY_THROW(driver.commit(instruction));
}

TEST_F(DriverTest, TestUDIVSourceRegs)
{
    DriverIS driver(testfixture("523-xalancbmk-maxk100-10mws-seed1.96264_0_udiv_reg_test.is.xz"));

    // first two instructions are uninteresting
    auto pc = driver.next_pc();
    ASSERT_EQ(0xffff0000101553f4, pc.value());
    auto instruction = driver.fetch(pc);
    ASSERT_EQ(0, instruction->divide_srcs());
    driver.commit(instruction);

    pc = driver.next_pc();
    ASSERT_EQ(0xffff0000101553f8, pc.value());
    instruction = driver.fetch(pc);
    ASSERT_EQ(0, instruction->divide_srcs());
    driver.commit(instruction);

    // this instruction is: `udiv  x3, x3, x4` x3 -> 0x2da2c8c x4 -> 0xb681
    pc = driver.next_pc();
    ASSERT_EQ(0xffff0000101553fc, pc.value());
    instruction = driver.fetch(pc);
    ASSERT_EQ(2, instruction->divide_srcs());
    uint64_t x3 = instruction->state().read(instruction->divide_src(0));
    uint64_t x4 = instruction->state().read(instruction->divide_src(1));
    ASSERT_EQ(0x2da2c8c, x3);
    ASSERT_EQ(0xb681, x4);
    driver.commit(instruction);

    // `udiv  x1, x1, x4` x1 -> 0x2d9ff82 x4 -> 0xb681
    pc = driver.next_pc();
    ASSERT_EQ(0xffff000010155400, pc.value());
    instruction = driver.fetch(pc);
    ASSERT_EQ(2, instruction->divide_srcs());
    uint64_t x1 = instruction->state().read(instruction->divide_src(0));
    x4 = instruction->state().read(instruction->divide_src(1));
    ASSERT_EQ(0x2d9ff82, x1);
    ASSERT_EQ(0xb681, x4);
    driver.commit(instruction);

    // `udiv  w2, w21, w4` x4 -> 0xb681 x21 -> 0x2d9ff82
    pc = driver.next_pc();
    ASSERT_EQ(0xffff000010155404, pc.value());
    instruction = driver.fetch(pc);
    ASSERT_EQ(2, instruction->divide_srcs());
    uint64_t x21 = instruction->state().read(instruction->divide_src(0));
    x4 = instruction->state().read(instruction->divide_src(1));
    ASSERT_EQ(0x2d9ff82, x21);
    ASSERT_EQ(0xb681, x4);
    driver.commit(instruction);

    // uninteresting instruction, again
    pc = driver.next_pc();
    ASSERT_EQ(0xffff000010155408, pc.value());
    instruction = driver.fetch(pc);
    ASSERT_EQ(0, instruction->divide_srcs());
    driver.commit(instruction);
}

TEST_F(DriverTest, TestVFPSourceRegs)
{
    DriverIS driver(testfixture("521-wrf-1.19880_0_fdiv_reg_test.is.xz"));

    // first two instructions are uninteresting
    auto pc = driver.next_pc();
    ASSERT_EQ(0x4f0310, pc.value());
    auto instruction = driver.fetch(pc);
    ASSERT_EQ(0, instruction->divide_srcs());
    driver.commit(instruction);

    pc = driver.next_pc();
    ASSERT_EQ(0x4f0314, pc.value());
    instruction = driver.fetch(pc);
    ASSERT_EQ(0, instruction->divide_srcs());
    driver.commit(instruction);

    // this instruction is fdiv  s30, s18, s25` v18 -> 0x0_377041c9 v25 -> 0x0_398d6f89
    pc = driver.next_pc();
    ASSERT_EQ(0x4f0318, pc.value());
    instruction = driver.fetch(pc);
    ASSERT_EQ(2, instruction->divide_srcs());
    auto v18 = instruction->state().read_vfp(instruction->divide_src(0));
    auto v25 = instruction->state().read_vfp(instruction->divide_src(1));
    ASSERT_EQ(0x377041c9, v18[0]);
    ASSERT_EQ(0x0, v18[1]);
    ASSERT_EQ(0x398d6f89, v25[0]);
    ASSERT_EQ(0x0, v25[1]);

    driver.commit(instruction);
}

} // namespace archx
} // namespace ampere

/*
 * vim: sw=4:ts=4:et:si:cindent:cino=\:0,(0,g0,t0:
 */
