/*
 * Copyright (c) 2024 Ampere Computing. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "accumulator.h"
#include "fmt/format.h"
#include "protocols.h"
#include "rules.h"
#include "model.h"
#include "test.h"
#include "traffic.h"

using namespace ampere::lynx;

class TrafficTest : public ::testing::Test
{
protected:
    Accumulator acc;
    Model mesh1x1;
    Model mesh2x1;
    Model mesh2x2;

    TrafficTest()
    {
        setup_mesh1x1(mesh1x1);
        setup_mesh2x1(mesh2x1);
        setup_mesh2x2(mesh2x2);
    }

    float traffic_on_link(Opcode opcode, const Component* from, const Component* to)
    {
        return acc.traffic_on_link(Rules::default_channel_map(opcode), {from, to})[opcode];
    };
};

TEST_F(TrafficTest, ReadSharedToMemoryMesh1x1)
{
    Rules rules{}; // default rules, so X-first

    auto core = mesh1x1.find_component({0, 0, 0, 0});
    auto pxha = mesh1x1.find_component({0, 0, 0, 1});
    auto pmha = mesh1x1.find_component({0, 0, 0, 2});
    auto mcu = mesh1x1.find_component({0, 0, 0, 3});
    auto rtr = mesh1x1.find_component({0, 0, 0});

    // Inject traffic load of 100% ReadShared from the core
    protocols::ReadShared_L3Miss_DMT(Traffic{mesh1x1, rules, acc, 100.0}, core);

    EXPECT_EQ(traffic_on_link(Opcode::ReadShared, core, rtr), 100.0);
    EXPECT_EQ(traffic_on_link(Opcode::ReadShared, rtr, pxha), 100.0);
    EXPECT_EQ(traffic_on_link(Opcode::ReadShared, pxha, rtr), 100.0);
    EXPECT_EQ(traffic_on_link(Opcode::ReadShared, rtr, pmha), 100.0);
    EXPECT_EQ(traffic_on_link(Opcode::ReadNoSnpSep, pmha, rtr), 100.0);
    EXPECT_EQ(traffic_on_link(Opcode::ReadNoSnpSep, rtr, mcu), 100.0);
}

/* Mesh 2x1 looks like this:
 *
 *               PxHA     PmHA
 *                   \   /
 *         0,0 ------ 1,0
 *        /          /   \
 *   Core        PxHA     MCU
 *
 * TrafficTest should split 50/50 from the core to the PxHAs.
 */
TEST_F(TrafficTest, ReadSharedToMemoryMesh2x1)
{
    Rules rules{}; // default rules, so X-first

    auto core = mesh2x1.find_component({0, 0, 0, 0});
    auto pxha0 = mesh2x1.find_component({0, 1, 0, 0});
    auto pxha1 = mesh2x1.find_component({0, 1, 0, 1});
    auto rtr0 = mesh2x1.find_component({0, 0, 0});
    auto rtr1 = mesh2x1.find_component({0, 1, 0});

    // Inject traffic load of 100% ReadShared from the core
    protocols::ReadShared_L3Miss_DMT(Traffic{mesh2x1, rules, acc, 100.0}, core);

    EXPECT_EQ(traffic_on_link(Opcode::ReadShared, core, rtr0), 100.0);
    EXPECT_EQ(traffic_on_link(Opcode::ReadShared, rtr0, rtr1), 100.0);
    EXPECT_EQ(traffic_on_link(Opcode::ReadShared, rtr1, pxha0), 50.0);
    EXPECT_EQ(traffic_on_link(Opcode::ReadShared, rtr1, pxha1), 50.0);

    EXPECT_EQ(traffic_on_link(Opcode::ReadShared, pxha0, rtr1), 50.0);
    EXPECT_EQ(traffic_on_link(Opcode::ReadShared, pxha1, rtr1), 50.0);
}

TEST_F(TrafficTest, ReadSharedToMemoryMesh2x2)
{
    Rules rules{}; // default rules, so X-first

    // Inject traffic load of 100% ReadShared from the core in the upper left corner
    auto src = mesh2x2.find_component({0, 0, 1, 0});
    protocols::ReadShared_L3Miss_DMT(Traffic{mesh2x2, rules, acc, 100.0}, src);

    // We should see:
    //
    //     1) 50% ReadShared to the co-located PxHA
    //     2) 50% ReadShared to the PxHA at 0,0
    //     3) 25% ReadShared from 1,1 -> 1,0 and from 1,0 -> 1,1 because each PxHA's share
    //        will be split among the PmHAs and only half will flow north or south
    //
    auto traffic_to_colocated_pxha =
        traffic_on_link(Opcode::ReadShared, mesh2x2.find_rtr(src->loc()), mesh2x2.find_component({0, 0, 1, 1}));
    auto traffic_from_x0y1_to_x0y0 =
        traffic_on_link(Opcode::ReadShared, mesh2x2.find_rtr({0, 0, 1}), mesh2x2.find_rtr({0, 0, 0}));

    EXPECT_EQ(traffic_to_colocated_pxha, 50.0);
    EXPECT_EQ(traffic_from_x0y1_to_x0y0, 50.0);

    auto rtr10 = mesh2x2.find_rtr({0, 1, 0});
    auto rtr11 = mesh2x2.find_rtr({0, 1, 1});

    EXPECT_EQ(traffic_on_link(Opcode::ReadShared, rtr10, rtr11), 25.0);
    EXPECT_EQ(traffic_on_link(Opcode::ReadShared, rtr11, rtr10), 25.0);
}

TEST_F(TrafficTest, ReadSharedToMemoryMesh2x2WithDMT)
{
    Rules rules{}; // default rules, so X-first

    rules.dmt_allow(0, 0);

    // Inject traffic load of 100% ReadShared from the core in the upper left corner
    auto src = mesh2x2.find_component({0, 0, 1, 0});
    protocols::ReadShared_L3Miss(Traffic{mesh2x2, rules, acc, 100.0}, src);

    // We should see RespSepData responses from both PmHAs now that we have enabled
    // DMT everywhere.
    //
    auto rsp_from_pmha0 =
        traffic_on_link(Opcode::RespSepData, mesh2x2.find_component({0, 1, 1, 1}), mesh2x2.find_rtr({0, 1, 1}));
    auto rsp_from_pmha1 =
        traffic_on_link(Opcode::RespSepData, mesh2x2.find_component({0, 1, 0, 1}), mesh2x2.find_rtr({0, 1, 0}));

    EXPECT_EQ(rsp_from_pmha0, 50.0);
    EXPECT_EQ(rsp_from_pmha1, 50.0);
}

/** Set up two 2x2 meshes on die 0 and 1, then connect them with two ADI
 *  links. This allows us to test cases where there are two paths to a
 *  single target agent (i.e., two different ADIs through which to reach
 *  the ultimate target).
 */
class CrossDieTrafficTest : public ::testing::Test
{
protected:
    CrossDieTrafficTest()
    {
        setup_mesh2x2(model, 0);
        setup_mesh2x2(model, 1);

        auto adi_0_a = model.find_component({0, 1, 0, 3});
        auto adi_1_a = model.find_component({1, 1, 0, 3});
        auto adi_0_b = model.find_component({0, 1, 1, 3});
        auto adi_1_b = model.find_component({1, 1, 1, 3});

        model.connect_components(adi_0_a, adi_1_a);
        model.connect_components(adi_0_b, adi_1_b);

        rules.add_cross_die_routes(0, 1, {{adi_0_a}, {adi_0_b}});
        rules.add_cross_die_routes(1, 0, {{adi_1_a}, {adi_1_b}});
    }

    Model model;
    Rules rules;
    Accumulator acc;

    float traffic_on_link(Opcode opcode, const Component* from, const Component* to)
    {
        try {
            return acc.traffic_on_link(Rules::default_channel_map(opcode), {from, to})[opcode];
        } catch (std::out_of_range&) {
            return 0.0;
        }
    };
};

/** Test if traffic is split properly for cases where there are two paths
 *  to the same target. In this test cases, there are four total PmHAs,
 *  which should be targeted evenly by `ReadShared`. However, the two PmHAs
 *  that are on the remote die (die 1) are each reachable through two ADI
 *  links.
 *
 *  Traffic should be divided up first by target, and then by paths to that
 *  target to get an equal value arriving at each PmHA.
 */
TEST_F(CrossDieTrafficTest, MultiPathTrafficSplit)
{
    auto core = model.find_component({0, 0, 0, 0});
    protocols::ReadShared_L3Miss(Traffic{model, rules, acc, 100.0}, core);

    for (const auto* pmha : model.components_by_type(ComponentType::PmHA)) {
        const auto* rtr = model.find_rtr(pmha->loc());
        auto t = traffic_on_link(Opcode::ReadShared, rtr, pmha);
        EXPECT_NEAR(t, 100.0 / 4, 0.001);
    }
}
