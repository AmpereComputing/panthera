//
// Copyright (c) 2021 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//

#include "ampere/uarch_common/cache.h"

#include "ampere/sc/testing/test_module.h"

using namespace ampere::sc;

namespace ampere {
namespace cache {

using namespace archx;
using namespace uarch_common;

class CLine
{
public:
    CLine() = default;
    explicit CLine(int id) : id(id) {}
    int id{0};
};

class RRTest : public TestModule
{
    void SetUp() override { Environment::get_root()->parameters.clear(); }
};

TEST_F(RRTest, testInOrderReplacement)
{
    auto env = Environment::get_root();
    env->parameters.add_setting("cache.sets", 1, "unit test");
    env->parameters.add_setting("cache.ways", 4, "unit test");
    env->parameters.add_setting("cache.cache_replacement_policy.type", "RR", "unit test");
    env->parameters.add_setting("cache.crp_victim_always", "true", "unit test");

    Cache<VirtAddr, CLine> cache{"cache"};
    Txn transaction = make<Txn>();

    ASSERT_FALSE(cache.lookup(transaction, VirtAddr(0)));
    cache.refill(transaction, VirtAddr(0), CLine(1));
    ASSERT_TRUE(cache.lookup(transaction, VirtAddr(0)));
    ASSERT_TRUE(cache.lookup(transaction, VirtAddr(0x10)));
    ASSERT_FALSE(cache.lookup(transaction, VirtAddr(0x40)));

    ASSERT_FALSE(cache.lookup(transaction, VirtAddr(0xc0de0000)));
    cache.refill(transaction, VirtAddr(0xc0de0000), CLine(1));
    ASSERT_TRUE(cache.lookup(transaction, VirtAddr(0xc0de0000)));
    ASSERT_TRUE(cache.lookup(transaction, VirtAddr(0)));

    ASSERT_FALSE(cache.lookup(transaction, VirtAddr(0xffffffffffffffff)));
    cache.refill(transaction, VirtAddr(0xffffffffffffffff), CLine(1));
    ASSERT_TRUE(cache.lookup(transaction, VirtAddr(0xffffffffffffffff)));
    ASSERT_TRUE(cache.lookup(transaction, VirtAddr(0xc0de0000)));
    ASSERT_TRUE(cache.lookup(transaction, VirtAddr(0)));

    cache.refill(transaction, VirtAddr(0xdeadc0de), CLine(1));
    ASSERT_TRUE(cache.lookup(transaction, VirtAddr(0xdeadc0de)));
    ASSERT_TRUE(cache.lookup(transaction, VirtAddr(0xffffffffffffffff)));
    ASSERT_TRUE(cache.lookup(transaction, VirtAddr(0xc0de0000)));
    ASSERT_TRUE(cache.lookup(transaction, VirtAddr(0)));

    cache.refill(transaction, VirtAddr(0xf00baa), CLine(1));
    ASSERT_TRUE(cache.lookup(transaction, VirtAddr(0xf00baa)));
    ASSERT_TRUE(cache.lookup(transaction, VirtAddr(0xdeadc0de)));
    ASSERT_TRUE(cache.lookup(transaction, VirtAddr(0xffffffffffffffff)));
    ASSERT_TRUE(cache.lookup(transaction, VirtAddr(0xc0de0000)));
    ASSERT_FALSE(cache.lookup(transaction, VirtAddr(0)));

    cache.refill(transaction, VirtAddr(0x7ffff409), CLine(1));
    ASSERT_TRUE(cache.lookup(transaction, VirtAddr(0x7ffff409)));
    ASSERT_TRUE(cache.lookup(transaction, VirtAddr(0xf00baa)));
    ASSERT_TRUE(cache.lookup(transaction, VirtAddr(0xdeadc0de)));
    ASSERT_TRUE(cache.lookup(transaction, VirtAddr(0xffffffffffffffff)));
    ASSERT_FALSE(cache.lookup(transaction, VirtAddr(0xc0de0000)));
    ASSERT_FALSE(cache.lookup(transaction, VirtAddr(0)));
}

TEST_F(RRTest, testOutOfOrderLookupsSameReplacement)
{
    auto env = Environment::get_root();
    env->parameters.add_setting("cache.sets", 1, "unit test");
    env->parameters.add_setting("cache.ways", 4, "unit test");
    env->parameters.add_setting("cache.cache_replacement_policy.type", "RR", "unit test");
    env->parameters.add_setting("cache.crp_victim_always", "true", "unit test");

    Cache<VirtAddr, CLine> cache{"cache"};
    Txn transaction = make<Txn>();

    // Populate cache, filling VA 0x0 first
    cache.refill(transaction, VirtAddr(0), CLine(1));
    cache.refill(transaction, VirtAddr(0xc0de0000), CLine(1));
    cache.refill(transaction, VirtAddr(0xffffffffffffffff), CLine(1));
    cache.refill(transaction, VirtAddr(0xdeadc0de), CLine(1));

    // Ensure the entry for VA 0 is present, and access it again. Because this
    // is RR, this extra access should change nothing.
    ASSERT_TRUE(cache.lookup(transaction, VirtAddr(0)));
    cache.lookup(transaction, VirtAddr(0xc0de0000));
    cache.lookup(transaction, VirtAddr(0));
    cache.lookup(transaction, VirtAddr(0));
    cache.lookup(transaction, VirtAddr(0));

    cache.refill(transaction, VirtAddr(0xf00baa), CLine(1));
    ASSERT_TRUE(cache.lookup(transaction, VirtAddr(0xf00baa)));
    ASSERT_TRUE(cache.lookup(transaction, VirtAddr(0xdeadc0de)));
    ASSERT_TRUE(cache.lookup(transaction, VirtAddr(0xffffffffffffffff)));
    ASSERT_TRUE(cache.lookup(transaction, VirtAddr(0xc0de0000)));
    ASSERT_FALSE(cache.lookup(transaction, VirtAddr(0)));

    cache.lookup(transaction, VirtAddr(0xc0de0000));
    cache.lookup(transaction, VirtAddr(0xffffffffffffffff));
    cache.lookup(transaction, VirtAddr(0xc0de0000));

    cache.refill(transaction, VirtAddr(0x7ffff409), CLine(1));
    ASSERT_TRUE(cache.lookup(transaction, VirtAddr(0x7ffff409)));
    ASSERT_TRUE(cache.lookup(transaction, VirtAddr(0xf00baa)));
    ASSERT_TRUE(cache.lookup(transaction, VirtAddr(0xdeadc0de)));
    ASSERT_TRUE(cache.lookup(transaction, VirtAddr(0xffffffffffffffff)));
    ASSERT_FALSE(cache.lookup(transaction, VirtAddr(0xc0de0000)));
    ASSERT_FALSE(cache.lookup(transaction, VirtAddr(0)));
}

TEST_F(RRTest, testInvalidation)
{
    auto env = Environment::get_root();
    env->parameters.add_setting("cache.sets", 1, "unit test");
    env->parameters.add_setting("cache.ways", 4, "unit test");
    env->parameters.add_setting("cache.cache_replacement_policy.type", "RR", "unit test");
    env->parameters.add_setting("cache.crp_victim_always", "true", "unit test");

    Cache<VirtAddr, CLine> cache{"cache"};
    Txn transaction = make<Txn>();

    // Populate cache, filling VA 0xa first
    cache.refill(transaction, VirtAddr(0xa), CLine(1));
    cache.refill(transaction, VirtAddr(0xc0de0000), CLine(1));
    cache.refill(transaction, VirtAddr(0xffffffffffffffff), CLine(1));
    cache.refill(transaction, VirtAddr(0xdeadc0de), CLine(1));

    // Ensure the entry for VA 0 is present, and access it again. Because this
    // is RR, this extra access should change nothing. Also invalidate another
    // way (this also shouldn't change anything)
    ASSERT_TRUE(cache.lookup(transaction, VirtAddr(0xa)));
    ASSERT_TRUE(cache.lookup(transaction, VirtAddr(0xc0de0000)));
    ASSERT_TRUE(cache.lookup(transaction, VirtAddr(0xffffffffffffffff)));
    ASSERT_TRUE(cache.lookup(transaction, VirtAddr(0xdeadc0de)));
    cache.lookup(transaction, VirtAddr(0xc0de0000));
    cache.lookup(transaction, VirtAddr(0xa));
    cache.invalidate(transaction, VirtAddr(0xffffffffffffffff));
    cache.lookup(transaction, VirtAddr(0xa));

    cache.refill(transaction, VirtAddr(0xf00baa), CLine(1));
    ASSERT_TRUE(cache.lookup(transaction, VirtAddr(0xf00baa)));
    ASSERT_TRUE(cache.lookup(transaction, VirtAddr(0xdeadc0de)));
    ASSERT_TRUE(cache.lookup(transaction, VirtAddr(0xc0de0000)));
    ASSERT_FALSE(cache.lookup(transaction, VirtAddr(0xffffffffffffffff))); // invalidated
    ASSERT_FALSE(cache.lookup(transaction, VirtAddr(0xa)));                // victimized

    cache.lookup(transaction, VirtAddr(0xc0de0000));
    cache.lookup(transaction, VirtAddr(0xc0de0000));
    cache.invalidate(transaction, VirtAddr(0xf00baa));
    cache.lookup(transaction, VirtAddr(0xc0de0000));

    cache.refill(transaction, VirtAddr(0x7ffff409), CLine(1));
    ASSERT_TRUE(cache.lookup(transaction, VirtAddr(0x7ffff409)));
    ASSERT_TRUE(cache.lookup(transaction, VirtAddr(0xdeadc0de)));
    ASSERT_FALSE(cache.lookup(transaction, VirtAddr(0xf00baa)));           // invalidated
    ASSERT_FALSE(cache.lookup(transaction, VirtAddr(0xffffffffffffffff))); // invalidated
    ASSERT_FALSE(cache.lookup(transaction, VirtAddr(0xc0de0000)));         // victimized
    ASSERT_FALSE(cache.lookup(transaction, VirtAddr(0xa)));                // victimized
}

} // namespace cache
} // namespace ampere
