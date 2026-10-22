//
// Copyright (c) 2021 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//

#include <gtest/gtest.h>
#include "ampere/archx/prefetch_type.h"
#include "ampere/archx/prefetch_address.h"

namespace ampere::prefetch_address {

using namespace ampere::archx;

TEST(testPrefetchAddress, testImmediateAddress)
{
    // [ 0x005486d0:0x13f8886d0 ]: 0xf9800020 (43012)      ; PRFM  0, [X1]
    // X1 <- 0x2abf1ac0
    VirtAddr prfm_address = prefetch_immediate_address(0xf9800020, 0x2abf1ac0);
    ASSERT_EQ(prfm_address, VirtAddr(0x2abf1ac0));

    // 8-byte offset
    prfm_address = prefetch_immediate_address(0xf9800420, 0x2abf1ac0);
    ASSERT_EQ(prfm_address, VirtAddr(0x2abf1ac8));

    // 32760-byte offset
    prfm_address = prefetch_immediate_address(0xf9bffc20, 0x2abf1ac0);
    ASSERT_EQ(prfm_address, VirtAddr(0x2abf9ab8));
}

TEST(testPrefetchAddress, testLiteralAddress)
{
    // no offset from PC
    VirtAddr prfm_address = prefetch_literal_address(ProgramCounter(0x484d20), 0xd8000000);
    ASSERT_EQ(prfm_address, VirtAddr(0x484d20));

    // +4 offset from PC
    prfm_address = prefetch_literal_address(ProgramCounter(0xfffea1550010), 0xd8000020);
    ASSERT_EQ(prfm_address, VirtAddr(0xfffea1550014));

    // (4 bytes less than) +1Mb offset from PC
    prfm_address = prefetch_literal_address(ProgramCounter(0xfffea1550010), 0xd87fffe0);
    ASSERT_EQ(prfm_address, VirtAddr(0xfffea165000c));

    // -1Mb offset from PC
    prfm_address = prefetch_literal_address(ProgramCounter(0xfffea1550010), 0xd8800000);
    ASSERT_EQ(prfm_address, VirtAddr(0xfffea1450010));

    // -512Mb offset from PC
    prfm_address = prefetch_literal_address(ProgramCounter(0xfffea1550010), 0xd8c00000);
    ASSERT_EQ(prfm_address, VirtAddr(0xfffea14d0010));
}

TEST(testPrefetchAddress, testRegisterAddress)
{
    // [ 0x00484d20:0x13f924d20 ]: 0xf8a07880 (788676)     ; PRFM  0, [X4, X0, LSL #3]
    // X4 <- 0xfffea1570010
    // X0 <- 0x00000a50
    VirtAddr prfm_address = prefetch_register_address(0xf8a07880, 0xfffea1570010, 0x00000a50);
    ASSERT_EQ(prfm_address, VirtAddr(0xfffea1575290));

    // [ 0x00484d24:0x13f924d24 ]: 0xf8ad6980 (788677)     ; PRFM  0, [X12, X13, LSL #0]
    // X12 <- 0xfffea1550010
    // X13 <- 0x000014a0
    prfm_address = prefetch_register_address(0xf8ad6980, 0xfffea1550010, 0x000014a0);
    ASSERT_EQ(prfm_address, VirtAddr(0xfffea15514b0));

    // 0xf8ad6980 PRFM  0, [X12, X13, LSL #0]
    prfm_address = prefetch_register_address(0xf8ad6980, 0xfffea1550010, 0x000014a0);
    ASSERT_EQ(prfm_address, VirtAddr(0xfffea15514b0));

    // 0xf8a07880 PRFM  0, [X4, X0, SXTX #3]
    // X4 <- 0xfffea1570010
    // X0 <- 0x00000a50
    prfm_address = prefetch_register_address(0xf8a0f880, 0xfffea1570010, 0x00000a50);
    ASSERT_EQ(prfm_address, VirtAddr(0xfffea1575290));

    // half-width register signed (positive)
    // 0xf8a0d880 PRFM  0, [X4, X0, SXTW #3]
    prfm_address = prefetch_register_address(0xf8a0d880, 0xfffea1570010, 0xa50);
    ASSERT_EQ(prfm_address, VirtAddr(0xfffea1575290));

    // half-width register signed (negative)
    // 0xf8a0d880 PRFM  0, [X4, X0, SXTW #3]
    prfm_address = prefetch_register_address(0xf8a0d880, 0xfffea1570010, 0xfffffff0);
    ASSERT_EQ(prfm_address, VirtAddr(0xfffea156ff90));

    // half-width register signed (negative)
    // 0xf8a0c880 PRFM  0, [X4, X0, SXTW #0]
    prfm_address = prefetch_register_address(0xf8a0c880, 0xfffea1570010, 0xfffffff0);
    ASSERT_EQ(prfm_address, VirtAddr(0xfffea1570000));

    // half-width register unsigned, shifted
    // 0xf8a05880 PRFM  0, [X4, X0, UXTW #3]
    prfm_address = prefetch_register_address(0xf8a05880, 0xfffea1570010, 0xfffffff0);
    ASSERT_EQ(prfm_address, VirtAddr(0x10006a156ff90));

    // 0xf8a04880 PRFM  0, [X4, X0, UXTW #0]
    prfm_address = prefetch_register_address(0xf8a04880, 0xfffea1570010, 0x48);
    ASSERT_EQ(prfm_address, VirtAddr(0xfffea1570058));

    // full-width register signed, shifted
    // 0xf8a0f880 PRFM  0, [X4, X0, SXTX #3]
    prfm_address = prefetch_register_address(0xf8a0f880, 0xfffea1570010, 0xfffffff0);
    ASSERT_EQ(prfm_address, VirtAddr(0x10006a156ff90));

    // full-width register unsigned, shifted
    // 0xf8a07880 PRFM  0, [X4, X0, LSL #3]
    prfm_address = prefetch_register_address(0xf8a07880, 0xfffea1570010, 0xfffffff0);
    ASSERT_EQ(prfm_address, VirtAddr(0x10006a156ff90));

    // full-width register signed, unshifted
    // 0xf8a0e880 PRFM  0, [X4, X0, SXTX #0]
    prfm_address = prefetch_register_address(0xf8a0e880, 0xfffea1570010, 0xfefeabab);
    ASSERT_EQ(prfm_address, VirtAddr(0xffffa055abbb));

    // full-width register signed, unshifted
    // 0xf8a0e880 PRFM  0, [X4, X0, SXTX #0]
    prfm_address = prefetch_register_address(0xf8a0e880, 0xfffea1570010, 0xfffffffffffffff0);
    ASSERT_EQ(prfm_address, VirtAddr(0xfffea1570000));

    // full-width register signed, shifted
    // 0xf8a0f880 PRFM  0, [X4, X0, SXTX #3]
    prfm_address = prefetch_register_address(0xf8a0f880, 0xfffea1570010, 0xfffffffffffffff0);
    ASSERT_EQ(prfm_address, VirtAddr(0xfffea156ff90));

    // full-width register signed, unshifted
    // 0xf8a0e880 PRFM  0, [X4, X0, SXTX #0]
    prfm_address = prefetch_register_address(0xf8a0e880, 0xfffea1570010, 0xffff001000000000);
    ASSERT_EQ(prfm_address, VirtAddr(0xea1570010));

    // full-width register unsigned, unshifted
    // 0xf8a06880 PRFM  0, [X4, X0, LSL #0]
    prfm_address = prefetch_register_address(0xf8a06880, 0xfffea1570010, 0x1000000000);
    ASSERT_EQ(prfm_address, VirtAddr(0x1000ea1570010));

    // full-width register unsigned, shifted
    // 0xf8a07880 PRFM  0, [X4, X0, LSL #3]
    prfm_address = prefetch_register_address(0xf8a07880, 0xfffea1570010, 0x200000000);
    ASSERT_EQ(prfm_address, VirtAddr(0x1000ea1570010));
}

TEST(testPrefetchAddress, testUnscaledAddress)
{
    // 0xf880c100 PRFUM 00, [X8, #12]
    VirtAddr prfm_address = prefetch_unscaled_address(0xf880c100, 0xfffea15cf790);
    ASSERT_EQ(prfm_address, VirtAddr(0xfffea15cf79c));

    // 0xf88ff220 PRFUM 00, [X17, #255]
    prfm_address = prefetch_unscaled_address(0xf88ff220, 0x40a77c);
    ASSERT_EQ(prfm_address, VirtAddr(0x40a87b));

    // 0xf89ff100 PRFUM 00, [X8, #-1]
    prfm_address = prefetch_unscaled_address(0xf89ff100, 0x400000);
    ASSERT_EQ(prfm_address, VirtAddr(0x3fffff));

    // 0xf8900100 PRFUM 00, [X8, #-256]
    prfm_address = prefetch_unscaled_address(0xf8900100, 0x400000);
    ASSERT_EQ(prfm_address, VirtAddr(0x3fff00));
}

} // namespace ampere::prefetch_address

/*
 * vim: sw=4:ts=4:et:si:cindent:cino=\:0,(0,g0,t0:
 */
