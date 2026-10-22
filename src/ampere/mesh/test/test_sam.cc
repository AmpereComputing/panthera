//
// Copyright (c) 2022 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//

#include <gtest/gtest.h>
#include <random>

#include "ampere/mesh/sam.h"
#include "ampere/sc/testing/test_module.h"

namespace ampere::mesh {

class SAMTest : public Test
{
protected:
    void SetUp() override { Environment::get_root()->parameters.clear(); }

    void regions(const char* cfg_str)
    {
        Environment::get_root()->parameters.add_setting("sam.regions", json::parse(cfg_str), "test bench");
    }
};

class SAMPublicPermute : public SAM
{
public:
    using SAM::permute;
    using SAM::SAM;
};

TEST_F(SAMTest, TestPermute)
{
    SAMPublicPermute sam{"sam"};

#define B(n) (((a) >> (n)) & 1)
    auto permute_the_hard_way = [](uint64_t a) -> uint64_t {
        auto p0 = B(0) ^ B(3) ^ B(6) ^ B(7) ^ B(9);
        auto p1 = B(0) ^ B(1) ^ B(2) ^ B(3) ^ B(5) ^ B(6) ^ B(8) ^ B(9);
        auto p2 = B(2) ^ B(3) ^ B(8) ^ B(9);
        auto p3 = B(2) ^ B(4) ^ B(9);
        auto p4 = B(0) ^ B(1) ^ B(4) ^ B(5) ^ B(6) ^ B(8) ^ B(9);
        auto p5 = B(3) ^ B(5) ^ B(9);
        auto p6 = B(0) ^ B(2) ^ B(5) ^ B(7) ^ B(9);
        auto p7 = B(2) ^ B(3) ^ B(6) ^ B(7) ^ B(9);
        auto p8 = B(0) ^ B(2) ^ B(3) ^ B(4) ^ B(7);
        auto p9 = B(3) ^ B(4);

        a = ((a & ~mask(10)) | (p0 << 0) | (p1 << 1) | (p2 << 2) | (p3 << 3) | (p4 << 4) | (p5 << 5) | (p6 << 6)
             | (p7 << 7) | (p8 << 8) | (p9 << 9));

        return a;
    };
#undef B

    std::uniform_int_distribution<uint64_t> dist{0, (1ULL << 42) - 1};
    std::default_random_engine engine;

    for (int i = 0; i < 100; ++i) {
        uint64_t a = dist(engine);
        ASSERT_EQ(sam.permute(a), permute_the_hard_way(a));
    }
}

} // namespace ampere::mesh
