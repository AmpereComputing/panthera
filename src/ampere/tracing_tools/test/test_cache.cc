//
// Copyright (c) 2020 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//

#include <stdarg.h>
#include <gtest/gtest.h>

#include "ampere/tracing_tools/cache.h"

using namespace boost::filesystem;

using namespace ampere;
using namespace tracer;
using namespace archx;

class CacheTest : public ::testing::Test
{
protected:
    void SetUp() override;
    void TearDown() override;
    Cache<PhysAddr> *cache;
};

void CacheTest::SetUp()
{
    /* 1024 bytes, 64-byte lines, 4 sets, 4 ways */
    cache = new Cache<PhysAddr>(1024);
}

void CacheTest::TearDown()
{
    delete cache;
    cache = nullptr;
}

TEST_F(CacheTest, testLookup)
{
    PhysAddr addr1 = PhysAddr(0xdeadbeef);
    PhysAddr addr2 = PhysAddr(0xdeadc0de);

    ASSERT_FALSE(cache->lookup(addr1, false));
    cache->refill(addr1, true, false);
    ASSERT_TRUE(cache->lookup(addr1, false));

    // check for hits on other addresses in same cache line
    ASSERT_TRUE(cache->lookup(0xdeadbec0_pa, false));
    ASSERT_TRUE(cache->lookup(0xdeadbed3_pa, false));
    ASSERT_TRUE(cache->lookup(0xdeadbeff_pa, false));

    ASSERT_FALSE(cache->lookup(addr2, false));
    cache->refill(addr2, true, false);
    ASSERT_TRUE(cache->lookup(addr2, false));

    // Ensure this refill didn't kick out original
    ASSERT_TRUE(cache->lookup(addr1, false));
}

TEST_F(CacheTest, testRefill)
{
    PhysAddr orig_addr = PhysAddr(0xd5d2c595e4);

    // Fill all ways in set
    for (PhysAddr addr = orig_addr; addr < orig_addr + (64 * 4 * 4); addr += (64 * 4)) {
        ASSERT_FALSE(cache->lookup(addr, false));
        cache->refill(addr, true, false);
    }

    // Refill worked for all ways in set
    for (PhysAddr addr = orig_addr; addr < orig_addr + (64 * 4 * 4); addr += (64 * 4)) {
        ASSERT_TRUE(cache->lookup(addr, false));
    }

    // Refill a fifth line to the same set, causing eviction
    PhysAddr evicting_addr = orig_addr + (64 * 4 * 4);
    ASSERT_FALSE(cache->lookup(evicting_addr, false));
    cache->refill(evicting_addr, true, false);
    ASSERT_TRUE(cache->lookup(evicting_addr, false));

    // Ensure oldest line was evicted
    ASSERT_FALSE(cache->lookup(orig_addr, false));
    for (PhysAddr addr = orig_addr + (64 * 4); addr < orig_addr + (64 * 4 * 5); addr += (64 * 4)) {
        ASSERT_TRUE(cache->lookup(addr, false));
    }

    // Test another eviction, making orig_addr + (64 * 4 * 2) the oldest
    cache->lookup(orig_addr + (64 * 4), false);
    cache->refill(orig_addr, true, false);

    ASSERT_TRUE(cache->lookup(orig_addr, false));
    ASSERT_TRUE(cache->lookup(orig_addr + (64 * 4), false));
    ASSERT_FALSE(cache->lookup(orig_addr + (64 * 4 * 2), false));
    ASSERT_TRUE(cache->lookup(orig_addr + (64 * 4 * 3), false));
    ASSERT_TRUE(cache->lookup(orig_addr + (64 * 4 * 4), false));
}

TEST_F(CacheTest, testRefillResident)
{
    PhysAddr orig_addr = PhysAddr(0x7fffffa790);

    // Fill all ways in set
    for (PhysAddr addr = orig_addr; addr < orig_addr + (64 * 4 * 4); addr += (64 * 4)) {
        cache->refill(addr, false, false);
    }

    // Refill second address again. It should detect the existing entry and not
    // evict the first address.
    cache->refill(orig_addr + (64 * 4), false, false);

    for (PhysAddr addr = orig_addr; addr < orig_addr + (64 * 4 * 4); addr += (64 * 4)) {
        ASSERT_TRUE(cache->lookup(addr, false));
    }
}

TEST_F(CacheTest, testWriteMemoryStream)
{
    const ::testing::TestInfo *const test_info = ::testing::UnitTest::GetInstance()->current_test_info();
    path p(test_info->name());
    p.replace_extension("ms.xz");

    cache->refill(0x748213f008032_pa, false, false);
    cache->refill(0x7ffff71c84_pa, true, false);
    cache->refill(0x7482139487232_pa, false, false);
    cache->refill(0x748213f008028_pa, false, true);

    cache->to_memory_stream(p);

    auto reader = ms::MemoryStreamReader(p);
    ms::Access access;

    ASSERT_TRUE(reader.filter_was_unified());
    ASSERT_EQ(reader.filter_dcache_size(), 1024UL);
    ASSERT_EQ(reader.filter_icache_size(), 1024UL);

    ASSERT_TRUE(reader.read(access));
    ASSERT_EQ(access.type(), ms::Access_Type_READ);
    ASSERT_EQ(access.pa(), (uint64_t)0x7ffff71c80);
    ASSERT_EQ(access.source(), ms::Access_Source_INSTRUCTION);
    ASSERT_EQ(access.size(), 64ul);
    ASSERT_EQ(access.offset(), 1ul);
    ASSERT_EQ(access.pc(), 0ul);
    access.Clear();

    ASSERT_TRUE(reader.read(access));
    ASSERT_EQ(access.type(), ms::Access_Type_READ);
    ASSERT_EQ(access.pa(), (uint64_t)0x7482139487200);
    ASSERT_EQ(access.source(), ms::Access_Source_DATA);
    ASSERT_EQ(access.size(), 64ul);
    ASSERT_EQ(access.offset(), 2ul);
    ASSERT_EQ(access.pc(), 0ul);
    access.Clear();

    ASSERT_TRUE(reader.read(access));
    ASSERT_EQ(access.type(), ms::Access_Type_WRITE);
    ASSERT_EQ(access.pa(), (uint64_t)0x748213f008000);
    ASSERT_EQ(access.source(), ms::Access_Source_DATA);
    ASSERT_EQ(access.size(), 64ul);
    ASSERT_EQ(access.offset(), 3ul);
    ASSERT_EQ(access.pc(), 0ul);

    ASSERT_FALSE(reader.read(access));

    unlink(p.string().c_str());
}

/*
 * vim: sw=4:ts=4:et:si:cindent:cino=\:0,(0,g0,t0:
 */
