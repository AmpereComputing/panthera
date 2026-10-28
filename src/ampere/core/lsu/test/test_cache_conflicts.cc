//
// Copyright (c) 2023 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//

#include <gmock/gmock.h>
#include "ampere/archx/addr.h"
#include "ampere/core/lsu/cache.h"
#include "ampere/sc/testing/test_module.h"
#include "ampere/core/testing/uops.h"
#include "ampere/core/lsu/cache_conflicts.h"

using archx::operator""_va;
using archx::operator""_pa;

struct LSUCacheConflictsTest : ampere::sc::TestModule
{
    core::L1DCache cache{"cache"};
    sc::ModuleWrapper<core::LSUCacheConflicts> conflicts{"top", cache, 2};
    core::Uop uop;

    LSUCacheConflictsTest(std::initializer_list<std::pair<std::string, sc::json>> settings) : TestModule(settings) {}

    LSUCacheConflictsTest()
        : TestModule({
              {"top.enable_read_read_chunk_conflicts", false},
              {"top.enable_write_read_chunk_bank_conflicts", false},
              {"top.enable_fill_read_chunk_bank_conflicts", false},
              {"top.enable_refill_tag_conflict", false},
          })
    {
    }

    void SetUp() override { uop = core::create_load_uop(0x0_va); }

    auto load_cache_conflicts(const core::ConstUop& uop, VirtAddr virt_addr, unsigned datasize)
    {
        auto c = conflicts->load_cache_conflicts(
            uop, virt_addr, archx::PhysAddr(virt_addr.value()), datasize, uop->sch.lsu_scheduler_pipe());
        if (!c.any())
            conflicts->load_cache_read(uop, virt_addr, datasize, uop->sch.lsu_scheduler_pipe(), c);
        return c;
    }
};

TEST_F(LSUCacheConflictsTest, BankForOffset)
{
    EXPECT_EQ(conflicts->offset_to_bank(0x0_va), 0);
    EXPECT_EQ(conflicts->offset_to_bank(0x1_va), 0);
    EXPECT_EQ(conflicts->offset_to_bank(0x2_va), 1);
    EXPECT_EQ(conflicts->offset_to_bank(0x3_va), 1);
    EXPECT_EQ(conflicts->offset_to_bank(0x2f_va), 7);
    EXPECT_EQ(conflicts->offset_to_bank(0x30_va), 0);
}

TEST_F(LSUCacheConflictsTest, BanksRead)
{
    EXPECT_EQ(conflicts->banks_to_read(0x0_va, 0), 0);
    EXPECT_EQ(conflicts->banks_to_read(0x0_va, 2), 1);
    EXPECT_EQ(conflicts->banks_to_read(0x0_va, 3), 2);
    EXPECT_EQ(conflicts->banks_to_read(0x1_va, 1), 1);
    EXPECT_EQ(conflicts->banks_to_read(0x1_va, 2), 2);
    EXPECT_EQ(conflicts->banks_to_read(0x1_va, 4), 3);
    EXPECT_EQ(conflicts->banks_to_read(0x1_va, 5), 3);
    EXPECT_EQ(conflicts->banks_to_read(0x0_va, 64), 8);
}

TEST_F(LSUCacheConflictsTest, BanksReadAligned)
{
    EXPECT_EQ(conflicts->banks_to_read_aligned(0x3d_va, 2), 2);
    EXPECT_EQ(conflicts->banks_to_read_aligned(0x3e_va, 2), 1);
    EXPECT_EQ(conflicts->banks_to_read_aligned(0x3f_va, 2), 1);
}

TEST_F(LSUCacheConflictsTest, BankOffset)
{
    EXPECT_EQ(conflicts->bank_offset(0x0_va, 0), 0);
    EXPECT_EQ(conflicts->bank_offset(0x0_va, 1), 2);
    EXPECT_EQ(conflicts->bank_offset(0x0_va, 7), 14);
    EXPECT_EQ(conflicts->bank_offset(0x2_va, 1), 2);
    EXPECT_EQ(conflicts->bank_offset(0xf_va, 7), 14);
    EXPECT_EQ(conflicts->bank_offset(0xf_va, 0), 16);
    EXPECT_EQ(conflicts->bank_offset(0xf_va, 1), 18);
}

TEST_F(LSUCacheConflictsTest, NoConflicts)
{
    auto c = load_cache_conflicts(uop, 0x0_va, 2);
    EXPECT_FALSE(c.any());

    c = load_cache_conflicts(uop, 0x0_va, 2);
    EXPECT_FALSE(c.any());
}

struct LSUCacheConflictsReadReadTest : LSUCacheConflictsTest
{
    LSUCacheConflictsReadReadTest()
        : LSUCacheConflictsTest({
              {"top.enable_read_read_chunk_conflicts", true},
              {"top.enable_write_read_chunk_bank_conflicts", false},
              {"top.enable_fill_read_chunk_bank_conflicts", false},
              {"top.enable_refill_tag_conflict", false},
              {"top.threecyc_conflict", true},
          })
    {
    }

    void SetUp() override
    {
        LSUCacheConflictsTest::SetUp();

        auto c = load_cache_conflicts(uop, 0x0_va, 2);
        EXPECT_FALSE(c.any());
        uop->sch.partition = 1;
    }
};

TEST_F(LSUCacheConflictsReadReadTest, SameExact)
{
    auto c = load_cache_conflicts(uop, 0x0_va, 2);
    EXPECT_TRUE(c.read);
}

TEST_F(LSUCacheConflictsReadReadTest, SameSet)
{
    auto c = load_cache_conflicts(uop, 0x100000001_va, 2);
    EXPECT_TRUE(c.read);
}

TEST_F(LSUCacheConflictsReadReadTest, SameSetBank)
{
    auto c = load_cache_conflicts(uop, 0x41_va, 2);
    EXPECT_TRUE(c.read);
}

TEST_F(LSUCacheConflictsReadReadTest, DifferentSetBank)
{
    auto c = load_cache_conflicts(uop, 0x80_va, 2);
    EXPECT_FALSE(c.any());
}

struct LSUCacheConflictsReadBackToBackTest : LSUCacheConflictsTest
{
    LSUCacheConflictsReadBackToBackTest(bool any_way = false, bool any_way_bank = false)
        : LSUCacheConflictsTest({
              {"top.enable_read_read_chunk_conflicts", true},
              {"top.back_to_back_read_no_conflicts_self", true},
              {"top.back_to_back_any_way", any_way},
              {"top.back_to_back_any_way_bank", any_way_bank},
              {"top.enable_write_read_chunk_bank_conflicts", false},
              {"top.enable_fill_read_chunk_bank_conflicts", false},
              {"top.enable_refill_tag_conflict", false},
              {"top.threecyc_conflict", true},
          })
    {
    }

    core::Uop uop1;

    void SetUp() override
    {
        LSUCacheConflictsTest::SetUp();
        uop1 = core::create_load_uop(0x0_va);
        uop->sch.partition = 0;
        uop1->sch.partition = 1;

        auto c = load_cache_conflicts(uop1, 0x0_va, 2);
        EXPECT_FALSE(c.read);

        conflicts->reset();

        c = load_cache_conflicts(uop, 0x0_va, 2);
        EXPECT_FALSE(c.read);
    }
};

TEST_F(LSUCacheConflictsReadBackToBackTest, NoConflictsIfExact)
{
    auto c = load_cache_conflicts(uop1, 0x0_va, 2);
    EXPECT_FALSE(c.read);
}

TEST_F(LSUCacheConflictsReadBackToBackTest, ConflictIfNotExactAddr)
{
    auto c = load_cache_conflicts(uop1, 0x1_va, 2);
    EXPECT_TRUE(c.read);
}

TEST_F(LSUCacheConflictsReadBackToBackTest, ConflictIfNotExactSize)
{
    auto c = load_cache_conflicts(uop1, 0x0_va, 1);
    EXPECT_TRUE(c.read);
}

struct LSUCacheConflictsReadBackToBackWayTest : LSUCacheConflictsReadBackToBackTest
{
    LSUCacheConflictsReadBackToBackWayTest() : LSUCacheConflictsReadBackToBackTest(true) {}

    void SetUp() override
    {
        cache.refill(uop, {0x0_va, 0x0_pa}, {}, true);
        cache.refill(uop, {0x1000000000_va, 0x1000000000_pa}, {}, true);

        LSUCacheConflictsReadBackToBackTest::SetUp();
    }
};

TEST_F(LSUCacheConflictsReadBackToBackWayTest, NoConflictsIfExactDifferentWay)
{
    auto c = load_cache_conflicts(uop1, 0x1000000000_va, 2);
    EXPECT_FALSE(c.read);
}

TEST_F(LSUCacheConflictsReadBackToBackWayTest, ConflictIfDifferentOffsetDifferentWay)
{
    auto c = load_cache_conflicts(uop1, 0x1000000001_va, 2);
    EXPECT_TRUE(c.read);
}

TEST_F(LSUCacheConflictsReadBackToBackWayTest, ConflictIfDifferentSizeDifferentWay)
{
    auto c = load_cache_conflicts(uop1, 0x1000000000_va, 1);
    EXPECT_TRUE(c.read);
}

struct LSUCacheConflictsReadBackToBackWayBankTest : LSUCacheConflictsReadBackToBackTest
{
    LSUCacheConflictsReadBackToBackWayBankTest() : LSUCacheConflictsReadBackToBackTest(true, true) {}

    void SetUp() override
    {
        cache.refill(uop, {0x0_va, 0x0_pa}, {}, true);
        cache.refill(uop, {0x1000000000_va, 0x1000000000_pa}, {}, true);

        LSUCacheConflictsReadBackToBackTest::SetUp();

        conflicts->reset();

        load_cache_conflicts(uop, 0x4_va, 2);
        load_cache_conflicts(uop1, 0x66_va, 2);

        conflicts->reset();

        load_cache_conflicts(uop, 0x0_va, 2);
    }
};

TEST_F(LSUCacheConflictsReadBackToBackWayBankTest, NoConflictIfSameExactBank)
{
    auto c = load_cache_conflicts(uop1, 0x0_va, 2);
    EXPECT_FALSE(c.read);
    EXPECT_EQ(c.back_to_back.banks_used, 1);
}

TEST_F(LSUCacheConflictsReadBackToBackWayBankTest, NoConflictIfSameExactBankDifferentWay)
{
    auto c = load_cache_conflicts(uop1, 0x1000000000_va, 2);
    EXPECT_FALSE(c.read);
    EXPECT_EQ(c.back_to_back.banks_used, 1);
}

TEST_F(LSUCacheConflictsReadBackToBackWayBankTest, NoReuseIfSameBankDifferentOffset)
{
    auto c = load_cache_conflicts(uop1, 0x10_va, 2);
    EXPECT_EQ(c.back_to_back.banks_used, 0);
}

struct LSUCacheConflictsCombineTest : LSUCacheConflictsTest
{
    LSUCacheConflictsCombineTest(unsigned combine_within = 0)
        : LSUCacheConflictsTest({
              {"top.enable_read_read_chunk_conflicts", true},
              {"top.back_to_back_read_no_conflicts_self", true},
              {"top.combine_within", combine_within},
              {"top.enable_write_read_chunk_bank_conflicts", false},
              {"top.enable_fill_read_chunk_bank_conflicts", false},
              {"top.enable_refill_tag_conflict", false},
              {"top.threecyc_conflict", true},
          })
    {
    }
};

TEST_F(LSUCacheConflictsCombineTest, DifferentPartitions)
{
    load_cache_conflicts(uop, 0x8_va, 8);
    uop->sch.partition = 1;

    EXPECT_FALSE(conflicts->can_load_combine(uop->sch.partition, {0x7_va, 2}));
    EXPECT_FALSE(conflicts->can_load_combine(uop->sch.partition, {0x8_va, 8}));
}

TEST_F(LSUCacheConflictsCombineTest, NoOverlappingAcrossBlocks)
{
    load_cache_conflicts(uop, 0x8_va, 8);

    EXPECT_TRUE(conflicts->can_load_combine(uop->sch.partition, {0x07_va, 2}));
    EXPECT_FALSE(conflicts->can_load_combine(uop->sch.partition, {0x17_va, 2}));
    EXPECT_FALSE(conflicts->can_load_combine(uop->sch.partition, {0x27_va, 2}));
    EXPECT_FALSE(conflicts->can_load_combine(uop->sch.partition, {0x37_va, 2}));
}

struct LSUCacheConflictsCombineWithin16Test : LSUCacheConflictsCombineTest
{
    LSUCacheConflictsCombineWithin16Test() : LSUCacheConflictsCombineTest(16) {}
};

TEST_F(LSUCacheConflictsCombineWithin16Test, Within16ByteBlock)
{
    load_cache_conflicts(uop, 0x8_va, 8);
    EXPECT_TRUE(conflicts->can_load_combine(uop->sch.partition, {0x0_va, 8}));
    EXPECT_FALSE(conflicts->can_load_combine(uop->sch.partition, {0x8_va, 9}));
}

/*
 * vim: sw=4:ts=4:et:si:cindent:cino=\:0,(0,g0,t0:
 */
