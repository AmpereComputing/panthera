//
// Copyright (c) 2023 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//

#include <gtest/gtest.h>
#include <tuple>
#include <utility>
#include "ampere/archx/addr.h"
#include "ampere/archx/page_size.h"
#include "ampere/core/lsu/ofb.h"
#include "ampere/core/uop.h"
#include "ampere/sc/testing/test_module.h"
#include "ampere/core/testing/uops.h"
#include "ampere/uarch_common/memreq.h"

using archx::PageSize;
using archx::PhysAddr;
using archx::VirtAddr;
using core::OFB;
using core::Uop;
using uarch_common::MemReq;
using uarch_common::MemReqObj;
using archx::operator""_pa;
using archx::operator""_va;
using archx::operator""_pc;

struct OFBTest : ampere::sc::TestModule
{
    OFBTest() = default;
    OFBTest(std::initializer_list<std::pair<std::string, sc::json>> settings) : TestModule(settings) {}
    OFBTest(std::initializer_list<std::pair<std::string, sc::json>> settings, size_t partitions)
        : TestModule(settings), ofb("ofb", partitions)
    {
    }

    OFB ofb{"ofb", 0};

    auto make_req(PhysAddr phys_addr)
    {
        Uop uop = core::create_load_uop(VirtAddr(phys_addr.value()), phys_addr);
        auto req = make_child<MemReq>(
            uop, MemReqObj::load(0, 0_pc, phys_addr, VirtAddr(phys_addr.value()), 1, 1, PageSize::Size::PS_4KB));

        return std::make_pair(uop, req);
    }

    void fill(unsigned amount = 1, PhysAddr start_addr = 0x0_pa, size_t partition = 0)
    {
        for (unsigned i = 0; i < amount; ++i) {
            auto [uop, req] = make_req(PhysAddr(start_addr.value() + i * PhysAddr::LINE_SIZE));
            uop->sch.partition = partition;
            ofb.allocate(req, uop);
        }
    }
};

TEST_F(OFBTest, Allocate)
{
    EXPECT_EQ(ofb.size(), 0);
    fill();
    EXPECT_EQ(ofb.size(), 1);
}

TEST_F(OFBTest, Deallocate)
{
    auto [uop, req] = make_req(0_pa);
    ofb.allocate(req, uop);
    EXPECT_EQ(ofb.size(), 1);
    ofb.deallocate(0x0_pa, uop);
    EXPECT_EQ(ofb.size(), 0);
}

TEST_F(OFBTest, Full)
{
    fill(11);
    EXPECT_TRUE(ofb.can_allocate());
    EXPECT_FALSE(ofb.full());

    fill(1, PhysAddr(PhysAddr::LINE_SIZE * 12));
    EXPECT_FALSE(ofb.can_allocate());
    EXPECT_TRUE(ofb.full());
}

TEST_F(OFBTest, Flush)
{
    auto [uop, req] = make_req(0_pa);
    uop->alloc_id = 8;

    ofb.allocate(req, uop);
    fill(8);

    ofb.flush(8);
    EXPECT_FALSE(ofb.find(0_pa)->buffer_id);
}

struct OFBReservationTest : OFBTest
{
    OFBReservationTest() : OFBTest({{"ofb.reserve_for_oldest_after_full", true}}) {}

    Uop uop;
    MemReq req;

    void SetUp() override
    {
        std::tie(uop, req) = make_req(0xaaaa_pa);
        uop->alloc_id = 5;

        fill(12);
        EXPECT_TRUE(ofb.full());
        EXPECT_FALSE(ofb.can_allocate(uop));
        EXPECT_TRUE(ofb.check_if_should_sleep(uop, 5));
        EXPECT_TRUE(ofb.reserved());
    }
};

TEST_F(OFBReservationTest, Reservation)
{
    auto [younger_uop, younger_req] = make_req(0xbbbb_pa);
    younger_uop->alloc_id = 6;

    ofb.deallocate(0x0_pa, {});

    EXPECT_FALSE(ofb.full());
    EXPECT_FALSE(ofb.can_allocate(younger_uop));
    EXPECT_FALSE(ofb.check_if_should_sleep(younger_uop, 5));

    EXPECT_TRUE(ofb.can_allocate(uop));
    ofb.allocate(req, uop);
    EXPECT_FALSE(ofb.reserved());

    ofb.deallocate(PhysAddr(PhysAddr::LINE_SIZE), {});
    EXPECT_TRUE(ofb.can_allocate(younger_uop));
}

TEST_F(OFBReservationTest, CanAllocateIfReservedButNotAlmostFull)
{
    auto [younger_uop, younger_req] = make_req(0xbbbb_pa);
    younger_uop->alloc_id = 6;

    ofb.deallocate(0x0_pa, {});
    ofb.deallocate(PhysAddr(PhysAddr::LINE_SIZE), {});

    EXPECT_TRUE(ofb.can_allocate(younger_uop));
}

TEST_F(OFBReservationTest, ResetReservation)
{
    ofb.reset_reservation(5);
    EXPECT_FALSE(ofb.reserved());
}

TEST_F(OFBReservationTest, FlushResetsReservation)
{
    ofb.flush(5);
    EXPECT_FALSE(ofb.reserved());
}

struct OFBPartitionTest : OFBTest
{
    OFBPartitionTest() : OFBTest({{"ofb.allocations_per_partition", 1}}, 2) {}
};

TEST_F(OFBPartitionTest, AllocateOnePerPartition)
{
    EXPECT_EQ(ofb.size(), 0);
    fill(1, 0xaaaa_pa, 0);
    fill(1, 0xbbbb_pa, 1);
    EXPECT_EQ(ofb.size(), 2);
}

TEST_F(OFBPartitionTest, CannotAllocateMoreThanOnePerPartition)
{
    auto [uop, req] = make_req(0_pa);
    uop->sch.partition = 0;
    EXPECT_TRUE(ofb.can_allocate(uop));
    fill(1, 0xaaaa_pa, 0);
    EXPECT_FALSE(ofb.can_allocate(uop));

    auto [uop2, req2] = make_req(256_pa);
    uop2->sch.partition = 1;
    EXPECT_TRUE(ofb.can_allocate(uop2));
    fill(1, 0xbbbb_pa, 1);
    EXPECT_FALSE(ofb.can_allocate(uop2));
}

TEST_F(OFBPartitionTest, PartitionedOFBAllowsNonPartitionedAllocation)
{
    fill(1, 0xaaaa_pa, 0);
    fill(1, 0xbbbb_pa, 1);

    EXPECT_TRUE(ofb.can_allocate());
    fill(1);
    EXPECT_EQ(ofb.size(), 3);
}

/*
 * vim: sw=4:ts=4:et:si:cindent:cino=\:0,(0,g0,t0:
 */
