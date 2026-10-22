/*
 * Copyright (c) 2024 Ampere Computing. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "routing.h"
#include "rules.h"
#include "test.h"

using namespace ampere::lynx;

class TestPath : public ::testing::Test
{
public:
    TestPath() { setup_mesh2x2(model); }

    Model model;
};

TEST_F(TestPath, SimplePath)
{
    auto a = model.find_component({0, 0, 1, 0});
    auto b = model.find_component({0, 1, 0, 0});

    auto paths = paths_to_target(model, Rules{}, a, b, Channel::REQ);

    ASSERT_EQ(paths.size(), 1u);

    std::stringstream ss;
    ss << *paths.begin();

    EXPECT_EQ(ss.str(), "0: Core x0y1p0 -> 0: RTR x0y1 -> 0: RTR x1y1 -> 0: RTR x1y0 -> 0: Core x1y0p0");
}

class TestCrossDiePath : public ::testing::Test
{
public:
    TestCrossDiePath()
    {
        setup_mesh2x2(model, 0);
        setup_mesh2x2(model, 1);

        adis.resize(2);
        adis[0] = model.find_component({0, 1, 0, 3});
        adis[1] = model.find_component({1, 1, 0, 3});

        model.connect_components(adis[0], adis[1]);
    }

    Model model;
    std::vector<const Component*> adis;
};

TEST_F(TestCrossDiePath, CrossDiePath)
{
    auto a = model.find_component(Location{0, 0, 0, 1});
    auto b = model.find_component(Location{1, 1, 1, 1});

    Rules rules{};
    rules.add_cross_die_route(0, 1, Path{adis[0]});
    rules.add_cross_die_route(1, 0, Path{adis[1]});

    auto paths = paths_to_target(model, rules, a, b, Channel::REQ);

    ASSERT_EQ(paths.size(), 1u);

    std::stringstream ss;
    ss << *paths.begin();

    EXPECT_EQ(ss.str(),
              "0: PxHA x0y0p1 -> 0: RTR x0y0 -> 0: RTR x1y0 -> 0: ADI x1y0p3 -> 1: ADI x1y0p3 -> 1: RTR x1y0 -> 1: RTR "
              "x1y1 -> 1: PmHA x1y1p1");
}
