//
// Copyright (c) 2019 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//

#include <gmock/gmock.h>

#include "ampere/archx/addr.h"
#include "ampere/archx/page_size.h"
#include "ampere/core/l1tlb.h"
#include "ampere/core/tlb.h"
#include "ampere/sc/transaction.h"

#include "ampere/sc/testing/test_module.h"
#include "ampere/archx/testing/mock_driver.h"
#include "gmock/gmock.h"

namespace {

using namespace ampere;
using namespace ampere::core;
using namespace ampere::archx;

using Tag = L1TLBTag<VirtAddr>;
using PCTag = L1TLBTag<ProgramCounter>;

using ::testing::Return;

template<typename Addr, core::TLBType Type>
struct L1TLBTest : public sc::TestModule
{
    L1TLB<Addr, Type> l1tlb{"l1tlb"};
    Txn transaction = make<Txn>();

    L1TLBTest()
        : TestModule({
              {"l1tlb.sets", 1},
              {"l1tlb.ways", 16},
              {"l1tlb.line_size", 4096},
          })
    {
    }

    L1TLBTest(std::initializer_list<std::pair<std::string, sc::json>> settings) : TestModule(settings) {}
};

using L1TLBCodeTest = L1TLBTest<ProgramCounter, core::TLBType::INSTRUCTION>;
using L1TLBDataTest = L1TLBTest<VirtAddr, core::TLBType::DATA>;

TEST_F(L1TLBCodeTest, Refill)
{
    auto pc = 0x4003838_pc;
    auto pa = 0x229838_pa;

    auto tag = PCTag(pc, 0);
    auto entry = L1TLBEntry(pa, PageSize::Size::PS_2MB);

    ASSERT_FALSE(l1tlb.lookup(transaction, tag));
    ASSERT_FALSE(l1tlb.refill(transaction, tag, entry));

    auto hit = l1tlb.lookup(transaction, tag);
    ASSERT_TRUE(hit);
    ASSERT_EQ(hit->page_size, PageSize::Size::PS_2MB);
    ASSERT_EQ(hit->phys_addr, pa);
}

TEST_F(L1TLBDataTest, RefillEviction)
{
    auto orig_va = 0x7ffff8d9a8d7f_va;
    auto orig_pa = 0x5029d7f_pa;
    auto va = orig_va;
    auto pa = orig_pa;

    for (int i = 0; i < 16; i++) {
        auto tag = Tag(va, 7);
        auto entry = L1TLBEntry(pa, PageSize::Size::PS_4KB);

        auto victim = l1tlb.refill(transaction, tag, entry);
        ASSERT_FALSE(victim);
        ASSERT_TRUE(l1tlb.lookup(transaction, tag));

        va += PageSize::Size::PS_4KB;
        pa += PageSize::Size::PS_4KB;
    }

    // Ensure that the 17th refill causes an eviction
    va += PageSize::Size::PS_4KB;
    pa += PageSize::Size::PS_4KB;
    auto tag = Tag(va, 7);
    auto entry = L1TLBEntry(pa, PageSize::Size::PS_4KB);

    // And that that eviction was least recently accessed
    auto victim = l1tlb.refill(transaction, tag, entry);
    ASSERT_TRUE(victim);
    ASSERT_EQ(victim->page_size, PageSize::Size::PS_4KB);
    ASSERT_EQ(victim->phys_addr, orig_pa);

    // Ensure that entry does not result in a successful lookup
    ASSERT_FALSE(l1tlb.lookup(transaction, Tag(orig_va, 7)));
}

struct L1TLBCode4WayTest : L1TLBCodeTest
{
    L1TLBCode4WayTest()
        : L1TLBCodeTest({
              {"l1tlb.sets", 16},
              {"l1tlb.ways", 4},
              {"l1tlb.line_size", 4096},
          })
    {
    }
};

TEST_F(L1TLBCode4WayTest, RefillEvictionWith4Way)
{
    auto orig_pc = 0x7ffff8d9a8d7f_pc;
    auto orig_pa = 0x5029d7f_pa;

    // First, insert a page in each set, except the set of of orig_pc
    auto pc = orig_pc + PageSize::Size::PS_4KB;
    auto pa = orig_pa + PageSize::Size::PS_4KB;
    for (int i = 0; i < 15; i++) {
        auto tag = PCTag(pc, 7);
        auto entry = L1TLBEntry(pa, PageSize::Size::PS_4KB);

        auto victim = l1tlb.refill(transaction, tag, entry);
        ASSERT_FALSE(victim);
        ASSERT_TRUE(l1tlb.lookup(transaction, tag));

        // increment by 4k to loop through all sets
        pc += PageSize::Size::PS_4KB;
        pa += PageSize::Size::PS_4KB;
    }

    // Insert original page, then 3 more to fill all the ways in that set
    pc = orig_pc;
    pa = orig_pa;
    for (int i = 0; i < 4; i++) {
        auto tag = PCTag(pc, 7);
        auto entry = L1TLBEntry(pa, PageSize::Size::PS_4KB);

        auto victim = l1tlb.refill(transaction, tag, entry);
        ASSERT_FALSE(victim);
        ASSERT_TRUE(l1tlb.lookup(transaction, tag));

        // increment by 64k to remain in the same set
        pc += PageSize::Size::PS_64KB;
        pa += PageSize::Size::PS_64KB;
    }

    // Ensure that the 5th refill to the set causes an eviction
    pc += PageSize::Size::PS_64KB;
    pa += PageSize::Size::PS_64KB;
    auto tag = PCTag(pc, 7);
    auto entry = L1TLBEntry(pa, PageSize::Size::PS_4KB);

    // And that that eviction was the least recently accessed
    auto victim = l1tlb.refill(transaction, tag, entry);
    ASSERT_TRUE(victim);
    ASSERT_EQ(victim->page_size, PageSize::Size::PS_4KB);
    ASSERT_EQ(victim->phys_addr, orig_pa);

    // Ensure that lookup is not successful for victim
    ASSERT_FALSE(l1tlb.lookup(transaction, PCTag(orig_pc, 7)));

    // Ensure page in other sets are still resident
    for (int i = 0; i < 16; i++) {
        ASSERT_TRUE(l1tlb.lookup(transaction, PCTag(orig_pc += PageSize::Size::PS_4KB, 7)));
    }
}

TEST_F(L1TLBDataTest, Lookup_4KB)
{
    auto va = 0xdeadbeefdeadbeef_va;
    auto pa = 0xffff000040583eef_pa;

    auto tag = Tag(va, 0);
    auto entry = L1TLBEntry(pa, PageSize::Size::PS_4KB);

    ASSERT_FALSE(l1tlb.refill(transaction, tag, entry));
    auto hit = l1tlb.lookup(transaction, tag);
    ASSERT_TRUE(hit);
    ASSERT_EQ(hit->page_size, PageSize::Size::PS_4KB);
    ASSERT_EQ(hit->phys_addr, pa);

    // Within page
    ASSERT_TRUE(l1tlb.lookup(transaction, Tag(0xdeadbeefdeadb000_va, 0)));
    ASSERT_TRUE(l1tlb.lookup(transaction, Tag(0xdeadbeefdeadb0b5_va, 0)));
    ASSERT_TRUE(l1tlb.lookup(transaction, Tag(0xdeadbeefdeadbfff_va, 0)));
    // Outside page
    ASSERT_FALSE(l1tlb.lookup(transaction, Tag(0xdeadbeefdeadafff_va, 0)));
    ASSERT_FALSE(l1tlb.lookup(transaction, Tag(0xdeadbeefdeadc000_va, 0)));
    ASSERT_FALSE(l1tlb.lookup(transaction, Tag(0xfffffffffffffeef_va, 0)));
    ASSERT_FALSE(l1tlb.lookup(transaction, Tag(0xeef_va, 0)));
    ASSERT_FALSE(l1tlb.lookup(transaction, Tag(0x0_va, 0)));
    ASSERT_FALSE(l1tlb.lookup(transaction, Tag(0xffffffffffffffff_va, 0)));
}

TEST_F(L1TLBDataTest, Lookup_4TB)
{
    auto va = 0xfff0480000000000_va;
    auto pa = 0x40000000000_pa;

    auto tag = Tag(va, 5);
    auto entry = L1TLBEntry(pa, PageSize::Size::PS_4TB);

    ASSERT_FALSE(l1tlb.refill(transaction, tag, entry));
    auto hit = l1tlb.lookup(transaction, tag);
    ASSERT_TRUE(hit);
    ASSERT_EQ(hit->page_size, PageSize::Size::PS_4TB);
    ASSERT_EQ(hit->phys_addr, pa);

    // Within page
    ASSERT_TRUE(l1tlb.lookup(transaction, Tag(0xfff0490000000000_va, 5)));
    ASSERT_TRUE(l1tlb.lookup(transaction, Tag(0xfff04bffffffffff_va, 5)));
    ASSERT_TRUE(l1tlb.lookup(transaction, Tag(0xfff04a5a50000000_va, 5)));
    // Outside page
    ASSERT_FALSE(l1tlb.lookup(transaction, Tag(0xfff04c0000000000_va, 5)));
    ASSERT_FALSE(l1tlb.lookup(transaction, Tag(0xfff0440000000000_va, 5)));
    ASSERT_FALSE(l1tlb.lookup(transaction, Tag(0xfff0580000000000_va, 5)));
    ASSERT_FALSE(l1tlb.lookup(transaction, Tag(0xeef_va, 5)));
    ASSERT_FALSE(l1tlb.lookup(transaction, Tag(0x0_va, 5)));
    ASSERT_FALSE(l1tlb.lookup(transaction, Tag(0xffffffffffffffff_va, 5)));
}

TEST_F(L1TLBDataTest, Lookup_4KB_zero_ASID)
{
    auto va = 0xdeadbeefdeadbeef_va;
    auto pa = 0xffff000040583eef_pa;
    uint16_t asid = 0;

    auto tag = Tag(va, asid);
    auto entry = L1TLBEntry(pa, PageSize::Size::PS_4KB);

    ASSERT_FALSE(l1tlb.refill(transaction, tag, entry));
    auto hit = l1tlb.lookup(transaction, tag);
    ASSERT_TRUE(hit);
    ASSERT_EQ(hit->page_size, PageSize::Size::PS_4KB);
    ASSERT_EQ(hit->phys_addr, pa);

    // Within page, correct ASID
    ASSERT_TRUE(l1tlb.lookup(transaction, Tag(0xdeadbeefdeadb0b5_va, asid)));
    // Within page, incorrect ASID
    ASSERT_FALSE(l1tlb.lookup(transaction, Tag(0xdeadbeefdeadb0b5_va, 0xa37)));
    ASSERT_FALSE(l1tlb.lookup(transaction, Tag(0xdeadbeefdeadb0b5_va, 0x1)));
    ASSERT_FALSE(l1tlb.lookup(transaction, Tag(0xdeadbeefdeadb0b5_va, 0xffff)));
    // Outside page, correct ASID
    ASSERT_FALSE(l1tlb.lookup(transaction, Tag(0xdeadbeefdeadc0b5_va, asid)));
    // Outside page, incorrect ASID
    ASSERT_FALSE(l1tlb.lookup(transaction, Tag(0xdeadbeefdeadc0b5_va, 0xf838)));
    ASSERT_FALSE(l1tlb.lookup(transaction, Tag(0xdeadbeefdeadc0b5_va, 0x1)));
}

TEST_F(L1TLBDataTest, Lookup_2MB_fullwidth_ASID)
{
    auto va = 0xdeadbeefdeadbeef_va;
    auto pa = 0xffff000040583eef_pa;
    uint16_t asid = 0xe947;

    auto tag = Tag(va, asid);
    auto entry = L1TLBEntry(pa, PageSize::Size::PS_2MB);

    ASSERT_FALSE(l1tlb.refill(transaction, tag, entry));
    auto hit = l1tlb.lookup(transaction, tag);
    ASSERT_TRUE(hit);
    ASSERT_EQ(hit->page_size, PageSize::Size::PS_2MB);
    ASSERT_EQ(hit->phys_addr, pa);

    // Within page, correct ASID
    ASSERT_TRUE(l1tlb.lookup(transaction, Tag(0xdeadbeefdebfb8b2_va, asid)));
    // Within page, incorrect ASID
    ASSERT_FALSE(l1tlb.lookup(transaction, Tag(0xdeadbeefdebfb8b2_va, 0xa37)));
    ASSERT_FALSE(l1tlb.lookup(transaction, Tag(0xdeadbeefdeadb0b5_va, 0x0)));
    ASSERT_FALSE(l1tlb.lookup(transaction, Tag(0xdeadbeefdeadb0b5_va, 0xffff)));
    // Outside page, correct ASID
    ASSERT_FALSE(l1tlb.lookup(transaction, Tag(0xdeadb33f00000000_va, asid)));
    // Outside page, incorrect ASID
    ASSERT_FALSE(l1tlb.lookup(transaction, Tag(0xdeadb33f00000000_va, 0xf838)));
    ASSERT_FALSE(l1tlb.lookup(transaction, Tag(0xdeadb33f00000000_va, 0x0)));
}

template<typename Addr, core::TLBType Type>
struct L1TLBPerfectTest : L1TLBTest<Addr, Type>
{
    ::testing::StrictMock<MockDriver> driver;

    static inline const auto pc = 0x54e838_pc;
    static inline const auto pa = 0x13beb3838_pa;
    static inline const auto tag = PCTag(pc, 0);
    static inline const auto entry = L1TLBEntry(pa, PageSize::Size::PS_4KB);

    L1TLBPerfectTest()
        : L1TLBTest<Addr, Type>({
              {"l1tlb.sets", 1},
              {"l1tlb.ways", 16},
              {"l1tlb.line_size", 4096},
              {"l1tlb.enable_perfect_mode", true},
          })
    {
        this->l1tlb.driver.bind(driver);
    }

    void SetUp() override { this->start_simulation(); }
};

using L1TLBPerfectCodeTest = L1TLBPerfectTest<ProgramCounter, core::TLBType::INSTRUCTION>;

TEST_F(L1TLBPerfectCodeTest, Lookup)
{
    EXPECT_CALL(driver, read_sysreg(TCR_EL1)).WillOnce(Return(0x34b5503510));
    EXPECT_CALL(driver, read_sysreg(TTBR0_EL1)).WillOnce(Return(0x13a179000));
    EXPECT_CALL(driver, read_sysreg(TTBR1_EL1)).WillOnce(Return(0x13000041df8000));
    EXPECT_CALL(driver, read_memory(0x13a179000_pa)).WillOnce(Return(0x13a22e003));
    EXPECT_CALL(driver, read_memory(0x13a22e000_pa)).WillOnce(Return(0x13aef2003));
    EXPECT_CALL(driver, read_memory(0x13aef2010_pa)).WillOnce(Return(0x13a062003));
    EXPECT_CALL(driver, read_memory(0x13a062a70_pa)).WillOnce(Return(0x2000013beb3fd3));

    ASSERT_TRUE(l1tlb.lookup(transaction, tag));
}

TEST_F(L1TLBPerfectCodeTest, Refill)
{
    ASSERT_FALSE(l1tlb.refill(transaction, tag, entry));
    auto hit = l1tlb.lookup(transaction, tag);
    ASSERT_TRUE(hit);
}

template<typename Addr, core::TLBType Type>
struct L1TLBAntiPerfectTest : L1TLBTest<Addr, Type>
{
    static inline const auto pc = 0x4003838_pc;
    static inline const auto pa = 0x229838_pa;
    static inline const auto tag = PCTag(pc, 0);
    static inline const auto entry = L1TLBEntry(pa, PageSize::Size::PS_2MB);

    L1TLBAntiPerfectTest()
        : L1TLBTest<Addr, Type>({
              {"l1tlb.sets", 1},
              {"l1tlb.ways", 16},
              {"l1tlb.line_size", 4096},
              {"l1tlb.enable_antiperfect_mode", true},
          })
    {
    }
};

using L1TLBAntiPerfectCodeTest = L1TLBAntiPerfectTest<ProgramCounter, core::TLBType::INSTRUCTION>;

TEST_F(L1TLBAntiPerfectCodeTest, Lookup)
{
    ASSERT_FALSE(l1tlb.lookup(transaction, tag));
}

TEST_F(L1TLBAntiPerfectCodeTest, Refill)
{
    ASSERT_FALSE(l1tlb.refill(transaction, tag, entry));
    auto hit = l1tlb.lookup(transaction, tag);
    ASSERT_FALSE(hit);
}

} // namespace

/*
 * vim: sw=4:ts=4:et:si:cindent:cino=\:0,(0,g0,t0:
 */
