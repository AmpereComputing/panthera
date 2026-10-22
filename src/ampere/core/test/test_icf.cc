//
// Copyright (c) 2018-2020 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//

#include <boost/filesystem/operations.hpp>
#include <ampere/archx/instruction_simple.h>

#include "ampere/core/icf.h"
#include "ampere/archx/branch_target.h"
#include "ampere/archx/driver_is.h"
#include "ampere/core/testing/instruction_consumer.h"
#include "ampere/sc/testing/test_module.h"
#include "ampere/sc/testing/test_interfaces.h"
#include "ampere/archx/testing/mock_driver.h"
#include "ampere/core/testing/mock_mmu.h"
#include "ampere/core/testing/mock_l2.h"
#include "ampere/core/testing/mock_indexer.h"
#include "ampere/core/testing/mock_coherency_validator.h"
#include "ampere/core/testing/mock_flush.h"

#include "ampere/sc/testing/clock_dummy.h"
#include "ampere/uarch_common/memreq.h"

namespace bfs = boost::filesystem;

namespace ampere {
namespace icf {

using namespace core;
using namespace uarch_common;
using namespace ::testing;

class ICFTest : public TestModule
{
protected:
    ICFTest()
        : TestModule{
              {"icf.itlb.ways", 64},
              {"icf.itlb.sets", 1},
              {"icf.itlb.line_size", 4096},
              {"icf.itlb_miss_credits.size", 8},
              {"~icf.ifb.credits_[0-1].size", 8},
              {"icf.ifb.l2c_fifo.size", 3},
              {"icf.instruction_fifo.size", 128},
              {"l2.loopback.latency", 4},
              {"feq.size", 16},
          }
    {
    }

    void SetUp() override
    {
        icf.iq_write_port.bind(ic);
        icf.driver_port.bind(driver);
        icf.fetch_port.bind(fetch_port);
        icf.bp_clear_port.bind(clear_fetch_port);
        icf.ittage_stall_port.bind(ittage_stall);
        icf.snoop_port.bind(snoop_port);
        immu.flush.bind(immu_flush);
        icf.flush_port.bind(icf_flush);
        icf.instruction_credit_return.bind(credit_return);
        icf.pmu_idr_tlb_miss.bind(pmu_tlb_miss);
        icf.pmu_idr_icache_miss.bind(pmu_cache_miss);
        icf.pmu_idr_l2_miss.bind(pmu_feq_l2_miss);
        icf.pmu_idr_feq_empty.bind(pmu_feq_empty);
        icf.pmu_idr_mem_stall.bind(pmu_mem_stall);
        icf.pmu_idr_btc_clear.bind(pmu_btc_clear);
        icf.pmu_idr_btb_clear.bind(pmu_btb_clear);
        icf.icache_misses.bind(icache_misses);
        icf.tlbi_request.bind(tlbi_in_mock);
        icf.br_check_flush_port.bind(br_check_flush_port);
        connect(icf.mmu_request, immu.request).name("mmu_request");
        connect(immu.ack, icf.mmu_ack).name("mmu_ack");
        connect(immu.response, icf.mmu_response).name("mmu_response");
        connect(icf.miss_request, l2.request);
        connect(l2.request_scheduled, icf.miss_request_scheduled).name("icf_to_l2_request_scheduled");
        connect(l2.response, icf.miss_response);
        icf.loop_restart.bind(loop_restart_mock);
        icf.loop_fetch_port.bind(loop_fetch_mock);
        icf.l2_indexer.bind(l2_indexer);
        icf.coherency_validator.bind(coherency_validator);
        icf.pmu_l2_miss_in_icf.bind(pmu_l2_miss);
        archx::Instruction::set_decoders({"furiosa"});

        TestModule::SetUp();
        EXPECT_CALL(driver, asid()).WillRepeatedly(Return(10));
        EXPECT_CALL(clear_fetch_port, can_take()).WillRepeatedly(Return(false));

        ON_CALL(snoop_port, can_take()).WillByDefault(Return(false));
    }

    void TearDown() override { ic.release(); };

    Fetch make_fetch(ProgramCounter pc,
                     ProgramCounter next_pc,
                     int num_instructions,
                     BranchType branch_type = BranchType::CONDITIONAL_DIRECT)
    {
        auto fetch = make<Fetch>(pc, current_fetch_id++);
        auto exit_pc = pc + 4 * (num_instructions - 1);
        fetch->feq_credit = feq.acquire();

        if (next_pc != exit_pc + 4) {
            fetch->update_prediction({branch_type, exit_pc, next_pc});
            auto &p = fetch->line_prediction[fetch->start_instruction_offset() + num_instructions - 1];
            p.branch_type = branch_type;
            p.primary.taken = true;
            p.primary.target = next_pc;
        }

        return fetch;
    }

    MMUDummy immu{"immu"};
    L2InclusiveDummy l2{"l2"};
    ICF icf{"icf"};

    InstructionConsumer ic;
    NiceMock<MockDriver> driver;
    NiceMock<MockTaker<Fetch>> fetch_port;
    NiceMock<MockTaker<Fetch>> clear_fetch_port;
    NiceMock<MockTaker<MemReq>> snoop_port;
    NiceMock<MockFlushReader> immu_flush;
    NiceMock<MockFlushReader> icf_flush;
    NiceMock<MockTLBIReqReader> tlbi_in_mock;
    NiceMock<MockPutter<LoopRestart>> loop_restart_mock;
    NiceMock<MockPutter<Fetch>> loop_fetch_mock;
    NiceMock<MockTaker<Credit>> credit_return;
    NiceMock<MockIndexer> l2_indexer;
    NiceMock<MockCoherencyValidator> coherency_validator;
    NiceMock<MockFlushWriter> br_check_flush_port;
    NiceMock<MockPutter<bool>> pmu_tlb_miss;
    NiceMock<MockPutter<bool>> pmu_cache_miss;
    NiceMock<MockPutter<bool>> pmu_feq_l2_miss;
    NiceMock<MockPutter<bool>> pmu_feq_empty;
    NiceMock<MockPutter<bool>> pmu_mem_stall;
    NiceMock<MockPutter<bool>> pmu_btc_clear;
    NiceMock<MockPutter<bool>> pmu_btb_clear;
    NiceMock<MockPutter<uint>> icache_misses;
    NiceMock<MockReader<PhysAddr>> pmu_l2_miss;
    NeverTake<FetchId> ittage_stall;

    CreditPool feq{"feq"};

    int current_fetch_id{1};
};

TEST(TestFetch, TestFetchIndex)
{
    FetchObj f1(ProgramCounter(0x100), 1);
    FetchObj f2(ProgramCounter(0x104), 2);

    ASSERT_EQ(0, f1.start_instruction_offset());
    ASSERT_EQ(1, f2.start_instruction_offset());
}

TEST_F(ICFTest, TestITLBandCacheMiss)
{
    ProgramCounter pc(0x113c);
    VirtAddr va(0x113c);
    PhysAddr pa(0x113c);
    auto fetch = make_fetch(pc, pc + 4, 1);

    EXPECT_CALL(driver, fetch(_)).WillRepeatedly(Return(ByMove(driver.return_nop(pc))));

    EXPECT_CALL(snoop_port, can_take()).WillRepeatedly(Return(false));
    EXPECT_CALL(fetch_port, can_take()).WillOnce(Return(true)).WillRepeatedly(Return(false));
    EXPECT_CALL(fetch_port, take()).WillOnce(Return(fetch));

    tick(30);
    ASSERT_EQ(1, immu.request_count);
    ASSERT_EQ(1, l2.request_count);
    ASSERT_EQ(1, ic.instructions.size());
}

TEST_F(ICFTest, TestCacheMissHit)
{
    ProgramCounter pc(0x113c);
    VirtAddr va(0x113c);
    PhysAddr pa(0x113c);
    auto mmuresp = trans_make<MMUResp>(0, va, true, false, pa, PageSize::Size::PS_4KB, 10 /*ASID*/, false);
    auto fetch1 = make_fetch(pc, pc + 4, 1);
    auto fetch2 = make_fetch(pc, pc + 4, 1);

    EXPECT_CALL(driver, fetch(_))
        .WillOnce(Return(ByMove(driver.return_nop(pc))))
        .WillOnce(Return(ByMove(driver.return_nop(pc))));
    EXPECT_CALL(snoop_port, can_take()).WillRepeatedly(Return(false));
    EXPECT_CALL(fetch_port, can_take()).WillOnce(Return(true)).WillOnce(Return(true)).WillRepeatedly(Return(false));
    EXPECT_CALL(fetch_port, take()).WillOnce(Return(fetch1)).WillOnce(Return(fetch2));

    tick(32);

    ASSERT_EQ(1, immu.request_count);
    ASSERT_EQ(1, l2.request_count);
    ASSERT_EQ(2, ic.instructions.size());
}

TEST_F(ICFTest, TestCacheMissThreeLines)
{
    std::vector<ProgramCounter> pcs;
    std::vector<Fetch> fetches;

    for (uint i = 0; i < 3; i++) {
        ProgramCounter pc((i << 6) + 0x3c);
        pcs.push_back(pc);
        fetches.push_back(make_fetch(pc, pc.next_line_address(), 1));
    }

    EXPECT_CALL(driver, fetch(_))
        .WillOnce(Return(ByMove(driver.return_nop(pcs[0]))))
        .WillOnce(Return(ByMove(driver.return_nop(pcs[1]))))
        .WillOnce(Return(ByMove(driver.return_nop(pcs[2]))));
    EXPECT_CALL(snoop_port, can_take()).WillRepeatedly(Return(false));
    EXPECT_CALL(fetch_port, can_take())
        .WillOnce(Return(true))
        .WillOnce(Return(true))
        .WillOnce(Return(true))
        .WillRepeatedly(Return(false));
    EXPECT_CALL(fetch_port, take())
        .WillOnce(Return(fetches[0]))
        .WillOnce(Return(fetches[1]))
        .WillOnce(Return(fetches[2]));

    tick(100);

    ASSERT_EQ(1, immu.request_count);
    ASSERT_EQ(3, l2.request_count);
    ASSERT_EQ(3, l2.max_concurrent_requests);
    ASSERT_EQ(3, ic.instructions.size());
}

TEST_F(ICFTest, TestCacheHalfCachelineSplit)
{
    ProgramCounter pc(0x101c);
    PhysAddr pa(0x101c);
    auto fetch = make_fetch(pc, pc + 12, 2);

    EXPECT_CALL(driver, fetch(_))
        .WillOnce(Return(ByMove(driver.return_nop(pc))))
        .WillOnce(Return(ByMove(driver.return_b(pc + 4))));

    EXPECT_CALL(snoop_port, can_take()).WillRepeatedly(Return(false));
    EXPECT_CALL(fetch_port, can_take()).WillOnce(Return(true)).WillRepeatedly(Return(false));
    EXPECT_CALL(fetch_port, take()).WillOnce(Return(fetch));

    tick(14);

    ASSERT_EQ(1, immu.request_count);
    ASSERT_EQ(1, l2.request_count);
    ASSERT_EQ(1, ic.instructions.size());

    tick(1);

    ASSERT_EQ(2, ic.instructions.size());
}

TEST_F(ICFTest, TestBPClear)
{
    auto fetch1 = make_fetch(0x1000_pc, 0x1020_pc, 1, BranchType::UNCONDITIONAL_DIRECT);
    auto fetch2 = make_fetch(0x1020_pc, 0x1040_pc, 1, BranchType::UNCONDITIONAL_DIRECT);
    fetch2->clear_type = ClearType::DIFFERENT_TARGET;
    auto fetch3 = make_fetch(0x1040_pc, 0x1060_pc, 1, BranchType::UNCONDITIONAL_DIRECT);

    auto fetch_after_clear = make_fetch(0x2000_pc, 0x2020_pc, 1);

    EXPECT_CALL(snoop_port, can_take()).WillRepeatedly(Return(false));

    EXPECT_CALL(fetch_port, can_take())
        .WillOnce(Return(true))
        .WillOnce(Return(false))
        .WillOnce(Return(true))
        .WillOnce(Return(false))
        .WillOnce(Return(true))
        .WillOnce(Return(false))
        .WillOnce(Return(true))
        .WillRepeatedly(Return(false));

    EXPECT_CALL(fetch_port, take())
        .WillOnce(Return(fetch1))
        .WillOnce(Return(fetch2))
        .WillOnce(Return(fetch3))
        .WillOnce(Return(fetch_after_clear));

    EXPECT_CALL(clear_fetch_port, can_take())
        .WillOnce(Return(false))
        .WillOnce(Return(false))
        .WillOnce(Return(true))
        .WillOnce(Return(false))
        .WillRepeatedly(Return(false));

    EXPECT_CALL(clear_fetch_port, take()).WillOnce(Return(fetch2));

    {
        InSequence instruction_fetches;
        EXPECT_CALL(driver, fetch(0x1000_pc)).WillOnce(Return(ByMove(driver.return_b(0x1000_pc))));
        EXPECT_CALL(driver, fetch(0x1020_pc)).WillOnce(Return(ByMove(driver.return_b(0x1020_pc))));
        EXPECT_CALL(driver, fetch(0x2000_pc)).WillOnce(Return(ByMove(driver.return_b(0x2020_pc))));
    }

    tick(25);
}

class BTCTest : public ICFTest
{
public:
    void SetUp() override
    {
        ICFTest::SetUp();

        EXPECT_CALL(fetch_port, can_take()).WillOnce(Return(true)).WillRepeatedly(Return(false));

        EXPECT_CALL(br_check_flush_port, can_put()).WillRepeatedly(Return(true));
    }

    Fetch fetch;
};

TEST_F(BTCTest, BTCUndetectedUnconditionalBranch)
{
    fetch = make_fetch(0x1000_pc, 0x1040_pc, 16);

    EXPECT_CALL(fetch_port, take()).WillOnce(Return(fetch));
    EXPECT_CALL(driver, fetch(0x1000_pc)).WillOnce(Return(ByMove(driver.return_b(0x1000_pc))));
    EXPECT_CALL(br_check_flush_port, put(Pointee(Field(&FlushObj::type, FlushType::BRANCH_CHECK_UNCOND_BRANCH))));

    tick(16);
}

TEST_F(BTCTest, BTCUndetectedCallDirect)
{
    fetch = make_fetch(0x1000_pc, 0x1040_pc, 16);

    EXPECT_CALL(fetch_port, take()).WillOnce(Return(fetch));
    EXPECT_CALL(driver, fetch(0x1000_pc)).WillOnce(Return(ByMove(driver.return_op(0x1000_pc, 0x94000001))));
    EXPECT_CALL(br_check_flush_port, put(Pointee(Field(&FlushObj::type, FlushType::BRANCH_CHECK_CALL_DIRECT))));

    tick(16);
}

TEST_F(BTCTest, BTCUndetectedCallIndirect)
{
    fetch = make_fetch(0x1000_pc, 0x1040_pc, 16);

    EXPECT_CALL(fetch_port, take()).WillOnce(Return(fetch));
    EXPECT_CALL(driver, fetch(0x1000_pc)).WillOnce(Return(ByMove(driver.return_op(0x1000_pc, 0xd63f0200))));
    EXPECT_CALL(br_check_flush_port,
                put(Pointee(AllOf(Field(&FlushObj::type, FlushType::BRANCH_CHECK_CALL_INDIRECT),
                                  Field(&FlushObj::flushing_pc, 0x1000_pc),
                                  Field(&FlushObj::pc, NON_CANONICAL_TARGET)))));

    tick(16);
}

TEST_F(BTCTest, BTCUndetectedUncondIndirect)
{
    fetch = make_fetch(0x1000_pc, 0x1040_pc, 16);

    EXPECT_CALL(fetch_port, take()).WillOnce(Return(fetch));
    EXPECT_CALL(driver, fetch(0x1000_pc)).WillOnce(Return(ByMove(driver.return_op(0x1000_pc, 0xd61f0020))));
    EXPECT_CALL(br_check_flush_port,
                put(Pointee(AllOf(Field(&FlushObj::type, FlushType::BRANCH_CHECK_UNCOND_INDIRECT),
                                  Field(&FlushObj::flushing_pc, 0x1000_pc),
                                  Field(&FlushObj::pc, NON_CANONICAL_TARGET)))));

    tick(16);
}

TEST_F(BTCTest, BTCUndetectedReturn)
{
    fetch = make_fetch(0x1000_pc, 0x1040_pc, 16);

    EXPECT_CALL(fetch_port, take()).WillOnce(Return(fetch));
    EXPECT_CALL(driver, fetch(0x1000_pc)).WillOnce(Return(ByMove(driver.return_op(0x1000_pc, 0xd65f0000))));
    EXPECT_CALL(br_check_flush_port,
                put(Pointee(AllOf(Field(&FlushObj::type, FlushType::BRANCH_CHECK_RETURN),
                                  Field(&FlushObj::flushing_pc, 0x1000_pc),
                                  Field(&FlushObj::pc, NON_CANONICAL_TARGET)))));

    tick(16);
}

TEST_F(BTCTest, BTCUndetectedConditionalBranch)
{
    fetch = make_fetch(0x1000_pc, 0x1040_pc, 16);
    fetch->line_prediction[0].primary.taken = true;
    fetch->line_prediction[0].conditional_primary_taken = true;

    EXPECT_CALL(fetch_port, take()).WillOnce(Return(fetch));
    EXPECT_CALL(driver, fetch(0x1000_pc)).WillOnce(Return(ByMove(driver.return_op(0x1000_pc, 0x54000000))));
    EXPECT_CALL(br_check_flush_port, put(Pointee(Field(&FlushObj::type, FlushType::BRANCH_CHECK_COND_BRANCH))));

    tick(16);
}

TEST_F(BTCTest, BTCWontFireForNotTakenConditionalBranch)
{
    fetch = make_fetch(0x1000_pc, 0x1040_pc, 16);

    EXPECT_CALL(fetch_port, take()).WillOnce(Return(fetch));
    EXPECT_CALL(driver, fetch(0x1000_pc)).WillOnce(Return(ByMove(driver.return_op(0x1000_pc, 0x54000000))));
    for (auto pc = 0x1004_pc; pc <= 0x103c_pc; pc += 4)
        EXPECT_CALL(driver, fetch(pc)).WillOnce(Return(ByMove(driver.return_nop(pc))));

    EXPECT_CALL(br_check_flush_port, put(_)).Times(0);

    tick(20);
}

TEST_F(BTCTest, BTCTargetMismatch)
{
    fetch = make_fetch(0x1000_pc, 0x1040_pc, 1);

    EXPECT_CALL(fetch_port, take()).WillOnce(Return(fetch));
    EXPECT_CALL(driver, fetch(0x1000_pc)).WillOnce(Return(ByMove(driver.return_op(0x1000_pc, 0x54000000))));
    EXPECT_CALL(br_check_flush_port, put(Pointee(Field(&FlushObj::type, FlushType::BRANCH_CHECK_TARGET))));

    tick(16);
}

TEST_F(BTCTest, BTCTargetMismatchWontFireOnIndirectBranch)
{
    fetch = make_fetch(0x1000_pc, 0x1040_pc, 1, BranchType::CALL_INDIRECT);

    EXPECT_CALL(fetch_port, take()).WillOnce(Return(fetch));
    EXPECT_CALL(driver, fetch(0x1000_pc)).WillOnce(Return(ByMove(driver.return_op(0x1000_pc, 0xd63f0000))));
    EXPECT_CALL(br_check_flush_port, put(_)).Times(0);

    tick(16);
}

TEST_F(BTCTest, BTCBranchTypeMismatchNotBranch)
{
    fetch = make_fetch(0x1000_pc, 0x1040_pc, 1, BranchType::CONDITIONAL_DIRECT);

    EXPECT_CALL(fetch_port, take()).WillOnce(Return(fetch));
    EXPECT_CALL(driver, fetch(0x1000_pc)).WillOnce(Return(ByMove(driver.return_nop(0x1000_pc))));
    EXPECT_CALL(br_check_flush_port, put(Pointee(Field(&FlushObj::type, FlushType::BRANCH_CHECK_WRONG_TYPE))));

    tick(16);
}

TEST_F(BTCTest, BTCBranchTypeMismatchDifferentBranch)
{
    fetch = make_fetch(0x1000_pc, 0x1040_pc, 1, BranchType::CONDITIONAL_DIRECT);

    EXPECT_CALL(fetch_port, take()).WillOnce(Return(fetch));
    EXPECT_CALL(driver, fetch(0x1000_pc)).WillOnce(Return(ByMove(driver.return_b(0x1000_pc))));
    EXPECT_CALL(br_check_flush_port, put(Pointee(Field(&FlushObj::type, FlushType::BRANCH_CHECK_WRONG_TYPE))));

    tick(16);
}

TEST_F(BTCTest, BTCNoBranchLeavesExitPCIntact)
{
    fetch = make_fetch(0x1000_pc, 0x1040_pc, 1);
    fetch->update_prediction(
        {BranchType::CONDITIONAL_DIRECT, fetch->get_prediction().branch_pc, fetch->get_prediction().target});
    fetch->line_prediction[0].branch_type = BranchType::CONDITIONAL_DIRECT;

    EXPECT_CALL(fetch_port, take()).WillOnce(Return(fetch));
    EXPECT_CALL(driver, fetch(0x1000_pc)).WillOnce(Return(ByMove(driver.return_nop(0x1000_pc))));
    EXPECT_CALL(br_check_flush_port, put(Pointee(Field(&FlushObj::type, FlushType::BRANCH_CHECK_WRONG_TYPE))));

    tick(16);

    EXPECT_FALSE(fetch->get_prediction().is_taken());
    EXPECT_EQ(fetch->exit_pc(), 0x1000_pc);
}

class ICFFlushTest : public ICFTest, public WithParamInterface<int>
{};

TEST_P(ICFFlushTest, TestFlushOneFetch)
{
    ProgramCounter pc(0x113c);
    ProgramCounter flush_pc(0x8123c);
    auto flush_fetch = make_fetch(0x4000_pc, 0x4040_pc, 16);
    auto flush = trans_make<Flush>(flush_pc, pc, flush_fetch, nullptr, 0, FlushType::BR_MISPREDICT);

    EXPECT_CALL(snoop_port, can_take()).WillRepeatedly(Return(false));

    EXPECT_CALL(driver, fetch(flush_pc)).WillOnce(Return(driver.return_nop(flush_pc)));
    EXPECT_CALL(fetch_port, take())
        .WillOnce(Return(make_fetch(pc, pc + 4, 1)))
        .WillOnce(Return(make_fetch(flush_pc, flush_pc + 4, 1)));
    EXPECT_CALL(immu_flush, read()).WillOnce(Return(flush));
    EXPECT_CALL(icf_flush, read()).WillOnce(Return(flush));

    int delay = GetParam();

    {
        InSequence dummy_immu_flush;
        for (int i = 0; i < delay; i++)
            EXPECT_CALL(immu_flush, can_read()).WillOnce(Return(false));
        EXPECT_CALL(immu_flush, can_read()).WillOnce(Return(true));
        EXPECT_CALL(immu_flush, can_read()).WillRepeatedly(Return(false));
    }

    {
        InSequence dummy_icf_flush;
        for (int i = 0; i < delay; i++)
            EXPECT_CALL(icf_flush, can_read()).WillOnce(Return(false));
        EXPECT_CALL(icf_flush, can_read()).WillOnce(Return(true));
        EXPECT_CALL(icf_flush, can_read()).WillRepeatedly(Return(false));
    }

    {
        InSequence dummy_fetch_can_take;
        EXPECT_CALL(fetch_port, can_take()).WillOnce(Return(true));
        for (int i = 0; i < 13; i++)
            EXPECT_CALL(fetch_port, can_take()).WillOnce(Return(false));
        EXPECT_CALL(fetch_port, can_take()).WillOnce(Return(true));
        EXPECT_CALL(fetch_port, can_take()).WillRepeatedly(Return(false));
    }

    tick(50);

    release_credit(flush->fetch->feq_credit);

    ASSERT_EQ((delay > 3 ? 2 : 1), immu.request_count);
    ASSERT_EQ((delay > 8 ? 2 : 1), l2.request_count);
    ASSERT_EQ(1, ic.instructions.size());
}

INSTANTIATE_TEST_SUITE_P(ICFFlushTests, ICFFlushTest, Range(2, 12));

} // namespace icf
} // namespace ampere

/*
 * vim: sw=4:ts=4:et:si:cindent:cino=\:0,(0,g0,t0:
 */
