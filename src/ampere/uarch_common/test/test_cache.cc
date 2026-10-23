//
// Copyright (c) 2018-2020 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//

#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <optional>
#include <type_traits>

#include "ampere/archx/addr.h"
#include "ampere/uarch_common/cache.h"

#include "ampere/sc/testing/test_module.h"
#include "ampere/sc/testing/assertions.h"

using ::testing::HasSubstr;

namespace {

using namespace ampere;
using namespace ampere::archx;
using namespace ampere::uarch_common;

using ::testing::Optional;

class CLine
{
public:
    CLine() = default;
    explicit CLine(int id) : id(id) {}
    int id{0};
    bool operator==(const CLine& other) const { return other.id == id; }
};

template<typename Addr, typename Value>
class BaseCacheTest : public TestModule
{
protected:
    Txn transaction = make<Txn>();
    Cache<Addr, Value> cache{"cache", CacheEntry::default_entry_value<Value>()};

    BaseCacheTest() = default;
    BaseCacheTest(std::initializer_list<std::pair<std::string, sc::json>> settings) : TestModule(settings) {}

    void fill_set(unsigned set, unsigned ways = 0)
    {
        for (unsigned way = 0; way < (ways ? ways : cache.get_num_ways()); ++way) {
            auto addr = Addr(((static_cast<uint64_t>(way) << cache.get_set_bits()) | static_cast<uint64_t>(set))
                             << cache.get_line_bits());
            cache.refill(transaction, addr, Value(way));
        }
    }
};

struct CacheTest : BaseCacheTest<VirtAddr, CLine>
{
    inline static const int num_ways{12};
    CacheTest()
        : BaseCacheTest<VirtAddr, CLine>{
              {"cache.ways", num_ways},
              {"cache.sets", 8},
          }
    {
    }
};

TEST_F(CacheTest, Constructor) {}

TEST_F(CacheTest, AddressBits)
{
    ASSERT_EQ(0, compute_num_address_bits(1));
    ASSERT_EQ(1, compute_num_address_bits(2));
    ASSERT_ANY_THROW(compute_num_address_bits(3));
    ASSERT_EQ(2, compute_num_address_bits(4));
    ASSERT_EQ(3, compute_num_address_bits(8));
    ASSERT_EQ(31, compute_num_address_bits(0x80000000));
    ASSERT_EQ(VirtAddr::LINE_BITWIDTH, compute_num_address_bits(VirtAddr::LINE_SIZE));
}

TEST_F(CacheTest, GetSets)
{
    EXPECT_EQ(cache.get_num_sets(), 8);
    EXPECT_EQ(cache.get_sets().size(), 8);
    EXPECT_EQ(cache.get_set_bits(), 3);
}

TEST_F(CacheTest, GetWays)
{
    EXPECT_EQ(cache.get_num_ways(), num_ways);
}

TEST_F(CacheTest, LineBits)
{
    EXPECT_EQ(cache.get_line_bits(), 6);
}

TEST_F(CacheTest, Set)
{
    EXPECT_EQ(cache.compute_set(0x0_va), 0UL);
    EXPECT_EQ(cache.compute_set(0x3f_va), 0UL);
    EXPECT_EQ(cache.compute_set(0x40_va), 1UL);
    EXPECT_EQ(cache.compute_set(0x60_va), 1UL);
    EXPECT_EQ(cache.compute_set(0x80_va), 2UL);
    EXPECT_EQ(cache.compute_set(0xa0_va), 2UL);
    EXPECT_EQ(cache.compute_set(0xc0_va), 3UL);
    EXPECT_EQ(cache.compute_set(0xe0_va), 3UL);
    EXPECT_EQ(cache.compute_set(0x100_va), 4UL);
    EXPECT_EQ(cache.compute_set(0x1000_va), 0UL);
}

TEST_F(CacheTest, Tag)
{
    EXPECT_EQ(cache.compute_tag(0x1_va), 0ULL);
    EXPECT_EQ(cache.compute_tag(0x40_va), 0ULL);
    EXPECT_EQ(cache.compute_tag(0xffffffffffffffff_va), 0x7fffffffffffffULL);
}

TEST_F(CacheTest, RefillNoVictim)
{
    EXPECT_FALSE(cache.refill(transaction, 0x0_va, CLine(1)));
}

TEST_F(CacheTest, RefillWithVictim)
{
    fill_set(0);
    auto victim = cache.refill(transaction, 0xff0000_va, CLine(1));
    EXPECT_TRUE(victim);
    EXPECT_EQ(victim->id, 0);
}

TEST_F(CacheTest, RefillDuplicate)
{
    EXPECT_FALSE(cache.refill(transaction, 0x0_va, CLine(1)));
    EXPECT_AMPERE_FATAL(cache.refill(transaction, 0x0_va, CLine(1)), HasSubstr("contains a duplicate"));
}

TEST_F(CacheTest, WouldFillWithoutVictim)
{
    EXPECT_TRUE(cache.would_fill_without_victim(0x0_va));
    fill_set(0, num_ways - 1);
    EXPECT_TRUE(cache.would_fill_without_victim(0x0_va));

    fill_set(1);
    EXPECT_TRUE(cache.would_fill_without_victim(0x40_va));
    EXPECT_FALSE(cache.would_fill_without_victim(0xff0040_va));
}

TEST_F(CacheTest, NextVictim)
{
    fill_set(0);
    auto victim = cache.next_victim(0xff0000_va);
    EXPECT_TRUE(victim);
    EXPECT_EQ(victim->id, 0);
}

TEST_F(CacheTest, InvalidateNothing)
{
    EXPECT_FALSE(cache.invalidate(transaction, 0x0_va));
}

TEST_F(CacheTest, Invalidate)
{
    fill_set(0);
    EXPECT_FALSE(cache.would_fill_without_victim(0xff0000_va));

    auto e = cache.invalidate(transaction, 0x0_va);
    EXPECT_TRUE(e);
    EXPECT_EQ(e->id, 0);

    EXPECT_TRUE(cache.would_fill_without_victim(0xff0000_va));
}

TEST_F(CacheTest, Find)
{
    EXPECT_FALSE(cache.find(0x0_va));
    cache.refill(transaction, 0x0_va, CLine(1));

    auto e = cache.find(0x0_va);
    EXPECT_TRUE(e);
    EXPECT_EQ(e->id, 1);
    EXPECT_EQ(e.way(), 0);
}

TEST_F(CacheTest, Lookup)
{
    EXPECT_FALSE(cache.lookup(transaction, 0x0_va));
    cache.refill(transaction, 0x0_va, CLine(1));

    auto e = cache.lookup(transaction, 0x0_va);
    EXPECT_TRUE(e);
    EXPECT_EQ(e->id, 1);
}

TEST_F(CacheTest, LookupAndModify)
{
    cache.refill(transaction, 0x0_va, CLine(1));
    auto a = cache.lookup(transaction, 0x0_va);
    a->id = 2;

    auto b = cache.lookup(transaction, 0x0_va);
    EXPECT_TRUE(b);
    EXPECT_EQ(b->id, 2);
}

TEST_F(CacheTest, UpdateValueMissing)
{
    cache.update_value(transaction, 0x0_va, {});
}

TEST_F(CacheTest, UpdateValue)
{
    cache.refill(transaction, 0x0_va, CLine(1));

    auto e = *cache.find(0x0_va);
    e.id = 3;
    cache.update_value(transaction, 0x0_va, e);

    EXPECT_EQ(cache.find(0x0_va)->id, 3);
}

TEST_F(CacheTest, RefillMoreThanNumWays)
{
    auto address = 0x1000000_va;
    for (int i = 0; i < num_ways; i++) {
        ASSERT_FALSE(cache.refill(transaction, address, CLine(i)));
        auto result = cache.lookup(transaction, address);
        ASSERT_TRUE(result);
        ASSERT_EQ(result->id, i);
        ASSERT_EQ(result.way(), i);
        address -= 1024;
    }
    auto victim = cache.refill(transaction, address, CLine(99));
    ASSERT_TRUE(victim);
    ASSERT_EQ(victim->id, 0);
    ASSERT_TRUE(cache.lookup(transaction, address));
    ASSERT_EQ(cache.lookup(transaction, address)->id, 99);

    auto result = cache.lookup(transaction, 0x1000000_va);
    ASSERT_FALSE(result);
}

TEST_F(CacheTest, SetContents)
{
    std::list<CLine> expected;
    for (unsigned way = 0; way < num_ways; ++way)
        expected.emplace_back(way);

    fill_set(0);
    auto values = cache.set_contents(0x0_va);
    EXPECT_EQ(values, expected);
}

TEST_F(CacheTest, Clear)
{
    fill_set(0);
    fill_set(1);
    cache.clear();

    EXPECT_TRUE(cache.set_contents(0x0_va).empty());
    EXPECT_TRUE(cache.set_contents(0x40_va).empty());
}

struct CacheInvalidateDuplicatesTest : BaseCacheTest<VirtAddr, CLine>
{
    CacheInvalidateDuplicatesTest()
        : BaseCacheTest<VirtAddr, CLine>{
              {"cache.size", 4096},
              {"cache.ways", 4},
              {"cache.invalidate_duplicates_before_fill", true},
          }
    {
    }
};

TEST_F(CacheInvalidateDuplicatesTest, Lookup)
{
    ASSERT_FALSE(cache.lookup(transaction, 0_va));
    cache.refill(transaction, 0_va, CLine(1));
    ASSERT_TRUE(cache.lookup(transaction, 0_va));
    ASSERT_TRUE(cache.lookup(transaction, 0x10_va));
    ASSERT_FALSE(cache.lookup(transaction, 0x40_va));

    ASSERT_FALSE(cache.lookup(transaction, 0xc0de0000_va));
    cache.refill(transaction, 0xc0de0000_va, CLine(1));
    ASSERT_TRUE(cache.lookup(transaction, 0xc0de0000_va));
    ASSERT_TRUE(cache.lookup(transaction, 0_va));

    ASSERT_FALSE(cache.lookup(transaction, 0xffffffffffffffff_va));
    cache.refill(transaction, 0xffffffffffffffff_va, CLine(1));
    ASSERT_TRUE(cache.lookup(transaction, 0xffffffffffffffff_va));

    VirtAddr address(0x1000000);
    for (int i = 0; i < 5; i++) {
        cache.refill(transaction, address, CLine(1));
        ASSERT_TRUE(cache.lookup(transaction, address));
        ASSERT_TRUE(cache.lookup(transaction, 0x1000000_va));
        // address -= 1024;
    }
    cache.refill(transaction, address, CLine(1));
    ASSERT_TRUE(cache.lookup(transaction, address));
    ASSERT_TRUE(cache.lookup(transaction, 0x1000000_va));
}

struct CacheDirectMappedTest : BaseCacheTest<VirtAddr, CLine>
{
    CacheDirectMappedTest()
        : BaseCacheTest<VirtAddr, CLine>{
              {"cache.ways", 1},
              {"cache.sets", 4},
              {"cache.tag_bits", 8},
          }
    {
    }
};

TEST_F(CacheDirectMappedTest, TagConflict)
{
    cache.refill(transaction, 0xAAAAA_va, CLine(1));

    ASSERT_TRUE(cache.lookup(transaction, 0xBAAAA_va));
    ASSERT_TRUE(cache.lookup(transaction, 0xAAAAA_va));
    ASSERT_FALSE(cache.lookup(transaction, 0xBAAA_va));
}

struct CacheSingleEntryTest : BaseCacheTest<VirtAddr, CLine>
{
    CacheSingleEntryTest()
        : BaseCacheTest<VirtAddr, CLine>{
              {"cache.size", 1},
              {"cache.ways", 1},
              {"cache.line_size", 1},
          }
    {
    }
};

TEST_F(CacheSingleEntryTest, RefillAndLookup)
{
    ASSERT_FALSE(cache.lookup(transaction, 0x100_va));
    cache.refill(transaction, 0x100_va, CLine(1));
    ASSERT_TRUE(cache.lookup(transaction, 0x100_va));
}

struct PerfectCacheTest : BaseCacheTest<VirtAddr, CLine>
{
    PerfectCacheTest()
        : BaseCacheTest<VirtAddr, CLine>{
              {"cache.size", 4096},
              {"cache.ways", 4},
              {"cache.enable_perfect_mode", true},
          }
    {
    }
};

TEST_F(PerfectCacheTest, Lookup)
{
    EXPECT_THAT(cache.lookup(transaction, 0_va), Optional(CLine{}));
}

TEST_F(PerfectCacheTest, LookupAndRefill)
{
    EXPECT_THAT(cache.lookup(transaction, 0_va), Optional(CLine{}));

    cache.refill(transaction, 0_va, CLine(1));
    EXPECT_THAT(cache.lookup(transaction, 0_va), Optional(CLine{1}));
    EXPECT_THAT(cache.lookup(transaction, 0x10_va), Optional(CLine{1}));
    EXPECT_THAT(cache.lookup(transaction, 0x40_va), Optional(CLine{}));
}

TEST_F(PerfectCacheTest, LookupAndModify)
{
    auto result = cache.lookup(transaction, 0_va);
    result->id = 5;
    EXPECT_THAT(cache.lookup(transaction, 0_va), Optional(CLine{}));
}

TEST_F(PerfectCacheTest, SameSetLookupAndRefill)
{
    cache.refill(transaction, 0_va, CLine(1));
    cache.refill(transaction, 0xc0de0000_va, CLine(2));

    EXPECT_THAT(cache.lookup(transaction, 0xc0de0000_va), Optional(CLine{2}));
    EXPECT_THAT(cache.lookup(transaction, 0_va), Optional(CLine{1}));
}

TEST_F(PerfectCacheTest, Evict)
{
    VirtAddr address(0x1000000);
    for (int i = 0; i < 5; i++) {
        cache.refill(transaction, address, CLine(i + 1));
        EXPECT_THAT(cache.lookup(transaction, address), Optional(CLine(i + 1)));
        address += 1024;
    }

    EXPECT_THAT(cache.lookup(transaction, 0x1000000_va), Optional(CLine{}));
}

struct AntiPerfectCacheTest : BaseCacheTest<VirtAddr, CLine>
{
    AntiPerfectCacheTest()
        : BaseCacheTest<VirtAddr, CLine>{
              {"cache.size", 4096},
              {"cache.ways", 4},
              {"cache.enable_antiperfect_mode", true},
          }
    {
    }
};

TEST_F(AntiPerfectCacheTest, Lookup)
{
    ASSERT_FALSE(cache.lookup(transaction, 0_va));
    ASSERT_FALSE(cache.lookup(transaction, 0xc0de0000_va));
}

TEST_F(AntiPerfectCacheTest, RefillAndLookup)
{
    cache.refill(transaction, 0_va, CLine(1));
    ASSERT_FALSE(cache.lookup(transaction, 0_va));
    ASSERT_FALSE(cache.lookup(transaction, 0x10_va));
    ASSERT_FALSE(cache.lookup(transaction, 0x40_va));

    cache.refill(transaction, 0xc0de0000_va, CLine(1));
    ASSERT_FALSE(cache.lookup(transaction, 0xc0de0000_va));
    ASSERT_FALSE(cache.lookup(transaction, 0_va));
}

struct TLBTest : BaseCacheTest<VirtAddr, PhysAddr>
{
    TLBTest() : BaseCacheTest<VirtAddr, PhysAddr>{{"cache.sets", 2}, {"cache.ways", 4}} {}
};

TEST_F(TLBTest, RefillAndLookup)
{
    cache.refill(transaction, 0xffff0000_va, 0xffff_pa);
    ASSERT_TRUE(cache.lookup(transaction, 0xffff0000_va));
    ASSERT_EQ(*cache.lookup(transaction, 0xffff0000_va), 0xffff_pa);
}

struct FullyAssociativeTLBTest : BaseCacheTest<VirtAddr, PhysAddr>
{
    FullyAssociativeTLBTest()
        : BaseCacheTest<VirtAddr, PhysAddr>{
              {"cache.sets", 1},
              {"cache.ways", 4},
              {"cache.line_size", 4096},
          }
    {
    }
};

TEST_F(FullyAssociativeTLBTest, RefillAndLookup)
{
    cache.refill(transaction, 0xffff0000_va, 0xffff_pa);
    ASSERT_TRUE(cache.lookup(transaction, 0xffff0000_va));
    ASSERT_EQ(*cache.lookup(transaction, 0xffff0000_va), 0xffff_pa);
    ASSERT_TRUE(cache.lookup(transaction, 0xffff0800_va));
    ASSERT_FALSE(cache.lookup(transaction, 0xffff1000_va));

    cache.refill(transaction, 0xeeeeeeee_va, 0x11111111_pa);
    ASSERT_TRUE(cache.lookup(transaction, 0xeeeeeeee_va));
    ASSERT_TRUE(cache.lookup(transaction, 0xeeeee000_va));
    ASSERT_EQ(*cache.lookup(transaction, 0xeeeee000_va), 0x11111111_pa);
}

struct NonDefaultLine
{
    explicit NonDefaultLine(int value) : value(value) {}
    int value;
};

struct NonDefaultCacheTest : BaseCacheTest<VirtAddr, NonDefaultLine>
{
    NonDefaultCacheTest()
        : BaseCacheTest<VirtAddr, NonDefaultLine>({
              {"other_cache.enable_perfect_mode", true},
          })
    {
    }
};

TEST_F(NonDefaultCacheTest, Construct) {}

TEST_F(NonDefaultCacheTest, PerfectThrows)
{
    using cache_t = Cache<VirtAddr, NonDefaultLine>;
    EXPECT_ANY_THROW({ cache_t other{"other_cache"}; });
}

} // namespace
