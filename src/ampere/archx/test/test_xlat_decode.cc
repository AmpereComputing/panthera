//
// Copyright (c) 2019 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//
#include <gtest/gtest.h>
#include "test_xlat.h"

namespace ampere::archx {

TEST(TestXlatDecode, MovzMovkFusion)
{
    std::uint32_t insns[2] = {0b110100101u << 23, 0b111100101u << 23};

    auto uops = test_xlat::decode_fusion(insns, true);
    EXPECT_EQ(uops.second.size(), 1);
}

} // namespace ampere::archx
