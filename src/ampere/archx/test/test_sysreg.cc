//
// Copyright (c) 2021 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//

#include <gtest/gtest.h>
#include "ampere/archx/sysreg.h"

namespace ampere::sysreg {

using namespace ampere::archx;

TEST(testSysReg, testConstructor)
{
    constexpr SysReg test_reg(1, 2, 3, 4, 5);
    ASSERT_EQ(test_reg.op0, 1);
    ASSERT_EQ(test_reg.op1, 2);
    ASSERT_EQ(test_reg.crn, 3);
    ASSERT_EQ(test_reg.crm, 4);
    ASSERT_EQ(test_reg.op2, 5);
}

TEST(testSysReg, testEquality)
{
    constexpr SysReg reg1(1, 2, 3, 4, 5);
    constexpr SysReg reg2(1, 2, 3, 4, 5);
    constexpr SysReg reg3(2, 2, 3, 4, 5);
    constexpr SysReg reg4(1, 2, 8, 4, 5);

    ASSERT_TRUE(reg1 == reg2);
    ASSERT_FALSE(reg1 == reg3);
    ASSERT_FALSE(reg2 == reg4);
    ASSERT_TRUE(VTCR_EL2 == VTCR_EL2);
    ASSERT_FALSE(VTCR_EL2 == TCR_EL2);
}

TEST(testSysReg, testLessThan)
{
    constexpr SysReg reg1(1, 2, 3, 4, 5);
    constexpr SysReg reg2(1, 2, 3, 4, 5);
    constexpr SysReg reg3(2, 2, 3, 4, 5);
    constexpr SysReg reg4(1, 2, 8, 4, 5);

    ASSERT_FALSE(reg1 < reg2);
    ASSERT_FALSE(reg2 < reg1);
    ASSERT_TRUE(reg1 < reg3);
    ASSERT_TRUE(reg1 < reg4);
    ASSERT_TRUE(SCXTNUM_EL1 < SCXTNUM_EL3);
    ASSERT_TRUE(TCR_EL1 < TCR_EL3);
}

TEST(testSysReg, testAddr)
{
    ASSERT_EQ(HCR_EL2.addr(), 0b1110000010001000);
    ASSERT_EQ(TCR_EL1.addr(), 0b1100000100000010);
}

TEST(testSysReg, testInstructionIdentification)
{
    // Do we properly identify MRS instructions
    ASSERT_TRUE(IS_MRS(0xd53bd042)); // mrs   x2, tpidr_el0
    ASSERT_TRUE(IS_MRS(0xd5384115)); // mrs   x21, sp_el0
    ASSERT_TRUE(IS_MRS(0xd5382021)); // mrs   x1, ttbr1_el1

    // Do we properly identify MSR instructions
    ASSERT_TRUE(IS_MSR(0xd5184016)); // msr   spsr_el1, x22
    ASSERT_TRUE(IS_MSR(0xd5182021)); // msr  ttbr1_el1, x1
    ASSERT_TRUE(IS_MSR(0xd5184035)); // msr    elr_el1, x21

    // Do we properly *not* identify non-MRS instructions
    ASSERT_FALSE(IS_MRS(0xd5033a9f)); // dsb   ishst
    ASSERT_FALSE(IS_MRS(0xd5033fdf)); // isb
    ASSERT_FALSE(IS_MRS(0x540000a1)); // b.ne  #0xffff000010eaeee8
    ASSERT_FALSE(IS_MRS(0xd503201f)); // nop
    ASSERT_FALSE(IS_MRS(0xd5184016)); // msr   spsr_el1, x22
    ASSERT_FALSE(IS_MRS(0xd5182021)); // msr  ttbr1_el1, x1
    ASSERT_FALSE(IS_MRS(0xd5184035)); // msr    elr_el1, x21
    ASSERT_FALSE(IS_MRS(0xd50b7395)); // cfp rctx

    // Do we properly *not* identify non-MSR instructions
    ASSERT_FALSE(IS_MSR(0xd5033a9f)); // dsb   ishst
    ASSERT_FALSE(IS_MSR(0xd5033fdf)); // isb
    ASSERT_FALSE(IS_MSR(0x540000a1)); // b.ne  #0xffff000010eaeee8
    ASSERT_FALSE(IS_MSR(0xd503201f)); // nop
    ASSERT_FALSE(IS_MSR(0xd53bd042)); // mrs   x2, tpidr_el0
    ASSERT_FALSE(IS_MSR(0xd5384115)); // mrs   x21, sp_el0
    ASSERT_FALSE(IS_MSR(0xd5382021)); // mrs   x1, ttbr1_el1
    ASSERT_FALSE(IS_MSR(0xd50b7395)); // cfp rctx
}

TEST(testSysReg, testSysRegIdentification)
{
    // Do we properly identify MRS instructions and register reads
    ASSERT_TRUE(IS_MRS_REG(0xd53bd042, TPIDR_EL0));     // mrs   x2, tpidr_el0
    ASSERT_TRUE(IS_MRS_REG(0xd5384115, SP_EL0));        // mrs   x21, sp_el0
    ASSERT_TRUE(IS_MRS_REG(0xd5382021, TTBR1_EL1));     // mrs   x1, ttbr1_el1
    ASSERT_TRUE(IS_SYSREG_READ(0xd53bd042, TPIDR_EL0)); // mrs   x2, tpidr_el0
    ASSERT_TRUE(IS_SYSREG_READ(0xd5384115, SP_EL0));    // mrs   x21, sp_el0
    ASSERT_TRUE(IS_SYSREG_READ(0xd5382021, TTBR1_EL1)); // mrs   x1, ttbr1_el1

    // Do we properly identify MSR instructions and register writes
    ASSERT_TRUE(IS_MSR_REG(0xd5184016, SPSR_EL1));       // msr   spsr_el1, x22
    ASSERT_TRUE(IS_MSR_REG(0xd5182021, TTBR1_EL1));      // msr  ttbr1_el1, x1
    ASSERT_TRUE(IS_MSR_REG(0xd5184035, ELR_EL1));        // msr    elr_el1, x21
    ASSERT_TRUE(IS_SYSREG_WRITE(0xd5184016, SPSR_EL1));  // msr   spsr_el1, x22
    ASSERT_TRUE(IS_SYSREG_WRITE(0xd5182021, TTBR1_EL1)); // msr  ttbr1_el1, x1
    ASSERT_TRUE(IS_SYSREG_WRITE(0xd5184035, ELR_EL1));   // msr    elr_el1, x21

    // Do we properly identify other system instructions
    ASSERT_TRUE(IS_SYS_OP(0xd50b7395, CFP_RCTX)); // cfp rctx

    // Do we properly *not* identify non-MRS instructions
    ASSERT_FALSE(IS_MRS_REG(0xd5184016, SPSR_EL1));  // msr   spsr_el1, x22
    ASSERT_FALSE(IS_MRS_REG(0xd5182021, TTBR1_EL1)); // msr  ttbr1_el1, x1
    ASSERT_FALSE(IS_MRS_REG(0xd5184035, ELR_EL1));   // msr    elr_el1, x21
    ASSERT_FALSE(IS_MRS_REG(0xd50b7395, CFP_RCTX));  // cfp rctx

    // Do we properly *not* identify non-MSR instructions
    ASSERT_FALSE(IS_MSR_REG(0xd53bd042, TPIDR_EL0)); // mrs   x2, tpidr_el0
    ASSERT_FALSE(IS_MSR_REG(0xd5384115, SP_EL0));    // mrs   x21, sp_el0
    ASSERT_FALSE(IS_MSR_REG(0xd5382021, TTBR1_EL1)); // mrs   x1, ttbr1_el1
    ASSERT_FALSE(IS_MSR_REG(0xd50b7395, CFP_RCTX));  // cfp rctx

    // Do we properly *not* identify the wrong register
    ASSERT_FALSE(IS_MRS_REG(0xd53bd042, TPIDR_EL1));   // mrs   x2, tpidr_el0
    ASSERT_FALSE(IS_MRS_REG(0xd5384115, SP_EL2));      // mrs   x21, sp_el0
    ASSERT_FALSE(IS_MRS_REG(0xd5382021, TTBR0_EL1));   // mrs   x1, ttbr1_el1
    ASSERT_FALSE(IS_MSR_REG(0xd5184016, VTTBR_EL2));   // msr   spsr_el1, x22
    ASSERT_FALSE(IS_MSR_REG(0xd5182021, TTBR1_EL2));   // msr  ttbr1_el1, x1
    ASSERT_FALSE(IS_MSR_REG(0xd5184035, SCXTNUM_EL1)); // msr    elr_el1, x21
    ASSERT_FALSE(IS_SYS_OP(0xd50b7395, CPP_RCTX));     // cfp rctx
}

} // namespace ampere::sysreg

/*
 * vim: sw=4:ts=4:et:si:cindent:cino=\:0,(0,g0,t0:
 */
