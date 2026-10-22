//
// Copyright (c) 2019 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//

#include <gtest/gtest.h>
#include <boost/dynamic_bitset.hpp>
#include "ampere/sc/testing/test_module.h"
#include "ampere/core/branch_history.h"

using namespace ampere::archx;
using namespace ampere::sc;
using namespace ampere::core;

class BranchHistoryTestComponent : public BranchHistory
{
public:
    using BranchHistory::BranchHistory;
    using BranchHistory::history_begin;
    using BranchHistory::history_end;
    using BranchHistory::size;
};

class BranchHistoryTest : public TestModule
{
public:
    BranchHistoryTest() : TestModule{} {}
    BranchHistoryTest(std::initializer_list<std::pair<std::string, json>> settings) : TestModule{settings} {}

protected:
    BranchHistoryTestComponent bhr{"bhr"};

    using PCVec = std::vector<ProgramCounter>;

    PCVec bhr_to_vec()
    {
        std::vector<ProgramCounter> result;
        auto p = bhr.history_begin();

        while (p != bhr.history_end()) {
            result.push_back(p->pc);
            ++p;
        }

        return result;
    }

    void SetUp() override { bhr.add_history_size(1); }
};

TEST_F(BranchHistoryTest, NewHistoryHasSizeZero)
{
    EXPECT_EQ(bhr.size(), 0);
}

TEST_F(BranchHistoryTest, PushOneBranch)
{
    bhr.push(0, 12_pc);
    ASSERT_EQ(bhr.size(), 1);
    EXPECT_EQ(bhr.history_begin()->pc, 12_pc);
}

TEST_F(BranchHistoryTest, PushThreeBranches)
{
    bhr.push(0, 12_pc);
    bhr.push(1, 16_pc);
    bhr.push(2, 20_pc);

    auto expected = PCVec{20_pc, 16_pc, 12_pc};
    EXPECT_EQ(bhr_to_vec(), expected);
}

TEST_F(BranchHistoryTest, PushingDuplicateFetchIdFails)
{
    bhr.push(0, 12_pc);

    EXPECT_ANY_THROW(bhr.push(0, 16_pc));
}

TEST_F(BranchHistoryTest, CommitWillRemoveUnreachableHistory)
{
    for (unsigned i = 0; i < 20; i++)
        bhr.push(i + 1, ProgramCounter(10 + 10 * i));

    EXPECT_EQ(bhr.size(), 20);

    auto old_bhr_as_vec = bhr_to_vec();
    EXPECT_EQ(old_bhr_as_vec.at(old_bhr_as_vec.size() - 1), 10_pc);
    EXPECT_EQ(old_bhr_as_vec.at(old_bhr_as_vec.size() - 2), 20_pc);
    EXPECT_EQ(old_bhr_as_vec.at(old_bhr_as_vec.size() - 3), 30_pc);

    bhr.commit(14);

    // This checks to see if at least *some* of the oldest PCs have
    // been removed. But there's a buffer, so it's tricky to be exact.
    //
    auto bhr_as_vec = bhr_to_vec();
    EXPECT_NE(bhr_as_vec.at(bhr_as_vec.size() - 1), 10_pc);
    EXPECT_NE(bhr_as_vec.at(bhr_as_vec.size() - 2), 20_pc);
    EXPECT_NE(bhr_as_vec.at(bhr_as_vec.size() - 3), 30_pc);
}

TEST_F(BranchHistoryTest, UncommittedDepth)
{
    EXPECT_EQ(bhr.uncommitted_depth(), 0);

    for (unsigned i = 0; i < 20; i++)
        bhr.push(i + 1, ProgramCounter(10 + 10 * i));

    EXPECT_EQ(bhr.uncommitted_depth(), 20);
    bhr.commit(1);
    EXPECT_EQ(bhr.uncommitted_depth(), 19);

    bhr.commit(14);
    EXPECT_EQ(bhr.uncommitted_depth(), 6);

    // note: flushing 17 *removes* it from the history (the correct branch must
    // later be re-pushed)
    bhr.flush(17);
    EXPECT_EQ(bhr.uncommitted_depth(), 2);

    for (unsigned i = 16; i < 25; i++)
        bhr.push(i + 1, ProgramCounter(10 + 10 * i));
    EXPECT_EQ(bhr.uncommitted_depth(), 11);

    // note: flushing 21 *removes* it from the history (the correct branch must
    // later be re-pushed)
    bhr.flush(21);
    EXPECT_EQ(bhr.uncommitted_depth(), 6);

    bhr.commit(19);
    EXPECT_EQ(bhr.uncommitted_depth(), 1);
    bhr.commit(20);
    EXPECT_EQ(bhr.uncommitted_depth(), 0);
}

TEST_F(BranchHistoryTest, FlushOfLatestFetchRemovesOneEntry)
{
    bhr.push(27, 10_pc);
    bhr.push(28, 20_pc);
    bhr.push(31, 30_pc);

    bhr.flush(31);

    auto expected = PCVec{20_pc, 10_pc};
    EXPECT_EQ(bhr_to_vec(), expected);
}

TEST_F(BranchHistoryTest, FlushOfSecondYoungestFesthRemovesTwoEntries)
{
    bhr.push(27, 10_pc);
    bhr.push(28, 20_pc);
    bhr.push(31, 30_pc);

    bhr.flush(28);

    auto expected = PCVec{10_pc};
    EXPECT_EQ(bhr_to_vec(), expected);
}

TEST_F(BranchHistoryTest, FlushWillEliminateAnythingYounger)
{
    bhr.push(27, 10_pc);
    bhr.push(28, 20_pc);
    bhr.push(31, 30_pc);

    bhr.flush(30); // not in the BHR, still should flush 31

    auto expected = PCVec{20_pc, 10_pc};
    EXPECT_EQ(bhr_to_vec(), expected);
}

TEST_F(BranchHistoryTest, TestHistoryHashes)
{
    bhr.add_history_size(3);
    bhr.add_history_size(6);

    bhr.push(1, 4_pc);
    bhr.push(2, 28_pc);
    bhr.push(3, 1040_pc);

    bhr.start_hashing(4);

    auto hash_3_3 = bhr.hash(3);
    auto hash_3_6 = bhr.hash(6);

    EXPECT_NE(hash_3_3, 0);
    EXPECT_NE(hash_3_6, 0);

    bhr.push(4, 4_pc);
    bhr.push(5, 28_pc);
    bhr.push(6, 1040_pc);

    bhr.start_hashing(7);

    auto hash_6_3 = bhr.hash(3);
    auto hash_6_6 = bhr.hash(6);

    EXPECT_NE(hash_6_3, hash_6_6);
    EXPECT_NE(hash_3_6, hash_6_6);

    bhr.push(7, 4_pc);
    bhr.push(8, 28_pc);
    bhr.push(9, 1040_pc);
    bhr.push(10, 4_pc);
    bhr.push(11, 28_pc);
    bhr.push(12, 1040_pc);

    bhr.start_hashing(13);

    bhr.hash(3);
    bhr.hash(6);

    // We've tested that the hashes at prediction are matching up, but let's also see if the historical hashes
    // still match, as we would need to access them during training.
    //
    bhr.start_hashing(4);
    auto hash_3_3_again = bhr.hash(3);
    EXPECT_EQ(hash_3_3, hash_3_3_again);

    bhr.start_hashing(7);
    auto hash_6_6_again = bhr.hash(6);
    EXPECT_EQ(hash_6_6, hash_6_6_again);
}

class BranchHistoryTestFNV : public BranchHistoryTest
{
public:
    BranchHistoryTestFNV() : BranchHistoryTest{{"bhr.hash.type", "FNVHasher"}} {}
};

TEST_F(BranchHistoryTestFNV, TestHistoryHashes)
{
    bhr.add_history_size(3);
    bhr.add_history_size(6);

    bhr.push(1, 4_pc);
    bhr.push(2, 28_pc);
    bhr.push(3, 1040_pc);

    bhr.start_hashing(4);

    auto hash_3_3 = bhr.hash(3);
    auto hash_3_6 = bhr.hash(6);

    EXPECT_NE(hash_3_3, 0);
    EXPECT_NE(hash_3_6, 0);

    bhr.push(4, 4_pc);
    bhr.push(5, 28_pc);
    bhr.push(6, 1040_pc);

    bhr.start_hashing(7);

    auto hash_6_3 = bhr.hash(3);
    auto hash_6_6 = bhr.hash(6);

    EXPECT_NE(hash_6_3, hash_6_6);
    EXPECT_EQ(hash_3_3, hash_6_3);
    EXPECT_NE(hash_3_6, hash_6_6);

    bhr.push(7, 4_pc);
    bhr.push(8, 28_pc);
    bhr.push(9, 1040_pc);
    bhr.push(10, 4_pc);
    bhr.push(11, 28_pc);
    bhr.push(12, 1040_pc);

    bhr.start_hashing(13);

    auto hash_12_3 = bhr.hash(3);
    auto hash_12_6 = bhr.hash(6);

    EXPECT_EQ(hash_3_3, hash_12_3);
    EXPECT_EQ(hash_12_6, hash_6_6);

    // We've tested that the hashes at prediction are matching up, but let's also see if the historical hashes
    // still match, as we would need to access them during training.
    //
    bhr.start_hashing(4);
    auto hash_3_3_again = bhr.hash(3);
    EXPECT_EQ(hash_3_3, hash_3_3_again);

    bhr.start_hashing(7);
    auto hash_6_6_again = bhr.hash(6);
    EXPECT_EQ(hash_6_6, hash_6_6_again);
}

std::uint64_t F(ProgramCounter PC)
{
    return (PC.extract_range(5, 2) ^ PC.extract_range(9, 6) ^ PC.extract_range(13, 10) ^ PC.extract_range(17, 14)
            ^ PC.extract_range(21, 18) ^ PC.extract_range(25, 22) ^ PC.extract_range(29, 26) ^ PC.extract_range(33, 30)
            ^ PC.extract_range(37, 34) ^ PC.extract_range(41, 38) ^ PC.extract_range(45, 42) ^ PC.extract_range(49, 46)
            ^ PC.extract_range(52, 50));
}

std::uint64_t G(ProgramCounter PC)
{
    std::uint64_t result = 0;

    for (int i = 2; i <= 50; i += 2) {
        result ^= PC.extract_range(i + 1, i);
    }
    result ^= PC.extract_range(52, 52);

    return result;
}

TEST(XORHashTest, TestFHash)
{
    ProgramCounter pcs[] = {0x828d5e40334e0fc5_pc,
                            0xf6940606eb0c629e_pc,
                            0xb7386ee8284867e0_pc,
                            0x1db354cf33bc7a77_pc,
                            0xbd283c0ca9808998_pc,
                            0x53aeec00b6765573_pc,
                            0xa59cc73528432351_pc,
                            0xfabb84be3a3076f3_pc,
                            0x9d89d34f267ca8bc_pc,
                            0x4ea2abef2a931c86_pc,
                            0xffffabef2a931c86_pc};

    for (int i = 0; i < 10; i++)
        EXPECT_EQ(f_hash(pcs[i]), F(pcs[i]));
}

TEST(XORHashTest, TestGHash)
{
    ProgramCounter pcs[] = {
        0xe4060bced8ff52cb_pc,
        0xf325d022eff26415_pc,
        0x0abb93ed4fdc9f36_pc,
        0x978121d14a080000_pc,
        0xf4f674d24d9ff9d5_pc,
        0x117aa14ea97620b7_pc,
        0xcc24bba71a705129_pc,
        0x929129d3da12b7a8_pc,
        0x779a3764e998d7e7_pc,
        0xc1703eb5de2ad51d_pc,
        0xffff3eb5de2ad51d_pc,
    };

    for (int i = 0; i < 1; i++)
        EXPECT_EQ(g_hash(pcs[i]), G(pcs[i]));
}

class XORHashHistoryTest : public TestModule
{
protected:
    XORHasher hash{"hash"};
};

std::uint64_t fold_bhr(boost::dynamic_bitset<>& bhr, size_t depth, int nbits, int start_idx = 0)
{
    int total_bits = 2 * depth + (2 - start_idx);

    std::uint64_t result = 0;
    std::uint64_t word = 0;
    int idx = start_idx;
    int word_bit = 0;

    while (total_bits) {
        word |= bhr[idx] << word_bit;

        ++idx;
        ++word_bit;
        --total_bits;

        if (word_bit == nbits) {
            result ^= word;
            word = 0;
            word_bit = 0;
        }
    }

    return result ^ word;
}

TEST_F(XORHashHistoryTest, TestHistoryHash)
{
    ProgramCounter pcs[] = {
        0x29f0d52c925d126c_pc,
        0xf8cfc50e9d2b3cd4_pc,
        0x9e463b57a737d660_pc,
        0xa018f1d6543a77cc_pc,
        0x65d16bb184f0b634_pc,
        0x678f2c4df95c9888_pc,
        0x863ec661cb847548_pc,
        0xc3e58f88bc203c68_pc,
        0x191dab2e07b5b6bc_pc,
        0x4e979b8cbca3397c_pc,
    };

    BranchHistoryQueue history;

    for (size_t i = 0; i < 10; i++)
        history.emplace_back(i + 1, pcs[i], 0_pc, i);

    boost::dynamic_bitset<> bhr(64);

    // Build the BHR from the history based on the specification
    for (auto& entry : history) {
        auto f_pc = F(entry.pc);
        bhr <<= 2;
        bhr[0] = (f_pc >> 0) & 1;
        bhr[1] = (f_pc >> 1) & 1;
        bhr[2] = bhr[2] ^ ((f_pc >> 2) & 1);
        bhr[3] = bhr[3] ^ ((f_pc >> 3) & 1);
    }

    hash.start_hashing(history.rbegin(), history.rend());

    auto h = hash.hash_history(1, 10);
    EXPECT_EQ(h, fold_bhr(bhr, 1, 10));

    h = hash.hash_history(2, 10);
    EXPECT_EQ(h, fold_bhr(bhr, 2, 10));

    h = hash.hash_history(3, 10);
    EXPECT_EQ(h, fold_bhr(bhr, 3, 10));

    h = hash.hash_history(4, 10);
    EXPECT_EQ(h, fold_bhr(bhr, 4, 10));

    h = hash.hash_history(5, 10);
    EXPECT_EQ(h, fold_bhr(bhr, 5, 10));

    h = hash.hash_history(6, 10);
    EXPECT_EQ(h, fold_bhr(bhr, 6, 10));

    h = hash.hash_history(7, 10);
    EXPECT_EQ(h, fold_bhr(bhr, 7, 10));

    h = hash.hash_history(8, 10);
    EXPECT_EQ(h, fold_bhr(bhr, 8, 10));
}

TEST_F(XORHashHistoryTest, TestHashSetTag)
{
    ProgramCounter pcs[] = {
        0x29f0d52c925d126c_pc,
        0xf8cfc50e9d2b3cd4_pc,
        0x9e463b57a737d660_pc,
        0xa018f1d6543a77cc_pc,
        0x65d16bb184f0b634_pc,
        0x678f2c4df95c9888_pc,
        0x863ec661cb847548_pc,
        0xc3e58f88bc203c68_pc,
        0x191dab2e07b5b6bc_pc,
        0x4e979b8cbca3397c_pc,
    };

    BranchHistoryQueue history;

    for (size_t i = 0; i < 10; i++)
        history.emplace_back(i + 1, pcs[i], 0_pc, i);

    boost::dynamic_bitset<> bhr(64);

    // Build the BHR from the history based on the specification
    for (auto& entry : history) {
        auto f_pc = F(entry.pc);
        bhr <<= 2;
        bhr[0] = (f_pc >> 0) & 1;
        bhr[1] = (f_pc >> 1) & 1;
        bhr[2] = bhr[2] ^ ((f_pc >> 2) & 1);
        bhr[3] = bhr[3] ^ ((f_pc >> 3) & 1);
    }

    hash.start_hashing(history.rbegin(), history.rend());

    auto [set, tag] = hash.hash_set_tag(0_pc, 1, 10, 8);
    EXPECT_EQ(set, fold_bhr(bhr, 1, 10, 2));
    EXPECT_EQ(tag, fold_bhr(bhr, 1, 8));

    std::tie(set, tag) = hash.hash_set_tag(0_pc, 2, 10, 8);
    EXPECT_EQ(set, fold_bhr(bhr, 2, 10, 2));
    EXPECT_EQ(tag, fold_bhr(bhr, 2, 8));

    std::tie(set, tag) = hash.hash_set_tag(0_pc, 3, 10, 8);
    EXPECT_EQ(set, fold_bhr(bhr, 3, 10, 2));
    EXPECT_EQ(tag, fold_bhr(bhr, 3, 8));

    std::tie(set, tag) = hash.hash_set_tag(0_pc, 4, 10, 8);
    EXPECT_EQ(set, fold_bhr(bhr, 4, 10, 2));
    EXPECT_EQ(tag, fold_bhr(bhr, 4, 8));

    std::tie(set, tag) = hash.hash_set_tag(0_pc, 5, 10, 8);
    EXPECT_EQ(set, fold_bhr(bhr, 5, 10, 2));
    EXPECT_EQ(tag, fold_bhr(bhr, 5, 8));

    std::tie(set, tag) = hash.hash_set_tag(0_pc, 6, 10, 8);
    EXPECT_EQ(set, fold_bhr(bhr, 6, 10, 2));
    EXPECT_EQ(tag, fold_bhr(bhr, 6, 8));

    std::tie(set, tag) = hash.hash_set_tag(0_pc, 7, 10, 8);
    EXPECT_EQ(set, fold_bhr(bhr, 7, 10, 2));
    EXPECT_EQ(tag, fold_bhr(bhr, 7, 8));

    std::tie(set, tag) = hash.hash_set_tag(0_pc, 8, 10, 8);
    EXPECT_EQ(set, fold_bhr(bhr, 8, 10, 2));
    EXPECT_EQ(tag, fold_bhr(bhr, 8, 8));
}

class BranchHistoryBitsTester : public BranchHistoryBits
{
public:
    using BranchHistoryBits::BranchHistoryBits;
    std::uint64_t hash_entry(const BranchHistoryEntry& entry) const override { return entry.pc.extract_range(1, 0); }
};

TEST(TestBranchHistoryBits, GetBits)
{
    BranchHistoryQueue bhq;
    BranchHistoryBitsTester bhb;

    // Should set up a pattern in the bhb that looks like:
    //   1b1b1b1b1b1b1b1b1b1b
    //
    for (unsigned int i = 0; i < 40; i++)
        bhq.emplace_back(1, ProgramCounter(i), 0_pc, 0);

    bhb.build(bhq.rbegin(), bhq.rend());

    EXPECT_EQ(bhb[0], 0x1b1b1b1b1b1b1b1b);
    EXPECT_EQ(bhb[1], 0x1b1b);

    EXPECT_EQ(bhb.get(0, 1), 3);
    EXPECT_EQ(bhb.get(2, 3), 2);
    EXPECT_EQ(bhb.get(4, 5), 1);
    EXPECT_EQ(bhb.get(6, 7), 0);

    EXPECT_EQ(bhb.get(0, 3), 0xb);
    EXPECT_EQ(bhb.get(4, 7), 0x1);

    EXPECT_EQ(bhb.get(63, 64), 2);
    EXPECT_EQ(bhb.get(62, 65), 0xc);

    EXPECT_EQ(bhb.get(0, 63), 0x1b1b1b1b1b1b1b1b);
    EXPECT_EQ(bhb.get(2, 65), 0xc6c6c6c6c6c6c6c6);
}
