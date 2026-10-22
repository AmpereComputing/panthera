//
// Copyright (c) 2018-2019 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//

#include <gmock/gmock.h>

#include "ampere/sc/module.h"
#include "ampere/sc/poly.h"
#include "ampere/sc/poly_constructors.h"
#include "ampere/sc/submodule.h"
#include "ampere/sc/testing/test.h"

namespace ampere::sc {

class A : public Module, poly<A>
{
public:
    explicit A(const sc_core::sc_module_name &name) : Module(name) {}
    virtual int magic() { return 42; }
};

class B : public A, poly<B>
{
public:
    explicit B(const sc_core::sc_module_name &name) : A(name) {}
    int magic() override { return 84; }
};

class Top : public Module
{
public:
    explicit Top(const sc_core::sc_module_name &name) : Module(name) {}
    Submodule<A> a{"a"};
};

class SubmoduleTest : public Test
{};

TEST_F(SubmoduleTest, SubmoduleCreatesAByDefault)
{
    Top top{"top"};
    EXPECT_EQ(top.a->magic(), 42);
}

TEST_F(SubmoduleTest, SubmoduleCreatesBIfAsked)
{
    get_environment()->parameters.add_setting("top.a.type", "ampere::sc::B", "test");
    Top top{"top"};

    EXPECT_EQ(top.a->magic(), 84);
}

TEST_F(SubmoduleTest, SubmoduleCreatesBWithShortName)
{
    get_environment()->parameters.add_setting("top.a.type", "sc::B", "test");
    Top top{"top"};

    EXPECT_EQ(top.a->magic(), 84);
}

TEST_F(SubmoduleTest, SubmoduleCreatesBWithShortestName)
{
    get_environment()->parameters.add_setting("top.a.type", "B", "test");
    Top top{"top"};

    EXPECT_EQ(top.a->magic(), 84);
}

namespace n1 {
struct C : public A, poly<C>
{
    explicit C(const sc_core::sc_module_name &name) : A(name) {}
    int magic() override { return 92; }
};
} // namespace n1

namespace n2 {
struct C : public A, poly<C>
{
    explicit C(const sc_core::sc_module_name &name) : A(name) {}
    int magic() override { return 101; }
};
} // namespace n2

TEST_F(SubmoduleTest, SubmoduleHandlesNamespace)
{
    get_environment()->parameters.add_setting("top.a.type", "n1::C", "test");
    get_environment()->parameters.add_setting("top2.a.type", "n2::C", "test");
    Top top{"top"};
    Top top2{"top2"};

    EXPECT_EQ(top.a->magic(), 92);
    EXPECT_EQ(top2.a->magic(), 101);
}

TEST_F(SubmoduleTest, SubmoduleDetectsAmbiguousName)
{
    get_environment()->parameters.add_setting("top.a.type", "C", "test");
    EXPECT_ANY_THROW({ Top top("top"); });
}

class X : public Module
{
public:
    explicit X(const sc_core::sc_module_name &name) : Module(name) {}
    virtual int magic() = 0;
};

class Y : public implements<Y, X>
{
public:
    explicit Y(const sc_core::sc_module_name &name) : implements<Y, X>(name) {}
    int magic() override { return 1; }
};

class Z : public implements<Z, X>
{
public:
    explicit Z(const sc_core::sc_module_name &name) : implements<Z, X>(name) {}
    int magic() override { return 2; }
};

class ImplementsTop : public Module
{
public:
    explicit ImplementsTop(const sc_core::sc_module_name &name) : Module(name) {}
    Submodule<X> x{"x"};
};

TEST_F(SubmoduleTest, SubmoduleImpelementsDefault)
{
    get_environment()->parameters.add_setting("top.x.type", "ampere::sc::Y", "test");
    ImplementsTop top{"top"};

    EXPECT_EQ(top.x->magic(), 1);
}

TEST_F(SubmoduleTest, SubmoduleImpelementsOverride)
{
    get_environment()->parameters.add_setting("top.x.type", "ampere::sc::Z", "test");
    ImplementsTop top{"top"};

    EXPECT_EQ(top.x->magic(), 2);
}

TEST_F(SubmoduleTest, SubmoduleImpelementations)
{
    auto range = find_registered_modules_for_constructor<X>();
    EXPECT_EQ(range.begin()->first.first, "ampere::sc::X");

    auto values = range | std::views::values;
    std::vector<std::string> types(values.begin(), values.end());
    EXPECT_THAT(types, ::testing::UnorderedElementsAre("Y", "Z"));
}

class I;
using i_poly_def = poly_definition<I, poly_constructor_default, poly_constructor_clock, poly_constructor<const int &>>;

class I : public Module, i_poly_def::base
{
public:
    explicit I(const sc_core::sc_module_name &name) : Module(name) {}
    I(const sc_core::sc_module_name &name, Clock *clock) : Module(name, clock) {}
    I(const sc_core::sc_module_name &name, const int &x) : Module(name) {}
    virtual int magic() { return 1; }
};

class J : public I, i_poly_def::implementation<J>
{
    int x = 2;

public:
    explicit J(const sc_core::sc_module_name &name) : I(name) {}
    J(const sc_core::sc_module_name &name, Clock *clock) : I(name, clock), x(3) {}
    J(const sc_core::sc_module_name &name, const int &x) : I(name, x), x(x) {}
    int magic() override { return x; }
};

class OtherConstructorsTop : public Module
{
public:
    explicit OtherConstructorsTop(const sc_core::sc_module_name &name) : Module(name), i("i") {}
    explicit OtherConstructorsTop(const sc_core::sc_module_name &name, Clock *clock) : Module(name), i("i", clock) {}
    explicit OtherConstructorsTop(const sc_core::sc_module_name &name, const int &x) : Module(name), i("i", x) {}
    template<typename... Args>
    explicit OtherConstructorsTop(int, const sc_core::sc_module_name &name, Args &&...args)
        : Module(name), i("i", std::forward<Args>(args)...)
    {
    }
    Submodule<I> i;
};

TEST_F(SubmoduleTest, NormalConstructor)
{
    get_environment()->parameters.add_setting("top.i.type", "ampere::sc::J", "test");
    OtherConstructorsTop top{"top"};

    EXPECT_EQ(top.i->magic(), 2);
}

TEST_F(SubmoduleTest, PointerConstructor)
{
    get_environment()->parameters.add_setting("top.i.type", "ampere::sc::J", "test");
    OtherConstructorsTop top{"top", get_clock()};

    EXPECT_EQ(top.i->magic(), 3);
}

TEST_F(SubmoduleTest, ReferenceConstructor)
{
    get_environment()->parameters.add_setting("top.i.type", "ampere::sc::J", "test");
    int x = 5;
    OtherConstructorsTop top{"top", x};

    EXPECT_EQ(top.i->magic(), 5);
}

TEST_F(SubmoduleTest, OtherConstructorForwarding)
{
    get_environment()->parameters.add_setting("top.i.type", "ampere::sc::J", "test");
    OtherConstructorsTop top{1, "top", get_clock()};

    EXPECT_EQ(top.i->magic(), 3);
}

template<bool Default = true>
class EnabledTop : public Module
{
public:
    explicit EnabledTop(const sc_core::sc_module_name &name) : Module(name) {}
    EnabledSubmodule<Top> other{"other", Default};
};

TEST_F(SubmoduleTest, EnabledSubmoduleByDefault)
{
    EnabledTop<> top{"top"};
    EXPECT_TRUE(top.other.enabled());
}

TEST_F(SubmoduleTest, EnabledSubmoduleByDefaultDisableSetting)
{
    get_environment()->parameters.add_setting("top.other.enable", false, "test");
    EnabledTop<> top{"top"};
    EXPECT_FALSE(top.other.enabled());
}

TEST_F(SubmoduleTest, EnabledSubmoduleDisabledByDefault)
{
    EnabledTop<false> top{"top"};
    EXPECT_FALSE(top.other.enabled());
}

TEST_F(SubmoduleTest, EnabledSubmoduleDisabledByDefaultEnableSetting)
{
    get_environment()->parameters.add_setting("top.other.enable", true, "test");
    EnabledTop<false> top{"top"};
    EXPECT_TRUE(top.other.enabled());
}

} // namespace ampere::sc

/*
 * vim: sw=4:ts=4:et:si:cindent:cino=\:0,(0,g0,t0:
 */
