//
// Copyright (c) 2018-2023 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//

#include <gtest/gtest.h>
#include "furiosa.h"
#include "ripley.h"
#include "trinity.h"
#include "connor.h"
#include "trinity_decoder_mode.h"

namespace ampere {
namespace archx {

TEST(TestDecode, UndefinedInstructionReturnsZeroUops)
{
    UopSet uops = furiosa::decode(0);
    EXPECT_EQ(uops.size(), 0);
}

TEST(TestDecode, RipleyNopDecode)
{
    UopSet uops = ripley::decode(0xd503201f);
    EXPECT_EQ(uops.size(), 1);
}

TEST(TestDecode, TrinityNopDecode)
{
    UopSet uops = trinity::decode(0xd503201f);
    EXPECT_EQ(uops.size(), 1);
}

TEST(TestDecode, AddImmDecodesToSingleAddUop)
{
    std::uint32_t insn = ((0b00010001 << 24) |     // opcode
                          (0b000000001011 << 10) | // immediate
                          (0b00001 << 5) |         // Rn
                          (0b00010));              // Rd

    UopSet uops = furiosa::decode(insn);
    EXPECT_EQ(uops.size(), 1);
    EXPECT_EQ(uops[0]->opcode, Uopcode::ADD32);
    EXPECT_EQ(uops[0]->src1, LReg::INVALID);
    EXPECT_EQ(uops[0]->src2, LReg::L1);
    EXPECT_EQ(uops[0]->dst, LReg::L2);
    EXPECT_EQ(uops[0]->imm, 0xb);
}

TEST(TestDecode, LoadPostIndexDecodesToLoadUopFollowedByAddr)
{
    std::uint32_t insn = ((0b00111000010 << 21) |         // opcode
                          (0b00000101 << 12) |            // immediate
                          (0b01 << 10) | (0b00111 << 5) | // Rn
                          (0b01001));                     // Rt

    UopSet uops = furiosa::decode(insn);
    EXPECT_EQ(uops.size(), 2);
    EXPECT_EQ(uops[0]->opcode, Uopcode::LDR_B_32);
    EXPECT_EQ(uops[0]->src1, LReg::L7);
    EXPECT_EQ(uops[0]->src2, LReg::ZR);
    EXPECT_EQ(uops[0]->dst, LReg::L9);
    EXPECT_EQ(uops[1]->opcode, Uopcode::ADD64_IDXIMM);
    EXPECT_EQ(uops[1]->src2, LReg::L7);
    EXPECT_EQ(uops[1]->imm, 5);
    EXPECT_EQ(uops[1]->dst, LReg::L7);
}

TEST(TestDecode, SingleUopIsMarkedFirstAndLast)
{
    std::uint32_t insn = ((0b00010001 << 24) |     // opcode
                          (0b000000001011 << 10) | // immediate
                          (0b00001 << 5) |         // Rn
                          (0b00010));              // Rd

    UopSet uops = furiosa::decode(insn);
    EXPECT_EQ(uops[0]->end_inst, 1);
}

TEST(TestDecode, UopPairHasOneMarkedFirstAndAnotherMarkedLast)
{
    std::uint32_t insn = ((0b00111000010 << 21) |         // opcode
                          (0b00000101 << 12) |            // immediate
                          (0b01 << 10) | (0b00111 << 5) | // Rn
                          (0b01001));                     // Rt

    UopSet uops = furiosa::decode(insn);
    EXPECT_EQ(uops[0]->end_inst, 0);
    EXPECT_EQ(uops[1]->end_inst, 1);
}

TEST(TestDecode, ThreeUopFlowHasMiddleUopNeitherFirstNorLast)
{
    std::uint32_t insn = 0b00'101'0'011'1'0000000'00000'00000'00000 | // LDP_32_ldstpair_pre
                         (0b00011 << 10) |                            // Rt2 = 3
                         (0b00010 << 5) |                             // Rn = 2
                         (0b00001);                                   // Rt = 1

    UopSet uops = furiosa::decode(insn);

    ASSERT_EQ(uops.size(), 3);
    EXPECT_EQ(uops[0]->end_inst, 0);
    EXPECT_EQ(uops[1]->end_inst, 0);
    EXPECT_EQ(uops[2]->end_inst, 1);
}

TEST(TestDecode, FMULDecodesSuccessfully)
{
    std::uint32_t insn = ((0b00011110001 << 21) |            // opcode
                          (0b00010 << 16) |                  // Rm
                          (0b0 << 15) |                      // op
                          (0b00010 << 10) | (0b00001 << 5) | // Rn
                          (0b00011));                        // Rd

    UopSet uops = furiosa::decode(insn);
    EXPECT_EQ(uops.size(), 1);
    EXPECT_EQ(uops[0]->opcode, Uopcode::FMUL_S_NV);
    EXPECT_EQ(uops[0]->src1, LReg::V1);
    EXPECT_EQ(uops[0]->src2, LReg::V2);
    EXPECT_EQ(uops[0]->dst, LReg::V3);
}

TEST(TestDecode, AddWithAllOnesUsesSP)
{
    std::uint32_t insn = ((0b00010001 << 24) |     // opcode
                          (0b000000001011 << 10) | // immediate
                          (0b11111 << 5) |         // Rn
                          (0b11111));              // Rd

    UopSet uops = furiosa::decode(insn);
    EXPECT_EQ(uops.size(), 1);
    EXPECT_EQ(uops[0]->opcode, Uopcode::ADD32);
    EXPECT_EQ(uops[0]->src1, LReg::INVALID);
    EXPECT_EQ(uops[0]->src2, LReg::SP);
    EXPECT_EQ(uops[0]->dst, LReg::SP);
    EXPECT_EQ(uops[0]->imm, 0xb);
}

TEST(TestDecode, TestFlagProducer)
{
    std::uint32_t insn = ((0b00110001 << 24) |     // opcode
                          (0b000000001011 << 10) | // immediate
                          (0b00001 << 5) |         // Rn
                          (0b00010));              // Rd

    UopSet uops = furiosa::decode(insn);
    EXPECT_EQ(uops.size(), 1);
    EXPECT_TRUE(uops[0]->produce_flags);
}

TEST(TestDecode, TestFlagConsumer)
{
    std::uint32_t insn = 0x54001aab; // b.lt #0x1354
    UopSet uops = furiosa::decode(insn);

    EXPECT_EQ(uops.size(), 1);
    EXPECT_EQ(uops[0]->src3, LReg::FLAGS);
}

TEST(TestDecode, TestSVEBasic)
{
    // bfcvt predicated
    //                                                Pg   Zn    Zd
    std::uint32_t insn = 0b01100101'1'0'0010'1'0'101'100'00100'00001;
    UopSet uops = connor::decode(insn);

    ASSERT_EQ(uops.size(), 1);
    EXPECT_EQ(uops[0]->dst, LReg::V1);
    EXPECT_EQ(uops[0]->src1, LReg::V4);
    EXPECT_EQ(uops[0]->src2, LReg::V1);
    EXPECT_EQ(uops[0]->src3, LReg::INVALID);
    EXPECT_EQ(uops[0]->pred, LReg::P4);

    // Add non-predicated
    //                              sz    Zm             Zn    Zd
    std::uint32_t insn2 = 0b0000100'00'1'01001'000'00'0'00100'00001;
    UopSet uops2 = connor::decode(insn2);

    ASSERT_EQ(uops2.size(), 1);
    EXPECT_EQ(uops2[0]->dst, LReg::V1);
    EXPECT_EQ(uops2[0]->src1, LReg::V4);
    EXPECT_EQ(uops2[0]->src2, LReg::V9);
}

TEST(TestDecode, TestSVECopyGprToDoubleElements)
{
    // CPY Z10.D, P8/M, X0.  This is representative of the D-element CPY
    // encodings exercised by the double-pump I2F integration test.
    UopSet uops = connor::decode(0x05e8a40a);

    ASSERT_EQ(uops.size(), 2);
    EXPECT_EQ(uops[1]->dst, LReg::V10);
}

TEST(TestDecode, TestVectorRegisterDecode)
{
    //                                       Rm            Rn    Rd
    std::uint32_t insn = 0b0'0'0'01110'00'1'00011'00011'1'00100'00001;
    UopSet uops = furiosa::decode(insn);

    ASSERT_EQ(uops.size(), 1);
    EXPECT_EQ(uops[0]->dst, LReg::V1);
    EXPECT_EQ(uops[0]->src1, LReg::V4);
    EXPECT_EQ(uops[0]->src2, LReg::V3);
}

TEST(TestDecode, TestTemporaryIntegerRegisterIsDecoded)
{
    // ADD_32_addsub_ext
    //                                        Rm   o  imm   Rn   Rd
    //                                             p
    //                                             t
    //                                             i
    //                                             o
    //                                             n
    std::uint32_t insn = 0b0'0'0'01011'00'1'01010'100'000'00000'10000;
    UopSet uops = furiosa::decode(insn);

    ASSERT_EQ(uops.size(), 2);
    EXPECT_EQ(uops[0]->dst, LReg::s0);
    EXPECT_EQ(uops[1]->src2, LReg::s0);
}

TEST(TestDecode, TestTemporaryVectorRegisterIsDecoded)
{
    // ADDHN_asimddiff_N
    std::uint32_t insn = 0b0'1'0'01110'10'1'00000'01'0'0'00'11110'11111;
    UopSet uops = furiosa::decode(insn);

    ASSERT_EQ(uops.size(), 2);
    EXPECT_EQ(uops[0]->dst, LReg::t0);
    EXPECT_EQ(uops[1]->src2, LReg::t0);
}

/* FIXME: reinstate after updating xlat (current xlat for STn/LDn is incorrect)
TEST(TestDecode, TestMixedIntegerAndVectorRegisterDecode)
{
    // ST2_asisdlsep_I2_i                     Rm            Rn    Rt
    std::uint32_t insn = 0b0'1'0011001'0'0'11111'1000'11'01011'10101;
    UopSet uops = furiosa::decode(insn);

    ASSERT_EQ(uops.size(), 4);
    EXPECT_EQ(uops[0]->dst, LReg::t0);
    EXPECT_EQ(uops[0]->src1, LReg::V21);
    EXPECT_EQ(uops[0]->src2, LReg::V21);
    EXPECT_EQ(uops[1]->dst, LReg::t1);
    EXPECT_EQ(uops[1]->src1, LReg::V21);
    EXPECT_EQ(uops[1]->src2, LReg::V21);
    EXPECT_EQ(uops[2]->src1, LReg::L11);
    EXPECT_EQ(uops[2]->src2, LReg::t0);
    EXPECT_EQ(uops[3]->src1, LReg::L11);
    EXPECT_EQ(uops[3]->src2, LReg::t1);
}
*/

TEST(TestDecode, TestLDPSecondUop)
{
    std::uint32_t insn = 0b10'101'0'010'1'0000000'00100'00000'00101;
    UopSet uops = furiosa::decode(insn);
    ASSERT_EQ(uops.size(), 2);
    EXPECT_FALSE(uops[0]->ldp_2nd_uop);
    EXPECT_TRUE(uops[1]->ldp_2nd_uop);
}

TEST(TestDecode, TestSTPPreIncrementOfSP)
{
    std::uint32_t insn = 0xa9bd7bfd; // stp x29, x30, [sp, #-0x30]!

    UopSet uops = furiosa::decode(insn);
    ASSERT_EQ(uops.size(), 3);
    EXPECT_EQ(uops[2]->src2, LReg::SP);
    EXPECT_EQ(uops[2]->dst, LReg::SP);
}

TEST(TestDecode, TestADDWhichUsesSP)
{
    std::uint32_t insn = 0x910003fd; // mov x29, sp

    UopSet uops = furiosa::decode(insn);
    ASSERT_EQ(uops.size(), 1);
    EXPECT_EQ(uops[0]->src2, LReg::SP);
    EXPECT_EQ(uops[0]->dst, LReg::L29);
}

TEST(TestDecode, TestADDWhichDoesNotUseSP)
{
    // All "11111" register encodings should decode to ZR, not SP.
    //
    //                                  s    Rm    imm6   Rn    Rd
    //                                  h
    //                                  i
    //                                  f
    //                                  t
    std::uint32_t insn = 0b1'0'0'01011'00'0'11111'000000'11111'11111;

    UopSet uops = furiosa::decode(insn);
    ASSERT_EQ(uops.size(), 1);
    EXPECT_EQ(uops[0]->src2, LReg::ZR);
    EXPECT_EQ(uops[0]->src2, LReg::ZR);
    EXPECT_EQ(uops[0]->dst, LReg::ZR);
}

TEST(TestDecode, TestFusionRequirements)
{
    //                              SUBS             shift amt.
    //                                                   |
    //                                                   v
    std::uint32_t small_shift[2] = {0b11101011001'00000'011'000'0000000000, 0b01010100000000000000000000000000};
    std::uint32_t large_shift[2] = {0b11101011001'00000'100'000'0000000000, 0b01010100000000000000000000000000};

    auto [fusion_type, uops] = furiosa::decode_fusion(small_shift, true);
    ASSERT_EQ(uops.size(), 1);

    auto [fusion_type2, uops2] = furiosa::decode_fusion(large_shift, true);
    ASSERT_EQ(uops2.size(), 0);
}

TEST(TestDecode, PAC_ia_OFF)
{
    std::uint32_t insn = 0b1101101011000001000000'00000'00000; // PACIA_64P_dp_1src
    UopSet uops = trinity::decode(insn, trinity::DecoderMode_Pac_Ia_Enabled_OFF);
    EXPECT_EQ(uops.size(), 1);
    EXPECT_EQ(uops[0]->opcode, Uopcode::NOP);
}
TEST(TestDecode, PAC_ia_ON)
{
    std::uint32_t insn = 0b1101101011000001000000'00000'00000; // PACIA_64P_dp_1src
    UopSet uops = trinity::decode(insn, trinity::DecoderMode_Pac_Ia_Enabled_ON);
    EXPECT_EQ(uops.size(), 1);
    EXPECT_EQ(uops[0]->opcode, Uopcode::PACIA);
}

} // namespace archx
} // namespace ampere

/*
 * vim: sw=4:ts=4:et:si:cindent:cino=\:0,(0,g0,t0:
 */
