//
// Copyright (c) 2023 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//

#include <gmock/gmock.h>

#include "ampere/archx/addr.h"
#include "ampere/core/backend_prefetcher.h"
#include "ampere/core/prefetchers/l1p_pcp.h"
#include "ampere/sc/param_registry.h"
#include "ampere/sc/testing/test_module.h"
#include "ampere/core/testing/uops.h"
#include "ampere/sc/testing/test_interfaces.h"
#include "gmock/gmock.h"

#include <nlohmann/json.hpp>

namespace ampere::test_pcp {

using ampere::core::PCP;
using ampere::sc::TestModule;
using archx::VirtAddr;
using archx::operator""_va;
using ::testing::_;

MATCHER_P(ReqAddrMatcher, addr, "")
{
    return (arg.virt_addr == addr);
}

struct PCPTestBase : TestModule
{
    uint64_t alloc_id = 1;
    PCP pcp;

    ::testing::StrictMock<sc::MockPutter<core::BackendMemReq>> prefetch_request;

    PCPTestBase() : pcp("pcp") { bind_ports(); }
    PCPTestBase(std::initializer_list<std::pair<std::string, sc::json>> settings) : TestModule(settings), pcp("pcp")
    {
        bind_ports();
    }

    void SetUp() override
    {
        EXPECT_CALL(prefetch_request, can_put).WillRepeatedly(testing::Return(true));
        EXPECT_CALL(prefetch_request, size).WillRepeatedly(testing::Return(0));
    }

    void bind_ports() { pcp.prefetch_request.bind(prefetch_request); }

    void train_load(VirtAddr addr)
    {
        auto uop = core::create_load_uop(addr);
        uop->alloc_id = alloc_id++;
        pcp.train(uop);
        tick();
    }

    void train_loads(VirtAddr start, unsigned step, unsigned amount)
    {
        for (unsigned i = 0; i < amount; ++i, start += step)
            train_load(start);
    }
};

struct PCPTest : PCPTestBase
{
    PCPTest()
        : PCPTestBase({
              {"pcp.addr_mask", 0xfff},
          })
    {
    }
};

TEST_F(PCPTest, UntrainedDoesNotTrigger)
{
    auto uop = core::create_load_uop(0x0_va);
    pcp.train(uop);
}

TEST_F(PCPTest, TrainedTwiceTriggersOnThird)
{
    EXPECT_CALL(prefetch_request, put(ReqAddrMatcher(VirtAddr{0x8 + 4 * 2})));
    train_loads(0x0_va, 0x4, 3);
}

TEST_F(PCPTest, IncrementMultiplierAffectsOffset)
{
    train_loads(0x0_va, 0x4, 2);
    pcp.stride_mult_inc(core::create_load_uop(), false);

    EXPECT_CALL(prefetch_request, put(ReqAddrMatcher(VirtAddr{0x8 + 4 * 4})));
    train_load(0x8_va);
}

TEST_F(PCPTest, FullyTrainedDoesNotFullyResetOnStreamRestart)
{
    EXPECT_CALL(prefetch_request, put(_)).Times(1).RetiresOnSaturation();
    train_loads(0x0_va, 0x4, 3);

    EXPECT_CALL(prefetch_request, put(ReqAddrMatcher(VirtAddr{0x8 + 4 * 2})));
    train_loads(0x0_va, 0x4, 3);
}

TEST_F(PCPTest, MoreThanTwoMismatchesResets)
{
    EXPECT_CALL(prefetch_request, put(_)).Times(1).RetiresOnSaturation();
    train_loads(0x0_va, 0x4, 3);

    pcp.stride_mult_inc(core::create_load_uop(), false);

    train_load(0x28_va);

    EXPECT_CALL(prefetch_request, put(ReqAddrMatcher(VirtAddr{0x8 + 4 * 2})));
    train_loads(0x0_va, 0x4, 3);
}

TEST_F(PCPTest, AddressMaskingCausesOverflowAndMismatch)
{
    train_loads(0xff6_va, 0x4, 3);
    train_load(0x1002_va);
}

} // namespace ampere::test_pcp

/*
 * vim: sw=4:ts=4:et:si:cindent:cino=\:0,(0,g0,t0:
 */
