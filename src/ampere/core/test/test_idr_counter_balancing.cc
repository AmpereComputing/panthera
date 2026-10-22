//
// Copyright (c) 2018-2023 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//

#include <gmock/gmock.h>
#include "ampere/core/idr/idr_counter_balancing.h"
#include "ampere/core/idr/idr_defs.h"
#include "ampere/core/ixu_defs.h"
#include "ampere/core/testing/uop_groups.h"
#include "ampere/sc/credit.h"
#include "ampere/sc/testing/test_module.h"

using core::DecodedUopGroup;
using core::SchedulerType;
using core::testing::make_uop_group;
using core::testing::make_uop_group_from_opcodes;
using ::testing::ElementsAre;

constexpr uint32_t add32 = 0b0'0'0'01011'00'1'00000'000'000'00000'00000;
constexpr uint32_t ldr32 = 0b00'111'0'01'01'000000000000'00000'00000;
constexpr uint32_t str32 = 0b00'111'0'01'00'000000000000'00000'00000;
constexpr uint32_t adc32 = 0b0'0'0'11010000'00000'000000'00000'00000;  // A-only
constexpr uint32_t addg = 0b1'0'0'100011'0'000000'00'0000'00000'00000; // B-only

struct CounterBalancingTest : sc::TestModule
{
    sc::ModuleWrapper<core::IDRCounterBalancing> balancer;
    core::SchedulerCredits sch{"sch", ampere::core::scheduler_enum()};

    unsigned width;
    std::vector<sc::Credit> allocated;

    explicit CounterBalancingTest(std::initializer_list<std::pair<std::string, sc::json>> settings,
                                  unsigned width,
                                  unsigned partitions)
        : TestModule(settings), balancer("test", width, partitions), width(width)
    {
    }

    void TearDown() override
    {
        for (auto &credit : allocated)
            credit->release();
    }

    void balance(DecodedUopGroup &uop_group) { balancer->balance(uop_group, sch); }
    void dispatch(const std::vector<core::Scheduler> &group_sched)
    {
        for (const auto &sched : group_sched)
            allocated.emplace_back(sch[sched.scheduler()].acquire());
    }

    void balance_and_dispatch(DecodedUopGroup &uop_group, const std::vector<core::Scheduler> &group_sched)
    {
        balancer->balance(uop_group, sch);
        dispatch(group_sched);
    }

    static void cleave(core::Uop &uop0, core::Uop &uop1)
    {
        uop0->instructions.front()->has_internal_cleaving = uop1->instructions.front()->has_internal_cleaving = true;
        uop0->uop_number = 0;
        uop0->cleaved_first_half = true;
        uop1->uop_number = 1;
        uop1->cleaved_second_half = true;
    }
};

struct CounterBalancing4Wide2PartitionTest : CounterBalancingTest
{
    CounterBalancing4Wide2PartitionTest()
        : CounterBalancingTest(
              {{"~sch.*.size", 32}, {"test.counter_balance", true}, {"test.bal_round_robin", false}}, 4, 2)
    {
    }
};

#define EXPECT_OP_BALANCE(amount, opcode, ...)              \
    do {                                                    \
        auto group = make_uop_group(opcode, amount, width); \
        balance(group);                                     \
        EXPECT_THAT(group.lanes, ElementsAre(__VA_ARGS__)); \
    } while (false)

#define EXPECT_BALANCE(group, ...)                          \
    do {                                                    \
        balance(group);                                     \
        EXPECT_THAT(group.lanes, ElementsAre(__VA_ARGS__)); \
    } while (false)

TEST_F(CounterBalancing4Wide2PartitionTest, AlreadyBalancedNoBalancing)
{
    for (unsigned i = 0; i < 4; ++i) {
        auto group = make_uop_group(add32, 4, width);
        balance(group);
    }

    EXPECT_OP_BALANCE(4, add32, 0, 1, 2, 3);
}

TEST_F(CounterBalancing4Wide2PartitionTest, AllOneUopAlternates)
{
    EXPECT_OP_BALANCE(1, add32, 0, 1, 2, 3);
    EXPECT_OP_BALANCE(1, add32, 2, 1, 0, 3);
    EXPECT_OP_BALANCE(1, add32, 0, 1, 2, 3);
}

TEST_F(CounterBalancing4Wide2PartitionTest, AllTwoUopsDistribute)
{
    EXPECT_OP_BALANCE(2, add32, 0, 3, 2, 1);
    EXPECT_OP_BALANCE(2, add32, 0, 3, 2, 1);
}

TEST_F(CounterBalancing4Wide2PartitionTest, AllThreeUopsAlternates)
{
    EXPECT_OP_BALANCE(3, add32, 0, 1, 2, 3);
    EXPECT_OP_BALANCE(3, add32, 0, 3, 2, 1);
    EXPECT_OP_BALANCE(3, add32, 0, 1, 2, 3);
}

TEST_F(CounterBalancing4Wide2PartitionTest, AllFourUopsNothing)
{
    EXPECT_OP_BALANCE(4, add32, 0, 1, 2, 3);
    EXPECT_OP_BALANCE(4, add32, 0, 1, 2, 3);
}

TEST_F(CounterBalancing4Wide2PartitionTest, OneAOnlyAlternates)
{
    std::vector<DecodedUopGroup> groups(3, make_uop_group_from_opcodes({add32, adc32, add32, add32}, width));

    EXPECT_BALANCE(groups[0], 0, 1, 2, 3);
    EXPECT_BALANCE(groups[1], 0, 3, 2, 1);
    EXPECT_BALANCE(groups[2], 0, 1, 2, 3);
}

TEST_F(CounterBalancing4Wide2PartitionTest, OneBOnlyAlternates)
{
    std::vector<DecodedUopGroup> groups(3, make_uop_group_from_opcodes({add32, addg, add32, add32}, width));

    EXPECT_BALANCE(groups[0], 0, 1, 2, 3);
    EXPECT_BALANCE(groups[1], 0, 3, 2, 1);
    EXPECT_BALANCE(groups[2], 0, 1, 2, 3);
}

TEST_F(CounterBalancing4Wide2PartitionTest, OneLoadAlternates)
{
    std::vector<DecodedUopGroup> groups(3, make_uop_group_from_opcodes({add32, ldr32, add32, add32}, width));

    EXPECT_BALANCE(groups[0], 0, 1, 2, 3);
    EXPECT_BALANCE(groups[1], 0, 3, 2, 1);
    EXPECT_BALANCE(groups[2], 0, 1, 2, 3);
}

TEST_F(CounterBalancing4Wide2PartitionTest, OneStoreAlternates)
{
    std::vector<DecodedUopGroup> groups(3, make_uop_group_from_opcodes({add32, str32, add32, add32}, width));

    EXPECT_BALANCE(groups[0], 0, 1, 2, 3);
    EXPECT_BALANCE(groups[1], 0, 3, 2, 1);
    EXPECT_BALANCE(groups[2], 0, 1, 2, 3);
}

TEST_F(CounterBalancing4Wide2PartitionTest, MultipleGroups)
{
    std::vector<DecodedUopGroup> groups(3, make_uop_group_from_opcodes({addg, str32, ldr32, adc32}, width));

    EXPECT_BALANCE(groups[0], 0, 1, 2, 3);
    EXPECT_BALANCE(groups[1], 2, 3, 0, 1);
    EXPECT_BALANCE(groups[2], 0, 1, 2, 3);
}

TEST_F(CounterBalancing4Wide2PartitionTest, CleavingWithMemAlternates)
{
    auto cleaved_group = make_uop_group_from_opcodes({add32, ldr32, add32, str32}, width);
    cleave(cleaved_group.uops[0], cleaved_group.uops[1]);
    cleave(cleaved_group.uops[2], cleaved_group.uops[3]);
    std::vector<DecodedUopGroup> groups(3, cleaved_group);

    EXPECT_BALANCE(groups[0], 0, 3, 2, 1);
    EXPECT_BALANCE(groups[1], 2, 1, 0, 3);
    EXPECT_BALANCE(groups[2], 0, 3, 2, 1);
}

TEST_F(CounterBalancing4Wide2PartitionTest, CleavingWithABOnlyTreatedLikeTwo)
{
    auto cleaved_group = make_uop_group_from_opcodes({add32, addg, add32, adc32}, width);
    cleave(cleaved_group.uops[0], cleaved_group.uops[1]);
    cleave(cleaved_group.uops[2], cleaved_group.uops[3]);
    std::vector<DecodedUopGroup> groups(3, cleaved_group);

    EXPECT_BALANCE(groups[0], 0, 3, 2, 1);
    EXPECT_BALANCE(groups[1], 0, 3, 2, 1);
    EXPECT_BALANCE(groups[2], 0, 3, 2, 1);
}

/*
 * vim: sw=4:ts=4:et:si:cindent:cino=\:0,(0,g0,t0:
 */
