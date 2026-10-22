//
// Copyright (c) 2018-2019 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//

#include <gtest/gtest.h>
#include "ampere/archx/arch_state.h"

namespace ampere {
namespace arch_state {

using namespace ampere::archx;

TEST(testMemory, testRead)
{
    ArchState state;
    ASSERT_EQ(state.read(0_pa), 0);
    ASSERT_EQ(state.read(0xc3746352fa8d7f68_pa), 0);

    state = std::move(state).write(0xc3746352fa8d7f68_pa, 0xdeadbeefdeadbeef);
    ASSERT_EQ(state.read(0xc3746352fa8d7f68_pa), 0xdeadbeefdeadbeef);
}

TEST(testMemory, testWrite)
{
    ArchState state;

    uint8_t data[8] = {0xef, 0xbe, 0xad, 0xde, 0xa1, 0xa2, 0xa3, 0xa4};

    ASSERT_EQ(state.read(0xf00_pa), 0);
    state = std::move(state).write(0xf01_pa, &data[1], 2);
    ASSERT_EQ(state.read(0xf00_pa), 0xadbe00);
    state = std::move(state).write(0xf00_pa, &data[0], 1);
    ASSERT_EQ(state.read(0xf00_pa), 0xadbeef);
    state = std::move(state).write(0xf03_pa, &data[3], 5);
    ASSERT_EQ(state.read(0xf00_pa), 0xa4a3a2a1deadbeef);

    state = std::move(state).write(0xf04_pa, &data[0], 8);
    ASSERT_EQ(state.read(0xf00_pa), 0xdeadbeefdeadbeef);
    ASSERT_EQ(state.read(0xf08_pa), 0xa4a3a2a1);

    state = std::move(state).write(0xf0c_pa, &data[0], 3);
    state = std::move(state).write(0xf0f_pa, &data[3], 1);
    ASSERT_EQ(state.read(0xf08_pa), 0xdeadbeefa4a3a2a1);

    state = std::move(state).write(0xf08_pa, &data[0], 4);
    ASSERT_EQ(state.read(0xf08_pa), 0xdeadbeefdeadbeef);
    state = std::move(state).write(0xf0c_pa, &data[4], 4);
    ASSERT_EQ(state.read(0xf08_pa), 0xa4a3a2a1deadbeef);

    state = std::move(state).write(0xf10_pa, &data[0], 8);
    ASSERT_EQ(state.read(0xf10_pa), 0xa4a3a2a1deadbeef);
}

TEST(testMemory, testReadFineGrained8)
{
    ArchState state;

    uint8_t data[19] = {0xef,
                        0xbe,
                        0xad,
                        0xde,
                        0xa1,
                        0xa2,
                        0xa3,
                        0xa4,
                        0xa5,
                        0xa6,
                        0xa7,
                        0xa8,
                        0xa9,
                        0xaa,
                        0xab,
                        0xac,
                        0xad,
                        0xae,
                        0xaf};

    ASSERT_EQ(state.read(0xf00_pa), 0);
    state = std::move(state).write(0xf03_pa, &data[0], 19);

    ASSERT_EQ(state.read(0xf00_pa), 0xa1deadbeef000000);
    ASSERT_EQ(state.read(0xf08_pa), 0xa9a8a7a6a5a4a3a2);
    ASSERT_EQ(state.read(0xf10_pa), 0xafaeadacabaa);

    ASSERT_EQ(state.read(0xf03_pa, 2), 0xbeef);
    ASSERT_EQ(state.read(0xf0b_pa, 8), 0xacabaaa9a8a7a6a5);
    ASSERT_EQ(state.read(0xf09_pa, 4), 0xa6a5a4a3);
    ASSERT_EQ(state.read(0xf0f_pa, 4), 0xacabaaa9);
    ASSERT_EQ(state.read(0xf06_pa, 8), 0xa7a6a5a4a3a2a1de);
    ASSERT_EQ(state.read(0xf06_pa, 1), 0xde);

    uint8_t data_1[8] = {0xef, 0xbe, 0xad, 0xde, 0xa1, 0xa2, 0xa3, 0xa4};
    ASSERT_EQ(state.read(0xff8_pa), 0);
    state = std::move(state).write(0xffd_pa, &data_1[0], 8);
    ASSERT_EQ(state.read(0xff8_pa), 0xadbeef0000000000);
    ASSERT_EQ(state.read(0x1000_pa), 0xa4a3a2a1de);
}

TEST(testMemory, testReadFineGrained16)
{
    ArchState state;

    uint16_t data[8] = {0xefbe, 0xadde, 0xa1a2, 0xa3a4, 0xa5a6, 0xa7a8, 0xa9aa, 0xabac};

    ASSERT_EQ(state.read(0xf00_pa), 0);
    state = std::move(state).write(0xf00_pa, (uint8_t*)&data[0], 16);

    ASSERT_EQ(state.read(0xf00_pa), 0xa3a4a1a2addeefbe);
    ASSERT_EQ(state.read(0xf08_pa), 0xabaca9aaa7a8a5a6);

    ASSERT_EQ(state.read(0xf03_pa, 2), 0xa2ad);
    ASSERT_EQ(state.read(0xf04_pa, 8), 0xa7a8a5a6a3a4a1a2);
    ASSERT_EQ(state.read(0xf09_pa, 4), 0xaaa7a8a5);

    uint16_t val = 0xddcc;

    state = std::move(state).write(0xf07_pa, (uint8_t*)&val, sizeof(val));
    ASSERT_EQ(state.read(0xf04_pa, 8), 0xa7a8a5ddcca4a1a2);
}

TEST(testMemory, testReadFineGrainedMixed)
{
    ArchState state;

    uint32_t data[3] = {0xefbeadde, 0xa1a2a3a4, 0xa5a6a7a8};

    ASSERT_EQ(state.read(0xf00_pa), 0);
    state = std::move(state).write(0xf02_pa, (uint8_t*)&data[0], 3 * sizeof(data[0]));

    ASSERT_EQ(state.read(0xf00_pa), 0xa3a4efbeadde0000);
    ASSERT_EQ(state.read(0xf08_pa), 0xa5a6a7a8a1a2);
    ASSERT_EQ(state.read(0xf02_pa, 1), 0xde);
    ASSERT_EQ(state.read(0xf03_pa, 2), 0xbead);
    ASSERT_EQ(state.read(0xf05_pa, 4), 0xa2a3a4ef);
    ASSERT_EQ(state.read(0xf06_pa, 8), 0xa5a6a7a8a1a2a3a4);

    uint64_t val = 0xa9aaabacdef3abc2;
    uint16_t val1 = 0x1357;
    uint8_t val2 = 0xba;

    state = std::move(state).write(0xf02_pa, (uint8_t*)&val2, sizeof(val2));
    ASSERT_EQ(state.read(0xf02_pa, 1), 0xba);
    ASSERT_EQ(state.read(0xf02_pa, 4), 0xefbeadba);

    state = std::move(state).write(0xf02_pa, (uint8_t*)&val1, sizeof(val1));
    ASSERT_EQ(state.read(0xf02_pa, 1), 0x57);
    ASSERT_EQ(state.read(0xf02_pa, 4), 0xefbe1357);

    state = std::move(state).write(0xf03_pa, (uint8_t*)&val, sizeof(val));
    ASSERT_EQ(state.read(0xf03_pa, 8), 0xa9aaabacdef3abc2);
    ASSERT_EQ(state.read(0xf0c_pa, 4), 0xa5a6);
    ASSERT_EQ(state.read(0xf07_pa, 2), 0xabac);
}

TEST(testMemory, testWrite8)
{
    ArchState state;
    state = std::move(state).write(0xf00_pa, (uint8_t)0xa1);
    ASSERT_EQ(state.read(0xf00_pa), 0xa1);
    state = std::move(state).write(0xf07_pa, (uint8_t)0xbf);
    ASSERT_EQ(state.read(0xf00_pa), 0xbf000000000000a1);
    state = std::move(state).write(0xf00_pa, (uint8_t)0x52);
    ASSERT_EQ(state.read(0xf00_pa), 0xbf00000000000052);
    state = std::move(state).write(0xf03_pa, (uint8_t)0xbe);
    ASSERT_EQ(state.read(0xf00_pa), 0xbf000000be000052);
    state = std::move(state).write(0xf03_pa, (uint8_t)0);
    ASSERT_EQ(state.read(0xf00_pa), 0xbf00000000000052);
}

TEST(testMemory, testWrite16)
{
    ArchState state;
    state = std::move(state).write(0xf00_pa, (uint16_t)0xa1);
    ASSERT_EQ(state.read(0xf00_pa), 0xa1);
    state = std::move(state).write(0xf00_pa, (uint16_t)0x6700);
    ASSERT_EQ(state.read(0xf00_pa), 0x6700);
    state = std::move(state).write(0xf06_pa, (uint16_t)0xbeef);
    ASSERT_EQ(state.read(0xf00_pa), 0xbeef000000006700);
    state = std::move(state).write(0xf06_pa, (uint16_t)0);
    ASSERT_EQ(state.read(0xf00_pa), 0x6700);
}

TEST(testMemory, testWrite32)
{
    ArchState state;
    state = std::move(state).write(0xf00_pa, (uint32_t)0xdeadbeef);
    ASSERT_EQ(state.read(0xf00_pa), 0xdeadbeef);
    state = std::move(state).write(0xf04_pa, (uint32_t)0xa5a5a5a5);
    ASSERT_EQ(state.read(0xf00_pa), 0xa5a5a5a5deadbeef);
    state = std::move(state).write(0xf00_pa, (uint32_t)0);
    ASSERT_EQ(state.read(0xf00_pa), 0xa5a5a5a500000000);
}

TEST(testMemory, testWrite64)
{
    ArchState state;
    state = std::move(state).write(0x7fffffff000047d8_pa, (uint64_t)0xdeadc0dedeadbeef);
    ASSERT_EQ(state.read(0x7fffffff000047d8_pa), 0xdeadc0dedeadbeef);
    state = std::move(state).write(0x7fffffff000047d8_pa, (uint64_t)0);
    ASSERT_EQ(state.read(0x7fffffff000047d8_pa), 0);
}

TEST(testSysregs, testReadWrite)
{
    ArchState state;
    state = std::move(state).write(TCR_EL1, (uint64_t)0xf00baa);
    state = std::move(state).write(TTBR1_EL1, (uint64_t)0xdeadbeefdeaddead);
    ASSERT_EQ(state.read(TCR_EL1), 0xf00baa);
    ASSERT_EQ(state.read(TTBR1_EL1), 0xdeadbeefdeaddead);
}

TEST(testGPRs, testReadWrite)
{
    ArchState state;
    state = std::move(state).write(L5, (uint64_t)0xf00ba7);
    state = std::move(state).write(SP, (uint64_t)0xdeadbeefdeaddead);
    ASSERT_EQ(state.read(L5), 0xf00ba7);
    ASSERT_EQ(state.read(SP), 0xdeadbeefdeaddead);
}

TEST(testVFPRegs, testReadWrite)
{
    ArchState state;
    std::array<uint64_t, 2> val1 = {0xdeadc0de83649292, 0x836128492afd};
    std::array<uint64_t, 2> val2 = {0xfaaaaaaaddddeeee, 0xa5a5a5a5a5a5a5a5};
    state = std::move(state).write_vfp(V20, val1);
    state = std::move(state).write_vfp(V2, val2);
    ASSERT_EQ(state.read_vfp(V20)[0], 0xdeadc0de83649292);
    ASSERT_EQ(state.read_vfp(V20)[1], 0x836128492afd);
    ASSERT_EQ(state.read_vfp(V2)[0], 0xfaaaaaaaddddeeee);
    ASSERT_EQ(state.read_vfp(V2)[1], 0xa5a5a5a5a5a5a5a5);
}

TEST(testArchState, testInterspersedWrites)
{
    ArchState state;
    state = std::move(state).write(TCR_EL1, (uint64_t)0xf00baa);
    state = std::move(state).write(0x7fffffff000047d8_pa, (uint64_t)0xdeadc0dedeadbeef);
    ASSERT_EQ(state.read(0x7fffffff000047d8_pa), 0xdeadc0dedeadbeef);
    state = std::move(state).write(TTBR1_EL1, (uint64_t)0xdeadbeefdeaddead);
    ASSERT_EQ(state.read(TCR_EL1), 0xf00baa);
    ASSERT_EQ(state.read(TTBR1_EL1), 0xdeadbeefdeaddead);
    state = std::move(state).write(0x7fffffff000047d8_pa, (uint64_t)0);
    ASSERT_EQ(state.read(0x7fffffff000047d8_pa), 0);
}

} // namespace arch_state
} // namespace ampere

/*
 * vim: sw=4:ts=4:et:si:cindent:cino=\:0,(0,g0,t0:
 */
