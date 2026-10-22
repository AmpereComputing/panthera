//
// Copyright (c) 2023 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//

#include <gtest/gtest.h>

#include "ampere/core/mmu/tpc.h"

using ampere::core::TPCTag;

TEST(TPCTagTest, Granule64k)
{
    auto tag = TPCTag(0x1ff, 0x3ff, 0x1fff, 16 /*64k granule*/, 0x0, true);

    ASSERT_EQ(tag.l0_index(), 0x1ff);
    ASSERT_EQ(tag.l1_index(), 0x3ff);
    ASSERT_EQ(tag.l2_index(), 0x1fff);
    ASSERT_EQ(tag.granule_64k(), true);
}

TEST(TPCTagTest, Granule4k)
{
    auto tag = TPCTag(0x7a, 0x58, 0x16a0, 12 /*4k granule*/, 0x2, true);

    ASSERT_EQ(tag.l0_index(), 0x7a);
    ASSERT_EQ(tag.l1_index(), 0x58);
    ASSERT_EQ(tag.l2_index(), 0x16a0);
    ASSERT_EQ(tag.granule_64k(), false);
}

TEST(TPCTagTest, Granule16k)
{
    auto tag = TPCTag(0x101, 0x2, 0x4, 14 /*16k granule*/, 0x3, false);

    ASSERT_EQ(tag.l0_index(), 0x101);
    ASSERT_EQ(tag.l1_index(), 0x2);
    ASSERT_EQ(tag.l2_index(), 0x4);
    ASSERT_EQ(tag.granule_64k(), false);
}

/*
 * vim: sw=4:ts=4:et:si:cindent:cino=\:0,(0,g0,t0:
 */
