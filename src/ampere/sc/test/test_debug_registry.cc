//
// Copyright (c) 2025 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//

#include "ampere/sc/debug_registry.h"
#include "ampere/sc/module.h"
#include "ampere/sc/testing/test_module.h"

#include <gtest/gtest.h>

using ampere::sc::DebugRegistry;
using ampere::sc::Module;
using ampere::sc::TestModule;

struct A : Module
{
    explicit A(const sc_core::sc_module_name &name) : Module(name) {}
};
struct B : Module
{
    A a{"child"};
    explicit B(const sc_core::sc_module_name &name) : Module(name) {}
};

struct DebugRegistryTest : TestModule
{
    B b{"parent"};
    DebugRegistry registry;

    void SetUp() override
    {
        registry.add(b, "ABC");
        registry.add(b, "XYZ");
        registry.add(b.a, "123");
    }
};

TEST_F(DebugRegistryTest, NotEnabled)
{
    EXPECT_FALSE(registry.enabled(b, "567"));
    EXPECT_FALSE(registry.enabled(b.a, "567"));
}

TEST_F(DebugRegistryTest, DirectlyEnabled)
{
    EXPECT_TRUE(registry.enabled(b, "ABC"));
    EXPECT_TRUE(registry.enabled(b.a, "123"));
}

TEST_F(DebugRegistryTest, IndirectlyEnabled)
{
    EXPECT_TRUE(registry.enabled(b.a, "XYZ"));
}

TEST_F(DebugRegistryTest, AllEnabled)
{
    registry.add(b, "*");
    EXPECT_TRUE(registry.enabled(b, "missing"));
    EXPECT_TRUE(registry.enabled(b.a, "missing"));
}

/*
 * vim: sw=4:ts=4:et:si:cindent:cino=\:0,(0,g0,t0:
 */
