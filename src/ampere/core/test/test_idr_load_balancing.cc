//
// Copyright (c) 2018-2023 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//

#include <gmock/gmock.h>
#include "ampere/core/idr/idr_load_balancing.h"
#include "ampere/core/testing/uop_groups.h"
#include "ampere/sc/testing/test_module.h"

using core::DecodedUopGroup;
using ::testing::ElementsAre;

struct LoadBalancingTest : sc::Test
{
    sc::ModuleWrapper<core::IDRLoadBalancing> load_balancer;
    unsigned width;
    explicit LoadBalancingTest(unsigned width, unsigned partitions)
        : load_balancer("test", width, partitions), width(width)
    {
    }

    auto make_uop_group(unsigned amount) const { return ::ampere::core::testing::make_uop_group(amount, width); }
};

#define EXPECT_SWAP(amount, expected, ...)                  \
    do {                                                    \
        auto group = make_uop_group(amount);                \
        EXPECT_EQ(load_balancer->balance(group), expected); \
        EXPECT_THAT(group.lanes, ElementsAre(__VA_ARGS__)); \
    } while (false)

struct LoadBalancing4Wide2PartitionTest : LoadBalancingTest
{
    LoadBalancing4Wide2PartitionTest() : LoadBalancingTest(4, 2) {}
};

TEST_F(LoadBalancing4Wide2PartitionTest, ZeroDoesntSwap)
{
    EXPECT_SWAP(0, false, 0, 1, 2, 3);
}

TEST_F(LoadBalancing4Wide2PartitionTest, OneAlternatesSwaps)
{
    EXPECT_SWAP(1, false, 0, 1, 2, 3);
    EXPECT_SWAP(1, true, 2, 1, 0, 3);
    EXPECT_SWAP(1, false, 0, 1, 2, 3);
}

TEST_F(LoadBalancing4Wide2PartitionTest, TwoAlwaysSwaps)
{
    EXPECT_SWAP(2, true, 0, 2, 1, 3);
    EXPECT_SWAP(2, true, 0, 2, 1, 3);
}

TEST_F(LoadBalancing4Wide2PartitionTest, ThreeAlternatesSwaps)
{
    EXPECT_SWAP(3, false, 0, 1, 2, 3);
    EXPECT_SWAP(3, true, 0, 2, 3, 1);
    EXPECT_SWAP(3, false, 0, 1, 2, 3);
}

TEST_F(LoadBalancing4Wide2PartitionTest, OneThreeAlternatesSwapsIndependently)
{
    EXPECT_SWAP(1, false, 0, 1, 2, 3);
    EXPECT_SWAP(1, true, 2, 1, 0, 3);

    EXPECT_SWAP(3, false, 0, 1, 2, 3);
    EXPECT_SWAP(3, true, 0, 2, 3, 1);
}

TEST_F(LoadBalancing4Wide2PartitionTest, FourDoesntSwap)
{
    EXPECT_SWAP(4, false, 0, 1, 2, 3);
}

struct LoadBalancing6Wide2PartitionTest : LoadBalancingTest
{
    LoadBalancing6Wide2PartitionTest() : LoadBalancingTest(6, 2) {}
};

TEST_F(LoadBalancing6Wide2PartitionTest, ZeroDoesntSwap)
{
    EXPECT_SWAP(0, false, 0, 1, 2, 3, 4, 5);
}

TEST_F(LoadBalancing6Wide2PartitionTest, OneAlternatesSwaps)
{
    EXPECT_SWAP(1, false, 0, 1, 2, 3, 4, 5);
    EXPECT_SWAP(1, true, 3, 1, 2, 0, 4, 5);
    EXPECT_SWAP(1, false, 0, 1, 2, 3, 4, 5);
}

TEST_F(LoadBalancing6Wide2PartitionTest, TwoAlwaysSwaps)
{
    EXPECT_SWAP(2, true, 0, 3, 2, 1, 4, 5);
    EXPECT_SWAP(2, true, 0, 3, 2, 1, 4, 5);
}

TEST_F(LoadBalancing6Wide2PartitionTest, ThreeAlwaysSwapsAlternating)
{
    EXPECT_SWAP(3, true, 0, 1, 3, 2, 4, 5);
    EXPECT_SWAP(3, true, 0, 3, 4, 1, 2, 5);
    EXPECT_SWAP(3, true, 0, 1, 3, 2, 4, 5);
}

TEST_F(LoadBalancing6Wide2PartitionTest, FourAlwaysSwaps)
{
    EXPECT_SWAP(4, true, 0, 1, 4, 2, 3, 5);
    EXPECT_SWAP(4, true, 0, 1, 4, 2, 3, 5);
}

TEST_F(LoadBalancing6Wide2PartitionTest, FiveAlternatesSwaps)
{
    EXPECT_SWAP(5, false, 0, 1, 2, 3, 4, 5);
    EXPECT_SWAP(5, true, 0, 1, 5, 2, 3, 4);
    EXPECT_SWAP(5, false, 0, 1, 2, 3, 4, 5);
}

TEST_F(LoadBalancing6Wide2PartitionTest, SixDoesntSwap)
{
    EXPECT_SWAP(6, false, 0, 1, 2, 3, 4, 5);
}

struct LoadBalancing6Wide3PartitionTest : LoadBalancingTest
{
    LoadBalancing6Wide3PartitionTest() : LoadBalancingTest(6, 3) {}
};

TEST_F(LoadBalancing6Wide3PartitionTest, OneAlternatesSwaps)
{
    EXPECT_SWAP(1, false, 0, 1, 2, 3, 4, 5);
    EXPECT_SWAP(1, true, 2, 1, 0, 3, 4, 5);
    EXPECT_SWAP(1, true, 4, 1, 2, 3, 0, 5);
    EXPECT_SWAP(1, false, 0, 1, 2, 3, 4, 5);
}

TEST_F(LoadBalancing6Wide3PartitionTest, TwoAlwaysSwapsAlternating)
{
    EXPECT_SWAP(2, true, 0, 2, 1, 3, 4, 5);
    EXPECT_SWAP(2, true, 2, 4, 0, 3, 1, 5);
    EXPECT_SWAP(2, true, 1, 4, 2, 3, 0, 5);
}

TEST_F(LoadBalancing6Wide3PartitionTest, ThreeAlwaysSwaps)
{
    EXPECT_SWAP(3, true, 0, 4, 1, 3, 2, 5);
    EXPECT_SWAP(3, true, 0, 4, 1, 3, 2, 5);
}

TEST_F(LoadBalancing6Wide3PartitionTest, FourAlwaysSwapsAlternating)
{
    EXPECT_SWAP(4, true, 0, 1, 2, 4, 3, 5);
    EXPECT_SWAP(4, true, 0, 4, 1, 5, 2, 3);
    EXPECT_SWAP(4, true, 0, 4, 1, 2, 3, 5);
    EXPECT_SWAP(4, true, 0, 1, 2, 4, 3, 5);
}

TEST_F(LoadBalancing6Wide3PartitionTest, FiveAlternatesSwaps)
{
    EXPECT_SWAP(5, false, 0, 1, 2, 3, 4, 5);
    EXPECT_SWAP(5, true, 0, 1, 2, 5, 3, 4);
    EXPECT_SWAP(5, true, 0, 5, 1, 2, 3, 4);
    EXPECT_SWAP(5, false, 0, 1, 2, 3, 4, 5);
}

/*
 * vim: sw=4:ts=4:et:si:cindent:cino=\:0,(0,g0,t0:
 */
