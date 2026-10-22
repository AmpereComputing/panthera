//
// Copyright (c) 2024 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//

#include <gmock/gmock.h>

#include "ampere/archx/addr.h"
#include "ampere/archx/instruction_simple.h"
#include "ampere/archx/page_size.h"
#include "ampere/core/prefetchers/oracle.h"
#include "ampere/sc/testing/test_module.h"
#include "ampere/sc/testing/test_interfaces.h"
#include "ampere/archx/testing/mock_driver.h"
#include "ampere/uarch_common/memreq.h"
#include "ampere/uarch_common/memop.h"

using archx::operator""_pa;
using archx::operator""_va;
using archx::operator""_pc;

using ampere::core::MemReq;
using ampere::core::MemReqObj;
using ampere::core::MockTaker;

using ::testing::_;
using ::testing::Invoke;
using ::testing::Return;
using ::testing::Throw;

MATCHER_P(MemReqAddr, addr, "")
{
    return (arg->phys_addr == addr);
}

struct OraclePrefetcherTest : ampere::core::TestModule
{
    archx::MockDriver driver;
    core::L2P_Oracle prefetcher;

    sc::MockPutter<MemReq> prefetch_requests;

    OraclePrefetcherTest() : TestModule({{"prefetcher.initial_offset", "32"}}), prefetcher("prefetcher")
    {
        prefetcher.prefetch_request.bind(prefetch_requests);
        prefetcher.connect(driver);
    }

    void SetUp() override { EXPECT_CALL(prefetch_requests, can_put()).WillRepeatedly(Return(true)); }

    auto make_load(archx::PhysAddr addr)
    {
        return make<MemReq>(MemReqObj::load(
            0, 0x0_pc, addr, archx::VirtAddr(addr.value()), 8, 1, ampere::archx::PageSize::Size::PS_4KB));
    }

    inline static archx::PhysAddr line_addr(uint64_t index)
    {
        return archx::PhysAddr(index * archx::PhysAddr::LINE_SIZE);
    }
};

TEST_F(OraclePrefetcherTest, StopMeasurement)
{
    EXPECT_CALL(driver, next_pc(nullptr)).WillOnce(Return(0x0_pc));
    EXPECT_CALL(driver, fetch(_)).WillOnce(Throw(std::runtime_error("stopped")));

    auto req = make_load(0x0_pa);
    prefetcher.train(0, req, false, false, true, {});
}

struct OraclePrefetcherSequenceTest : OraclePrefetcherTest
{
    void SetUp() override
    {
        OraclePrefetcherTest::SetUp();
        {
            testing::InSequence seq;

            for (uint64_t i = 1; i <= 64; ++i) {
                archx::ProgramCounter pc{i * 4};
                EXPECT_CALL(driver, next_pc(nullptr)).WillOnce(Return(pc));
                EXPECT_CALL(driver, fetch(pc))
                    .WillOnce(Return(driver.return_load(pc, archx::VirtAddr(i * ampere::archx::VirtAddr::LINE_SIZE))));
                EXPECT_CALL(driver, commit(_));
            }

            EXPECT_CALL(driver, next_pc(nullptr)).WillRepeatedly(Return(0x0_pc));
            EXPECT_CALL(driver, fetch(_)).WillRepeatedly(Throw(std::runtime_error("stopped")));
        }
    }
};

TEST_F(OraclePrefetcherSequenceTest, Initial)
{
    EXPECT_CALL(prefetch_requests, put(MemReqAddr(line_addr(33)))).Times(1);

    auto req = make_load(line_addr(1));
    prefetcher.train(0, req, false, false, true, {});
}

TEST_F(OraclePrefetcherSequenceTest, StopsAtEnd)
{
    {
        testing::InSequence seq;
        for (size_t i = 1; i <= 32; ++i)
            EXPECT_CALL(prefetch_requests, put(MemReqAddr(line_addr(32 + i))));
    }

    for (size_t i = 1; i <= 32; ++i) {
        auto req = make_load(line_addr(i));
        prefetcher.train(0, req, false, false, true, {});
        tick();
    }
}

TEST_F(OraclePrefetcherSequenceTest, FillAdjustsOffset)
{
    MemReq pref_req;
    auto capture_pref_req = [&](MemReq pf) {
        pref_req = pf;
    };
    {
        testing::InSequence seq;
        EXPECT_CALL(prefetch_requests, put(MemReqAddr(line_addr(33)))).WillOnce(Invoke(capture_pref_req));
        EXPECT_CALL(prefetch_requests, put(MemReqAddr(line_addr(25))));
    }

    auto req = make_load(line_addr(1));
    prefetcher.train(0, req, false, false, true, {});

    tick(8);

    ASSERT_TRUE(pref_req);
    prefetcher.fill(0, pref_req, true, true);

    tick();

    req = make_load(line_addr(17));
    prefetcher.train(0, req, false, false, true, {});
}

/*
 * vim: sw=4:ts=4:et:si:cindent:cino=\:0,(0,g0,t0:
 */
