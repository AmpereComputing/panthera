//
// Copyright (c) 2025 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//

#include "ampere/sc/map.h"

#include <array>
#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "ampere/sc/module.h"
#include "ampere/sc/testing/test_module.h"

struct MapObject : ampere::sc::Module
{
    explicit MapObject(const sc_core::sc_module_name &name) : Module(name) {}
};

struct MapTestBase : ampere::sc::TestModule
{
    ampere::sc::sc_map<MapObject> map;
    template<typename... Args>
    MapTestBase(Args &&...args) : map{"map", std::forward<Args>(args)...}
    {
    }
};

using EmptyMapTest = MapTestBase;

TEST_F(EmptyMapTest, Empty)
{
    EXPECT_TRUE(map.empty());
}

struct MapTest : MapTestBase
{
    MapTest() : MapTestBase(std::array{"a", "b", "c"}) {}
};

TEST_F(MapTest, Size)
{
    EXPECT_EQ(map.size(), 3);
}

TEST_F(MapTest, GetByIndex)
{
    EXPECT_STREQ(map[1].name(), "map.b");
}

TEST_F(MapTest, GetByName)
{
    EXPECT_STREQ(map["c"].name(), "map.c");
}

TEST_F(MapTest, Iterate)
{
    std::vector<std::string> names;
    for (const auto &e : map)
        names.emplace_back(e.name());

    EXPECT_THAT(names, ::testing::ElementsAre("map.a", "map.b", "map.c"));
}

/*
 * vim: sw=4:ts=4:et:si:cindent:cino=\:0,(0,g0,t0:
 */
