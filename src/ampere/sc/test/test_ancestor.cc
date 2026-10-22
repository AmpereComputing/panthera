//
// Copyright (c) 2021 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//

#include <gtest/gtest.h>

#include "ampere/sc/testing/test_module.h"

#include "ampere/sc/ancestor.h"
#include "ampere/sc/module.h"

using ampere::sc::Module;

struct Child : Module
{
    Child *other = nullptr;
    explicit Child(const sc_core::sc_module_name &name) : Module(name) {}

    void end_of_elaboration() override { other = ampere::sc::get_sibling<Child>(); }
};

struct ChildParent : Module
{
    Child child_a{"child_a"};
    explicit ChildParent(const sc_core::sc_module_name &name) : Module(name) {}
};

struct Grandparent;

struct Parent : Module
{
    Child child_a{"child_a"};
    Child child_b{"child_b"};
    ChildParent parent_c{"parent_c"};
    Grandparent *parent;
    explicit Parent(const sc_core::sc_module_name &name) : Module(name), parent(ampere::sc::get_nearest<Grandparent>())
    {
    }
};

struct Grandparent : Module
{
    Parent child{"parent"};
    Child other_child{"other_child"};
    explicit Grandparent(const sc_core::sc_module_name &name) : Module(name) {}
};

struct AncestorTest : ampere::sc::TestModule
{
    Grandparent grand_parent{"grand_parent"};
    void SetUp() override
    {
        tick(); // required to end elaboration
    }
};

TEST_F(AncestorTest, GetNearest)
{
    EXPECT_EQ(grand_parent.child.parent, &grand_parent);
}

TEST_F(AncestorTest, GetNearestAncestor)
{
    auto *parent = ampere::sc::get_nearest_ancestor<Grandparent>(&grand_parent.child);
    EXPECT_EQ(parent, &grand_parent);
}

TEST_F(AncestorTest, GetNearestAncestorNoneMatching)
{
    auto *parent = ampere::sc::get_nearest_ancestor<Child>(&grand_parent.child);
    EXPECT_EQ(parent, nullptr);
}

TEST_F(AncestorTest, GetSibling)
{
    EXPECT_EQ(grand_parent.child.child_a.other, &grand_parent.child.child_b);
    EXPECT_EQ(grand_parent.child.child_b.other, &grand_parent.child.child_a);
}

TEST_F(AncestorTest, GetSiblingNoneMatching)
{
    auto *sibling = ampere::sc::get_sibling<Parent>(&grand_parent.child.child_a);
    EXPECT_EQ(sibling, nullptr);
}

TEST_F(AncestorTest, GetNearestRelative)
{
    auto *sibling = ampere::sc::get_nearest_relative<Child>(&grand_parent.child.child_a);
    EXPECT_EQ(sibling, &grand_parent.child.child_a);

    auto *relative = ampere::sc::get_nearest_relative<Child>(&grand_parent.child);
    EXPECT_EQ(relative, &grand_parent.other_child);
}

TEST_F(AncestorTest, GetNearestDescendant)
{
    auto *child = ampere::sc::get_nearest_descendant<Child>(&grand_parent);
    EXPECT_EQ(child, &grand_parent.other_child);

    auto *parent = ampere::sc::get_nearest_descendant<ChildParent>(&grand_parent);
    EXPECT_EQ(parent, &grand_parent.child.parent_c);
}
