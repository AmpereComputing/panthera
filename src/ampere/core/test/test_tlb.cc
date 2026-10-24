//
// Copyright (c) 2019-2020 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//

#include <gmock/gmock.h>

#include "ampere/archx/addr.h"
#include "ampere/archx/page_size.h"
#include "ampere/archx/testing/mock_driver.h"
#include "ampere/core/tlb.h"
#include "ampere/sc/testing/test_module.h"
#include "ampere/sc/transaction.h"

using ampere::core::TLB;
using ampere::core::TLBEntry;
using ampere::core::TLBTag;
using ampere::core::TLBType;
using archx::operator""_pc;
using archx::operator""_va;
using archx::operator""_pa;

using ::testing::Return;

template<typename ADDR, TLBType TYPE>
struct TLBTest : ampere::sc::TestModule
{
    TLB<TLBTag<ADDR>, core::TLBEntry<archx::PhysAddr>, TYPE> tlb;
    TLBTest() : tlb("tlb", TLBEntry<archx::PhysAddr>(0_pa, archx::PageSize::Size::PS_4KB)) {}
};

using TLBCodeTest = TLBTest<archx::ProgramCounter, TLBType::INSTRUCTION>;
using TLBDataTest = TLBTest<archx::VirtAddr, TLBType::DATA>;

TEST_F(TLBCodeTest, IsInstruction)
{
    EXPECT_TRUE(tlb.is_instr(TLBTag<archx::ProgramCounter>(0_pc, 0)));
}

TEST_F(TLBDataTest, IsNotInstruction)
{
    EXPECT_FALSE(tlb.is_instr(TLBTag<archx::VirtAddr>(0_va, 0)));
}

struct BothTag : TLBTag<archx::VirtAddr>
{
    BothTag(archx::VirtAddr addr, uint16_t asid) : TLBTag<archx::VirtAddr>(addr, asid), is_instr(false) {}
    BothTag(archx::ProgramCounter addr, uint16_t asid)
        : TLBTag<archx::VirtAddr>(archx::VirtAddr(addr.value()), asid), is_instr(true)
    {
    }
    bool is_instr{false};
    bool instr() const { return is_instr; }
};

struct BothEntry : TLBEntry<>
{
    BothEntry(const archx::PhysAddr& pa, const ampere::archx::PageSize& ps, bool is_instr)
        : TLBEntry<>(pa, ps), is_instr(is_instr)
    {
    }
    bool is_instr{false};
};

struct TLBBothPerfectTest : ampere::sc::TestModule
{
    sc::Txn transaction;
    testing::StrictMock<archx::MockDriver> driver;
    TLB<BothTag, BothEntry, TLBType::BOTH> tlb;
    TLBBothPerfectTest()
        : ampere::sc::TestModule({
              {"tlb.enable_perfect_mode", true},
          }),
          tlb("tlb", BothEntry(0_pa, archx::PageSize::Size::PS_4KB, false))
    {
        tlb.driver.bind(driver);
    }

    void SetUp() override { this->start_simulation(); }
};

TEST_F(TLBBothPerfectTest, IsInstruction)
{
    EXPECT_FALSE(tlb.is_instr(BothTag(0_va, 0)));
    EXPECT_TRUE(tlb.is_instr(BothTag(0_pc, 0)));
}

TEST_F(TLBBothPerfectTest, LookupInstruction)
{
    EXPECT_CALL(driver, read_sysreg(archx::TCR_EL1)).WillOnce(Return(0x34b5503510));
    EXPECT_CALL(driver, read_sysreg(archx::TTBR0_EL1)).WillOnce(Return(0x13a179000));
    EXPECT_CALL(driver, read_sysreg(archx::TTBR1_EL1)).WillOnce(Return(0x13000041df8000));
    EXPECT_CALL(driver, read_memory(0x13a179000_pa)).WillOnce(Return(0x13a22e003));
    EXPECT_CALL(driver, read_memory(0x13a22e000_pa)).WillOnce(Return(0x13aef2003));
    EXPECT_CALL(driver, read_memory(0x13aef2010_pa)).WillOnce(Return(0x13a062003));
    EXPECT_CALL(driver, read_memory(0x13a062a70_pa)).WillOnce(Return(0x2000013beb3fd3));

    auto result = tlb.lookup(transaction, BothTag(0x54e838_pc, 0));
    EXPECT_TRUE(result);
    EXPECT_TRUE(result->is_instr);
    EXPECT_EQ(result->phys_addr, 0x13beb3000_pa);
    EXPECT_EQ(result->line_address(0x54e838_pc), 0x13beb3800ULL);
}

TEST_F(TLBBothPerfectTest, LookupData)
{
    EXPECT_CALL(driver, read_sysreg(archx::TCR_EL1)).WillOnce(Return(0x34b5503510));
    EXPECT_CALL(driver, read_sysreg(archx::TTBR0_EL1)).WillOnce(Return(0x13a179000));
    EXPECT_CALL(driver, read_sysreg(archx::TTBR1_EL1)).WillOnce(Return(0x13000041df8000));
    EXPECT_CALL(driver, read_memory(0x13a179ff8_pa)).WillOnce(Return(0x13a222003));
    EXPECT_CALL(driver, read_memory(0x13a222ff8_pa)).WillOnce(Return(0x13ac01003));
    EXPECT_CALL(driver, read_memory(0x13ac01338_pa)).WillOnce(Return(0x13aee0003));
    EXPECT_CALL(driver, read_memory(0x13aee0a58_pa)).WillOnce(Return(0xe800013fec5f53));

    auto result = tlb.lookup(transaction, BothTag(0xffffccf4b383_va, 0));
    EXPECT_TRUE(result);
    EXPECT_FALSE(result->is_instr);
    EXPECT_EQ(result->phys_addr, 0x13fec5000_pa);
    EXPECT_EQ(result->line_address(0xffffccf4b383_va), 0x13fec5380ULL);
}

/*
 * vim: sw=4:ts=4:et:si:cindent:cino=\:0,(0,g0,t0:
 */
