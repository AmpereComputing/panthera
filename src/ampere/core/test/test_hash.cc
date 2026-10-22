//
// Copyright (c) 2018-2020 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//

#include <gtest/gtest.h>
#include "ampere/archx/addr.h"
#include "ampere/core/hash.h"

namespace ampere {
namespace hash {

using namespace ampere::core;
using namespace ampere::archx;

uint64_t reduction_xor(uint64_t in, unsigned low_bit, unsigned high_bit)
{
    uint64_t res = in, shifted = in;
    for (int i = 0; i < high_bit - low_bit; i++) {
        shifted = shifted << 1;
        res = res ^ shifted;
    }
    return (res >> high_bit) & 0b1;
}

uint64_t rtl_bank_hash(PhysAddr pa)
{
    uint64_t index = pa.extract_range(11, 6);
    // from get_bank_hash() in <siryn_ws>/MAIN/dv/lib/Subsystems/cpu/sv/units/l2c/l2c_types.svh
    return reduction_xor(0b101001 & index, 0, 5) | (reduction_xor(0b111010 & index, 0, 5) << 1)
           | (reduction_xor(0b110100 & index, 0, 5) << 2);
}

TEST(testHash, testReductionXor)
{
    ASSERT_EQ(reduction_xor(0b11, 0, 1), 0);
    ASSERT_EQ(reduction_xor(0b00, 0, 1), 0);
    ASSERT_EQ(reduction_xor(0b10, 0, 1), 1);
    ASSERT_EQ(reduction_xor(0b01, 0, 1), 1);
    ASSERT_EQ(reduction_xor(0b1110, 1, 3), 1);
    ASSERT_EQ(reduction_xor(0b1111, 1, 3), 1);
    ASSERT_EQ(reduction_xor(0b0010, 2, 3), 0);
    ASSERT_EQ(reduction_xor(0b0011, 2, 3), 0);
    ASSERT_EQ(reduction_xor(0b010000, 0, 5), 1);
    ASSERT_EQ(reduction_xor(0b101100, 0, 5), 1);
    ASSERT_EQ(reduction_xor(0b000101, 0, 5), 0);
    ASSERT_EQ(reduction_xor(0b101010, 0, 5), 1);
}

TEST(testHash, testRTLBankHash)
{
    ASSERT_EQ(rtl_bank_hash(0x827f2_pa), 0b010);
    ASSERT_EQ(rtl_bank_hash(0x9a5d987c_pa), 0b110);
    ASSERT_EQ(rtl_bank_hash(0x8913a88_pa), 0b110);
    ASSERT_EQ(rtl_bank_hash(0x8137b64d7a8f1_pa), 0b100);
    ASSERT_EQ(rtl_bank_hash(0x491_pa), 0b100);
    ASSERT_EQ(rtl_bank_hash(0x995_pa), 0b001);
    ASSERT_EQ(rtl_bank_hash(0x93f_pa), 0b011);
}

/*
 * Ensure that the RauHash being used in panthera in the default configuration
 * is identical to the current RTL implementation.
 */
TEST(testHash, testRTLHashEquivalence)
{
    unsigned high = 11, low = 6;
    unsigned rau_polynomial = 11;

    RauHash rau_hash(high - low + 1, rau_polynomial);

    for (int i = 0; i < (1ul << (high - low + 2)) - 1; i++) {
        auto pa = PhysAddr(i << low);
        ASSERT_EQ(rau_hash(pa.extract_range(high, low)), rtl_bank_hash(pa));
    }
}

TEST(testHash, testConstructor)
{
    RauHash hash(8, 11);
    ASSERT_EQ(hash.get_order(), 3);
}

TEST(testHash, testHashing)
{
    RauHash hash(8, 11);

    ASSERT_EQ(hash(0), 0);
    ASSERT_EQ(hash(7), 7);

    ASSERT_EQ(hash(71), 2);
    ASSERT_EQ(hash(127), 0);
    ASSERT_EQ(hash(143), 5);
    ASSERT_EQ(hash(144), 7);
}

} // namespace hash
} // namespace ampere
