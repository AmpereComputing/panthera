//
// Copyright (c) 2023 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//

#include "ampere/core/ifb_victim_cache.h"
#include "ampere/sc/testing/test_module.h"

#include "ampere/sc/testing/assertions.h"
using ::testing::HasSubstr;

namespace ampere {
namespace ifb {

using namespace core;

class IFBVictimCacheTest : public TestModule
{
protected:
    IFBVictimCacheTest()
        : TestModule{
              {"ifb_victim_cache.num_entries", 4},
          }
    {
    }

    Fetch make_fetch(ProgramCounter pc)
    {
        auto fetch = make<Fetch>(pc, current_fetch_id++);
        fetch->phys_addr = PhysAddr(pc.value()); // flat VA->PA mapping for testing

        return fetch;
    }

    IFBVictimCache ivc{"ifb_victim_cache"};

    int current_fetch_id{1};
};

TEST_F(IFBVictimCacheTest, TestHitMiss)
{
    auto f1 = make_fetch(0x100_pc);
    auto f2 = make_fetch(0x100_pc);
    auto f3 = make_fetch(0x100_pc);
    auto f4 = make_fetch(0x200_pc);

    ASSERT_FALSE(ivc.lookup(f2));

    ivc.refill(f1->phys_addr.value());

    ASSERT_TRUE(ivc.lookup(f2));
    EXPECT_FALSE(ivc.lookup(f4));
    EXPECT_AMPERE_ERROR(ivc.lookup(f3), HasSubstr("IFB victim cache already has an outstanding hit"));
}

TEST_F(IFBVictimCacheTest, TestReplacement)
{
    auto f1 = make_fetch(0x100_pc);
    auto f2 = make_fetch(0x200_pc);
    auto f3 = make_fetch(0x300_pc);
    auto f4 = make_fetch(0x400_pc);
    auto f5 = make_fetch(0x500_pc);

    ivc.refill(f1->phys_addr.value());
    ivc.refill(f2->phys_addr.value());
    ivc.refill(f3->phys_addr.value());
    ivc.refill(f4->phys_addr.value());

    // Ensure f5 is the only miss after filling f1-f4
    ASSERT_TRUE(ivc.lookup(f1));
    ASSERT_TRUE(ivc.lookup(f2));
    ASSERT_TRUE(ivc.lookup(f3));
    ASSERT_TRUE(ivc.lookup(f4));
    ASSERT_FALSE(ivc.lookup(f5));

    ivc.flush(); // We don't overwrite victim cache entries that have been
                 // looked up but neither flushed or deallocated, so flush here
                 // to allow them to be overwritten
    ivc.refill(f5->phys_addr.value());

    // Ensure f1 is the only miss after then filling f5
    ASSERT_FALSE(ivc.lookup(f1));
    ASSERT_TRUE(ivc.lookup(f2));
    ASSERT_TRUE(ivc.lookup(f3));
    ASSERT_TRUE(ivc.lookup(f4));
    ASSERT_TRUE(ivc.lookup(f5));

    ivc.flush(); // We don't overwrite victim cache entries that have been
                 // looked up but neither flushed or deallocated, so flush here
                 // to allow them to be overwritten
    ivc.lookup(f2);
    ivc.refill(f1->phys_addr.value());

    // Ensure f3 is the only miss after then filling f1 (while f2 is 'active')
    ASSERT_TRUE(ivc.lookup(f1));
    EXPECT_AMPERE_ERROR(ivc.lookup(f2), HasSubstr("IFB victim cache already has an outstanding hit"));
    ASSERT_FALSE(ivc.lookup(f3));
    ASSERT_TRUE(ivc.lookup(f4));
    ASSERT_TRUE(ivc.lookup(f5));

    ivc.refill(f3->phys_addr.value());

    // Ensure fill to f3 is ineffectual when all ways are 'active'
    EXPECT_AMPERE_ERROR(ivc.lookup(f1), HasSubstr("IFB victim cache already has an outstanding hit"));
    EXPECT_AMPERE_ERROR(ivc.lookup(f2), HasSubstr("IFB victim cache already has an outstanding hit"));
    ASSERT_FALSE(ivc.lookup(f3));
    EXPECT_AMPERE_ERROR(ivc.lookup(f4), HasSubstr("IFB victim cache already has an outstanding hit"));
    EXPECT_AMPERE_ERROR(ivc.lookup(f5), HasSubstr("IFB victim cache already has an outstanding hit"));
}

TEST_F(IFBVictimCacheTest, TestSnoop)
{
    auto f1 = make_fetch(0x100_pc);
    auto f2 = make_fetch(0x200_pc);
    auto f3 = make_fetch(0x300_pc);

    ivc.refill(f1->phys_addr.value());
    ivc.refill(f2->phys_addr.value());

    // Ensure present
    ASSERT_TRUE(ivc.lookup(f1));
    ASSERT_TRUE(ivc.lookup(f2));

    // Can't snoop non-present line
    ivc.snoop(f3->phys_addr.value());
    ASSERT_FALSE(ivc.was_snooped(f1->fetch_id));
    ASSERT_FALSE(ivc.was_snooped(f2->fetch_id));

    // Can snoop present line
    ivc.snoop(f1->phys_addr.value());
    ASSERT_TRUE(ivc.was_snooped(f1->fetch_id));
    ASSERT_FALSE(ivc.was_snooped(f2->fetch_id));

    ivc.flush();

    // Ensure in-use snooped line is absent after flush
    ASSERT_FALSE(ivc.lookup(f1));
    ASSERT_TRUE(ivc.lookup(f2));
}

TEST_F(IFBVictimCacheTest, TestDeallocate)
{
    auto f1 = make_fetch(0x100_pc);

    ivc.refill(f1->phys_addr.value());
    ASSERT_TRUE(ivc.lookup(f1));
    ivc.deallocate(f1->fetch_id);
    // Ensure absent after deallocate
    ASSERT_FALSE(ivc.lookup(f1));
}

TEST_F(IFBVictimCacheTest, TestFlush)
{
    auto f1 = make_fetch(0x100_pc);
    auto f2 = make_fetch(0x200_pc);
    auto f3 = make_fetch(0x300_pc);
    auto f4 = make_fetch(0x400_pc);

    ivc.refill(f1->phys_addr.value());
    ivc.refill(f2->phys_addr.value());
    ivc.refill(f3->phys_addr.value());
    ivc.refill(f4->phys_addr.value());

    // Ensure every entry is the oldest victim hit
    ASSERT_TRUE(ivc.lookup(f1));
    ASSERT_TRUE(ivc.lookup(f2));
    ASSERT_TRUE(ivc.lookup(f3));
    ASSERT_TRUE(ivc.lookup(f4));

    ivc.flush();

    // Ensure every entry is the oldest victim hit (again) after the flush
    ASSERT_TRUE(ivc.lookup(f1));
    ASSERT_TRUE(ivc.lookup(f2));
    ASSERT_TRUE(ivc.lookup(f3));
    ASSERT_TRUE(ivc.lookup(f4));
}

TEST_F(IFBVictimCacheTest, TestPartialFlush)
{
    auto f1 = make_fetch(0x100_pc);
    auto f2 = make_fetch(0x200_pc);
    auto f3 = make_fetch(0x300_pc);
    auto f4 = make_fetch(0x400_pc);

    ivc.refill(f1->phys_addr.value());
    ivc.refill(f2->phys_addr.value());
    ivc.refill(f3->phys_addr.value());
    ivc.refill(f4->phys_addr.value());

    // Ensure every entry is the oldest victim hit
    ASSERT_TRUE(ivc.lookup(f1));
    ASSERT_TRUE(ivc.lookup(f2));
    ASSERT_TRUE(ivc.lookup(f3));
    ASSERT_TRUE(ivc.lookup(f4));

    ivc.clear_from_fetch_id(f2->fetch_id);

    // Ensure only entries older than f3 are the oldest victim hit (again)
    // after the flush
    EXPECT_AMPERE_ERROR(ivc.lookup(f1), HasSubstr("IFB victim cache already has an outstanding hit"));
    EXPECT_AMPERE_ERROR(ivc.lookup(f2), HasSubstr("IFB victim cache already has an outstanding hit"));
    ASSERT_TRUE(ivc.lookup(f3));
    ASSERT_TRUE(ivc.lookup(f4));
}

} // namespace ifb
} // namespace ampere

/*
 * vim: sw=4:ts=4:et:si:cindent:cino=\:0,(0,g0,t0:
 */
