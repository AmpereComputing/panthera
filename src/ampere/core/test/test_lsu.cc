//
// Copyright (c) 2018-2019 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//

#include <gmock/gmock.h>
#include <boost/make_unique.hpp>

#include "ampere/archx/uop_info.h"
#include "ampere/archx/instruction_simple.h"
#include "ampere/core/lsu.h"
#include "ampere/core/uop.h"
#include "ampere/archx/testing/mock_driver.h"
#include "ampere/core/testing/mock_flush.h"
#include "ampere/core/testing/mock_mmu.h"
#include "ampere/core/testing/mock_l2.h"
#include "ampere/core/testing/mock_indexer.h"
#include "ampere/core/testing/mock_coherency_validator.h"
#include "ampere/sc/testing/test_module.h"
#include "ampere/sc/testing/test_interfaces.h"
#include "ampere/event_stream/testing/event_stream_mock.h"

namespace ampere::lsu {

using namespace core;
using namespace ::testing;

using MockUopReader = sc::MockTaker<core::Uop>;
using MockUopWriter = sc::MockPutter<core::Uop>;
using MockNumReader = sc::MockTaker<uint>;
using MockPMUWriter = sc::MockPutter<bool>;

#define EXPECT_WRITEBACK(uop)                                      \
    do {                                                           \
        EXPECT_CALL(top.uop_writeback_mock, put(uop));             \
        EXPECT_CALL(top.uop_complete_mock, put(Pair(uop, false))); \
    } while (0)

class Top : public Module
{
public:
    Top(const sc_core::sc_module_name& name) : Module(name)
    {
        lsu.driver_port.bind(driver);
        lsu.oracle_driver_port.bind(oracle_driver);
        for (int i = 0; i < lsu.num_execution_units; i++) {
            lsu.issue_mem[i].bind(uop_in_mock);
        }
        for (int i = 0; i < lsu.num_execution_units; i++) {
            lsu.writeback_mem[i].bind(uop_writeback_mock);
            lsu.complete_mem[i].bind(uop_complete_mock);
        }
        lsu.l2_snoop_request.bind(l2_snoop_in_mock);
        lsu.l2_load_miss_response.bind(l2_load_in_mock);
        lsu.l2_load_miss_request.bind(l2_load_out_mock);
        lsu.l2_store_miss_request.bind(l2_store_out_mock);
        lsu.l2_prefetch_request.bind(l2_prefetch_out_mock);
        lsu.l2_writeback_request.bind(l2_writeback_out_mock);
        lsu.l2_indexer.bind(l2_indexer_mock);
        lsu.coherency_validator.bind(validator_mock);
        lsu.credit_return.bind(dummy_credit_return);
        lsu.mmu_miss_response.bind(mmu_in_mock);
        lsu.mmu_miss_ack.bind(mmu_ack_mock);
        lsu.mmu_miss_request.bind(mmu_out_mock);
        lsu.tlbi_request_out.bind(tlbi_out_mock);
        lsu.tlbi_immu_request_out.bind(tlbi_out_mock);
        lsu.tlbi_request_in.bind(tlbi_in_mock);
        lsu.loads_committed_port.bind(gpc_load_in_mock);
        lsu.stores_committed_port.bind(gpc_store_in_mock);
        lsu.pmu_oldest_load_tlb_out.bind(pmu_tlb_out_mock);
        lsu.pmu_oldest_load_cache_out.bind(pmu_cache_out_mock);
        lsu.pmu_oldest_load_ofb_full_out.bind(pmu_ofb_full_out_mock);
        lsu.pmu_oldest_load_mem_out.bind(pmu_mem_out_mock);
        lsu.pmu_oldest_ldst_valid_out.bind(pmu_ldst_valid_out_mock);
        lsu.pmu_oldest_ldst_atomic_out.bind(pmu_ldst_atomic_out_mock);
        lsu.pmu_oldest_load_st_wait.bind(pmu_load_st_wait_out_mock);
        lsu.l2_no_pending_stores_in.bind(l2_barrier_in_mock);
        lsu.l2_barrier_in.bind(l2_barrier_in_mock);
        lsu.l2_store_release_in.bind(l2_barrier_in_mock);
        lsu.l2_store_exclusive_in.bind(l2_barrier_in_mock);
        lsu.next_to_commit_in.bind(barrier_next_to_commit_mock);
        lsu.pmu_l2_miss_in.bind(l2_miss_in_mock);
        lsu.mem_rob_preview_port.bind(uop_preview_rob);
        lsu.flush.bind(flush);
        lsu.lsu_has_barrier_out.bind(lsu_has_barrier_out);
        lsu.raw_scrub_port.bind(raw_scrub_port);
        lsu.load_cancel_out.bind(load_cancel_mock);
        lsu.lob_balance_out.bind(lob_balance_out_mock);
    }

    LSU lsu{"lsu"};
    NiceMock<MockDriver> driver;
    NiceMock<MockDriver> oracle_driver;
    NiceMock<MockUopReader> uop_in_mock;
    NiceMock<MockUopWriter> uop_writeback_mock;
    NiceMock<sc::MockPutter<std::pair<core::Uop, bool>>> uop_complete_mock;
    CreditReleaser dummy_credit_return;
    NiceMock<MockMemReqReader> l2_snoop_in_mock;
    NiceMock<MockMemRespReader> l2_load_in_mock;
    NiceMock<MockMemReqAndVictimWriter> l2_load_out_mock;
    NiceMock<MockMemReqWriter> l2_store_out_mock;
    NiceMock<MockMemReqWriter> l2_prefetch_out_mock;
    NiceMock<MockMemReqWriter> l2_writeback_out_mock;
    NiceMock<MockIndexer> l2_indexer_mock;
    NiceMock<MockCoherencyValidator> validator_mock;
    NiceMock<MockMMURespReader> mmu_in_mock;
    NiceMock<MockMMUAckReader> mmu_ack_mock;
    NiceMock<MockMMUReqWriter> mmu_out_mock;
    NiceMock<MockTLBIReqWriter> tlbi_out_mock;
    NiceMock<MockTLBIReqReader> tlbi_in_mock;
    NiceMock<MockNumReader> gpc_load_in_mock;
    NiceMock<MockNumReader> gpc_store_in_mock;
    NiceMock<MockPMUWriter> pmu_tlb_out_mock;
    NiceMock<MockPMUWriter> pmu_cache_out_mock;
    NiceMock<MockPMUWriter> pmu_ofb_full_out_mock;
    NiceMock<MockPMUWriter> pmu_ldst_valid_out_mock;
    NiceMock<MockPMUWriter> pmu_ldst_atomic_out_mock;
    NiceMock<MockPMUWriter> pmu_mem_out_mock;
    NiceMock<MockPMUWriter> pmu_load_st_wait_out_mock;
    NiceMock<MockTaker<bool>> l2_barrier_in_mock;
    NiceMock<MockTaker<uint64_t>> barrier_next_to_commit_mock;
    NiceMock<MockReader<PhysAddr>> l2_miss_in_mock;
    NiceMock<MockTaker<core::Uop>> uop_preview_rob;
    NiceMock<MockFlushReader> flush;
    NiceMock<MockPutter<bool>> lsu_has_barrier_out;
    NiceMock<MockPutter<core::Uop>> raw_scrub_port;
    NiceMock<MockPutter<core::Uop>> load_cancel_mock;
    NiceMock<MockPutter<bool>> lob_balance_out_mock;
};

class LSUTest : public TestModule
{
    void SetUp() override { archx::Instruction::set_decoders({"furiosa"}); }

protected:
    NiceMock<event_stream::MockEventAnnouncer> event_announcer;

    core::Uop create_uop(
        bool is_load, bool is_store, VirtAddr virt_addr, PhysAddr phys_addr, int id, unsigned partition)
    {
        constexpr std::uint32_t ldr_b_32_opcode = 0b00'111'0'01'01'000000000000'00000'00000;
        constexpr std::uint32_t str_b_32_opcode = 0b00'111'0'01'00'000000000000'00000'00000;

        std::shared_ptr<archx::Instruction> archx_instr;

        if (is_load)
            archx_instr = std::make_shared<archx::InstructionSimple>(0_pc, ldr_b_32_opcode);
        else if (is_store)
            archx_instr = std::make_shared<archx::InstructionSimple>(0_pc, str_b_32_opcode);
        else
            archx_instr = std::make_shared<archx::InstructionSimple>();

        UopSet archx_uops = furiosa::decode(archx_instr->opcode());

        auto instr = trans_make<core::Instruction>(std::move(archx_instr), nullptr);
        auto uop = make<core::Uop>(std::move(archx_uops[0]), std::move(instr));

        uop->virt_addr = virt_addr;
        uop->phys_addr = phys_addr;
        uop->alloc_id = id;
        uop->sch.partition = partition;
        if (is_load)
            uop->sch.pipe_type = ampere::core::Pipe::C;
        else if (is_store)
            uop->sch.pipe_type = ampere::core::Pipe::D;
        else
            uop->sch.pipe_type = ampere::core::Pipe::A;
        uop->store_associated_sta_id = id;
        uop->sus.sta_issued = true;
        uop->sus.std_issued = true;
        return uop;
    }

    core::Uop create_uop(bool is_load, bool is_store, int id)
    {
        return create_uop(is_load, is_store, 0_va, 0_pa, id, 0);
    }

    core::Uop create_store(VirtAddr virt_addr, PhysAddr phys_addr, uint64_t alloc_id, unsigned partition)
    {
        return create_uop(false, true, virt_addr, phys_addr, alloc_id, partition);
    }

    /*
      This test will create the following series of stores, and then see what a load does.
      We will check expected number of recycles and sleeps in each case

      Alloc ID   Address status           Base   walk_partials_only   wait_on_unknown_partial   stack_load_wait
      ===================================
      1          Unknown
      2          Unknown partial                                               sleep
      3          Unknown (stack source)   done                                                         sleep
      4          Partial match            recyc       done                     recyc                   recyc
      5          Partial match            recyc       recyc                    recyc                   recyc
      6          Unknown                  recyc
      7          Unknown partial          recyc                                sleep
      8          Unknown                  recyc
    */
    void store_forward_walk_test(Top& top)
    {
        core::Uop store[8];
        core::MMUResp mmuresp[8];
        core::MMUAck mmuack[8];

        for (uint64_t i = 0; i < 8; i++) {
            uint64_t alloc_id = i + 1;
            unsigned partition = i & 0x1;
            uint64_t addr = (0x11111000 * (i + 1)) + 0x123;
            store[i] = create_store(VirtAddr{addr}, PhysAddr{addr}, alloc_id, partition);
            mmuresp[i] = make<core::MMUResp>(alloc_id,
                                             *store[i]->virt_addr,
                                             false,
                                             true,
                                             *store[i]->phys_addr,
                                             PageSize::Size::PS_4KB,
                                             0 /*ASID*/,
                                             false);
            mmuack[i] = make<core::MMUAck>(alloc_id, alloc_id);
        }

        auto load = create_uop(true, false, VirtAddr{0x45454123}, PhysAddr{0x45454123}, 9, 0);
        auto mmuack_load = make<core::MMUAck>(9, 9);
        auto mmuresp_load = make<core::MMUResp>(
            9, *load->virt_addr, false, false, *load->phys_addr, PageSize::Size::PS_4KB, 0 /*ASID*/, false);

        // Put all the uops into the preview ROB so the load will see all the unknown stores
        {
            InSequence seq;
            EXPECT_CALL(top.uop_preview_rob, can_take())
                .Times(9) // 8 stores and 1 load
                .WillRepeatedly(Return(true));
            EXPECT_CALL(top.uop_preview_rob, can_take()).WillRepeatedly(Return(false));
        }

        EXPECT_CALL(top.uop_preview_rob, take())
            .WillOnce(Return(store[0]))
            .WillOnce(Return(store[1]))
            .WillOnce(Return(store[2]))
            .WillOnce(Return(store[3]))
            .WillOnce(Return(store[4]))
            .WillOnce(Return(store[5]))
            .WillOnce(Return(store[6]))
            .WillOnce(Return(store[7]))
            .WillOnce(Return(load));

        {
            InSequence seq;

            // Issue stores 1, 3, 4, and 6 so we can see the VA to know they are partial matches
            EXPECT_CALL(top.uop_in_mock, can_take()).Times(4).WillRepeatedly(Return(true));

            EXPECT_CALL(top.uop_in_mock, can_take()).Times(top.lsu.num_execution_units).WillRepeatedly(Return(false));

            // Then wait 5 cycles for PAs to be computed. We will block the PA for stores 1 and 6
            // by not sending an MMU response.
            for (int i = 0; i < 5; i++) {
                EXPECT_CALL(top.uop_in_mock, can_take())
                    .Times(top.lsu.num_execution_units)
                    .WillRepeatedly(Return(false));
            }

            // Now issue the load
            EXPECT_CALL(top.uop_in_mock, can_take()).WillOnce(Return(true)).WillRepeatedly(Return(false));
        }

        EXPECT_CALL(top.uop_in_mock, take())
            .WillOnce(Return(store[1]))
            .WillOnce(Return(store[3]))
            .WillOnce(Return(store[4]))
            .WillOnce(Return(store[6]))
            .WillOnce(Return(load));

        // Any uop that needs to can send an MMU request
        EXPECT_CALL(top.mmu_out_mock, can_put()).WillRepeatedly(Return(true));

        // We only get 3 acks back, for stores 3 and 4 and the load
        EXPECT_CALL(top.mmu_ack_mock, can_take())
            .WillOnce(Return(false))
            .WillOnce(Return(true))
            .WillOnce(Return(true))
            .WillOnce(Return(true))
            .WillRepeatedly(Return(false));

        EXPECT_CALL(top.mmu_ack_mock, take())
            .WillOnce(Return(mmuack[3]))
            .WillOnce(Return(mmuack[4]))
            .WillOnce(Return(mmuack_load));

        // Stores 3 and 4 and the load get MMU responses
        EXPECT_CALL(top.mmu_in_mock, can_take())
            .WillOnce(Return(false))
            .WillOnce(Return(true))
            .WillOnce(Return(true))
            .WillOnce(Return(true))
            .WillRepeatedly(Return(false));

        EXPECT_CALL(top.mmu_in_mock, take())
            .WillOnce(Return(mmuresp[3]))
            .WillOnce(Return(mmuresp[4]))
            .WillOnce(Return(mmuresp_load));

        // Allow anything to complete
        EXPECT_CALL(top.uop_writeback_mock, can_put()).WillRepeatedly(Return(true));

        tick(20);
    }
};

MATCHER_P(HasVirtAddr, addr, "")
{
    return (arg->virt_addr && (*arg->virt_addr == addr));
}
MATCHER_P(HasPhysAddr, addr, "")
{
    return (arg.first->phys_addr == addr);
}

TEST_F(LSUTest, testConstructor)
{
    get_environment()->parameters.add_setting("top.lsu.dl1tlb.sets", 1, "unit test");
    Top top("top");
}

TEST_F(LSUTest, testRunBadUop)
{
    // Tests input Uops that are not identified as load or store or that do not have a virtual addr

    get_environment()->parameters.add_setting("top.lsu.dl1tlb.sets", 1, "unit test");
    Top top("top");

    // GPC will not indicate any retired loads/stores
    EXPECT_CALL(top.gpc_load_in_mock, can_take()).WillRepeatedly(Return(true));
    EXPECT_CALL(top.gpc_store_in_mock, can_take()).WillRepeatedly(Return(true));
    EXPECT_CALL(top.gpc_load_in_mock, take()).WillRepeatedly(Return(0));
    EXPECT_CALL(top.gpc_store_in_mock, take()).WillRepeatedly(Return(0));

    // Don't expect any txns in/out of MMU
    EXPECT_CALL(top.mmu_ack_mock, can_take()).WillRepeatedly(Return(false));
    EXPECT_CALL(top.mmu_in_mock, can_take()).WillRepeatedly(Return(false));

    // Don't expect any txns in/out of L2
    EXPECT_CALL(top.l2_snoop_in_mock, can_take()).WillRepeatedly(Return(false));
    EXPECT_CALL(top.l2_load_in_mock, can_take()).WillRepeatedly(Return(false));

    // Non-mem Uop
    EXPECT_CALL(top.uop_in_mock, can_take()).WillOnce(Return(true)).WillRepeatedly(Return(false));
    EXPECT_CALL(top.uop_in_mock, take()).WillOnce(Return(create_uop(false, false, 0)));

    ASSERT_ANY_THROW(top.lsu.run());

    // Don't expect any transactions in/out of L2
    EXPECT_CALL(top.l2_load_in_mock, can_take()).WillRepeatedly(Return(false));

    // Don't expect any transactions in/out of MMU
    EXPECT_CALL(top.mmu_ack_mock, can_take()).WillRepeatedly(Return(false));
    EXPECT_CALL(top.mmu_in_mock, can_take()).WillRepeatedly(Return(false));

    // No virtual address
    EXPECT_CALL(top.uop_in_mock, can_take()).WillOnce(Return(true)).WillRepeatedly(Return(false));
    EXPECT_CALL(top.uop_in_mock, take()).WillOnce(Return(create_uop(true, false, 0)));

    ASSERT_ANY_THROW(top.lsu.run());
}

TEST_F(LSUTest, testRunValidLoadUops)
{
    get_environment()->parameters.add_setting("top.lsu.enable_read_read_chunk_conflicts", false, "test bench");
    get_environment()->parameters.add_setting("top.lsu.enable_write_read_chunk_bank_conflicts", false, "test bench");
    get_environment()->parameters.add_setting("top.lsu.load_complete_pipe.latency", 2, "test bench");
    get_environment()->parameters.add_setting("top.lsu.lsu_to_l2c_response_complete_pipe.latency", 2, "test bench");
    get_environment()->parameters.add_setting("top.lsu.l2c_refill_wakeup_pipe.latency", 0, "test bench");
    get_environment()->parameters.add_setting("top.lsu.dl1tlb.sets", 1, "unit test");
    get_environment()->parameters.add_setting("top.lsu.delay_after_lob_bypass", 0, "unit test");
    get_environment()->parameters.add_setting("top.lsu.delay_after_sob_bypass", 0, "unit test");
    // Something in the test harness does not correctly schedule the module based on incoming connections, so we have to
    // run every cycle
    get_environment()->parameters.add_setting("top.lsu.run_always", true, "unit test");
    Top top("top");

    if (!get_environment()->parameters.check_all_matched()) {
        std::cerr << "\nParameters:\n";
        get_environment()->parameters.dump_parameters(std::cerr);
        AMPERE_MODULE_THROW("Not all parameters were matched. Giving up.");
    }

    auto virt_addr = VirtAddr{0xdededede};
    auto phys_addr = PhysAddr{0xdededede};
    auto memresp = make<uarch_common::MemResp>(phys_addr.value(), phys_addr, true);
    auto mmuack1 = make<core::MMUAck>(0, 7);
    auto mmuack2 = make<core::MMUAck>(1, 7);
    auto mmuresp =
        make<core::MMUResp>(7, virt_addr, false, false, phys_addr, PageSize::Size::PS_4KB, 0 /*ASID*/, false);

    // No flush
    EXPECT_CALL(top.flush, can_read()).WillRepeatedly(Return(false));

    // GPC will not indicate any retired stores
    EXPECT_CALL(top.gpc_load_in_mock, can_take()).WillRepeatedly(Return(true));
    EXPECT_CALL(top.gpc_store_in_mock, can_take()).WillRepeatedly(Return(true));
    EXPECT_CALL(top.gpc_store_in_mock, take()).WillRepeatedly(Return(0));

    // Two loads to the same addr
    //   - First will miss DTLB, get handled by MMU, then miss L1D cache, get handled by L2 and writeback.
    //   - Second will also miss DTLB, get its request duplicate-combined to same response from MMU,
    //     then miss L1D and sleep from first load L2 req. Will then hit in DL1 and WB

    // First cycle (cycle 0)
    //   - Both loads miss DL1_TLB, requests then sent to MMU
    EXPECT_CALL(top.uop_in_mock, can_take())
        .WillOnce(Return(true))
        .WillOnce(Return(true))
        .WillRepeatedly(Return(false));
    EXPECT_CALL(top.uop_in_mock, take())
        .WillOnce(Return(create_uop(true, false, virt_addr, phys_addr, 0, 0)))
        .WillOnce(Return(create_uop(true, false, virt_addr, phys_addr, 1, 1)));

    // Expect both loads to send MMU requests, duplicate requests that MMU will de-dup
    EXPECT_CALL(top.mmu_out_mock, can_put())
        .WillOnce(Return(true))
        .WillOnce(Return(true))
        .WillRepeatedly(Return(false));
    EXPECT_CALL(top.mmu_out_mock, put(Pointee(Field(&MMUReqObj::virt_addr, virt_addr)))).Times(2);

    // Expect no response back from MMU this cycle
    EXPECT_CALL(top.mmu_ack_mock, can_take()).WillRepeatedly(Return(false));
    EXPECT_CALL(top.mmu_in_mock, can_take()).WillRepeatedly(Return(false));

    // Expect no response back from L2 this cycle
    EXPECT_CALL(top.l2_snoop_in_mock, can_take()).WillRepeatedly(Return(false));
    EXPECT_CALL(top.l2_load_in_mock, can_take()).WillRepeatedly(Return(false));

    // Expect no writeback this cycle - waiting for response from MMU & L2
    EXPECT_CALL(top.uop_writeback_mock, can_put()).WillRepeatedly(Return(false));

    // GPC will indicate no retired loads
    EXPECT_CALL(top.gpc_load_in_mock, take()).WillOnce(Return(0));

    tick(); // End first cycle (0)

    // Second cycle (cycle 1)
    //  - Both loads will get responses back from MMU, hold for one cycle before completing DTLB refill

    // Expect MMU response back this cycle for both loads
    EXPECT_CALL(top.mmu_ack_mock, can_take())
        .WillOnce(Return(true))
        .WillOnce(Return(true))
        .WillRepeatedly(Return(false));
    EXPECT_CALL(top.mmu_ack_mock, take()).WillOnce(Return(mmuack1)).WillOnce(Return(mmuack2));
    EXPECT_CALL(top.mmu_in_mock, can_take()).WillOnce(Return(true)).WillRepeatedly(Return(false));
    EXPECT_CALL(top.mmu_in_mock, take()).WillOnce(Return(mmuresp));

    // GPC will indicate no retired loads
    EXPECT_CALL(top.gpc_load_in_mock, take()).WillOnce(Return(0));

    tick(); // End second cycle (1)

    // Third cycle (cycle 2)
    //  - First load will complete refill from MMU response, now misses L1D lookup and initiates request to L2
    //  - Second load detects previous miss to same address, put to sleep waiting on L2 response

    // No additional UOPs from IXU
    EXPECT_CALL(top.uop_in_mock, can_take()).WillRepeatedly(Return(false));

    // Expect first load to send L2 request
    EXPECT_CALL(top.l2_load_out_mock, can_put()).WillOnce(Return(true)).WillRepeatedly(Return(false));
    EXPECT_CALL(top.l2_load_out_mock, put(HasPhysAddr(phys_addr)));

    // Expect no response back from L2 this cycle
    EXPECT_CALL(top.l2_load_in_mock, can_take()).WillRepeatedly(Return(false));

    // Expect no writeback this cycle - waiting for response from L2
    EXPECT_CALL(top.uop_writeback_mock, can_put()).WillRepeatedly(Return(false));

    // GPC will indicate no retired loads
    EXPECT_CALL(top.gpc_load_in_mock, take()).WillOnce(Return(0));

    tick(); // End third cycle (2)

    // Fourth cycle (cycle 3)
    //  - First load will get response back from L2, holds for 2 cycles before completing L2 refill, WB, and wakeups

    // No additional UOPs from IXU
    EXPECT_CALL(top.uop_in_mock, can_take()).WillRepeatedly(Return(false));

    // Expect L2 response back this cycle for first load
    EXPECT_CALL(top.l2_load_in_mock, can_take()).WillOnce(Return(true)).WillRepeatedly(Return(false));
    EXPECT_CALL(top.l2_load_in_mock, take()).WillOnce(Return(memresp));

    // GPC will indicate no retired loads
    EXPECT_CALL(top.gpc_load_in_mock, take()).WillOnce(Return(0));

    tick(); // End fourth cycle (3)

    // Fifth cycle (cycle 4)
    //  - First load still waiting for L2 resp to complete

    // No additional UOPs from IXU
    EXPECT_CALL(top.uop_in_mock, can_take()).WillRepeatedly(Return(false));

    // GPC will indicate no retired loads
    EXPECT_CALL(top.gpc_load_in_mock, take()).WillOnce(Return(0));

    tick(); // End fifth cycle (4)

    // Sixth cycle (cycle 5)
    //  - First load will complete L2 refill, writeback, and wakeup second load
    //  - GPC will commit the first load next cycle

    // No additional UOPs from IXU
    EXPECT_CALL(top.uop_in_mock, can_take()).WillRepeatedly(Return(false));

    // Expect writeback from L2 response this cycle
    EXPECT_WRITEBACK(HasVirtAddr(virt_addr));

    // GPC will indicate no retired loads as IXU just received writeback
    EXPECT_CALL(top.gpc_load_in_mock, take()).WillOnce(Return(0));

    tick(); // End sixth cycle (5)

    // Seventh cycle (cycle 6)
    //  - GPC returns commit from first load allowing LSU to deallocate its LOB entry
    //  - Second load will recycle, schedule, and send to L1D hit complete stage

    // No additional UOPs from IXU
    EXPECT_CALL(top.uop_in_mock, can_take()).WillRepeatedly(Return(false));

    // GPC will indicate 1 committed load
    EXPECT_CALL(top.gpc_load_in_mock, take()).WillOnce(Return(1));

    tick(); // End seventh cycle (6)

    // Eighth cycle (cycle 7)
    //  - Second load will complete L1D hit and writeback
    //  - GPC will commit the second load next cycle

    EXPECT_CALL(top.uop_writeback_mock, can_put()).WillOnce(Return(true)).WillRepeatedly(Return(false));
    EXPECT_WRITEBACK(HasVirtAddr(virt_addr));

    // GPC will indicate no retired loads as IXU just received writeback
    EXPECT_CALL(top.gpc_load_in_mock, take()).WillOnce(Return(0));

    tick(); // End ninth cycle (8)

    // Ninth cycle (cycle 8)
    //  - GPC returns commit from second load allowing LSU to deallocate its LOB entry

    EXPECT_CALL(top.gpc_load_in_mock, take()).WillOnce(Return(1));

    tick(); // End tenth cycle (9)

} // testRunValidLoadUops

TEST_F(LSUTest, StoreMissWillWriteBackAndGenerateL2Request)
{
    get_environment()->parameters.add_setting("top.lsu.dl1tlb.sets", 1, "unit test");
    Top top("top");

    auto the_store = create_store(VirtAddr{0xabababab}, PhysAddr{0xabababab}, 0, 0);
    auto mmuack = make<core::MMUAck>(0, 0);
    auto mmuresp = make<core::MMUResp>(
        0, *the_store->virt_addr, false, true, *the_store->phys_addr, PageSize::Size::PS_4KB, 0 /*ASID*/, false);

    // No flush
    EXPECT_CALL(top.flush, can_read()).WillRepeatedly(Return(false));

    EXPECT_CALL(top.mmu_out_mock, can_put())
        .WillOnce(Return(true)) // cycle 0
        .WillRepeatedly(Return(false));

    EXPECT_CALL(top.mmu_out_mock, put(Pointee(Field(&MMUReqObj::virt_addr, *the_store->virt_addr))));

    EXPECT_CALL(top.mmu_ack_mock, can_take())
        .WillOnce(Return(false))
        .WillOnce(Return(true))
        .WillRepeatedly(Return(false));

    EXPECT_CALL(top.mmu_ack_mock, take()).WillOnce(Return(mmuack));

    EXPECT_CALL(top.mmu_in_mock, can_take())
        .WillOnce(Return(false)) // cycle 0
        .WillOnce(Return(true))  // cycle 1 - MMU resp
        .WillRepeatedly((Return(false)));

    EXPECT_CALL(top.mmu_in_mock, take()).WillOnce(Return(mmuresp));

    // One store
    EXPECT_CALL(top.uop_in_mock, can_take()).WillOnce(Return(true)).WillRepeatedly(Return(false));

    EXPECT_CALL(top.uop_in_mock, take()).WillOnce(Return(the_store));

    EXPECT_WRITEBACK(the_store);

    EXPECT_CALL(top.gpc_load_in_mock, can_take()).WillRepeatedly(Return(true));
    EXPECT_CALL(top.gpc_store_in_mock, can_take()).WillRepeatedly(Return(true));

    EXPECT_CALL(top.gpc_store_in_mock, take())
        .WillOnce(Return(0))
        .WillOnce(Return(0))
        .WillOnce(Return(0))
        .WillOnce(Return(0))
        .WillOnce(Return(1))
        .WillRepeatedly(Return(0));

    EXPECT_CALL(top.gpc_load_in_mock, take()).WillRepeatedly(Return(0));

    EXPECT_CALL(top.l2_store_out_mock, can_put()).WillOnce(Return(true)).WillRepeatedly(Return(false));

    EXPECT_CALL(top.l2_store_out_mock, put(Pointee(Field(&MemReqObj::phys_addr, *the_store->phys_addr))));

    EXPECT_CALL(top.l2_snoop_in_mock, can_take()).WillRepeatedly(Return(false));
    EXPECT_CALL(top.l2_load_in_mock, can_take()).WillRepeatedly(Return(false));

    tick(6);
}

TEST_F(LSUTest, StoreToLoadForward)
{
    get_environment()->event_stream = std::make_unique<event_stream::EventStreamDummy>(&event_announcer);

    get_environment()->parameters.add_setting("top.lsu.lsu_to_dmmu_response_complete_pipe.bandwidth", 2, "unit test");
    get_environment()->parameters.add_setting("top.lsu.lsu_to_dmmu_wakeup_pipe.bandwidth", 2, "unit test");
    get_environment()->parameters.add_setting("top.lsu.dl1tlb.ways", 64, "unit test");
    get_environment()->parameters.add_setting("top.lsu.dl1tlb.sets", 1, "unit test");
    get_environment()->parameters.add_setting("top.lsu.dl1tlb.line_size", 4096, "unit test");
    get_environment()->parameters.add_setting(
        "top.lsu.sf_predictor.forward_value", 4, "unit test"); // Always predict skip
    get_environment()->parameters.add_setting("top.lsu.delay_after_lob_bypass", 0, "unit test");
    get_environment()->parameters.add_setting("top.lsu.delay_after_sob_bypass", 0, "unit test");

    Top top("top");

    auto store1 = create_store(VirtAddr{0xabababab}, PhysAddr{0xabababab}, 0, 0);
    auto store2 = create_store(VirtAddr{0xcdcdcdcd}, PhysAddr{0xcdcdcdcd}, 1, 1);
    auto store3 = create_store(VirtAddr{0xefefefef}, PhysAddr{0xefefefef}, 2, 0);
    auto store4 = create_store(VirtAddr{0xbabababa}, PhysAddr{0xbabababa}, 3, 1);
    auto mmuresp1 = make<core::MMUResp>(
        0, *store1->virt_addr, false, true, *store1->phys_addr, PageSize::Size::PS_4KB, 0 /*ASID*/, false);
    auto mmuresp2 = make<core::MMUResp>(
        1, *store2->virt_addr, false, true, *store2->phys_addr, PageSize::Size::PS_4KB, 0 /*ASID*/, false);
    auto mmuresp3 = make<core::MMUResp>(
        2, *store3->virt_addr, false, true, *store3->phys_addr, PageSize::Size::PS_4KB, 0 /*ASID*/, false);
    auto mmuresp4 = make<core::MMUResp>(
        3, *store4->virt_addr, false, true, *store4->phys_addr, PageSize::Size::PS_4KB, 0 /*ASID*/, false);
    auto mmuack1 = make<core::MMUAck>(0, 0);
    auto mmuack2 = make<core::MMUAck>(1, 1);
    auto mmuack3 = make<core::MMUAck>(2, 2);
    auto mmuack4 = make<core::MMUAck>(3, 3);

    // Will forward from store2 on cycle 2
    auto load = create_uop(true, false, VirtAddr{0xcdcdcdcd}, PhysAddr{0xcdcdcdcd}, 4, 0);

    // No flush
    EXPECT_CALL(top.flush, can_read()).WillRepeatedly(Return(false));

    EXPECT_CALL(top.mmu_out_mock, can_put())
        .WillOnce(Return(true))
        .WillOnce(Return(true))
        .WillOnce(Return(true))
        .WillOnce(Return(true));

    EXPECT_CALL(top.mmu_out_mock, put(Pointee(Field(&MMUReqObj::virt_addr, *store1->virt_addr))));
    EXPECT_CALL(top.mmu_out_mock, put(Pointee(Field(&MMUReqObj::virt_addr, *store2->virt_addr))));
    EXPECT_CALL(top.mmu_out_mock, put(Pointee(Field(&MMUReqObj::virt_addr, *store3->virt_addr))));
    EXPECT_CALL(top.mmu_out_mock, put(Pointee(Field(&MMUReqObj::virt_addr, *store4->virt_addr))));

    EXPECT_CALL(top.gpc_load_in_mock, can_take()).WillRepeatedly(Return(true));
    EXPECT_CALL(top.gpc_store_in_mock, can_take()).WillRepeatedly(Return(true));

    EXPECT_CALL(top.mmu_ack_mock, can_take())
        .WillOnce(Return(false)) // cycle 0
        .WillOnce(Return(true))  // store1 MMU ack - cycle 1
        .WillOnce(Return(true))  // store2 MMU ack - cycle 1
        .WillOnce(Return(false))
        .WillOnce(Return(false))
        .WillOnce(Return(false))
        .WillOnce(Return(true)) // store3 MMU resp - cycle 4
        .WillOnce(Return(true)) // store4 MMU resp - cycle 4
        .WillRepeatedly((Return(false)));

    EXPECT_CALL(top.mmu_ack_mock, take())
        .WillOnce(Return(mmuack1))
        .WillOnce(Return(mmuack2))
        .WillOnce(Return(mmuack3))
        .WillOnce(Return(mmuack4));

    EXPECT_CALL(top.mmu_in_mock, can_take())
        .WillOnce(Return(false)) // cycle 0
        .WillOnce(Return(true))  // store1 MMU resp - cycle 1
        .WillOnce(Return(true))  // store2 MMU resp - cycle 1
        .WillOnce(Return(false))
        .WillOnce(Return(false))
        .WillOnce(Return(false))
        .WillOnce(Return(true)) // store3 MMU resp - cycle 4
        .WillOnce(Return(true)) // store4 MMU resp - cycle 4
        .WillRepeatedly((Return(false)));

    EXPECT_CALL(top.mmu_in_mock, take())
        .WillOnce(Return(mmuresp1))
        .WillOnce(Return(mmuresp2))
        .WillOnce(Return(mmuresp3))
        .WillOnce(Return(mmuresp4));

    {
        InSequence seq;

        // Two stores in cycle 0, another two in cycle 1
        for (int i = 0; i < 2; i++) {
            EXPECT_CALL(top.uop_in_mock, can_take()).WillOnce(Return(true)).WillOnce(Return(true));

            EXPECT_CALL(top.uop_in_mock, can_take()).Times(top.lsu.num_execution_units).WillRepeatedly(Return(false));
        }

        // Nothing in cycle 2
        EXPECT_CALL(top.uop_in_mock, can_take()).Times(top.lsu.num_execution_units).WillRepeatedly(Return(false));

        // Load in cycle 3
        EXPECT_CALL(top.uop_in_mock, can_take()).WillOnce(Return(true)).WillRepeatedly(Return(false));
    }

    EXPECT_CALL(top.uop_in_mock, take())
        .WillOnce(Return(store1))
        .WillOnce(Return(store2))
        .WillOnce(Return(store3))
        .WillOnce(Return(store4))
        .WillOnce(Return(load));

    EXPECT_CALL(top.uop_preview_rob, can_take())
        .WillOnce(Return(true)) // store1 - cycle 0
        .WillOnce(Return(true)) // store2 - cycle 0
        .WillOnce(Return(true)) // store3 - cycle 1
        .WillOnce(Return(true)) // store4 - cycle 1
        .WillOnce(Return(true)) // load1 - cycle 3
        .WillRepeatedly(Return(false));

    EXPECT_CALL(top.uop_preview_rob, take())
        .WillOnce(Return(store1))
        .WillOnce(Return(store2))
        .WillOnce(Return(store3))
        .WillOnce(Return(store4))
        .WillOnce(Return(load));

    EXPECT_CALL(top.gpc_store_in_mock, take())
        .WillOnce(Return(0)) // cycle 0
        .WillOnce(Return(0)) // cycle 1
        .WillOnce(Return(0)) // cycle 2
        .WillOnce(Return(2)) // cycle 3
        .WillOnce(Return(0)) // cycle 4
        .WillOnce(Return(0)) // cycle 5
        .WillOnce(Return(2)) // cycle 6
        .WillRepeatedly(Return(0));

    EXPECT_CALL(top.gpc_load_in_mock, take())
        .WillOnce(Return(0))
        .WillOnce(Return(0))
        .WillOnce(Return(0))
        .WillOnce(Return(0))
        .WillOnce(Return(0))
        .WillOnce(Return(1)) // cycle 4
        .WillRepeatedly(Return(0));

    EXPECT_CALL(top.uop_writeback_mock, can_put()).WillOnce(Return(true)).WillRepeatedly(Return(false));

    EXPECT_WRITEBACK(store1);
    EXPECT_WRITEBACK(store2);
    EXPECT_WRITEBACK(load);
    EXPECT_WRITEBACK(store3);
    EXPECT_WRITEBACK(store4);

    EXPECT_CALL(top.l2_store_out_mock, can_put())
        .WillOnce(Return(true))
        .WillOnce(Return(true))
        .WillOnce(Return(true))
        .WillOnce(Return(true));

    EXPECT_CALL(top.l2_store_out_mock, put(Pointee(Field(&MemReqObj::phys_addr, *store1->phys_addr))));
    EXPECT_CALL(top.l2_store_out_mock, put(Pointee(Field(&MemReqObj::phys_addr, *store2->phys_addr))));
    EXPECT_CALL(top.l2_store_out_mock, put(Pointee(Field(&MemReqObj::phys_addr, *store3->phys_addr))));
    EXPECT_CALL(top.l2_store_out_mock, put(Pointee(Field(&MemReqObj::phys_addr, *store4->phys_addr))));

    EXPECT_CALL(top.l2_snoop_in_mock, can_take()).WillRepeatedly(Return(false));
    EXPECT_CALL(top.l2_load_in_mock, can_take()).WillRepeatedly(Return(false));

    EXPECT_CALL(event_announcer, post_event(_, _)).Times(AnyNumber());
    EXPECT_CALL(event_announcer, post_event("top.lsu.uop_ld_st_fwd", _)).Times(1);

    tick(20);

    //    EXPECT_EQ(1, top.lsu.uop_ld_st_fwd_event.get_count());
}

TEST_F(LSUTest, StoreForwardWalk)
{
    get_environment()->event_stream = std::make_unique<event_stream::EventStreamDummy>(&event_announcer);
    get_environment()->event_stream->enable();

    get_environment()->parameters.add_setting(
        "top.lsu.sf_predictor.forward_value", 4, "unit test"); // Always predict skip
    get_environment()->parameters.add_setting("top.lsu.delay_after_lob_bypass", 0, "unit test");
    get_environment()->parameters.add_setting("top.lsu.delay_after_sob_bypass", 0, "unit test");
    get_environment()->parameters.add_setting("top.lsu.run_always", true, "unit test");
    get_environment()->parameters.add_setting("top.lsu.l1d_cache.enable_perfect_mode", true, "unit test");
    get_environment()->parameters.add_setting("top.lsu.lsu_to_dmmu_response_complete_pipe.bandwidth", 99, "unit test");
    get_environment()->parameters.add_setting("top.lsu.lsu_to_dmmu_wakeup_pipe.bandwidth", 99, "unit test");

    Top top("top");

    store_forward_walk_test(top);

    EXPECT_EQ(event_count("top.lsu.uop_ld_recycle"), 5);
}

TEST_F(LSUTest, SFWalkSkipAllUnknown)
{
    get_environment()->event_stream = std::make_unique<event_stream::EventStreamDummy>(&event_announcer);
    get_environment()->event_stream->enable();

    get_environment()->parameters.add_setting(
        "top.lsu.sf_predictor.forward_value", 4, "unit test"); // Always predict skip
    get_environment()->parameters.add_setting("top.lsu.delay_after_lob_bypass", 0, "unit test");
    get_environment()->parameters.add_setting("top.lsu.delay_after_sob_bypass", 0, "unit test");
    get_environment()->parameters.add_setting("top.lsu.run_always", true, "unit test");
    get_environment()->parameters.add_setting("top.lsu.l1d_cache.enable_perfect_mode", true, "unit test");
    get_environment()->parameters.add_setting("top.lsu.lsu_to_dmmu_response_complete_pipe.bandwidth", 99, "unit test");
    get_environment()->parameters.add_setting("top.lsu.lsu_to_dmmu_wakeup_pipe.bandwidth", 99, "unit test");

    get_environment()->parameters.add_setting("top.lsu.walk_partials_only", true, "unit test");

    Top top("top");

    store_forward_walk_test(top);

    EXPECT_EQ(event_count("top.lsu.uop_ld_recycle"), 1);
}

class LSUStoreFlushTest : public LSUTest, public WithParamInterface<int>
{};

// FIXME: disabled as it was accidentally not run for a long time and is now rotten
TEST_P(LSUStoreFlushTest, DISABLED_FlushStores)
{
    get_environment()->parameters.add_setting("top.lsu.lsu_to_dmmu_response_complete_pipe.bandwidth", 2, "unit test");
    get_environment()->parameters.add_setting("top.lsu.lsu_to_dmmu_wakeup_pipe.bandwidth", 2, "unit test");
    get_environment()->parameters.add_setting("top.lsu.dl1tlb.ways", 64, "unit test");
    get_environment()->parameters.add_setting("top.lsu.dl1tlb.sets", 1, "unit test");
    get_environment()->parameters.add_setting("top.lsu.dl1tlb.line_size", 4096, "unit test");

    Top top("top");

    auto store1 = create_store(VirtAddr{0xabababab}, PhysAddr{0xabababab}, 0, 0);
    auto store2 = create_store(VirtAddr{0xcdcdcdcd}, PhysAddr{0xcdcdcdcd}, 1, 1);
    auto store3 = create_store(VirtAddr{0xefefefef}, PhysAddr{0xefefefef}, 3, 0);
    auto store4 = create_store(VirtAddr{0xbabababa}, PhysAddr{0xbabababa}, 4, 1);

    auto flush = sc::trans_make<Flush>(
        ProgramCounter{0x1000}, ProgramCounter{0xfffc}, nullptr, nullptr, 2, archx::FlushType::ABORTED);

    auto mmuresp1 = make<core::MMUResp>(
        0, *store1->virt_addr, false, true, *store1->phys_addr, PageSize::Size::PS_4KB, 0 /*ASID*/, false);
    auto mmuresp2 = make<core::MMUResp>(
        1, *store2->virt_addr, false, true, *store2->phys_addr, PageSize::Size::PS_4KB, 0 /*ASID*/, false);
    auto mmuresp3 = make<core::MMUResp>(
        3, *store3->virt_addr, false, true, *store3->phys_addr, PageSize::Size::PS_4KB, 0 /*ASID*/, false);
    auto mmuresp4 = make<core::MMUResp>(
        4, *store4->virt_addr, false, true, *store4->phys_addr, PageSize::Size::PS_4KB, 0 /*ASID*/, false);

    auto mmuack1 = make<core::MMUAck>(0, 0);
    auto mmuack2 = make<core::MMUAck>(1, 1);
    auto mmuack3 = make<core::MMUAck>(3, 3);
    auto mmuack4 = make<core::MMUAck>(4, 4);

    // Four stores dispatched to LSU, 2 in cycle 0, 2 in cycle 1
    // Flush should clear second set of 2 stores (3 & 4) in cycle 4, 5, or 6
    //   - Flush in cycle 4:  flush before store 3 and 4 MMU response
    //   - Flush in cycle 5:  flush before store 3 and 4 STA complete
    //   - Flush in cycle 6:  flush after store 3 and 4 STA complete and writeback but before GPC commit

    int delay = GetParam();

    EXPECT_CALL(top.mmu_out_mock, can_put())
        .WillOnce(Return(true))  // store1 MMU req - cycle 0
        .WillOnce(Return(true))  // store2 MMU req - cycle 0
        .WillOnce(Return(true))  // store3 MMU req - cycle 3
        .WillOnce(Return(true)); // store4 MMU req - cycle 3

    EXPECT_CALL(top.mmu_out_mock, put(Pointee(Field(&MMUReqObj::virt_addr, *store1->virt_addr))));
    EXPECT_CALL(top.mmu_out_mock, put(Pointee(Field(&MMUReqObj::virt_addr, *store2->virt_addr))));
    EXPECT_CALL(top.mmu_out_mock, put(Pointee(Field(&MMUReqObj::virt_addr, *store3->virt_addr))));
    EXPECT_CALL(top.mmu_out_mock, put(Pointee(Field(&MMUReqObj::virt_addr, *store4->virt_addr))));

    EXPECT_CALL(top.gpc_load_in_mock, can_take()).WillRepeatedly(Return(true));
    EXPECT_CALL(top.gpc_store_in_mock, can_take()).WillRepeatedly(Return(true));

    EXPECT_CALL(top.mmu_ack_mock, can_take())
        .WillOnce(Return(false)) // cycle 0
        .WillOnce(Return(true))  // store1 MMU ack - cycle 1
        .WillOnce(Return(true))  // store2 MMU ack - cycle 1
        .WillOnce(Return(false))
        .WillOnce(Return(false))
        .WillOnce(Return(false))
        .WillOnce(Return(true)) // store3 MMU ack - cycle 4
        .WillOnce(Return(true)) // store4 MMU ack - cycle 4
        .WillRepeatedly((Return(false)));

    EXPECT_CALL(top.mmu_ack_mock, take())
        .WillOnce(Return(mmuack1))
        .WillOnce(Return(mmuack2))
        .WillOnce(Return(mmuack3))
        .WillOnce(Return(mmuack4));

    EXPECT_CALL(top.mmu_in_mock, can_take())
        .WillOnce(Return(false)) // cycle 0
        .WillOnce(Return(true))  // store1 MMU resp - cycle 1
        .WillOnce(Return(true))  // store2 MMU resp - cycle 1
        .WillOnce(Return(false))
        .WillOnce(Return(false))
        .WillOnce(Return(false))
        .WillOnce(Return(true)) // store3 MMU resp - cycle 4
        .WillOnce(Return(true)) // store4 MMU resp - cycle 4
        .WillRepeatedly((Return(false)));

    EXPECT_CALL(top.mmu_in_mock, take())
        .WillOnce(Return(mmuresp1))
        .WillOnce(Return(mmuresp2))
        .WillOnce(Return(mmuresp3))
        .WillOnce(Return(mmuresp4));

    // Four stores, two in cycle 0 and two in cycle 1
    {
        InSequence seq;

        EXPECT_CALL(top.uop_in_mock, can_take()).WillOnce(Return(true)).WillOnce(Return(true));

        EXPECT_CALL(top.uop_in_mock, can_take()).Times(top.lsu.num_execution_units).WillRepeatedly(Return(false));

        EXPECT_CALL(top.uop_in_mock, can_take())
            .WillOnce(Return(true))
            .WillOnce(Return(true))
            .WillRepeatedly(Return(false));
    }

    EXPECT_CALL(top.uop_in_mock, take())
        .WillOnce(Return(store1))
        .WillOnce(Return(store2))
        .WillOnce(Return(store3))
        .WillOnce(Return(store4));

    EXPECT_CALL(top.gpc_store_in_mock, take())
        .WillOnce(Return(0))
        .WillOnce(Return(0))
        .WillOnce(Return(0))
        .WillOnce(Return(0))
        .WillOnce(Return(2)) // cycle 4
        .WillRepeatedly(Return(0));

    EXPECT_CALL(top.gpc_load_in_mock, take()).WillRepeatedly(Return(0));

    EXPECT_CALL(top.uop_writeback_mock, can_put()).WillRepeatedly(Return(true));
    EXPECT_CALL(top.uop_complete_mock, can_put()).WillRepeatedly(Return(true));

    EXPECT_WRITEBACK(store1);
    EXPECT_WRITEBACK(store2);
    if (delay > 6) {
        EXPECT_WRITEBACK(store3);
        EXPECT_WRITEBACK(store4);
    }

    EXPECT_CALL(top.l2_store_out_mock, can_put()).WillOnce(Return(true)).WillOnce(Return(true));

    EXPECT_CALL(top.l2_store_out_mock, put(Pointee(Field(&MemReqObj::phys_addr, *store1->phys_addr))));
    EXPECT_CALL(top.l2_store_out_mock, put(Pointee(Field(&MemReqObj::phys_addr, *store2->phys_addr))));

    EXPECT_CALL(top.l2_snoop_in_mock, can_take()).WillRepeatedly(Return(false));
    EXPECT_CALL(top.l2_load_in_mock, can_take()).WillRepeatedly(Return(false));

    {
        InSequence dummy_flush;
        for (int i = 0; i < delay; i++) {
            EXPECT_CALL(top.flush, can_read()).WillOnce(Return(false));
        }

        EXPECT_CALL(top.flush, can_read()).WillOnce(Return(true)).WillRepeatedly(Return(false));
    }

    EXPECT_CALL(top.flush, read()).WillOnce(Return(flush));

    tick(10);
}

INSTANTIATE_TEST_SUITE_P(LSUStoreFlushTest, LSUStoreFlushTest, Range(1, 16));

class LSULoadFlushTest : public LSUTest, public WithParamInterface<int>
{};

TEST_P(LSULoadFlushTest, FlushLoads)
{
    get_environment()->parameters.add_setting("top.lsu.enable_read_read_chunk_conflicts", false, "test bench");
    get_environment()->parameters.add_setting("top.lsu.enable_write_read_chunk_bank_conflicts", false, "test bench");
    get_environment()->parameters.add_setting("top.lsu.lsu_to_dmmu_response_complete_pipe.bandwidth", 2, "unit test");
    get_environment()->parameters.add_setting("top.lsu.lsu_to_dmmu_wakeup_pipe.bandwidth", 2, "unit test");
    get_environment()->parameters.add_setting("top.lsu.dl1tlb.ways", 64, "unit test");
    get_environment()->parameters.add_setting("top.lsu.dl1tlb.sets", 1, "unit test");
    get_environment()->parameters.add_setting("top.lsu.dl1tlb.line_size", 4096, "unit test");
    get_environment()->parameters.add_setting("top.lsu.delay_after_lob_bypass", 0, "unit test");
    get_environment()->parameters.add_setting("top.lsu.delay_after_sob_bypass", 0, "unit test");
    // Something in the test harness does not correctly schedule the module based on incoming connections, so we have to
    // run every cycle
    get_environment()->parameters.add_setting("top.lsu.run_always", true, "unit test");
    get_environment()->event_stream = std::make_unique<event_stream::EventStreamDummy>(nullptr);
    get_environment()->event_stream->enable();

    Top top("top");

    auto load1 = create_uop(true, false, VirtAddr{0xabababab}, PhysAddr{0xabababab}, 0, 0);
    auto load2 = create_uop(true, false, VirtAddr{0xcdcdcdcd}, PhysAddr{0xcdcdcdcd}, 1, 1);
    auto load3 = create_uop(true, false, VirtAddr{0xefefefef}, PhysAddr{0xefefefef}, 3, 0);
    auto load4 = create_uop(true, false, VirtAddr{0xbabababa}, PhysAddr{0xbabababa}, 4, 1);

    auto mmuresp1 = make<core::MMUResp>(
        0, *load1->virt_addr, false, false, *load1->phys_addr, PageSize::Size::PS_4KB, 0 /*ASID*/, false);
    auto mmuresp2 = make<core::MMUResp>(
        1, *load2->virt_addr, false, false, *load2->phys_addr, PageSize::Size::PS_4KB, 0 /*ASID*/, false);
    auto mmuresp3 = make<core::MMUResp>(
        3, *load3->virt_addr, false, false, *load3->phys_addr, PageSize::Size::PS_4KB, 0 /*ASID*/, false);
    auto mmuresp4 = make<core::MMUResp>(
        4, *load4->virt_addr, false, false, *load4->phys_addr, PageSize::Size::PS_4KB, 0 /*ASID*/, false);

    auto mmuack1 = make<core::MMUAck>(0, 0);
    auto mmuack2 = make<core::MMUAck>(1, 1);
    auto mmuack3 = make<core::MMUAck>(3, 3);
    auto mmuack4 = make<core::MMUAck>(4, 4);

    auto memresp1 = make<uarch_common::MemResp>((*load1->phys_addr).value(), *load1->phys_addr, true);
    auto memresp2 = make<uarch_common::MemResp>((*load2->phys_addr).value(), *load2->phys_addr, true);
    auto memresp3 = make<uarch_common::MemResp>((*load3->phys_addr).value(), *load3->phys_addr, true);
    auto memresp4 = make<uarch_common::MemResp>((*load4->phys_addr).value(), *load4->phys_addr, true);

    auto flush = sc::trans_make<Flush>(
        ProgramCounter{0x1000}, ProgramCounter{0xfffc}, nullptr, nullptr, 2, archx::FlushType::ABORTED);

    // Four loads dispatched to LSU, 2 in cycle 0, 2 in cycle 1
    // Flush should clear second set of 2 loads (3 & 4) in cycle 1 to 6, loads 1 & 2 should complete normally
    //   - Flush in cycle 1:  flush before load 3 or 4 are scheduled in LSU
    //   - Flush in cycle 2:  flush before load 3 or 4 are scheduled in LSU
    //   - Flush in cycle 3:  flush after load 4 scheduled and misses DTLB in cycle 2;
    //                        load 3 blocked by load 1 L2 response consuming partition slot
    //   - Flush in cycle 4:  flush after load 3 has conflict with load 0 fill;
    //                        load 4 MMU response dropped
    //   - Flush in cycle 5:  flush after load 3 and load 4 have conflicts with load 0 fill;
    //   - Flush in cycle 6:  flush after load 3 and load 4 have conflicts with load 1 fill;
    //   - Flush in cycle 7:  flush after load 4 L2 req, load 3 MMU req;
    //   - Flush in cycle 8:  flush after load 4 L2 resp, load 3 MMU resp;
    //   - Flush in cycle 9:  flush before load 4 write-back, load 4 L2 resp fills the L1
    //   - Flush in cycle 10: flush after load 4 write-back, load 4 fill causes load 3 conflict;
    //   - Flush in cycle 11: flush after load 3, load 4 fill causes load 3 conflict;
    //   - Flush in cycle 6:  flush after load 3 & 4 MMU responses and sending their L2 reqs in cycle 4;
    //                        both load 3 and 4 L2 responses in cycle 5 and 6 will be dropped due to their lob entries
    //                        being flushed in cycle 5.
    //   - Flush in cycle 6:  flush after load 3 L2 response and write-back in cycle 5;
    //                        load 4 L2 response fills L1 even though its lob entry was flushed in cycle 6, load 3
    //                        write-back would get "cleared"/handled by GPC

    int delay = GetParam();

    EXPECT_CALL(top.l2_snoop_in_mock, can_take()).WillRepeatedly(Return(false));

    {
        InSequence dummy_mmu_req_can_put;

        EXPECT_CALL(top.mmu_out_mock, can_put()).WillOnce(Return(true)); // load1 MMU req - cycle 0
        EXPECT_CALL(top.mmu_out_mock, can_put()).WillOnce(Return(true)); // load2 MMU req - cycle 0

        if (delay >= 4)
            EXPECT_CALL(top.mmu_out_mock, can_put()).WillOnce(Return(true)); // load4 MMU req, partition 1 - cycle 3
        if (delay >= 7)
            EXPECT_CALL(top.mmu_out_mock, can_put()).WillOnce(Return(true)); // load3 MMU req, partition 0 - cycle 6
    }

    {
        InSequence dummy_mmu_req_put;

        EXPECT_CALL(top.mmu_out_mock, put(Pointee(Field(&MMUReqObj::virt_addr, *load1->virt_addr))));
        EXPECT_CALL(top.mmu_out_mock, put(Pointee(Field(&MMUReqObj::virt_addr, *load2->virt_addr))));

        if (delay >= 4)
            EXPECT_CALL(top.mmu_out_mock, put(Pointee(Field(&MMUReqObj::virt_addr, *load4->virt_addr))));
        if (delay >= 7)
            EXPECT_CALL(top.mmu_out_mock, put(Pointee(Field(&MMUReqObj::virt_addr, *load3->virt_addr))));
    }

    EXPECT_CALL(top.gpc_load_in_mock, can_take()).WillRepeatedly(Return(true));
    EXPECT_CALL(top.gpc_store_in_mock, can_take()).WillRepeatedly(Return(true));

    {
        InSequence dummy_mmu_ack_can_take;

        EXPECT_CALL(top.mmu_ack_mock, can_take())
            .WillOnce(Return(false))  // cycle 0
            .WillOnce(Return(true))   // load1 MMU ack - cycle 1
            .WillOnce(Return(true))   // load2 MMU ack - cycle 1
            .WillOnce(Return(false))  // cycle 2
            .WillOnce(Return(false)); // cycle 3

        if (delay > 3) {
            EXPECT_CALL(top.mmu_ack_mock, can_take())
                .WillOnce(Return(false)) // cycle 4
                .WillOnce(Return(true)); // load4 MMU ack - cycle 4
        }
        if (delay > 7) {
            EXPECT_CALL(top.mmu_ack_mock, can_take())
                .WillOnce(Return(false)) // cycle 5
                .WillOnce(Return(false)) // cycle 6
                .WillOnce(Return(false)) // cycle 7
                .WillOnce(Return(false)) // cycle 8
                .WillOnce(Return(true)); // load3 MMU ack - cycle 8
        }
        EXPECT_CALL(top.mmu_ack_mock, can_take()).WillRepeatedly((Return(false)));
    }

    {
        InSequence dummy_mmu_ack_take;

        EXPECT_CALL(top.mmu_ack_mock, take()).WillOnce(Return(mmuack1)).WillOnce(Return(mmuack2));
        if (delay > 3) {
            EXPECT_CALL(top.mmu_ack_mock, take()).WillOnce(Return(mmuack4));
        }
        if (delay > 7) {
            EXPECT_CALL(top.mmu_ack_mock, take()).WillOnce(Return(mmuack3));
        }
    }

    {
        InSequence dummy_mmu_resp_can_take;

        EXPECT_CALL(top.mmu_in_mock, can_take())
            .WillOnce(Return(false)) // cycle 0
            .WillOnce(Return(true))  // load1 MMU resp - cycle 1
            .WillOnce(Return(true))  // load2 MMU resp - cycle 1
            .WillOnce(Return(false))
            .WillOnce(Return(false));

        if (delay > 3) {
            EXPECT_CALL(top.mmu_in_mock, can_take())
                .WillOnce(Return(false))
                .WillOnce(Return(true)); // load4 MMU resp - cycle 4
        }
        if (delay > 7) {
            EXPECT_CALL(top.mmu_in_mock, can_take())
                .WillOnce(Return(false))
                .WillOnce(Return(false))
                .WillOnce(Return(false))
                .WillOnce(Return(false))
                .WillOnce(Return(true)); // load3 MMU resp - cycle 8
        }

        EXPECT_CALL(top.mmu_in_mock, can_take()).WillRepeatedly(Return(false));
    }

    {
        InSequence dummy_mmu_resp_take;

        EXPECT_CALL(top.mmu_in_mock, take()).WillOnce(Return(mmuresp1)).WillOnce(Return(mmuresp2));

        if (delay > 3) {
            EXPECT_CALL(top.mmu_in_mock, take()).WillOnce(Return(mmuresp4));
        }
        if (delay > 7) {
            EXPECT_CALL(top.mmu_in_mock, take()).WillOnce(Return(mmuresp3));
        }
    }

    // Four loads, two in cycle 0 and two in cycle 1
    {
        InSequence seq;

        EXPECT_CALL(top.uop_in_mock, can_take()).WillOnce(Return(true)).WillOnce(Return(true));

        EXPECT_CALL(top.uop_in_mock, can_take()).Times(top.lsu.num_execution_units).WillRepeatedly(Return(false));

        EXPECT_CALL(top.uop_in_mock, can_take())
            .WillOnce(Return(true))
            .WillOnce(Return(true))
            .WillRepeatedly(Return(false));
    }

    EXPECT_CALL(top.uop_in_mock, take())
        .WillOnce(Return(load1))
        .WillOnce(Return(load2))
        .WillOnce(Return(load3))
        .WillOnce(Return(load4));

    EXPECT_CALL(top.gpc_load_in_mock, take())
        .WillOnce(Return(0))
        .WillOnce(Return(0))
        .WillOnce(Return(0))
        .WillOnce(Return(0))
        .WillOnce(Return(0))
        .WillOnce(Return(1)) // load1 - commit cycle 5
        .WillOnce(Return(1)) // load2 - commit cycle 6
        .WillRepeatedly(Return(0));

    EXPECT_CALL(top.gpc_store_in_mock, take()).WillRepeatedly(Return(0));

    EXPECT_WRITEBACK(load1); // cycle 4 - load1 write-back
    EXPECT_WRITEBACK(load2); // cycle 5 - load2 write-back
    if (delay > 9)
        EXPECT_WRITEBACK(load4); // cycle 9 - load4 write-back, GPC will not commit
    if (delay > 13)
        EXPECT_WRITEBACK(load3); // cycle 13 - load3 write-back, GPC will not commit

    {
        InSequence dummy_l2_req_can_put;

        EXPECT_CALL(top.l2_load_out_mock, can_put())
            .WillOnce(Return(true))  // cycle 2 - load1 L2 req
            .WillOnce(Return(true)); // cycle 2 - load2 L2 req

        if (delay > 6)
            EXPECT_CALL(top.l2_load_out_mock, can_put()).WillOnce(Return(true)); // cycle 7 - load4 L2 req
        if (delay > 10)
            EXPECT_CALL(top.l2_load_out_mock, can_put()).WillOnce(Return(true)); // cycle 10 - load3 L2 req
    }

    {
        InSequence dummy_l2_req_put;

        EXPECT_CALL(top.l2_load_out_mock,
                    put(Field(&Fill::first, Pointee(Field(&MemReqObj::phys_addr, *load1->phys_addr)))));
        EXPECT_CALL(top.l2_load_out_mock,
                    put(Field(&Fill::first, Pointee(Field(&MemReqObj::phys_addr, *load2->phys_addr)))));

        if (delay > 6)
            EXPECT_CALL(top.l2_load_out_mock,
                        put(Field(&Fill::first, Pointee(Field(&MemReqObj::phys_addr, *load4->phys_addr)))));
        if (delay > 10)
            EXPECT_CALL(top.l2_load_out_mock,
                        put(Field(&Fill::first, Pointee(Field(&MemReqObj::phys_addr, *load3->phys_addr)))));
    }

    {
        InSequence dummy_l2_resp_can_take;

        EXPECT_CALL(top.l2_load_in_mock, can_take())
            .WillOnce(Return(false))  // cycle 0
            .WillOnce(Return(false))  // cycle 1
            .WillOnce(Return(false))  // cycle 2
            .WillOnce(Return(true))   // cycle 3 - load1 L2 resp
            .WillOnce(Return(false))  // cycle 3
            .WillOnce(Return(true))   // cycle 4 - load2 L2 resp
            .WillOnce(Return(false)); // cycle 4

        if (delay > 7) {
            EXPECT_CALL(top.l2_load_in_mock, can_take()).WillOnce(Return(false)); // cycle 5
            EXPECT_CALL(top.l2_load_in_mock, can_take()).WillOnce(Return(false)); // cycle 6
            EXPECT_CALL(top.l2_load_in_mock, can_take()).WillOnce(Return(false)); // cycle 7
            EXPECT_CALL(top.l2_load_in_mock, can_take()).WillOnce(Return(true));  // cycle 8 - load4 L2 resp
            EXPECT_CALL(top.l2_load_in_mock, can_take()).WillOnce(Return(false)); // cycle 8
        }
        if (delay > 11) {
            EXPECT_CALL(top.l2_load_in_mock, can_take()).WillOnce(Return(false)); // cycle 9
            EXPECT_CALL(top.l2_load_in_mock, can_take()).WillOnce(Return(false)); // cycle 10
            EXPECT_CALL(top.l2_load_in_mock, can_take()).WillOnce(Return(false)); // cycle 11
            EXPECT_CALL(top.l2_load_in_mock, can_take()).WillOnce(Return(true));  // cycle 12 - load3 L2 resp
        }

        EXPECT_CALL(top.l2_load_in_mock, can_take()).WillRepeatedly(Return(false));
    }

    {
        InSequence dummy_l2_resp_take;

        EXPECT_CALL(top.l2_load_in_mock, take()).WillOnce(Return(memresp1)).WillOnce(Return(memresp2));
        if (delay > 7)
            EXPECT_CALL(top.l2_load_in_mock, take()).WillOnce(Return(memresp4));
        if (delay > 11)
            EXPECT_CALL(top.l2_load_in_mock, take()).WillOnce(Return(memresp3));
    }

    {
        InSequence dummy_flush;
        for (int i = 0; i < delay; i++) {
            EXPECT_CALL(top.flush, can_read()).WillOnce(Return(false));
        }

        EXPECT_CALL(top.flush, can_read()).WillOnce(Return(true)).WillRepeatedly(Return(false));
    }

    EXPECT_CALL(top.flush, read()).WillOnce(Return(flush));

    start_simulation();

    if (delay >= 4)
        get_clock()->schedule(&top.lsu,
                              1); // simulates MMU or L2 response on input port to wake up LSU module after flush

    tick(20);
}

INSTANTIATE_TEST_SUITE_P(LSULoadFlushTest, LSULoadFlushTest, Range(1, 16));

} // namespace ampere::lsu

/*
 * vim: sw=4:ts=4:et:si:cindent:cino=\:0,(0,g0,t0:
 */
