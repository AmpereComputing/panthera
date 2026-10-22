//
// Copyright (c) 2018-2019 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//

#include <gtest/gtest.h>
#include "ampere/archx/uop.h"
#include "furiosa.h"

using namespace ampere::archx;

TEST(UopInfo, AddUopHasUnitOfIntegerAndLatencyOne)
{
    std::uint32_t insn = 0b1'0'0'01011'00'1'00000'000'000'00000'00000;
    UopSet uops = furiosa::decode(insn);

    ASSERT_GE(uops.size(), 1);
    EXPECT_EQ(uops[0]->info->unit, Uopunit::Integer);
    EXPECT_EQ(uops[0]->info->latency, 1);
}

TEST(UopInfo, MemUopsHaveDataSizes)
{
    std::uint32_t ldr_b_32_insn = 0b00'111'0'01'01'000000000000'00000'00000;
    std::uint32_t ldr_h_32_insn = 0b01'111'0'01'01'000000000000'00000'00000;
    std::uint32_t ldr_w_32_insn = 0b10'111'0'01'01'000000000000'00000'00000;
    std::uint32_t ldr_d_64_insn = 0b11'111'0'01'01'000000000000'00000'00000;
    std::uint32_t ldrv_q_128_insn = 0b10'011'1'00'0000000000000000000'00000;

    UopSet ldr_b_32_uops = furiosa::decode(ldr_b_32_insn);
    UopSet ldr_h_32_uops = furiosa::decode(ldr_h_32_insn);
    UopSet ldr_w_32_uops = furiosa::decode(ldr_w_32_insn);
    UopSet ldr_d_64_uops = furiosa::decode(ldr_d_64_insn);
    UopSet ldrv_q_128_uops = furiosa::decode(ldrv_q_128_insn);

    ASSERT_GE(ldr_b_32_uops.size(), 1);
    EXPECT_EQ(ldr_b_32_uops[0]->info->mem_datasize, 1);

    ASSERT_GE(ldr_h_32_uops.size(), 1);
    EXPECT_EQ(ldr_h_32_uops[0]->info->mem_datasize, 2);

    ASSERT_GE(ldr_w_32_uops.size(), 1);
    EXPECT_EQ(ldr_w_32_uops[0]->info->mem_datasize, 4);

    ASSERT_GE(ldr_d_64_uops.size(), 1);
    EXPECT_EQ(ldr_d_64_uops[0]->info->mem_datasize, 8);

    ASSERT_GE(ldrv_q_128_uops.size(), 1);
    EXPECT_EQ(ldrv_q_128_uops[0]->info->mem_datasize, 16);
}

TEST(UopInfo, StoresHaveProperTypes)
{
    std::uint32_t strb = 0b00'111'0'00'00'1'00000'010'0'10'00000'00000;
    UopSet strb_uops = furiosa::decode(strb);

    ASSERT_EQ(strb_uops.size(), 2);
    EXPECT_EQ(strb_uops[0]->info->store_type, StoreType::STA);
    EXPECT_EQ(strb_uops[1]->info->store_type, StoreType::STD);

    std::uint32_t stp = 0b10'101'0'010'0'0000000'00000'00000'00000;
    UopSet stp_uops = furiosa::decode(stp);

    ASSERT_EQ(stp_uops.size(), 2);
    EXPECT_EQ(stp_uops[0]->info->store_type, StoreType::SUS);
    EXPECT_EQ(stp_uops[1]->info->store_type, StoreType::STDHI);

    std::uint32_t clrex = 0b1101010100'0'00'011'0011'0000'010'11111;
    UopSet clrex_uops = furiosa::decode(clrex);

    ASSERT_EQ(clrex_uops.size(), 1);
    EXPECT_EQ(clrex_uops[0]->info->store_type, StoreType::SPECIAL);

    std::uint32_t ldr_b_32_insn = 0b00'111'0'01'01'000000000000'00000'00000;
    UopSet ldr_uops = furiosa::decode(ldr_b_32_insn);

    ASSERT_EQ(ldr_uops.size(), 1);
    EXPECT_EQ(ldr_uops[0]->info->store_type, StoreType::NONE);
}

TEST(UopInfo, MemUopsHavePairInfo)
{
    std::uint32_t stp = 0b10'101'0'010'0'0000000'00000'00000'00000;
    UopSet stp_uops = furiosa::decode(stp);

    EXPECT_EQ(stp_uops[0]->info->pair, 1);

    std::uint32_t ldp = 0b10'101'0'010'1'0000000'00000'00000'00000;
    UopSet ldp_uops = furiosa::decode(ldp);

    EXPECT_EQ(ldp_uops[0]->info->pair, 1);

    std::uint32_t str = 0b11'111'0'01'00'000000000000'00000'00000;
    UopSet str_uops = furiosa::decode(str);

    EXPECT_EQ(str_uops[0]->info->pair, 0);

    std::uint32_t ldr = 0b11'111'0'01'01'000000000000'00000'00000;
    UopSet ldr_uops = furiosa::decode(ldr);

    EXPECT_EQ(ldr_uops[0]->info->pair, 0);
}

/*
 * vim: sw=4:ts=4:et:si:cindent:cino=\:0,(0,g0,t0:
 */
