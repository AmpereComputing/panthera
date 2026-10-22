//
// Copyright (c) 2023 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//

#include <gtest/gtest.h>
#include <random>

#include "ampere/mesh/sam_xymap.h"
#include "ampere/sc/testing/test_module.h"

namespace ampere::mesh {

class XYMapTest : public ::testing::Test
{
protected:
    void SetUp() override { Environment::get_root()->parameters.clear(); }

    using addr_t = XYMap::addr_t;

    // Accessors into XYMap private methods, because this class is a friend.
    std::pair<size_t, addr_t> atoxy(XYMap& map, addr_t a) { return map.atoxy(a); }
    addr_t inv_atoxy(XYMap& map, std::pair<size_t, addr_t> xy) { return map.inv_atoxy(xy); }

    addr_t rand_addr() { return distribution(engine) & addr_mask; }

    addr_t rand_cl_addr() { return distribution(engine) & cl_addr_mask; }

    static constexpr uint64_t addr_mask = mask(XYMap::HA_BITS) & ~mask(XYMap::CL_BITS);
    static constexpr uint64_t cl_addr_mask = mask(XYMap::HA_BITS - XYMap::CL_BITS);

    std::uniform_int_distribution<uint64_t> distribution;
    std::mt19937_64 engine;
};

TEST_F(XYMapTest, TestAtoXYInversion)
{
    XYMap map{24};

    for (int i = 0; i < 100; i++) {
        addr_t a = rand_cl_addr();
        auto xy = atoxy(map, a);
        ASSERT_TRUE(std::get<0>(xy) <= 24);
        ASSERT_EQ(inv_atoxy(map, xy), a);
    }
}

TEST_F(XYMapTest, TestDecodeEncodeInversion)
{
    XYMap map{96};

    for (int i = 0; i < 100; i++) {
        auto a = PhysAddr{rand_addr()};
        ASSERT_EQ(map.encode(map.decode(a)), a);
    }
}

TEST_F(XYMapTest, TestBijective16x32)
{
    constexpr size_t N = 16;
    constexpr size_t M = 32;

    std::array<std::bitset<M>, N> xymap;

    XYMap map{N, M, 1, 1, true};

    for (addr_t a = 0; a < N * M * 64; a += 64) {
        auto [x, y] = map.decode(PhysAddr{a});
        ASSERT_LT(x, N);
        ASSERT_LT(y.line_number(), M);
        EXPECT_EQ(xymap[x][y.line_number()], 0);
        xymap[x][y.line_number()] = true;
    }

    for (size_t n = 0; n < N; ++n)
        EXPECT_TRUE(xymap[n].all());
}

TEST_F(XYMapTest, TestBijective24x32)
{
    constexpr size_t N = 24;
    constexpr size_t M = 32;

    std::array<std::bitset<M>, N> xymap;

    XYMap map{N, M, 1, 1, true};

    for (addr_t a = 0; a < N * M * 64; a += 64) {
        auto [x, y] = map.decode(PhysAddr{a});
        ASSERT_LT(x, N);
        ASSERT_LT(y.line_number(), M);
        EXPECT_EQ(xymap[x][y.line_number()], 0);
        xymap[x][y.line_number()] = true;
    }

    for (size_t n = 0; n < N; ++n)
        EXPECT_TRUE(xymap[n].all());
}

TEST_F(XYMapTest, TestBijective24x48)
{
    constexpr size_t N = 24;
    constexpr size_t M = 48;

    std::array<std::bitset<M>, N> xymap;

    XYMap map{N, M, 1, 1, true};

    for (addr_t a = 0; a < N * M * 64; a += 64) {
        auto [x, y] = map.decode(PhysAddr{a});
        ASSERT_LT(x, N);
        ASSERT_LT(y.line_number(), M);
        EXPECT_EQ(xymap[x][y.line_number()], 0);
        xymap[x][y.line_number()] = true;
    }

    for (size_t n = 0; n < N; ++n)
        EXPECT_TRUE(xymap[n].all());
}

TEST_F(XYMapTest, TestTwoBanger)
{
    constexpr size_t N = 16;
    constexpr size_t M = 32;

    XYMap map{N, M, 1, 2, false};

    int phase = 0;
    size_t prev_x = 0;

    for (addr_t a = 0; a < N * M * 64; a += 64) {
        auto [x, y] = map.decode(PhysAddr{a});

        if (phase == 0)
            prev_x = x;
        else
            EXPECT_EQ(x, prev_x);

        phase = (phase + 1) % 2;
    }
}

TEST_F(XYMapTest, TestFourBanger)
{
    constexpr size_t N = 16;
    constexpr size_t M = 32;

    XYMap map{N, M, 1, 4, false};

    int phase = 0;
    size_t prev_x = 0;

    for (addr_t a = 0; a < N * M * 64; a += 64) {
        auto [x, y] = map.decode(PhysAddr{a});

        if (phase == 0)
            prev_x = x;
        else
            EXPECT_EQ(x, prev_x);

        phase = (phase + 1) % 4;
    }
}

TEST_F(XYMapTest, TestImbalance)
{
    constexpr size_t N = 24;
    constexpr size_t num_sets = 1ull << 14;
    constexpr unsigned int rounds = 4;

    XYMap map{N};

    std::vector<unsigned int> xcount(N);
    std::vector<unsigned int> ycount(num_sets);

    for (addr_t a = 0; a < N * num_sets * rounds * 64; a += 64) {
        auto [x, y] = map.decode(PhysAddr{a});
        auto set = (y.value() >> 6) & (num_sets - 1);
        ++xcount[x];
        ++ycount[set];
    }

    auto xmin = *std::min_element(xcount.begin(), xcount.end());
    auto xmax = *std::max_element(xcount.begin(), xcount.end());

    auto ymin = *std::min_element(ycount.begin(), ycount.end());
    auto ymax = *std::max_element(ycount.begin(), ycount.end());

    EXPECT_EQ(xmin, rounds * num_sets);
    EXPECT_EQ(xmax, rounds * num_sets);

    EXPECT_EQ(ymin, N * rounds);
    EXPECT_EQ(ymax, N * rounds);
}

TEST_F(XYMapTest, TestImbalancePmHA)
{
    constexpr size_t N = 96;
    constexpr size_t num_sets = 1ull << 15;
    constexpr unsigned int rounds = 2;

    XYMap map{N};

    std::vector<unsigned int> xcount(N);
    std::vector<unsigned int> ycount(num_sets);

    for (addr_t a = 0; a < N * num_sets * rounds * 64; a += 64) {
        auto [x, y] = map.decode(PhysAddr{a});
        auto set = (y.value() >> 6) & (num_sets - 1);
        ++xcount[x];
        ++ycount[set];
    }

    auto xmin = *std::min_element(xcount.begin(), xcount.end());
    auto xmax = *std::max_element(xcount.begin(), xcount.end());

    auto ymin = *std::min_element(ycount.begin(), ycount.end());
    auto ymax = *std::max_element(ycount.begin(), ycount.end());

    EXPECT_EQ(xmin, rounds * num_sets);
    EXPECT_EQ(xmax, rounds * num_sets);

    EXPECT_EQ(ymin, N * rounds);
    EXPECT_EQ(ymax, N * rounds);
}

} // namespace ampere::mesh
