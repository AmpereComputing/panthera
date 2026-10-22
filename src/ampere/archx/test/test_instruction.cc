//
// Copyright (c) 2019 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//
#include "ampere/archx/driver_is.h"
#include <gtest/gtest.h>

namespace ampere {
namespace archx {

using namespace ::testing;

class InstructionTest : public Test
{
    void SetUp() override
    {
        is_instruction.mutable_program_counter()->set_va(0xf00f);
        is_instruction.set_opcode(0xbaba);
        archx::Instruction::set_decoders({"furiosa"});
    }

protected:
    ArchState state;
    ampere::is::Instruction is_instruction;
};

TEST_F(InstructionTest, testConstructor)
{
    is_instruction.add_memop()->set_va(0xdede);

    InstructionIS instruction(is_instruction, 1, state);

    ASSERT_EQ(ProgramCounter(0xf00f), instruction.program_counter());
    ASSERT_EQ(0xbaba, instruction.opcode());
    ASSERT_EQ(VirtAddr(0xdede), instruction.virtual_address());
}

TEST_F(InstructionTest, testExecute)
{
    ampere::is::Instruction is_instruction;
    InstructionIS instruction(is_instruction, 1, state);
    ASSERT_THROW(instruction.execute(), std::runtime_error);
}

TEST_F(InstructionTest, testBranchDecode)
{
    // d61f0220        br      x17
    is_instruction.set_opcode(0xd61f0220);
    InstructionIS br(is_instruction, 1, state);
    ASSERT_TRUE(br.is_branch());
    ASSERT_FALSE(is_call(br.branch_type()));
    ASSERT_FALSE(is_return(br.branch_type()));
    ASSERT_FALSE(is_direct(br.branch_type()));
    ASSERT_TRUE(is_indirect(br.branch_type()));
    ASSERT_FALSE(is_conditional(br.branch_type()));
    ASSERT_TRUE(is_unconditional(br.branch_type()));

    // 14000006        b       400c64
    is_instruction.set_opcode(0x14000006);
    InstructionIS b(is_instruction, 1, state);
    ASSERT_TRUE(b.is_branch());
    ASSERT_FALSE(is_call(b.branch_type()));
    ASSERT_FALSE(is_return(b.branch_type()));
    ASSERT_TRUE(is_direct(b.branch_type()));
    ASSERT_FALSE(is_indirect(b.branch_type()));
    ASSERT_FALSE(is_conditional(b.branch_type()));
    ASSERT_TRUE(is_unconditional(b.branch_type()));

    // 36000040        tbz w0, #0, 2
    is_instruction.set_opcode(0x36000040);
    InstructionIS tbz(is_instruction, 1, state);
    ASSERT_TRUE(tbz.is_branch());
    ASSERT_FALSE(is_call(tbz.branch_type()));
    ASSERT_FALSE(is_return(tbz.branch_type()));
    ASSERT_TRUE(is_direct(tbz.branch_type()));
    ASSERT_FALSE(is_indirect(tbz.branch_type()));
    ASSERT_TRUE(is_conditional(tbz.branch_type()));
    ASSERT_FALSE(is_unconditional(tbz.branch_type()));

    // 97fffbbe        bl -0x4420
    is_instruction.set_opcode(0x97fffbbe);
    InstructionIS bl(is_instruction, 1, state);
    ASSERT_TRUE(bl.is_branch());
    ASSERT_TRUE(is_call(bl.branch_type()));
    ASSERT_FALSE(is_return(bl.branch_type()));
    ASSERT_TRUE(is_direct(bl.branch_type()));
    ASSERT_FALSE(is_indirect(bl.branch_type()));
    ASSERT_FALSE(is_conditional(bl.branch_type()));
    ASSERT_TRUE(is_unconditional(bl.branch_type()));

    // d65f03c0        ret x30
    is_instruction.set_opcode(0xd65f03c0);
    InstructionIS ret(is_instruction, 1, state);
    ASSERT_TRUE(ret.is_branch());
    ASSERT_FALSE(is_call(ret.branch_type()));
    ASSERT_TRUE(is_return(ret.branch_type()));
    ASSERT_FALSE(is_direct(ret.branch_type()));
    ASSERT_TRUE(is_indirect(ret.branch_type()));
    ASSERT_FALSE(is_conditional(ret.branch_type()));
    ASSERT_TRUE(is_unconditional(ret.branch_type()));

    // 91002210        add     x16, x16, #0x8
    is_instruction.set_opcode(0x91002210);
    InstructionIS add(is_instruction, 1, state);
    ASSERT_FALSE(add.is_branch());

    // d69f03e0        eret
    is_instruction.set_opcode(0xd69f03e0);
    InstructionIS eret(is_instruction, 1, state);
    ASSERT_FALSE(eret.is_branch());
}

TEST_F(InstructionTest, testDivideDecode)
{
    // d61f0220        br      x17
    is_instruction.set_opcode(0xd61f0220);
    InstructionIS br(is_instruction, 1, state);
    ASSERT_FALSE(br.is_divide());

    // 1e640824        fmul  d4, d1, d4
    is_instruction.set_opcode(0x1e640824);
    InstructionIS fmul(is_instruction, 1, state);
    ASSERT_FALSE(fmul.is_divide());

    // 1e691809        fdiv  d9, d0, d9
    is_instruction.set_opcode(0x1e691809);
    InstructionIS fdiv(is_instruction, 1, state);
    ASSERT_TRUE(fdiv.is_divide());
    ASSERT_EQ(fdiv.divide_type(), DivideType::FP_SCALAR_DIVIDE);
    EXPECT_EQ(fdiv.divide_srcs(), 2);
    EXPECT_EQ(fdiv.divide_src(0), LReg::V0);
    EXPECT_EQ(fdiv.divide_src(1), LReg::V9);

    // 1e61c000        fsqrt d0, d0
    is_instruction.set_opcode(0x1e61c000);
    InstructionIS fsqrt(is_instruction, 1, state);
    ASSERT_TRUE(fsqrt.is_divide());
    ASSERT_EQ(fsqrt.divide_type(), DivideType::FP_SCALAR_SQRT);
    EXPECT_EQ(fsqrt.divide_srcs(), 1);
    EXPECT_EQ(fsqrt.divide_src(0), LReg::V0);

    // 1ac00902        udiv  w2, w8, w0
    is_instruction.set_opcode(0x1ac00902);
    InstructionIS udiv(is_instruction, 1, state);
    ASSERT_TRUE(udiv.is_divide());
    ASSERT_EQ(udiv.divide_type(), DivideType::INT_DIVIDE);
    EXPECT_EQ(udiv.divide_srcs(), 2);
    EXPECT_EQ(udiv.divide_src(0), LReg::L8);
    EXPECT_EQ(udiv.divide_src(1), LReg::L0);
}

TEST_F(InstructionTest, testTLBIDecode)
{
    // d50883a1        tlbi  vale1is, x1
    is_instruction.set_opcode(0xd50883a1);
    InstructionIS tlbi_vale1is(is_instruction, 1, state);
    ASSERT_TRUE(tlbi_vale1is.is_tlbi());
    ASSERT_TRUE(tlbi_vale1is.is_tlbi_by_va_last_level());
    ASSERT_FALSE(tlbi_vale1is.is_tlbi_by_va_all_level());
    ASSERT_EQ(tlbi_vale1is.sys_qualifier(), adis::sys_alias_t::TLBI_VALE1IS);
    ASSERT_NE(tlbi_vale1is.sys_qualifier(), adis::sys_alias_t::TLBI_VAE1IS);

    // d5088323        tlbi  vae1is, x3
    is_instruction.set_opcode(0xd5088323);
    InstructionIS tlbi_vae1is(is_instruction, 1, state);
    ASSERT_TRUE(tlbi_vae1is.is_tlbi());
    ASSERT_FALSE(tlbi_vae1is.is_tlbi_by_va_last_level());
    ASSERT_TRUE(tlbi_vae1is.is_tlbi_by_va_all_level());
    ASSERT_EQ(tlbi_vae1is.sys_qualifier(), adis::sys_alias_t::TLBI_VAE1IS);
    ASSERT_NE(tlbi_vae1is.sys_qualifier(), adis::sys_alias_t::TLBI_VALE1IS);

    // d50c8423  tlbi ipas2e1, x3
    is_instruction.set_opcode(0xd50c8423);
    InstructionIS tlbi_ipas2e1(is_instruction, 1, state);
    ASSERT_TRUE(tlbi_ipas2e1.is_tlbi());
    ASSERT_FALSE(tlbi_ipas2e1.is_tlbi_by_va_last_level());
    ASSERT_FALSE(tlbi_ipas2e1.is_tlbi_by_va_all_level());

    // d61f0220        br      x17
    is_instruction.set_opcode(0xd61f0220);
    InstructionIS br(is_instruction, 1, state);
    ASSERT_FALSE(br.is_tlbi());

    // 91002210        add     x16, x16, #0x8
    is_instruction.set_opcode(0x91002210);
    InstructionIS add(is_instruction, 1, state);
    ASSERT_FALSE(add.is_tlbi());
}

TEST_F(InstructionTest, testPrefetchDecode)
{
    // d61f0220        br      x17
    is_instruction.set_opcode(0xd61f0220);
    InstructionIS br(is_instruction, 1, state);
    ASSERT_FALSE(br.is_prefetch());

    // f9800020  prfm  pldl1keep, [x1]
    is_instruction.set_opcode(0xf9800020);
    InstructionIS prfm(is_instruction, 1, state);
    ASSERT_TRUE(prfm.is_prefetch());

    // f9800031  prfm  pstl1strm, [x1]
    is_instruction.set_opcode(0xf9800031);
    InstructionIS prfm2(is_instruction, 1, state);
    ASSERT_TRUE(prfm2.is_prefetch());
    ASSERT_EQ(prfm2.prefetch_type(), adis::ENC_PRFM_P_ldst_pos);

    is_instruction.set_opcode(0xd88243c9);
    InstructionIS prfm3(is_instruction, 1, state);
    ASSERT_TRUE(prfm3.is_prefetch());
    ASSERT_EQ(prfm3.prefetch_type(), adis::ENC_PRFM_P_loadlit);

    is_instruction.set_opcode(0xf8a24a20);
    InstructionIS prfm4(is_instruction, 1, state);
    ASSERT_TRUE(prfm4.is_prefetch());
    ASSERT_EQ(prfm4.prefetch_type(), adis::ENC_PRFM_P_ldst_regoff);

    is_instruction.set_opcode(0xf8924287);
    InstructionIS prfum(is_instruction, 1, state);
    ASSERT_TRUE(prfum.is_prefetch());
    ASSERT_EQ(prfum.prefetch_type(), adis::ENC_PRFUM_P_ldst_unscaled);
}

TEST_F(InstructionTest, testIC_DC)
{
    is_instruction.set_opcode(0xd50b7420); // dc zva
    InstructionIS inst(is_instruction, 1, state);
    ASSERT_TRUE(inst.is_dc());
    ASSERT_FALSE(inst.is_ic());

    is_instruction.set_opcode(0b1101'0101'0000'1'000'0111'0001'000'00000); // ic IALLUIS
    InstructionIS inst2(is_instruction, 1, state);
    ASSERT_FALSE(inst2.is_dc());
    ASSERT_TRUE(inst2.is_ic());

    is_instruction.set_opcode(0xf8924287);
    InstructionIS inst3(is_instruction, 1, state);
    ASSERT_FALSE(inst3.is_dc());
    ASSERT_FALSE(inst3.is_ic());
}

} // namespace archx
} // namespace ampere

/*
 * vim: sw=4:ts=4:et:si:cindent:cino=\:0,(0,g0,t0:
 */
