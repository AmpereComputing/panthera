//
// Copyright (c) 2018-2019 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//

#include "ampere/archx/page_size.h"
#include "ampere/core/flush.h"
#include "ampere/sc/testing/test_module.h"
#include "ampere/core/testing/mock_mmu.h"
#include "ampere/core/testing/mock_l2.h"
#include "ampere/core/testing/mock_flush.h"
#include "ampere/core/mmu.h"
#include "ampere/archx/testing/mock_driver.h"
#include "ampere/archx/arch_state.h"
#include "ampere/archx/sysreg.h"
#include "gmock/gmock.h"

namespace ampere::mmu {

using namespace core;
using namespace ::testing;

class FakeDriver : public MockDriver
{
public:
    void write_memory(PhysAddr pa, uint64_t value) { memory[pa.value()] = value; }
    uint64_t read_memory(PhysAddr pa) const override
    {
        if (memory.contains(pa.value()))
            return memory.at(pa.value());
        return 0;
    }
    void write_sysreg(SysReg sysreg, uint64_t value) { sysregs[sysreg.addr()] = value; }
    uint64_t read_sysreg(SysReg sysreg) const override
    {
        if (sysregs.contains(sysreg.addr()))
            return sysregs.at(sysreg.addr());
        return 0;
    }
    void write_exception_level(EL value) { el = value; }

private:
    std::unordered_map<uint64_t, uint64_t> memory;
    std::unordered_map<uint16_t, uint64_t> sysregs;
    EL el{EL0};
};

/* Provide just enough MMU configuration and page table contents to allow
 * translations for one instruction and one data page */
void seed_driver(FakeDriver& driver)
{
    driver.write_sysreg(TCR_EL1, 0x34b5503510);
    driver.write_sysreg(TTBR0_EL1, 0x13a179000);
    driver.write_sysreg(TTBR1_EL1, 0x13000041df8000);

    // PC 0x54e___ -> PA 0x13beb3___
    driver.write_memory(0x13a179000_pa, 0x13a22e003);
    driver.write_memory(0x13a22e000_pa, 0x13aef2003);
    driver.write_memory(0x13aef2010_pa, 0x13a062003);
    driver.write_memory(0x13a062a70_pa, 0x2000013beb3fd3);

    // VA 0xffffccf4b___ -> PA 0x13fec5___
    driver.write_memory(0x13a179ff8_pa, 0x13a222003);
    driver.write_memory(0x13a222ff8_pa, 0x13ac01003);
    driver.write_memory(0x13ac01338_pa, 0x13aee0003);
    driver.write_memory(0x13aee0a58_pa, 0xe800013fec5f53);
    // VA 0xffffccf4c___ -> PA 0x13fec6___
    driver.write_memory(0x13aee0a60_pa, 0xe800013fec6f53);
}

class Top : public Module
{
    void setup()
    {
        mmu.inst_mmu_request.bind(inst_mmureq_port);
        mmu.inst_mmu_ack.bind(inst_mmuack_port);
        mmu.inst_mmu_response.bind(inst_mmuresp_port);
        mmu.inst_tlbi_request_out.bind(inst_tlbireq_out_port);
        mmu.data_mmu_request.bind(data_mmureq_port);
        mmu.data_mmu_ack.bind(data_mmuack_port);
        mmu.data_mmu_response.bind(data_mmuresp_port);
        mmu.data_tlbi_request_out.bind(data_tlbireq_out_port);
        mmu.tlbi_request_in.bind(tlbireq_in_port);
        mmu.driver_port.bind(driver);
        mmu.flush_port.bind(mmu_flush);

        l2.observable_request.bind(memreq_port);
        l2.observable_response.bind(memresp_port);

        connect(mmu.pte_fetch_out, l2.request);
        connect(l2.response, mmu.pte_fetch_in);

        seed_driver(driver);
    }

public:
    explicit Top(const sc_core::sc_module_name& name) : Module(name) { setup(); }
    Top(const sc_core::sc_module_name& name, Clock* clock, Environment* env) : Module(name, clock, env) { setup(); }

    MMU mmu{"mmu"};
    ObservableL2Dummy l2{"l2"};

    NiceMock<MockMMUReqReader> inst_mmureq_port, data_mmureq_port;
    NiceMock<MockMMUAckWriter> inst_mmuack_port, data_mmuack_port;
    NiceMock<MockMMURespWriter> inst_mmuresp_port, data_mmuresp_port;
    NiceMock<MockTLBIReqWriter> inst_tlbireq_out_port, data_tlbireq_out_port;
    NiceMock<MockTLBIReqReader> tlbireq_in_port;
    NiceMock<MockFlushReader> mmu_flush;
    NiceMock<MockMemReqWriter> memreq_port;
    NiceMock<MockMemRespWriter> memresp_port;
    NiceMock<FakeDriver> driver;
};

struct MMUTestBase : public TestModule
{
    inline static constexpr uint64_t PAGE_SIZE = PageSize(PageSize::Size::PS_4KB).page_bytes();

    Top top{"top"};

    MMUTestBase() = default;
    MMUTestBase(std::initializer_list<std::pair<std::string, sc::json>> settings) : TestModule(settings) {}

    void SetUp() override
    {
        get_clock()->run_always(true);
        archx::Instruction::set_decoders({"furiosa"});

        EXPECT_CALL(top.inst_mmuack_port, can_put()).WillRepeatedly(Return(true));
        EXPECT_CALL(top.inst_mmuresp_port, can_put()).WillRepeatedly(Return(true));
        EXPECT_CALL(top.data_mmuack_port, can_put()).WillRepeatedly(Return(true));
        EXPECT_CALL(top.data_mmuresp_port, can_put()).WillRepeatedly(Return(true));
        EXPECT_CALL(top.memreq_port, can_put()).WillRepeatedly(Return(true));
    }

    auto make_page_req(unsigned i, bool non_spec = false, unsigned channel = 0)
    {
        auto req = make<MMUReq>(i, 0xffffccf4baf8_va + i * PAGE_SIZE, false, non_spec);
        req->channel = channel;
        return req;
    }

    auto make_code_page_req(unsigned i) { return make<MMUReq>(i, 0x54e008_pc + i * PAGE_SIZE); }
};

struct MMUTest : MMUTestBase
{
    MMUTest()
        : MMUTestBase({
              {"top.mmu.tlb.line_size", 4096},
              {"top.mmu.tlb.sets", 256},
              {"top.mmu.tlb.ways", 4},
              {"top.mmu.s1tpc.sets", 1},
              {"top.mmu.walk_fsms.size", 1},
              {"top.mmu.tlb.invalidate_duplicates_before_fill", true},
              {"top.mmu.s1tpc.invalidate_duplicates_before_fill", true},
          })
    {
    }
};

TEST_F(MMUTest, OneInstructionRequest)
{
    auto req = make<MMUReq>(5, 0x54e008_pc);

    EXPECT_CALL(top.inst_mmureq_port, can_take()).WillOnce(Return(true)).WillRepeatedly(Return(false));
    EXPECT_CALL(top.inst_mmureq_port, take()).WillOnce(Return(req));

    EXPECT_CALL(top.inst_mmuack_port, put(Pointee(Field(&MMUAckObj::otb_id, Optional(0)))));
    EXPECT_CALL(top.inst_mmuresp_port, put(Pointee(Field(&MMURespObj::phys_addr, 0x13beb3008_pa))));

    /*
     * Ensure each level of the walk is requested from the L2
     */
    EXPECT_CALL(top.memreq_port, put(Pointee(Field(&MemReqObj::phys_addr, 0x13a179000_pa))));
    EXPECT_CALL(top.memreq_port, put(Pointee(Field(&MemReqObj::phys_addr, 0x13a22e000_pa))));
    EXPECT_CALL(top.memreq_port, put(Pointee(Field(&MemReqObj::phys_addr, 0x13aef2010_pa))));
    EXPECT_CALL(top.memreq_port, put(Pointee(Field(&MemReqObj::phys_addr, 0x13a062a70_pa))));

    tick(100);
}

TEST_F(MMUTest, OneDataRequest)
{
    auto req = make<MMUReq>(0, 0xffffccf4bb08_va, false);

    EXPECT_CALL(top.data_mmureq_port, can_take()).WillOnce(Return(true)).WillRepeatedly(Return(false));
    EXPECT_CALL(top.data_mmureq_port, take()).WillOnce(Return(req));

    EXPECT_CALL(top.data_mmuack_port, put(Pointee(Field(&MMUAckObj::otb_id, Optional(0)))));
    EXPECT_CALL(top.data_mmuresp_port, put(Pointee(Field(&MMURespObj::phys_addr, 0x13fec5b08_pa))));

    /*
     * Ensure each level of the walk is requested from the L2
     */
    EXPECT_CALL(top.memreq_port, put(Pointee(Field(&MemReqObj::phys_addr, 0x13a179ff8_pa))));
    EXPECT_CALL(top.memreq_port, put(Pointee(Field(&MemReqObj::phys_addr, 0x13a222ff8_pa))));
    EXPECT_CALL(top.memreq_port, put(Pointee(Field(&MemReqObj::phys_addr, 0x13ac01338_pa))));
    EXPECT_CALL(top.memreq_port, put(Pointee(Field(&MemReqObj::phys_addr, 0x13aee0a58_pa))));

    tick(100);
}

TEST_F(MMUTest, FaultingRequest)
{
    auto req = make<MMUReq>(0, 0x7ff5b4c62000_pc);

    EXPECT_CALL(top.inst_mmureq_port, can_take()).WillOnce(Return(true)).WillRepeatedly(Return(false));
    EXPECT_CALL(top.inst_mmureq_port, take()).WillOnce(Return(req));

    EXPECT_CALL(top.inst_mmuack_port, put(Pointee(Field(&MMUAckObj::otb_id, Optional(0)))));
    EXPECT_CALL(top.inst_mmuresp_port, put(Pointee(Field(&MMURespObj::faulted, true))));

    /*
     * Ensure only first level of the walk is requested from the L2
     */
    EXPECT_CALL(top.memreq_port, put(Pointee(Field(&MemReqObj::phys_addr, 0x13a1797f8_pa))));

    tick(100);
}

TEST_F(MMUTest, OneMissOneHit)
{
    auto req = make<MMUReq>(0, 0xffffccf4baf8_va, false);
    auto req2 = make<MMUReq>(1, 0xffffccf4baf8_va, false);

    {
        InSequence mmu_req_can_take;

        EXPECT_CALL(top.data_mmureq_port, can_take()).WillOnce(Return(true));
        for (int i = 0; i < 100; i++)
            EXPECT_CALL(top.data_mmureq_port, can_take()).WillOnce(Return(false));
        EXPECT_CALL(top.data_mmureq_port, can_take()).WillOnce(Return(true)).WillRepeatedly(Return(false));
    }
    EXPECT_CALL(top.data_mmureq_port, take()).WillOnce(Return(req)).WillOnce(Return(req2));

    EXPECT_CALL(top.data_mmuack_port, put(Pointee(Field(&MMUAckObj::otb_id, Optional(0))))).Times(2);
    EXPECT_CALL(top.data_mmuresp_port, put(Pointee(Field(&MMURespObj::phys_addr, 0x13fec5af8_pa)))).Times(2);

    /*
     * Ensure each level of the walk is requested from the L2
     */
    EXPECT_CALL(top.memreq_port, put(Pointee(Field(&MemReqObj::phys_addr, 0x13a179ff8_pa))));
    EXPECT_CALL(top.memreq_port, put(Pointee(Field(&MemReqObj::phys_addr, 0x13a222ff8_pa))));
    EXPECT_CALL(top.memreq_port, put(Pointee(Field(&MemReqObj::phys_addr, 0x13ac01338_pa))));
    EXPECT_CALL(top.memreq_port, put(Pointee(Field(&MemReqObj::phys_addr, 0x13aee0a58_pa))));

    tick(200);
}

TEST_F(MMUTest, FillUpOTB)
{
    {
        InSequence seq;
        for (unsigned i = 0; i < 9; ++i) {
            auto req = make<MMUReq>(i, 0xffffccf4baf8_va + i * PAGE_SIZE, false);
            EXPECT_CALL(top.data_mmureq_port, can_take()).WillOnce(Return(true));
            EXPECT_CALL(top.data_mmureq_port, take()).WillOnce(Return(req));
        }

        EXPECT_CALL(top.data_mmureq_port, can_take()).WillRepeatedly(Return(false));
    }

    EXPECT_CALL(top.data_mmuack_port, put(Pointee(Field(&MMUAckObj::otb_id, Ne(std::nullopt))))).Times(8);
    EXPECT_CALL(top.data_mmuack_port, put(Pointee(Field(&MMUAckObj::otb_id, Eq(std::nullopt))))).Times(1);

    tick(1000);
}

struct MMUOTBReservationTest : MMUTestBase
{
    MMUOTBReservationTest()
        : MMUTestBase({
              {"top.mmu.tlb.line_size", 4096},
              {"top.mmu.tlb.sets", 256},
              {"top.mmu.tlb.ways", 4},
              {"top.mmu.s1tpc.sets", 1},
              {"top.mmu.walk_fsms.size", 1},
              {"top.mmu.otb.reserve_for_oldest_after_full", true},
              {"top.mmu.tlb.invalidate_duplicates_before_fill", true},
              {"top.mmu.s1tpc.invalidate_duplicates_before_fill", true},
          })
    {
    }

    // cycle 63 is after the first OTB is deallocated
    static const unsigned AFTER_FIRST_OTB_DEALLOC = 62;

    void SetUp() override
    {
        MMUTestBase::SetUp();

        {
            InSequence seq;
            EXPECT_CALL(top.inst_mmureq_port, can_take()).WillOnce(Return(false));

            for (unsigned i = 0; i < 8; ++i) {
                auto req = this->make_page_req(i);
                EXPECT_CALL(top.data_mmureq_port, can_take()).WillOnce(Return(true));
                EXPECT_CALL(top.data_mmureq_port, take()).WillOnce(Return(req));
            }

            EXPECT_CALL(top.data_mmureq_port, can_take()).WillOnce(Return(false));
        }

        EXPECT_CALL(top.data_mmuack_port, put(Pointee(Field(&MMUAckObj::otb_id, Ne(std::nullopt)))))
            .Times(8)
            .RetiresOnSaturation();

        tick();
    }

    void TearDown() override { tick(1000); }
};

TEST_F(MMUOTBReservationTest, NonSpeculativeGetsReservation)
{
    auto non_spec_req = this->make_page_req(9, true);
    auto spec_req = this->make_page_req(10);

    EXPECT_CALL(top.inst_mmureq_port, can_take()).WillRepeatedly(Return(false));

    {
        InSequence seq;
        EXPECT_CALL(top.data_mmureq_port, can_take()).WillOnce(Return(true));
        EXPECT_CALL(top.data_mmureq_port, take()).WillOnce(Return(non_spec_req));

        EXPECT_CALL(top.data_mmureq_port, can_take()).WillOnce(Return(true));
        EXPECT_CALL(top.data_mmureq_port, take()).WillOnce(Return(spec_req));

        EXPECT_CALL(top.data_mmureq_port, can_take()).Times(AFTER_FIRST_OTB_DEALLOC).WillRepeatedly(Return(false));

        EXPECT_CALL(top.data_mmureq_port, can_take()).WillOnce(Return(true));
        EXPECT_CALL(top.data_mmureq_port, take()).WillOnce(Return(spec_req));

        EXPECT_CALL(top.data_mmureq_port, can_take()).WillOnce(Return(true));
        EXPECT_CALL(top.data_mmureq_port, take()).WillOnce(Return(non_spec_req));

        EXPECT_CALL(top.data_mmureq_port, can_take()).Times(5).WillRepeatedly(Return(false));

        EXPECT_CALL(top.data_mmureq_port, can_take()).WillOnce(Return(true));
        EXPECT_CALL(top.data_mmureq_port, take()).WillOnce(Return(spec_req));

        EXPECT_CALL(top.data_mmureq_port, can_take()).WillRepeatedly(Return(false));
    }

    {
        InSequence seq;
        EXPECT_CALL(top.data_mmuack_port, put(Pointee(Field(&MMUAckObj::otb_id, Eq(std::nullopt))))).Times(2);
        EXPECT_CALL(top.data_mmuack_port, put(Pointee(Field(&MMUAckObj::otb_id, Eq(std::nullopt)))));
        EXPECT_CALL(top.data_mmuack_port, put(Pointee(Field(&MMUAckObj::otb_id, Ne(std::nullopt)))));
        EXPECT_CALL(top.data_mmuack_port, put(Pointee(Field(&MMUAckObj::otb_id, Eq(std::nullopt)))));
    }
}

TEST_F(MMUOTBReservationTest, ReservationTakenByInstruction)
{
    auto non_spec_req = this->make_page_req(9, true);
    auto inst_req = make<MMUReq>(10, 0x54e008_pc);

    {
        InSequence seq;
        EXPECT_CALL(top.data_mmureq_port, can_take()).WillOnce(Return(true));
        EXPECT_CALL(top.data_mmureq_port, take()).WillOnce(Return(non_spec_req));

        EXPECT_CALL(top.data_mmureq_port, can_take()).Times(AFTER_FIRST_OTB_DEALLOC).WillRepeatedly(Return(false));

        EXPECT_CALL(top.data_mmureq_port, can_take()).WillOnce(Return(true));
        EXPECT_CALL(top.data_mmureq_port, take()).WillOnce(Return(non_spec_req));

        EXPECT_CALL(top.data_mmureq_port, can_take()).WillRepeatedly(Return(false));
    }

    {
        InSequence seq;
        EXPECT_CALL(top.inst_mmureq_port, can_take()).Times(AFTER_FIRST_OTB_DEALLOC).WillRepeatedly(Return(false));

        EXPECT_CALL(top.inst_mmureq_port, can_take()).WillOnce(Return(true));
        EXPECT_CALL(top.inst_mmureq_port, take()).WillOnce(Return(inst_req));

        EXPECT_CALL(top.inst_mmureq_port, can_take()).WillRepeatedly(Return(false));
    }

    EXPECT_CALL(top.data_mmuack_port, put(Pointee(Field(&MMUAckObj::otb_id, Eq(std::nullopt))))).Times(2);
    EXPECT_CALL(top.inst_mmuack_port, put(Pointee(Field(&MMUAckObj::otb_id, Ne(std::nullopt)))));
}

TEST_F(MMUOTBReservationTest, ReservationClearedOnFlush)
{
    auto non_spec_req = this->make_page_req(9, true);

    EXPECT_CALL(top.inst_mmureq_port, can_take()).WillRepeatedly(Return(false));

    {
        InSequence seq;
        EXPECT_CALL(top.data_mmureq_port, can_take()).WillOnce(Return(true));
        EXPECT_CALL(top.data_mmureq_port, take()).WillOnce(Return(non_spec_req));

        EXPECT_CALL(top.data_mmureq_port, can_take()).Times(10).WillRepeatedly(Return(false));

        for (unsigned i = 0; i < 8; ++i) {
            auto req = this->make_page_req(i);
            EXPECT_CALL(top.data_mmureq_port, can_take()).WillOnce(Return(true));
            EXPECT_CALL(top.data_mmureq_port, take()).WillOnce(Return(req));
        }

        EXPECT_CALL(top.data_mmureq_port, can_take()).WillRepeatedly(Return(false));
    }

    auto flush = make<Flush>(0x0_pc, 0x0_pc, nullptr, nullptr, 8, FlushType::BR_MISPREDICT);
    EXPECT_CALL(top.mmu_flush, can_read()).WillOnce(Return(true)).WillRepeatedly(Return(false));
    EXPECT_CALL(top.mmu_flush, read()).WillOnce(Return(flush));

    {
        InSequence seq;
        EXPECT_CALL(top.data_mmuack_port, put(Pointee(Field(&MMUAckObj::otb_id, Eq(std::nullopt)))));
        EXPECT_CALL(top.data_mmuack_port, put(Pointee(Field(&MMUAckObj::otb_id, Ne(std::nullopt))))).Times(8);
    }
}

struct MMUTwoFSMTest : MMUTestBase
{
    MMUTwoFSMTest()
        : MMUTestBase({
              {"top.mmu.tlb.line_size", 4096},
              {"top.mmu.tlb.sets", 256},
              {"top.mmu.tlb.ways", 4},
              {"top.mmu.s1tpc.sets", 1},
              {"top.mmu.walk_fsms.size", 2},
              {"top.mmu.tlb.invalidate_duplicates_before_fill", true},
              {"top.mmu.s1tpc.invalidate_duplicates_before_fill", true},
          })
    {
    }
};

TEST_F(MMUTwoFSMTest, TwoSimultaneousMissesOneHit)
{
    VirtAddr va(0xffffccf4bb00);
    PhysAddr pa(0x13fec5b00);
    auto req1a = make<MMUReq>(1, va, false);
    auto req1b = make<MMUReq>(3, va, false);

    VirtAddr va2(0xffffccf4caf8);
    PhysAddr pa2(0x13fec6af8);
    auto req2 = make<MMUReq>(2, va2, false);

    {
        InSequence mmu_req_can_take;

        EXPECT_CALL(top.data_mmureq_port, can_take())
            .WillOnce(Return(true))
            .WillOnce(Return(false))
            .WillOnce(Return(true));
        for (int i = 0; i < 100; i++)
            EXPECT_CALL(top.data_mmureq_port, can_take()).WillOnce(Return(false));
        EXPECT_CALL(top.data_mmureq_port, can_take()).WillOnce(Return(true)).WillRepeatedly(Return(false));
    }
    EXPECT_CALL(top.data_mmureq_port, take()).WillOnce(Return(req1a)).WillOnce(Return(req2)).WillOnce(Return(req1b));

    EXPECT_CALL(top.data_mmuack_port,
                put(Pointee(AllOf(Field(&MMUAckObj::otb_id, Optional(0)), Field(&MMUAckObj::id, 1)))));
    EXPECT_CALL(top.data_mmuack_port,
                put(Pointee(AllOf(Field(&MMUAckObj::otb_id, Optional(1)), Field(&MMUAckObj::id, 2)))));
    EXPECT_CALL(top.data_mmuack_port,
                put(Pointee(AllOf(Field(&MMUAckObj::otb_id, Optional(0)), Field(&MMUAckObj::id, 3)))));

    EXPECT_CALL(top.data_mmuresp_port, put(Pointee(Field(&MMURespObj::phys_addr, pa)))).Times(2);
    EXPECT_CALL(top.data_mmuresp_port, put(Pointee(Field(&MMURespObj::phys_addr, pa2))));

    /*
     * Because the two misses are simultaneous, each PTE is requested twice
     * even though some are shared.
     */
    // First three levels are requested twice each
    EXPECT_CALL(top.memreq_port, put(Pointee(Field(&MemReqObj::phys_addr, 0x13a179ff8_pa)))).Times(2);
    EXPECT_CALL(top.memreq_port, put(Pointee(Field(&MemReqObj::phys_addr, 0x13a222ff8_pa)))).Times(2);
    EXPECT_CALL(top.memreq_port, put(Pointee(Field(&MemReqObj::phys_addr, 0x13ac01338_pa)))).Times(2);
    // Final level differs
    EXPECT_CALL(top.memreq_port, put(Pointee(Field(&MemReqObj::phys_addr, 0x13aee0a58_pa))));
    EXPECT_CALL(top.memreq_port, put(Pointee(Field(&MemReqObj::phys_addr, 0x13aee0a60_pa))));

    tick(200);
}

/*
 * Tests S1TPC is working, by checking only the final unshared portion of the
 * walk is duplicated.
 */
TEST_F(MMUTwoFSMTest, TwoMissOneHit)
{
    VirtAddr va(0xffffccf4bb00);
    PhysAddr pa(0x13fec5b00);
    auto req1a = make<MMUReq>(1, va, false);
    auto req1b = make<MMUReq>(3, va, false);

    VirtAddr va2(0xffffccf4caf8);
    PhysAddr pa2(0x13fec6af8);
    auto req2 = make<MMUReq>(2, va2, false);

    {
        InSequence mmu_req_can_take;

        EXPECT_CALL(top.data_mmureq_port, can_take()).WillOnce(Return(true));
        for (int i = 0; i < 100; i++)
            EXPECT_CALL(top.data_mmureq_port, can_take()).WillOnce(Return(false));
        EXPECT_CALL(top.data_mmureq_port, can_take()).WillOnce(Return(true));
        for (int i = 0; i < 100; i++)
            EXPECT_CALL(top.data_mmureq_port, can_take()).WillOnce(Return(false));
        EXPECT_CALL(top.data_mmureq_port, can_take()).WillOnce(Return(true)).WillRepeatedly(Return(false));
    }
    EXPECT_CALL(top.data_mmureq_port, take()).WillOnce(Return(req1a)).WillOnce(Return(req2)).WillOnce(Return(req1b));

    EXPECT_CALL(top.data_mmuack_port, put(Pointee(Field(&MMUAckObj::otb_id, Optional(0))))).Times(3);

    EXPECT_CALL(top.data_mmuresp_port, put(Pointee(Field(&MMURespObj::phys_addr, pa)))).Times(2);
    EXPECT_CALL(top.data_mmuresp_port, put(Pointee(Field(&MMURespObj::phys_addr, pa2))));

    /*
     * Ensure each level of the first walk is requested from the L2
     */
    EXPECT_CALL(top.memreq_port, put(Pointee(Field(&MemReqObj::phys_addr, 0x13a179ff8_pa))));
    EXPECT_CALL(top.memreq_port, put(Pointee(Field(&MemReqObj::phys_addr, 0x13a222ff8_pa))));
    EXPECT_CALL(top.memreq_port, put(Pointee(Field(&MemReqObj::phys_addr, 0x13ac01338_pa))));
    EXPECT_CALL(top.memreq_port, put(Pointee(Field(&MemReqObj::phys_addr, 0x13aee0a58_pa))));
    // Only this portion of the second walk should be requested
    EXPECT_CALL(top.memreq_port, put(Pointee(Field(&MemReqObj::phys_addr, 0x13aee0a60_pa))));

    tick(300);
}

TEST_F(MMUTwoFSMTest, CombineDuplicates)
{
    auto req = make<MMUReq>(0, 0xffffccf4baf8_va, false);
    auto req2 = make<MMUReq>(1, 0xffffccf4baf8_va, false);

    EXPECT_CALL(top.data_mmureq_port, can_take())
        .WillOnce(Return(true))
        .WillOnce(Return(false))
        .WillOnce(Return(true))
        .WillRepeatedly(Return(false));

    EXPECT_CALL(top.data_mmureq_port, take()).WillOnce(Return(req)).WillOnce(Return(req2));

    EXPECT_CALL(top.data_mmuack_port,
                put(Pointee(AllOf(Field(&MMUAckObj::otb_id, Optional(0)), Field(&MMUAckObj::id, 0)))));
    EXPECT_CALL(top.data_mmuack_port,
                put(Pointee(AllOf(Field(&MMUAckObj::otb_id, Optional(0)), Field(&MMUAckObj::id, 1)))));

    EXPECT_CALL(top.data_mmuresp_port, put(Pointee(Field(&MMURespObj::phys_addr, 0x13fec5af8_pa)))).Times(1);

    /*
     * Ensure each level of the first walk is requested from the L2
     */
    EXPECT_CALL(top.memreq_port, put(Pointee(Field(&MemReqObj::phys_addr, 0x13a179ff8_pa))));
    EXPECT_CALL(top.memreq_port, put(Pointee(Field(&MemReqObj::phys_addr, 0x13a222ff8_pa))));
    EXPECT_CALL(top.memreq_port, put(Pointee(Field(&MemReqObj::phys_addr, 0x13ac01338_pa))));
    EXPECT_CALL(top.memreq_port, put(Pointee(Field(&MemReqObj::phys_addr, 0x13aee0a58_pa))));

    tick(200);
}

TEST_F(MMUTwoFSMTest, PassInvalidationOn)
{
    auto mmureq = make<MMUReq>(0, 0xffffccf4bb08_va, false);

    std::shared_ptr<archx::Instruction> archx_instr = std::make_shared<archx::InstructionSimple>();
    auto instr = trans_make<core::Instruction>(std::move(archx_instr), nullptr);
    auto tlbireq = make<TLBIReq>(5, instr);

    EXPECT_CALL(top.data_mmureq_port, can_take()).WillOnce(Return(true)).WillRepeatedly(Return(false));
    EXPECT_CALL(top.data_mmureq_port, take()).WillOnce(Return(mmureq));

    EXPECT_CALL(top.data_mmuack_port, put(Pointee(Field(&MMUAckObj::otb_id, Optional(0)))));
    EXPECT_CALL(top.data_mmuresp_port, put(Pointee(Field(&MMURespObj::phys_addr, 0x13fec5b08_pa))));

    EXPECT_CALL(top.tlbireq_in_port, can_take())
        .WillOnce(Return(false))
        .WillOnce(Return(false))
        .WillOnce(Return(false))
        .WillOnce(Return(false))
        .WillOnce(Return(true))
        .WillRepeatedly(Return(false));
    EXPECT_CALL(top.tlbireq_in_port, take()).WillOnce(Return(tlbireq));

    EXPECT_CALL(top.data_tlbireq_out_port, put(Pointee(Field(&TLBIReqObj::id, 5))));

    tick(500);
}

struct MMUInvalTest : TestModule
{};

TEST_F(MMUInvalTest, InvalidationLengthensTranslation)
{
    ClockDummy clock;
    Environment env{"system"};
    env.parameters.add_setting("top.mmu.tlb.line_size", 4096, "test");
    env.parameters.add_setting("top.mmu.tlb.sets", 256, "test");
    env.parameters.add_setting("top.mmu.tlb.ways", 4, "test");
    env.parameters.add_setting("top.mmu.s1tpc.sets", 1, "test");
    env.parameters.add_setting("top.mmu.walk_fsms.size", 1, "test");
    env.parameters.add_setting("top.mmu.tlb.invalidate_duplicates_before_fill", true, "test");
    env.parameters.add_setting("top.mmu.s1tpc.invalidate_duplicates_before_fill", true, "test");
    env.parameters.add_setting("top.mmu.enable_invalidation_block", false, "test");
    Environment env_inval{"system"};
    env_inval.parameters.add_setting("top_inval.mmu.tlb.line_size", 4096, "test");
    env_inval.parameters.add_setting("top_inval.mmu.tlb.sets", 256, "test");
    env_inval.parameters.add_setting("top_inval.mmu.tlb.ways", 4, "test");
    env_inval.parameters.add_setting("top_inval.mmu.s1tpc.sets", 1, "test");
    env_inval.parameters.add_setting("top_inval.mmu.walk_fsms.size", 1, "test");
    env_inval.parameters.add_setting("top_inval.mmu.tlb.invalidate_duplicates_before_fill", true, "test");
    env_inval.parameters.add_setting("top_inval.mmu.s1tpc.invalidate_duplicates_before_fill", true, "test");
    env_inval.parameters.add_setting("top_inval.mmu.enable_invalidation_block", false, "test");
    Top top("top", &clock, &env);
    Top top_inval("top_inval", &clock, &env_inval);
    clock.run_always(true);

    /*
     * This test sets up two instances of the MMU and performs two identical
     * side-by-side translations. It then triggers an invalidation on one of
     * the two and attempts the identical translations over again, ensuring
     * that the invalidated one takes longer.
     */

    auto mmureq_0 = make<MMUReq>(0, 0xffffccf4bb08_va, false);
    auto mmureq_1 = make<MMUReq>(1, 0xffffccf4bb08_va, false);

    std::shared_ptr<archx::Instruction> archx_instr = std::make_shared<archx::InstructionSimple>();
    auto instr = trans_make<core::Instruction>(std::move(archx_instr), nullptr);
    auto tlbireq = make<TLBIReq>(5, instr);

    {
        InSequence s1;
        EXPECT_CALL(top.data_mmureq_port, can_take()).Times(100).WillOnce(Return(true)).WillRepeatedly(Return(false));
        EXPECT_CALL(top.data_mmureq_port, can_take()).WillOnce(Return(true)).WillRepeatedly(Return(false));
    }
    EXPECT_CALL(top.data_mmureq_port, take()).WillOnce(Return(mmureq_0)).WillOnce(Return(mmureq_1));

    EXPECT_CALL(top.data_mmuack_port, can_put()).WillOnce(Return(true)).WillOnce(Return(true));
    EXPECT_CALL(top.data_mmuack_port,
                put(Pointee(AllOf(Field(&MMUAckObj::otb_id, Optional(0)), Field(&MMUAckObj::id, 0)))));
    EXPECT_CALL(top.data_mmuack_port,
                put(Pointee(AllOf(Field(&MMUAckObj::otb_id, Optional(0)), Field(&MMUAckObj::id, 1)))));

    EXPECT_CALL(top.data_mmuresp_port, can_put()).WillOnce(Return(true)).WillOnce(Return(true));

    Expectation resp_exp_0 =
        EXPECT_CALL(top.data_mmuresp_port, put(Pointee(Field(&MMURespObj::phys_addr, 0x13fec5b08_pa))));
    Expectation resp_exp_1 =
        EXPECT_CALL(top.data_mmuresp_port, put(Pointee(Field(&MMURespObj::phys_addr, 0x13fec5b08_pa))))
            .After(resp_exp_0);

    {
        InSequence inval_s1;
        EXPECT_CALL(top_inval.data_mmureq_port, can_take())
            .Times(100)
            .WillOnce(Return(true))
            .WillRepeatedly(Return(false));
        EXPECT_CALL(top_inval.data_mmureq_port, can_take()).WillOnce(Return(true)).WillRepeatedly(Return(false));
    }
    EXPECT_CALL(top_inval.data_mmureq_port, take()).WillOnce(Return(mmureq_0)).WillOnce(Return(mmureq_1));

    EXPECT_CALL(top_inval.data_mmuack_port, can_put()).WillOnce(Return(true)).WillOnce(Return(true));
    EXPECT_CALL(top_inval.data_mmuack_port,
                put(Pointee(AllOf(Field(&MMUAckObj::otb_id, Optional(0)), Field(&MMUAckObj::id, 0)))));
    EXPECT_CALL(top_inval.data_mmuack_port,
                put(Pointee(AllOf(Field(&MMUAckObj::otb_id, Optional(0)), Field(&MMUAckObj::id, 1)))));

    EXPECT_CALL(top_inval.data_mmuresp_port, can_put()).WillOnce(Return(true)).WillOnce(Return(true));

    Expectation inval_resp_exp_0 =
        EXPECT_CALL(top_inval.data_mmuresp_port, put(Pointee(Field(&MMURespObj::phys_addr, 0x13fec5b08_pa))));
    Expectation inval_resp_exp_1 =
        EXPECT_CALL(top_inval.data_mmuresp_port, put(Pointee(Field(&MMURespObj::phys_addr, 0x13fec5b08_pa))))
            .After(resp_exp_1, inval_resp_exp_0);

    {
        InSequence inval_s2;
        EXPECT_CALL(top_inval.tlbireq_in_port, can_take()).Times(50).WillRepeatedly(Return(false));
        EXPECT_CALL(top_inval.tlbireq_in_port, can_take())
            .WillOnce(Return(false))
            .WillOnce(Return(false))
            .WillOnce(Return(true))
            .WillRepeatedly(Return(false));
    }
    EXPECT_CALL(top_inval.tlbireq_in_port, take()).WillOnce(Return(tlbireq));

    EXPECT_CALL(top_inval.data_tlbireq_out_port, put(Pointee(Field(&TLBIReqObj::id, 5))));

    // Run until the invalidation should've gone through
    for (int i = 0; i < 500; i++) {
        clock.tick();
    }
}

struct MMUFairnessTest : MMUTestBase
{
    std::array<MMUReq, 6> mmureq = {
        this->make_code_page_req(0),
        this->make_code_page_req(1),
        this->make_page_req(1, false, 0),
        this->make_page_req(2, false, 1),
        this->make_page_req(3, false, 0),
        this->make_page_req(4, false, 1),
    };

    MMUFairnessTest()
        : MMUTestBase({
              {"top.mmu.tlb.line_size", 4096},
              {"top.mmu.tlb.sets", 256},
              {"top.mmu.tlb.ways", 4},
              {"top.mmu.s1tpc.sets", 1},
              {"top.mmu.walk_fsms.size", 1},
              {"top.mmu.tlb.invalidate_duplicates_before_fill", true},
              {"top.mmu.s1tpc.invalidate_duplicates_before_fill", true},
              {"top.mmu.otb.entries", 4},
              {"top.mmu.otb.lsu_sections", 2},
              {"top.mmu.allocation_fairness", true},
          })
    {
    }
};

TEST_F(MMUFairnessTest, TwoRequestsTakesOlder)
{
    EXPECT_CALL(top.inst_mmureq_port, take()).WillOnce(Return(mmureq[0])).WillOnce(Return(mmureq[1]));

    EXPECT_CALL(top.data_mmureq_port, take())
        .WillOnce(Return(mmureq[3]))
        .WillOnce(Return(mmureq[2]))
        .WillOnce(Return(mmureq[5]));

    EXPECT_CALL(top.inst_mmureq_port, can_take())
        .WillOnce(Return(false))
        .WillOnce(Return(true))
        .WillOnce(Return(false))
        .WillOnce(Return(true))
        .WillRepeatedly(Return(false));
    {
        InSequence seq;
        EXPECT_CALL(top.inst_mmuack_port, put(Pointee(Field(&MMUAckObj::otb_id, Ne(std::nullopt))))).Times(1);
        EXPECT_CALL(top.inst_mmuack_port, put(Pointee(Field(&MMUAckObj::otb_id, std::nullopt)))).Times(1);
    }

    EXPECT_CALL(top.data_mmureq_port, can_take())
        .WillOnce(Return(true))
        .WillOnce(Return(false))
        .WillOnce(Return(true))
        .WillOnce(Return(false))
        .WillOnce(Return(true))
        .WillRepeatedly(Return(false));
    EXPECT_CALL(top.data_mmuack_port, put(Pointee(Field(&MMUAckObj::otb_id, Ne(std::nullopt))))).Times(3);

    tick(3);
}

TEST_F(MMUFairnessTest, ThreeRequestsTakesOlderTwo)
{
    EXPECT_CALL(top.inst_mmureq_port, take()).WillOnce(Return(mmureq[0])).WillOnce(Return(mmureq[1]));

    EXPECT_CALL(top.data_mmureq_port, take())
        .WillOnce(Return(mmureq[3]))
        .WillOnce(Return(mmureq[2]))
        .WillOnce(Return(mmureq[5]));

    EXPECT_CALL(top.inst_mmureq_port, can_take())
        .WillOnce(Return(true))
        .WillOnce(Return(false))
        .WillOnce(Return(false))
        .WillOnce(Return(true))
        .WillRepeatedly(Return(false));

    EXPECT_CALL(top.inst_mmuack_port, put(Pointee(Field(&MMUAckObj::otb_id, Ne(std::nullopt))))).Times(2);

    EXPECT_CALL(top.data_mmureq_port, can_take())
        .WillOnce(Return(false))
        .WillOnce(Return(true))
        .WillOnce(Return(false))
        .WillOnce(Return(true))
        .WillOnce(Return(true))
        .WillRepeatedly(Return(false));

    EXPECT_CALL(top.data_mmuack_port, put(Pointee(Field(&MMUAckObj::otb_id, Ne(std::nullopt))))).Times(2);
    EXPECT_CALL(top.data_mmuack_port, put(Pointee(Field(&MMUAckObj::otb_id, std::nullopt)))).Times(1);

    tick(3);
}

} // namespace ampere::mmu
