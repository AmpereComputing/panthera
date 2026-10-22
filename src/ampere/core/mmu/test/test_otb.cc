//
// Copyright (c) 2023 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//

#include <gtest/gtest.h>
#include <cstdint>

#include "ampere/archx/addr.h"
#include "ampere/archx/page_size.h"
#include "ampere/core/ixu_defs.h"
#include "ampere/core/mmu/mru.h"
#include "ampere/core/mmu/otb.h"
#include "ampere/sc/testing/test_module.h"

using ampere::core::MMUReq;
using ampere::core::OTB;
using ampere::core::ProgramCounter;
using ampere::core::VirtAddr;

struct OTBTest : ampere::sc::TestModule
{
    OTB otb{"otb"};
    core::MMUMRU mru;
    core::TranslationState state{0, true, 12};

    uint64_t req_id = 0;

    OTBTest() = default;
    OTBTest(std::initializer_list<std::pair<std::string, sc::json>> settings) : TestModule(settings) {}

    void TearDown() override { otb.clear(); }

    template<typename T>
    T page_addr(uint64_t page) const
    {
        return T(page * (1ULL << state.granule_size));
    }
    auto make_instruction_req(uint64_t page)
    {
        auto req = make<MMUReq>(++req_id, page_addr<ProgramCounter>(page));
        req->section = 2;
        return req;
    }
    auto make_data_req(uint64_t page, bool is_store = false, unsigned channel = 0)
    {
        auto req = make<MMUReq>(++req_id, page_addr<VirtAddr>(page), is_store);
        req->section = req->channel = channel;
        return req;
    }
};

struct OTBSectionedTest : OTBTest
{
    static constexpr uint64_t inst_offset = 1000;
    static constexpr uint64_t data_offset = 500;

    OTBSectionedTest()
        : OTBTest({
              {"otb.entries", 10},
              {"otb.sectioned", true},
              {"otb.lsu_sections", 2},
              {"otb.sections.LSU_0.size", 2},
              {"otb.sections.LSU_1.size", 2},
              {"otb.sections.ICF.size", 2},
              {"otb.sections.Shared.size", 4},
          })
    {
    }
};

TEST_F(OTBSectionedTest, FillOnlyOneSection)
{
    for (unsigned i = 0; i < 6; ++i) {
        auto req = make_data_req(i);
        EXPECT_TRUE(otb.try_allocate(req, mru, state));
    }

    auto req = make_data_req(7);
    EXPECT_FALSE(otb.try_allocate(req, mru, state));
    EXPECT_FALSE(otb.full());
}

struct OTBFullSectionTest : OTBSectionedTest
{
    using OTBSectionedTest::OTBSectionedTest;
    void SetUp() override
    {
        for (unsigned i = 0; i < 2; ++i) {
            auto req = make_instruction_req(i + inst_offset);
            auto otb_id = otb.try_allocate(req, mru, state);
            EXPECT_TRUE(otb_id);
            EXPECT_EQ(*otb_id, i);
        }
        for (unsigned p = 0; p < core::IXU_PARTITIONS; ++p) {
            for (unsigned i = 0; i < 2; ++i) {
                auto req = make_data_req(i + data_offset * 500, false, p);
                EXPECT_TRUE(otb.try_allocate(req, mru, state));
            }
        }

        for (unsigned i = 0; i < 4; ++i) {
            auto req = make_instruction_req(i + inst_offset);
            EXPECT_TRUE(otb.try_allocate(req, mru, state));
        }
    }
};

TEST_F(OTBFullSectionTest, IsFull)
{
    EXPECT_TRUE(otb.full());
}

TEST_F(OTBFullSectionTest, DeallocateNonSharedCanAllocateAgain)
{
    otb.deallocate_immediately(otb.at(0));

    auto req = make_data_req(32 + data_offset);
    EXPECT_FALSE(otb.try_allocate(req, mru, state));

    req = make_instruction_req(32 + inst_offset);
    EXPECT_TRUE(otb.try_allocate(req, mru, state));
}

TEST_F(OTBFullSectionTest, DeallocateSharedAnyCanAllocateAgain)
{
    otb.deallocate_immediately(otb.at(9));

    auto req = make_data_req(32 + data_offset);
    EXPECT_TRUE(otb.try_allocate(req, mru, state));
}

/*
 * vim: sw=4:ts=4:et:si:cindent:cino=\:0,(0,g0,t0:
 */
