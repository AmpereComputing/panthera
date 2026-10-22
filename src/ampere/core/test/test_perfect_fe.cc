//
// Copyright (c) 2021 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//

#include <ampere/archx/instruction_simple.h>

#include "ampere/core/perfect_fe.h"
#include "ampere/core/uop.h"
#include "ampere/core/testing/instruction_consumer.h"
#include "ampere/sc/testing/clock_dummy.h"
#include "ampere/sc/testing/test_module.h"
#include "ampere/sc/testing/test_interfaces.h"
#include "ampere/core/testing/mock_mmu.h"
#include "ampere/archx/testing/mock_driver.h"
#include "ampere/core/testing/mock_l2.h"
#include "ampere/core/testing/mock_flush.h"

namespace ampere {
namespace perfect_fe {

using namespace core;
using namespace uarch_common;
using namespace ::testing;

class PerfectFETest : public testing::Test
{
protected:
    explicit PerfectFETest() = default;

protected:
    ClockDummy clock;
    Environment env{"system",
                    {
                        {"top.pfe.instruction_fifo.size", 99},
                    }};

    void set(const std::string& name, const ampere::sc::json& value)
    {
        env.parameters.add_setting(name, value, "test bench");
    }
};

class Top : public Module
{
public:
    Top(const sc_core::sc_module_name& name, Clock* clock, Environment* env) : Module(name, clock, env)
    {
        pfe.bob_alloc.bind(uop_generator);
        pfe.bob_credit_return.bind(bob_credit_releaser);
        pfe.branches_committed.bind(commits);
        pfe.feq_credit_return.bind(feq_credit_return);
        pfe.frontend_stall.bind(frontend_stall);
        pfe.instructions_committed.bind(instructions_committed);
        pfe.iq_write_port.bind(ic);
        pfe.driver.bind(driver);
        pfe.snoop_port.bind(snoop_port);
        pfe.flush_port.bind(pfe_flush);
        pfe.instruction_credit_return.bind(credit_return);
        pfe.pmu_idr_tlb_miss.bind(pmu_tlb_miss);
        pfe.pmu_idr_icache_miss.bind(pmu_cache_miss);
        pfe.pmu_idr_l2_miss.bind(pmu_feq_l2_miss);
        pfe.pmu_idr_feq_empty.bind(pmu_feq_empty);
        pfe.pmu_idr_mem_stall.bind(pmu_mem_stall);
        pfe.pmu_idr_btc_clear.bind(pmu_btc_clear);
        pfe.pmu_idr_btb_clear.bind(pmu_btb_clear);
        pfe.tlbi_request.bind(tlbi_in_mock);
        connect(pfe.miss_request, l2.request);
        connect(l2.response, pfe.miss_response);
        pfe.pmu_l2_miss_in_icf.bind(pmu_l2_miss);
        archx::Instruction::set_decoders({"furiosa"});

        ON_CALL(snoop_port, can_take()).WillByDefault(Return(false));
    }

    PerfectFE pfe{"pfe"};
    L2InclusiveDummy l2{"l2"};

    InstructionConsumer ic;
    NiceMock<MockDriver> driver;
    NiceMock<MockTaker<MemReq>> snoop_port;
    NiceMock<MockFlushReader> pfe_flush;
    NiceMock<MockTLBIReqReader> tlbi_in_mock;
    NiceMock<MockTaker<Credit>> credit_return;
    NiceMock<MockPutter<bool>> pmu_tlb_miss;
    NiceMock<MockPutter<bool>> pmu_cache_miss;
    NiceMock<MockPutter<bool>> pmu_feq_l2_miss;
    NiceMock<MockPutter<bool>> pmu_feq_empty;
    NiceMock<MockPutter<bool>> pmu_mem_stall;
    NiceMock<MockPutter<bool>> pmu_btc_clear;
    NiceMock<MockPutter<bool>> pmu_btb_clear;
    NiceMock<MockReader<PhysAddr>> pmu_l2_miss;
    NiceMock<MockTaker<uint>> frontend_stall;
    NiceMock<MockTaker<uint>> instructions_committed;
    NiceMock<MockTaker<Credit>> feq_credit_return;
    Producer<core::Uop> uop_generator;
    CreditReleaser bob_credit_releaser;
    Producer<uint> commits;
};

TEST_F(PerfectFETest, TestUnlimitedInstructions)
{
    Top top("top", &clock, &env);

    ProgramCounter pc(0x113c);

    EXPECT_CALL(top.driver, fetch(_)).WillRepeatedly(Return(top.driver.return_nop(pc)));

    // Ensure the number of instructions sent is always the maximum allowed in a given cycle
    top.ic.set_max_size(4);
    clock.tick(1);
    ASSERT_EQ(4, top.ic.instructions.size());

    // limited to 99 by the size of the instruction_fifo
    top.ic.set_max_size(999);
    clock.tick(1);
    ASSERT_EQ(99, top.ic.instructions.size());

    // Clear the instruction consumer, and make sure we fill it up immediately yet again
    top.ic.consume_instructions(99);
    ASSERT_EQ(0, top.ic.instructions.size());
    clock.tick(1);
    ASSERT_EQ(99, top.ic.instructions.size());

    top.ic.release();
}

TEST_F(PerfectFETest, TestFetchingAcrossHalfCachelines)
{
    Top top("top", &clock, &env);

    ProgramCounter pc(0x1138);

    EXPECT_CALL(top.driver, fetch(_))
        .WillOnce(Return(ByMove(top.driver.return_nop(pc))))
        .WillOnce(Return(ByMove(top.driver.return_nop(pc + 4))))
        .WillOnce(Return(ByMove(top.driver.return_nop(pc + 8))))
        .WillOnce(Return(ByMove(top.driver.return_nop(pc + 12))))
        .WillOnce(Return(ByMove(top.driver.return_nop(pc + 16))))
        .WillOnce(Return(ByMove(top.driver.return_nop(pc + 20 + 8))));

    top.ic.set_max_size(6);

    clock.tick(1);
    ASSERT_EQ(6, top.ic.instructions.size());

    top.ic.release();
}

TEST_F(PerfectFETest, TestNotFetchingAcrossHalfCachelines)
{
    // Set alignment knob to 5 bits, meaning that we don't fetch across a
    // half-cacheline boundary in a single cycle
    set("top.pfe.fetch_alignment_bits", "5");
    Top top("top", &clock, &env);

    ProgramCounter pc(0x1138);

    EXPECT_CALL(top.driver, fetch(_))
        .WillOnce(Return(ByMove(top.driver.return_nop(pc))))       // PC: 0x1138
        .WillOnce(Return(ByMove(top.driver.return_nop(pc + 4))))   // PC: 0x113c
        .WillOnce(Return(ByMove(top.driver.return_nop(pc + 8))))   // PC: 0x1140 <- new half-cacheline
        .WillOnce(Return(ByMove(top.driver.return_nop(pc + 8))))   // PC: 0x1140 (repeated because it was put back)
        .WillOnce(Return(ByMove(top.driver.return_nop(pc + 12))))  // PC: 0x1144
        .WillOnce(Return(ByMove(top.driver.return_nop(pc + 32))))  // PC: 0x1158
        .WillOnce(Return(ByMove(top.driver.return_nop(pc + 36))))  // PC: 0x115c
        .WillOnce(Return(ByMove(top.driver.return_nop(pc + 40))))  // PC: 0x1160 <- new half-cacheline
        .WillOnce(Return(ByMove(top.driver.return_nop(pc + 40))))  // PC: 0x1160 (repeated because it was put back)
        .WillOnce(Return(ByMove(top.driver.return_nop(pc + 68))))  // PC: 0x117c
        .WillOnce(Return(ByMove(top.driver.return_nop(pc + 72)))); // PC: 0x1180 <- new half-cacheline

    // driver.recover() will get called three times because the perfect
    // frontend 'recovers' back to the first instruction in the next cacheline
    // (or other alignment boundary) boundary if it goes past the end of one.
    EXPECT_CALL(top.driver, recover(_)).Times(3);

    // First cycle fetches first 2 instructions, through 0x113c
    clock.tick(1);
    ASSERT_EQ(2, top.ic.instructions.size());

    // Second cycle fetches next 4 instructions, through 0x115c
    clock.tick(1);
    ASSERT_EQ(6, top.ic.instructions.size());

    // Third cycle fetches next 2 instructions, through 0x117c
    clock.tick(1);
    ASSERT_EQ(8, top.ic.instructions.size());

    top.ic.release();
}

TEST_F(PerfectFETest, TestFetchingAcrossTakenBranches)
{
    Top top("top", &clock, &env);

    ProgramCounter pc(0xf00ba7);

    EXPECT_CALL(top.driver, fetch(_))
        .WillOnce(Return(ByMove(top.driver.return_nop(pc))))
        .WillOnce(Return(ByMove(top.driver.return_b(pc))))
        .WillOnce(Return(ByMove(top.driver.return_nop(pc))))
        .WillOnce(Return(ByMove(top.driver.return_b(pc))))
        .WillOnce(Return(ByMove(top.driver.return_nop(pc))))
        .WillOnce(Return(ByMove(top.driver.return_b(pc))))
        .WillOnce(Return(ByMove(top.driver.return_nop(pc))))
        .WillOnce(Return(ByMove(top.driver.return_b(pc))))
        .WillOnce(Return(ByMove(top.driver.return_nop(pc))))
        .WillOnce(Return(ByMove(top.driver.return_b(pc))));

    top.ic.set_max_size(10);
    clock.tick(1);
    ASSERT_EQ(10, top.ic.instructions.size());

    top.ic.release();
}

TEST_F(PerfectFETest, TestFetchingAcrossOneTakenBranch)
{
    set("top.pfe.taken_branches_per_cycle", "1");
    Top top("top", &clock, &env);

    ProgramCounter pc(0xf00ba7);

    EXPECT_CALL(top.driver, fetch(_))
        .WillOnce(Return(ByMove(top.driver.return_nop(pc))))
        .WillOnce(Return(ByMove(top.driver.return_b(pc))))
        .WillOnce(Return(ByMove(top.driver.return_b(pc))))
        .WillOnce(Return(ByMove(top.driver.return_nop(pc))))
        .WillOnce(Return(ByMove(top.driver.return_b(pc))))
        .WillOnce(Return(ByMove(top.driver.return_nop(pc))))
        .WillOnce(Return(ByMove(top.driver.return_nop(pc))))
        .WillOnce(Return(ByMove(top.driver.return_b(pc))));

    top.ic.set_max_size(10);
    clock.tick(1);
    ASSERT_EQ(2, top.ic.instructions.size());
    clock.tick(1);
    ASSERT_EQ(3, top.ic.instructions.size());
    clock.tick(1);
    ASSERT_EQ(5, top.ic.instructions.size());
    clock.tick(1);
    ASSERT_EQ(8, top.ic.instructions.size());

    top.ic.release();
}

TEST_F(PerfectFETest, TestFetchingAcrossTwoTakenBranches)
{
    set("top.pfe.taken_branches_per_cycle", "2");
    Top top("top", &clock, &env);

    ProgramCounter pc(0xf00ba7);

    EXPECT_CALL(top.driver, fetch(_))
        .WillOnce(Return(ByMove(top.driver.return_nop(pc))))
        .WillOnce(Return(ByMove(top.driver.return_b(pc))))
        .WillOnce(Return(ByMove(top.driver.return_nop(pc))))
        .WillOnce(Return(ByMove(top.driver.return_b(pc))))
        .WillOnce(Return(ByMove(top.driver.return_b(pc))))
        .WillOnce(Return(ByMove(top.driver.return_nop(pc))))
        .WillOnce(Return(ByMove(top.driver.return_b(pc))))
        .WillOnce(Return(ByMove(top.driver.return_nop(pc))))
        .WillOnce(Return(ByMove(top.driver.return_b(pc))))
        .WillOnce(Return(ByMove(top.driver.return_nop(pc))))
        .WillOnce(Return(ByMove(top.driver.return_b(pc))));

    clock.tick(1);
    ASSERT_EQ(4, top.ic.instructions.size());
    clock.tick(1);
    ASSERT_EQ(7, top.ic.instructions.size());
    clock.tick(1);
    ASSERT_EQ(11, top.ic.instructions.size());

    top.ic.release();
}

} // namespace perfect_fe
} // namespace ampere

/*
 * vim: sw=4:ts=4:et:si:cindent:cino=\:0,(0,g0,t0:
 */
