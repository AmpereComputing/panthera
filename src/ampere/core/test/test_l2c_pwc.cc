//
// Copyright (c) 2023 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//

#include <gtest/gtest.h>
#include "ampere/archx/addr.h"
#include "ampere/archx/page_size.h"
#include "ampere/core/l2/l2c_op.h"
#include "ampere/core/l2/l2c_pwc.h"
#include "ampere/core/l2/l2c_reqqueue.h"
#include "ampere/sc/testing/test_module.h"
#include "ampere/uarch_common/memop.h"
#include "ampere/uarch_common/memreq.h"
#include "ampere/event_stream/testing/event_stream_mock.h"

#include <nlohmann/json.hpp>

namespace ampere::test_pwc {

using ampere::core::l2::PWC;
using ampere::sc::TestModule;
using archx::PhysAddr;
using uarch_common::MemReq;
using uarch_common::MemOp::Requester;
using uarch_common::MemOp::StoreType;
using archx::operator""_pa;

using namespace ::testing;

struct PWCTestBase : TestModule
{
    uint64_t id = 0;
    std::unique_ptr<PWC> pwc;

    PWCTestBase() = default;
    PWCTestBase(std::initializer_list<std::pair<std::string, sc::json>> settings) : TestModule(settings) {}

    void SetUp() override { pwc = std::make_unique<PWC>("pwc"); }

    auto request(PhysAddr phys_addr, unsigned size = PhysAddr::LINE_SIZE, uint64_t byte_mask = 0U)
    {
        return make<MemReq>(id++,
                            phys_addr,
                            Requester::LSU,
                            uarch_common::MemOp::Type::WRITE,
                            size,
                            false,
                            uarch_common::MemOp::PrefetchType::NONE,
                            uarch_common::MemOp::BAR_NONE,
                            uarch_common::MemOp::ATOMIC_NONE,
                            uarch_common::MemOp::SNOOP_NONE,
                            archx::PageSize::Size::INVALID,
                            byte_mask);
    }

    auto barrier_request(PhysAddr phys_addr, uarch_common::MemOp::BarrierType type)
    {
        return make<MemReq>(id++,
                            phys_addr,
                            Requester::LSU,
                            uarch_common::MemOp::Type::WRITE,
                            8,
                            false,
                            uarch_common::MemOp::PrefetchType::NONE,
                            type);
    }

    auto atomic_request(PhysAddr phys_addr, uarch_common::MemOp::AtomicType type = uarch_common::MemOp::ATOMIC_LOAD)
    {
        return make<MemReq>(id++,
                            phys_addr,
                            Requester::LSU,
                            uarch_common::MemOp::Type::WRITE,
                            8,
                            false,
                            uarch_common::MemOp::PrefetchType::NONE,
                            uarch_common::MemOp::BAR_STORE_EXCL_RELEASE,
                            type);
    }

    auto stlr_request(PhysAddr addr) { return barrier_request(addr, uarch_common::MemOp::BAR_STORE_EXCL_RELEASE); }

    static core::l2::ScheduleCheck always_schedule()
    {
        return [](core::l2::RequestQueueName name, core::l2::Op op) -> core::l2::PipelineArbitration {
            return {false, true, false};
        };
    }

    void fill(unsigned amount = 1, PhysAddr start_addr = 0x0_pa)
    {
        for (unsigned i = 0; i < amount; ++i)
            pwc->allocate(request(start_addr + PhysAddr::LINE_SIZE * i, PhysAddr::LINE_SIZE), 0);
    }

    auto drain(unsigned amount = 0)
    {
        core::l2::ScheduledOp last;
        for (unsigned i = 0; !pwc->empty() && (!amount || i < amount); ++i) {
            pwc->set_request_is_waiting();
            last = pwc->schedule(always_schedule());
        }

        return last;
    }
};

struct PWCTest : PWCTestBase
{
    PWCTest()
        : PWCTestBase({
              {"pwc.size", 4},
          })
    {
    }
};

TEST_F(PWCTest, InitiallyEmpty)
{
    EXPECT_TRUE(pwc->empty());
    EXPECT_EQ(pwc->store_types(), 0);
}

TEST_F(PWCTest, CanAllocate)
{
    EXPECT_FALSE(pwc->has_address_match(0x0_pa));

    fill();
    EXPECT_FALSE(pwc->empty());
    EXPECT_TRUE(pwc->has_address_match(0x0_pa));
    EXPECT_EQ(pwc->store_types(), core::StoreType::STORE_ANY);
}

TEST_F(PWCTest, Full)
{
    fill(4);
    EXPECT_TRUE(pwc->full());
}

TEST_F(PWCTest, CanCombineInOrder)
{
    fill();
    EXPECT_TRUE(pwc->can_combine(request(0x0_pa)));
}

TEST_F(PWCTest, CanCombineOutOfOrder)
{
    fill(2);
    EXPECT_TRUE(pwc->can_combine(request(0x0_pa)));
}

TEST_F(PWCTest, CannotCombineIntoOldestIfDraining)
{
    fill(2);
    pwc->set_pending_barrier(true);

    EXPECT_FALSE(pwc->can_combine(request(0x0_pa)));
    EXPECT_TRUE(pwc->can_combine(request(0x0_pa + PhysAddr::LINE_SIZE)));
}

TEST_F(PWCTest, CannotCombineWithOrdered)
{
    fill(2);
    auto stlr_req = stlr_request(0x0_pa);

    EXPECT_FALSE(pwc->can_combine(stlr_req));
    pwc->allocate(stlr_req, 0);

    EXPECT_FALSE(pwc->can_combine(request(0x0_pa)));
}

TEST_F(PWCTest, CannotCombineWithAtomic)
{
    fill(2);
    pwc->allocate(atomic_request(0x0_pa), 0);

    EXPECT_FALSE(pwc->can_combine(request(0x0_pa)));
    EXPECT_FALSE(pwc->can_combine(stlr_request(0x0_pa)));
    EXPECT_FALSE(pwc->can_combine(atomic_request(0x0_pa)));
}

TEST_F(PWCTest, CanCombineWithWideAtomic)
{
    fill(2);
    pwc->allocate(atomic_request(0x0_pa, uarch_common::MemOp::ATOMIC_LOAD_WIDE), 0);

    EXPECT_FALSE(pwc->can_combine(request(0x0_pa)));
    EXPECT_FALSE(pwc->can_combine(stlr_request(0x0_pa)));
    EXPECT_FALSE(pwc->can_combine(atomic_request(0x0_pa)));
    EXPECT_TRUE(pwc->can_combine(atomic_request(0x0_pa, uarch_common::MemOp::ATOMIC_LOAD_WIDE)));
}

TEST_F(PWCTest, CannotCombineAcrossAtomic)
{
    fill(2);
    pwc->allocate(atomic_request(0x0_pa + PhysAddr::LINE_SIZE * 3), 0);

    EXPECT_FALSE(pwc->can_combine(request(0x0_pa)));
}

TEST_F(PWCTest, AllocateBarrier)
{
    pwc->allocate(barrier_request(0x0_pa, uarch_common::MemOp::BAR_DMB), 0);
    EXPECT_EQ(pwc->store_types(), StoreType::STORE_BARRIER | StoreType::STORE_ANY);
}

TEST_F(PWCTest, AllocateAtomic)
{
    pwc->allocate(atomic_request(0x0_pa), 0);
    EXPECT_EQ(pwc->store_types(),
              StoreType::STORE_ATOMIC | StoreType::STORE_EXCLUSIVE | StoreType::STORE_RELEASE | StoreType::STORE_ANY);
}

TEST_F(PWCTest, ScheduleWhenEmpty)
{
    auto winner = pwc->schedule(always_schedule());
    EXPECT_FALSE(winner.write);
    EXPECT_EQ(winner.op, nullptr);
}

TEST_F(PWCTest, ScheduleTop)
{
    fill(4);
    auto winner = pwc->schedule(always_schedule());
    EXPECT_TRUE(winner.write);
    EXPECT_EQ(winner.op->addr, 0x0_pa);
}

TEST_F(PWCTest, CheckMatch)
{
    fill(4);

    EXPECT_EQ(pwc->check_partial_address_match(0xffff_pa), 0);
    EXPECT_EQ(pwc->check_partial_address_match(0x0_pa), 1);
    EXPECT_EQ(pwc->check_partial_address_match(0x1000000000_pa + PhysAddr::LINE_SIZE), 2);
}

struct PWCOutOfOrderDrainTest : PWCTestBase
{
    PWCOutOfOrderDrainTest()
        : PWCTestBase({
              {"pwc.size", 4},
              {"pwc.drain_in_order", false},
          })
    {
    }
};

TEST_F(PWCOutOfOrderDrainTest, ScheduleHitEntry)
{
    fill(4);

    auto load_addr = 0x0_pa + PhysAddr::LINE_SIZE;
    EXPECT_EQ(pwc->check_partial_address_match(load_addr), 2);

    auto winner = pwc->schedule(always_schedule());
    EXPECT_TRUE(winner.write);
    EXPECT_EQ(winner.op->addr, load_addr);
}

TEST_F(PWCOutOfOrderDrainTest, ScheduleTopIfOrdered)
{
    fill();
    pwc->allocate(barrier_request(0x0_pa + PhysAddr::LINE_SIZE, uarch_common::MemOp::BAR_STORE_EXCL), 0);

    auto load_addr = 0x0_pa + PhysAddr::LINE_SIZE * 2;
    fill(2, load_addr);
    EXPECT_EQ(pwc->check_partial_address_match(load_addr), 3);

    auto winner = pwc->schedule(always_schedule());
    EXPECT_TRUE(winner.write);
    EXPECT_EQ(winner.op->addr, 0x0_pa);
}

struct PWCEventTest : PWCTest
{
    NiceMock<event_stream::MockEventAnnouncer> event_announcer;

    void SetUp() override
    {
        get_environment()->event_stream = std::make_unique<event_stream::EventStreamDummy>(&event_announcer);
        EXPECT_CALL(event_announcer, post_event(_, _)).Times(AnyNumber());
        EXPECT_CALL(event_announcer, add_int_data(_, _, _, _, _)).Times(AnyNumber());
        EXPECT_CALL(event_announcer, add_uint_data(_, _, _, _)).Times(AnyNumber());
        EXPECT_CALL(event_announcer, add_string_data(_, _, _, _)).Times(AnyNumber());

        PWCTest::SetUp();
    }

    void TearDown() override { pwc = nullptr; }
};

TEST_F(PWCEventTest, DeallocatePartial)
{
    EXPECT_CALL(event_announcer, add_uint_data("pwc.deallocate", _, "pwc.full_line", 0)).Times(1);
    EXPECT_CALL(event_announcer, add_uint_data("pwc.deallocate", _, "pwc.chunks", 1)).Times(1);

    pwc->allocate(request(0x0_pa, 1, 1ULL), 0);
    fill(3, 0xff0_pa);
    pwc->schedule(always_schedule());
}

TEST_F(PWCEventTest, DeallocateFull)
{
    EXPECT_CALL(event_announcer, add_uint_data("pwc.deallocate", _, "pwc.full_line", 1)).Times(1);
    EXPECT_CALL(event_announcer, add_uint_data("pwc.deallocate", _, "pwc.chunks", 4)).Times(1);

    fill(4);
    pwc->schedule(always_schedule());
}

TEST_F(PWCEventTest, ChunksAtDeallocate)
{
    EXPECT_CALL(event_announcer, add_uint_data("pwc.deallocate", _, "pwc.chunks", 2)).Times(1);

    pwc->allocate(request(0x0_pa, 1, 1ULL), 0);
    pwc->combine(request(0x0_pa, 1, 1ULL << 32));
    pwc->combine(request(0x0_pa, 1, 1ULL << 47));
    fill(3, 0xff0_pa);
    pwc->schedule(always_schedule());
}

struct PWCCombineIntoOldestTest : PWCTestBase
{
    PWCCombineIntoOldestTest()
        : PWCTestBase({
              {"pwc.size", 4},
              {"pwc.can_combine_with_oldest_during_drain", true},
          })
    {
    }
};

TEST_F(PWCCombineIntoOldestTest, CanCombineWithOldestDuringDrain)
{
    fill(2);
    pwc->set_pending_barrier(true);

    EXPECT_TRUE(pwc->can_combine(request(0x0_pa)));
}

struct PWCLessOrderedCombineTest : PWCTestBase
{
    PWCLessOrderedCombineTest()
        : PWCTestBase({
              {"pwc.size", 8},
              {"pwc.unordered_can_combine_with_ordered", true},
              {"pwc.unordered_can_combine_with_previous_ordered", 1},
              {"pwc.ordered_can_combine_with_youngest", true},
          })
    {
    }

    void SetUp() override
    {
        PWCTestBase::SetUp();

        // Make sure we're not trying to combine with the oldest to drain
        fill(1, 0x0_pa + (PhysAddr::LINE_SIZE * 8));
    }

    void TearDown() override
    {
        // check ordered count is set correctly
        drain();
    }
};

TEST_F(PWCLessOrderedCombineTest, CannotCombineWithAtomic)
{
    pwc->allocate(atomic_request(0x0_pa), 0);

    EXPECT_FALSE(pwc->can_combine(request(0x0_pa)));
    EXPECT_FALSE(pwc->can_combine(stlr_request(0x0_pa)));
    EXPECT_FALSE(pwc->can_combine(atomic_request(0x0_pa, uarch_common::MemOp::ATOMIC_LOAD)));
}

TEST_F(PWCLessOrderedCombineTest, AtomicCannotCombineWithUnordered)
{
    pwc->allocate(request(0x0_pa), 0);

    EXPECT_FALSE(pwc->can_combine(atomic_request(0x0_pa, uarch_common::MemOp::ATOMIC_LOAD)));
}

TEST_F(PWCLessOrderedCombineTest, AtomicCannotCombineWithOrdered)
{
    pwc->allocate(stlr_request(0x0_pa), 0);
    EXPECT_FALSE(pwc->can_combine(atomic_request(0x0_pa, uarch_common::MemOp::ATOMIC_LOAD)));
}

TEST_F(PWCLessOrderedCombineTest, UnorderedCanCombineWithPriorOrdered)
{
    pwc->allocate(stlr_request(0x0_pa), 0);
    EXPECT_TRUE(pwc->try_combine(request(0x0_pa)));
    EXPECT_TRUE(drain().op->req->ordered());
}

TEST_F(PWCLessOrderedCombineTest, UnorderedCanCombineOrderedYoungest)
{
    auto req = stlr_request(0x0_pa);
    pwc->allocate(req, 0);

    EXPECT_TRUE(pwc->can_combine(req));
}

TEST_F(PWCLessOrderedCombineTest, UnorderedCanCombinePreviousAge)
{
    fill(2);

    auto stlr_req = stlr_request(0x0_pa + PhysAddr::LINE_SIZE);
    EXPECT_TRUE(pwc->try_combine(stlr_req));
    EXPECT_TRUE(pwc->try_combine(request(0x0_pa)));
    EXPECT_EQ(drain().op->req, stlr_req);
}

TEST_F(PWCLessOrderedCombineTest, OrderedCannotCombinePreviousAgeUnordered)
{
    fill(2);

    auto stlr_req = stlr_request(0x0_pa + PhysAddr::LINE_SIZE);
    EXPECT_TRUE(pwc->try_combine(stlr_req));
    EXPECT_FALSE(pwc->can_combine(stlr_request(0x0_pa)));
    EXPECT_EQ(drain().op->req, stlr_req);
}

TEST_F(PWCLessOrderedCombineTest, OrderedCannotCombinePreviousAgeOrdered)
{
    fill(1);

    auto stlr_req = stlr_request(0x0_pa);
    EXPECT_TRUE(pwc->try_combine(stlr_req));

    auto other_stlr_req = stlr_request(0x0_pa + PhysAddr::LINE_SIZE * 4);
    EXPECT_FALSE(pwc->can_combine(other_stlr_req));
    pwc->allocate(other_stlr_req, 0);

    EXPECT_FALSE(pwc->can_combine(stlr_req));
}

} // namespace ampere::test_pwc

/*
 * vim: sw=4:ts=4:et:si:cindent:cino=\:0,(0,g0,t0:
 */
