//
// Copyright (c) 2023 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//

#include <gmock/gmock.h>

#include "ampere/archx/flush_type.h"
#include "ampere/core/flush.h"
#include "ampere/core/prefetcher.h"
#include "ampere/core/prefetchers/multiple.h"
#include "ampere/core/prefetchers/test/mocks.h"
#include "ampere/sc/testing/test_module.h"
#include "ampere/sc/transaction.h"
#include "ampere/uarch_common/memop.h"
#include "ampere/uarch_common/memreq.h"

#include "ampere/sc/testing/test_interfaces.h"

using archx::operator""_pa;
using archx::operator""_va;
using archx::operator""_pc;

using ampere::archx::VirtAddr;
using ampere::core::MemReq;
using ampere::core::MockTaker;

using ::testing::Return;

struct MultiplePrefetcherTest : ampere::core::TestModule
{
    core::L2P_Multiple multiple;
    std::array<L2P_Strict_Mock*, 3> mocks;
    MemReq req;

    sc::MockPutter<MemReq> prefetch_requests;

    MultiplePrefetcherTest()
        : ampere::core::
              TestModule{{"prefetcher.prefetchers",
                          R"([{"name": "one", "type": "L2P_Strict_Mock"}, {"name": "two", "type": "L2P_Strict_Mock"}, {"name": "three", "type": "L2P_Strict_Mock"}])"_json},
                         {"prefetcher.unified_queue", true}},
          multiple("prefetcher"),
          mocks({
              L2P_Strict_Mock::get_instance("one"),
              L2P_Strict_Mock::get_instance("two"),
              L2P_Strict_Mock::get_instance("three"),
          }),
          req(make<MemReq>(0, 0x0_pa, uarch_common::MemOp::LSU))
    {
        multiple.prefetch_request.bind(prefetch_requests);
    }

    void SetUp() override { EXPECT_CALL(prefetch_requests, can_put()).WillRepeatedly(Return(true)); }
};

TEST_F(MultiplePrefetcherTest, TrainNoTrigger)
{
    for (auto* mock : mocks)
        EXPECT_CALL(*mock, train_impl(req, true, false, true));

    multiple.train(0, req, true, false, true, PrefetchType::NONE);
}

TEST_F(MultiplePrefetcherTest, NoTriggerAllDrop)
{
    for (auto* mock : mocks)
        EXPECT_CALL(*mock, drop_impl(req, core::PrefetchDropReason::CACHE_HIT));

    multiple.drop(req, core::PrefetchDropReason::CACHE_HIT);
}

TEST_F(MultiplePrefetcherTest, NoTriggerAllOverflow)
{
    for (auto* mock : mocks)
        EXPECT_CALL(*mock, overflow_impl(req));

    multiple.overflow(req);
}

TEST_F(MultiplePrefetcherTest, NoTriggerAllLaunch)
{
    for (auto* mock : mocks)
        EXPECT_CALL(*mock, launch_impl(req));

    multiple.launch(req);
}

TEST_F(MultiplePrefetcherTest, Fill)
{
    for (auto* mock : mocks)
        EXPECT_CALL(*mock, fill_impl(req, false, false));

    multiple.fill(0, req, false, false);
}

TEST_F(MultiplePrefetcherTest, Evict)
{
    for (auto* mock : mocks)
        EXPECT_CALL(*mock, evict_impl(0x0_pa, false, true));

    multiple.evict(0x0_pa, false, true);
}

TEST_F(MultiplePrefetcherTest, Flush)
{
    auto flush = make<Flush>(0_pc, 0_pc, nullptr, nullptr, 0, archx::FlushType::ABORTED);

    for (auto* mock : mocks)
        EXPECT_CALL(*mock, flush_impl(flush));

    multiple.flush(flush);
}

struct MultiplePrefetcherSingleTrainTest : MultiplePrefetcherTest
{
    MemReq pref;
    L2P_Strict_Mock* trigger_mock;

    void SetUp() override
    {
        trigger_mock = mocks[1];
        pref = make<MemReq>(1, 0x8_pa, uarch_common::MemOp::LSU);

        MultiplePrefetcherTest::SetUp();
        for (auto* mock : mocks)
            EXPECT_CALL(*mock, train_impl(req, true, false, true));

        EXPECT_CALL(prefetch_requests, put(pref));

        trigger_mock->prefetch_request->put(this->pref);
        multiple.train(0, req, true, false, true, PrefetchType::NONE);

        tick();
    }
};

TEST_F(MultiplePrefetcherSingleTrainTest, TrainThenDrop)
{
    EXPECT_CALL(*trigger_mock, drop_impl(this->pref, core::PrefetchDropReason::CACHE_HIT));
    multiple.drop(this->pref, core::PrefetchDropReason::CACHE_HIT);
}

TEST_F(MultiplePrefetcherSingleTrainTest, TrainThenOverflow)
{
    EXPECT_CALL(*trigger_mock, overflow_impl(this->pref));
    multiple.overflow(this->pref);
}

TEST_F(MultiplePrefetcherSingleTrainTest, TrainThenDropThenDropAgain)
{
    EXPECT_CALL(*trigger_mock, drop_impl(this->pref, core::PrefetchDropReason::CACHE_HIT));
    multiple.drop(this->pref, core::PrefetchDropReason::CACHE_HIT);

    tick();

    for (auto* mock : mocks)
        EXPECT_CALL(*mock, drop_impl(this->pref, core::PrefetchDropReason::CACHE_HIT));

    multiple.drop(this->pref, core::PrefetchDropReason::CACHE_HIT);
}

TEST_F(MultiplePrefetcherSingleTrainTest, TrainThenLaunchThenDrop)
{
    EXPECT_CALL(*trigger_mock, launch_impl(this->pref));
    multiple.launch(this->pref);

    tick();

    EXPECT_CALL(*trigger_mock, drop_impl(this->pref, core::PrefetchDropReason::CACHE_HIT));

    multiple.drop(this->pref, core::PrefetchDropReason::CACHE_HIT);
}

struct MultiplePrefetcherMultipleTrainSameAddressTest : MultiplePrefetcherTest
{
    MemReq pref;
    MemReq pref_2;

    void SetUp() override
    {
        pref = make<MemReq>(1, 0x8_pa, uarch_common::MemOp::LSU);
        pref_2 = make<MemReq>(2, 0x8_pa, uarch_common::MemOp::LSU);

        MultiplePrefetcherTest::SetUp();
        for (auto* mock : mocks)
            EXPECT_CALL(*mock, train_impl(req, true, false, true));

        EXPECT_CALL(prefetch_requests, put(pref));

        mocks[1]->prefetch_request->put(this->pref);
        mocks[2]->prefetch_request->put(this->pref_2);

        multiple.train(0, req, true, false, true, PrefetchType::NONE);

        tick();
    }
};

TEST_F(MultiplePrefetcherMultipleTrainSameAddressTest, TrainThenDrop)
{
    EXPECT_CALL(*mocks[1], drop_impl(this->pref, core::PrefetchDropReason::CACHE_HIT));
    EXPECT_CALL(*mocks[2], drop_impl(this->pref, core::PrefetchDropReason::CACHE_HIT));

    multiple.drop(this->pref, core::PrefetchDropReason::CACHE_HIT);
}

TEST_F(MultiplePrefetcherMultipleTrainSameAddressTest, TrainThenLaunchThenDrop)
{
    EXPECT_CALL(*mocks[1], launch_impl(this->pref));
    EXPECT_CALL(*mocks[2], launch_impl(this->pref));
    multiple.launch(this->pref);

    tick();

    EXPECT_CALL(*mocks[1], drop_impl(this->pref, core::PrefetchDropReason::CACHE_HIT));
    EXPECT_CALL(*mocks[2], drop_impl(this->pref, core::PrefetchDropReason::CACHE_HIT));

    multiple.drop(this->pref, core::PrefetchDropReason::CACHE_HIT);
}

struct MultiplePrefetcherTranslationTest : MultiplePrefetcherTest
{
    sc::MockPutter<core::PrefetchTranslateRequest<MemReq>> translation_requests;

    MultiplePrefetcherTranslationTest() { this->multiple.translate_prefetch_request.bind(translation_requests); }

    void SetUp() override
    {
        MultiplePrefetcherTest::SetUp();
        EXPECT_CALL(translation_requests, can_put()).WillRepeatedly(Return(true));
        tick();
    }
};

TEST_F(MultiplePrefetcherTranslationTest, TranslationAfterTrain)
{
    auto* trigger_mock = mocks[1];
    for (auto* mock : mocks)
        EXPECT_CALL(*mock, train_impl(req, true, false, true));

    core::PrefetchTranslateRequest<MemReq> trans_req{0x8_va, sc::Txn(), PrefetchType::NONE};
    EXPECT_CALL(translation_requests, put(testing::Field(&core::PrefetchTranslateRequest<MemReq>::virt_addr, 0x8_va)));

    trigger_mock->translate_prefetch_request->put(trans_req);
    multiple.train(0, req, true, false, true, PrefetchType::NONE);

    tick();
}

/*
 * vim: sw=4:ts=4:et:si:cindent:cino=\:0,(0,g0,t0:
 */
