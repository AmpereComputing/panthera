//
// Copyright (c) 2023 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//
#include "ampere/sc/sc.h"
#include "ampere/sc/module.h"
#include "ampere/sc/testing/test.h"

namespace ampere::sc {

TEST(LocalRandomTest, DefaultIsDeterministic)
{
    LocalRandom random;
    EXPECT_EQ(random.get(), 2357136044ULL);
    EXPECT_EQ(random.get(), 2546248239ULL);
    EXPECT_EQ(random.get(), 3071714933ULL);
    EXPECT_EQ(random.get(), 3626093760ULL);
    EXPECT_EQ(random.get(), 2588848963ULL);
}

TEST(LocalRandomTest, SeedIsDeterministic)
{
    LocalRandom random(42);
    EXPECT_EQ(random.get(), 1608637542ULL);
    EXPECT_EQ(random.get(), 3421126067ULL);
    EXPECT_EQ(random.get(), 4083286876ULL);
    EXPECT_EQ(random.get(), 787846414ULL);
    EXPECT_EQ(random.get(), 3143890026ULL);
}

class SomeModule : public Module
{
public:
    SomeModule(sc_core::sc_module_name name) : Module(name) {}
    bool run() override { return false; }
};

TEST(LocalRandomTest, ModuleIsDeterministic)
{
    SomeModule some_module("something");
    LocalRandom random(some_module);
    EXPECT_EQ(random.get(), 429976880ULL);
    EXPECT_EQ(random.get(), 935622079ULL);
    EXPECT_EQ(random.get(), 2831663551ULL);
    EXPECT_EQ(random.get(), 3681571941ULL);
    EXPECT_EQ(random.get(), 1632866498ULL);
}

} // namespace ampere::sc

/*
 * vim: sw=4:ts=4:et:si:cindent:cino=\:0,(0,g0,t0:
 */
