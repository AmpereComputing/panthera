//
// Copyright (c) 2022 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//

#include <gtest/gtest.h>

#include "ampere/mesh/pager.h"
#include "ampere/sc/sc.h"
#include "ampere/sc/testing/test_module.h"

namespace ampere::mesh {

class PagerTest : public sc::Test
{};

TEST_F(PagerTest, TestRandomPager)
{
    RandomPager pager{"pager"};

    sc::Random::seed(111);

    VirtAddr va1{0xdeadbeef};
    VirtAddr va2{0xdeadbe0d};
    VirtAddr va3{0xfacebeef};

    PhysAddr pa1 = pager.translate(va1, "default");
    PhysAddr pa2 = pager.translate(va2, "default");
    PhysAddr pa3 = pager.translate(va3, "default");

    EXPECT_EQ(pa1.extract_range(11, 0), 0xeeful);
    EXPECT_EQ(pa2.extract_range(11, 0), 0xe0dul);
    EXPECT_EQ(pa1.page_address_4k(), pa2.page_address_4k());
    EXPECT_NE(pa3.page_address_4k(), pa1.page_address_4k());
}

//
// Some quick tests to see if we're masking the right offset bits for different page sizes.
//
TEST_F(PagerTest, TestOtherPageSizes)
{
    Environment::get_root()->parameters.add_settings({{"pager2MB.page_size", "2MB"},
                                                      {"pager1GB.page_size", "1Gb"},
                                                      {"pager64KB.page_size", "64kb"},
                                                      {"pager512MB.page_size", "512MB"},
                                                      {"pager4TB.page_size", "4Tb"}},
                                                     "test bench");

    RandomPager pager2MB{"pager2MB"};
    RandomPager pager1GB{"pager1GB"};
    RandomPager pager64KB{"pager64KB"};
    RandomPager pager512MB{"pager512MB"};
    RandomPager pager4TB{"pager4TB"};

    sc::Random::seed(111);

    PhysAddr pa1 = pager2MB.translate(0xffffffffffff_va, "default");
    PhysAddr pa2 = pager1GB.translate(0xffffffffffff_va, "default");
    PhysAddr pa3 = pager64KB.translate(0xffffffffffff_va, "default");
    PhysAddr pa4 = pager512MB.translate(0xffffffffffff_va, "default");
    PhysAddr pa5 = pager4TB.translate(0xffffffffffff_va, "default");

    uint64_t mask1 = util::mask(sc::PageSize(sc::PageSize::Size::PS_2MB).page_bits());
    uint64_t mask2 = util::mask(sc::PageSize(sc::PageSize::Size::PS_1GB).page_bits());
    uint64_t mask3 = util::mask(sc::PageSize(sc::PageSize::Size::PS_64KB).page_bits());
    uint64_t mask4 = util::mask(sc::PageSize(sc::PageSize::Size::PS_512MB).page_bits());
    uint64_t mask5 = util::mask(sc::PageSize(sc::PageSize::Size::PS_4TB).page_bits());

    EXPECT_EQ(pa1.value() & mask1, mask1);
    EXPECT_EQ(pa2.value() & mask2, mask2);
    EXPECT_EQ(pa3.value() & mask3, mask3);
    EXPECT_EQ(pa4.value() & mask4, mask4);
    EXPECT_EQ(pa5.value() & mask5, mask5);
}

TEST_F(PagerTest, TestDifferentRegionsTranslateDifferently)
{
    RandomPager pager{"pager"};

    sc::Random::seed(5150);

    VirtAddr va{0xdeadbeef};

    PhysAddr pa1 = pager.translate(va, "region_1");
    PhysAddr pa2 = pager.translate(va, "region_2");

    EXPECT_NE(pa1, pa2);
}

} // namespace ampere::mesh
