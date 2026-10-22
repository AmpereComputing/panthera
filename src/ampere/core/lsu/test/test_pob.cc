//
// Copyright (c) 2024 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//

#include "ampere/archx/addr.h"
#include "ampere/core/backend_prefetcher.h"
#include "ampere/core/lsu/pob.h"
#include "ampere/sc/testing/test_module.h"

using ampere::sc::TestModule;
using archx::VirtAddr;
using archx::operator""_va;

struct POBTest : TestModule
{
    uint64_t id = 0;
    ampere::core::POB pob{"pob"};

    POBTest() = default;
    POBTest(std::initializer_list<std::pair<std::string, sc::json>> settings) : TestModule(settings) {}
};

TEST_F(POBTest, Empty)
{
    EXPECT_TRUE(pob.empty());
}

TEST_F(POBTest, Allocate)
{
    pob.allocate({0x0_va, nullptr}, core::PrefetchType::PCP, 0, core::POBState::WAIT_OTB_ACK);
    EXPECT_FALSE(pob.empty());
}

TEST_F(POBTest, AllocateTillFull)
{
    unsigned i = 0;
    for (; i < 16; ++i) {
        if (!pob.try_allocate(
                {0x0_va + VirtAddr::LINE_SIZE * i, nullptr}, core::PrefetchType::PCP, i, core::POBState::WAIT_OTB_ACK))
            break;
    }
    EXPECT_EQ(i, 12);
}

TEST_F(POBTest, AllocateTillFullMergeCachelines)
{
    unsigned i = 0;
    for (; i < 32; ++i) {
        if (!pob.try_allocate({0x0_va + (VirtAddr::LINE_SIZE / 2) * i, nullptr},
                              core::PrefetchType::PCP,
                              i,
                              core::POBState::WAIT_OTB_ACK))
            break;
    }
    // size check done before duplicate check
    EXPECT_EQ(i, 23);
}

struct POBTransitionTest : POBTest
{
    void SetUp() override
    {
        pob.allocate({0x0_va, nullptr}, core::PrefetchType::GRETCH, 0, core::POBState::WAIT_OTB_ACK);
    }
};

TEST_F(POBTransitionTest, RecieveAck)
{
    pob.handle_mmu_ack(0, 0);
    EXPECT_FALSE(pob.replay_ready(archx::EL::EL0));
}

TEST_F(POBTransitionTest, RecieveAckDropped)
{
    pob.handle_mmu_ack(0, {});
    EXPECT_FALSE(pob.replay_ready(archx::EL::EL0));
}

TEST_F(POBTransitionTest, RecieveAckThenResponseFault)
{
    pob.handle_mmu_ack(0, 0);
    pob.handle_mmu_response(0, true);
    EXPECT_FALSE(pob.replay_ready(archx::EL::EL0));
}

struct POBReplayTest : POBTransitionTest
{
    void SetUp() override
    {
        POBTransitionTest::SetUp();
        pob.handle_mmu_ack(0, 0);
        pob.handle_mmu_response(0, false);
    }
};

TEST_F(POBReplayTest, ReplayReady)
{
    EXPECT_TRUE(pob.replay_ready(archx::EL::EL0));
}

TEST_F(POBReplayTest, ReplayReadyNotInEL1)
{
    EXPECT_FALSE(pob.replay_ready(archx::EL::EL1));
}

TEST_F(POBTest, ReplayReadyPCP)
{
    pob.allocate({0x0_va, nullptr}, core::PrefetchType::PCP, 0, core::POBState::WAIT_OTB_ACK);
    pob.handle_mmu_ack(0, 0);
    pob.handle_mmu_response(0, false);
    EXPECT_TRUE(pob.replay_ready(archx::EL::EL0));
    EXPECT_TRUE(pob.replay_ready(archx::EL::EL1));
}

TEST_F(POBReplayTest, Replay)
{
    auto entry = pob.replay(archx::EL::EL0);
    EXPECT_EQ(entry->otb_id, 0);
    EXPECT_EQ(entry->virt_addr, 0x0_va);
    EXPECT_EQ(entry->type, ampere::core::PrefetchType::GRETCH);
}

/*
 * vim: sw=4:ts=4:et:si:cindent:cino=\:0,(0,g0,t0:
 */
