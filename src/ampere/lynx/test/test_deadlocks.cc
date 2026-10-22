/*
 * Copyright (c) 2024-2025 Ampere Computing. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include <fmt/format.h>
#include <nlohmann/json.hpp>

#include "link_graph.h"
#include "model.h"
#include "rules.h"
#include "test.h"

using namespace ampere::lynx;

using json = nlohmann::json;

class Deadlocks : public ::testing::Test
{
public:
    Model mesh2x2;

    Deadlocks() { setup_mesh2x2(mesh2x2); }
};

TEST_F(Deadlocks, NoDeadlockWithXYRouting)
{
    Rules rules{}; // default rules, so X-first

    auto g = build_link_graph(mesh2x2, rules, Channel::REQ);
    EXPECT_FALSE(g.has_cycle());
}

/* Mesh 2x2 looks like this:
 *
 *   PxHA        PmHA     MCU
 *        \          \    /
 *         0,1 ------ 1,1
 *        / |        / |
 *   Core   |    Core  |
 *          |          |
 *   PxHA   |    PmHA  |
 *        \ |        \ |
 *         0,0 ------ 1,0
 *        /          /   \
 *   Core        Core    MCU
 */
TEST_F(Deadlocks, DeadlockWithXYOverrides)
{
    Rules rules{}; // default rules, so X-first
    rules.add_xy_override(mesh2x2.find_rtr({0, 1, 1}), mesh2x2.find_component({0, 0, 0, 1}));
    rules.add_xy_override(mesh2x2.find_rtr({0, 0, 0}), mesh2x2.find_component({0, 1, 1, 1}));

    auto g = build_link_graph(mesh2x2, rules, Channel::REQ);
    EXPECT_TRUE(g.has_cycle());

    const auto cycle = g.find_cycle();

    auto rtrs = std::vector<const Component*>{mesh2x2.find_rtr({0, 0, 0}),
                                              mesh2x2.find_rtr({0, 0, 1}),
                                              mesh2x2.find_rtr({0, 1, 1}),
                                              mesh2x2.find_rtr({0, 1, 0})};
    std::vector<bool> found_rtr(4);

    for (const auto& comp : cycle) {
        size_t i = 0;
        bool was_rtr = false;
        for (const auto& rtr : rtrs) {
            if (comp == rtr) {
                found_rtr[i] = true;
                was_rtr = true;
            }
            ++i;
        }
        EXPECT_TRUE(was_rtr);
    }

    EXPECT_TRUE(found_rtr[0]);
    EXPECT_TRUE(found_rtr[1]);
    EXPECT_TRUE(found_rtr[2]);
    EXPECT_TRUE(found_rtr[3]);
}

TEST_F(Deadlocks, DeadlockWithXYOverridesContributingPaths)
{
    Rules rules{}; // default rules, so X-first
    rules.add_xy_override(mesh2x2.find_rtr({0, 1, 1}), mesh2x2.find_component({0, 0, 0, 1}));
    rules.add_xy_override(mesh2x2.find_rtr({0, 0, 0}), mesh2x2.find_component({0, 1, 1, 1}));

    // Disallow REQ: PxHA -> MCU directly simply to make the test more repeatable. Otherwise
    // it might find a loop with the MCUs, or it might find a loop with the PmHAs.
    rules.disallow(Channel::REQ, mesh2x2.find_component({0, 0, 0, 1}), mesh2x2.find_component({0, 1, 0, 2}));
    rules.disallow(Channel::REQ, mesh2x2.find_component({0, 0, 0, 1}), mesh2x2.find_component({0, 1, 1, 2}));
    rules.disallow(Channel::REQ, mesh2x2.find_component({0, 0, 1, 1}), mesh2x2.find_component({0, 1, 0, 2}));
    rules.disallow(Channel::REQ, mesh2x2.find_component({0, 0, 1, 1}), mesh2x2.find_component({0, 1, 1, 2}));

    auto g = build_link_graph(mesh2x2, rules, Channel::REQ);
    ASSERT_TRUE(g.has_cycle());

    auto paths = g.find_cycle_with_contributing_paths();

    std::set<std::string> expected_string_paths = {
        "0: PxHA x0y0p1 -> 0: RTR x0y0 -> 0: RTR x0y1 -> 0: RTR x1y1 -> 0: PmHA x1y1p1",
        "0: PxHA x0y1p1 -> 0: RTR x0y1 -> 0: RTR x1y1 -> 0: RTR x1y0 -> 0: PmHA x1y0p1",
        "0: Core x1y1p0 -> 0: RTR x1y1 -> 0: RTR x1y0 -> 0: RTR x0y0 -> 0: PxHA x0y0p1",
        "0: Core x1y0p0 -> 0: RTR x1y0 -> 0: RTR x0y0 -> 0: RTR x0y1 -> 0: PxHA x0y1p1"};

    std::set<std::string> string_paths;

    for (const auto& path : paths)
        string_paths.insert(fmt::format("{}", path));

    EXPECT_EQ(string_paths, expected_string_paths);
}

class DominoHalfQuadDeadlocks : public ::testing::Test
{
public:
    Model model;

    DominoHalfQuadDeadlocks() { setup_domino_half_quad(model); }

    void add_all_cross_die_routes(Rules& rules)
    {
        for (const auto& [from, to] : model.all_connections()) {
            rules.add_cross_die_route(from->die_id(), to->die_id(), Path{from});
        }
    }
};

TEST_F(DominoHalfQuadDeadlocks, DominoDataDeadlock)
{
    Rules rules{};
    add_all_cross_die_routes(rules);
    auto mcu = model.find_component(Location(0, 3, 0, 2, 0));
    auto core = model.find_component(Location(4, 0, 0, 2, 0));
    auto adi = model.find_component(Location(0, 2, 0, 3));
    auto p = path(model, mcu, {adi, core}, rules, Channel::DAT);
    ASSERT_FALSE(p.empty());
    auto pl = paths_to_target(model, rules, mcu, core, Channel::DAT);
    ASSERT_FALSE(pl.empty());
    EXPECT_EQ(pl.size(), 2u);
    auto g = build_link_graph(model, rules, Channel::DAT);
    EXPECT_TRUE(g.has_cycle());
}

class DominoDeadlocks : public ::testing::Test
{
public:
    Model model;

    DominoDeadlocks() = default;

    void add_all_cross_die_routes(Rules& rules)
    {
        add_one_hop_routes(rules);
        add_xfirst_inter_iod_routes(rules);
        add_multi_hop_routes(rules);
    }

    void add_one_hop_routes(Rules& rules)
    {
        for (const auto& [from, to] : model.all_connections()) {
            rules.add_cross_die_route(from->die_id(), to->die_id(), Path{from});
        }
    }

    void connect_die_transitively(Rules& rules, u8 die1, u8 die2, u8 via)
    {
        auto die1_to_via = rules.cross_die_routes(die1, via);
        auto via_to_die2 = rules.cross_die_routes(via, die2);

        for (auto seg1 : die1_to_via) {
            for (auto seg2 : via_to_die2) {
                rules.add_cross_die_route(die1, die2, seg1 + seg2);
            }
        }
    }

    void connect_die_transitively(Rules& rules, u8 die1, u8 die2, u8 via1, u8 via2)
    {
        auto die1_to_via1 = rules.cross_die_routes(die1, via1);
        auto via1_to_via2 = rules.cross_die_routes(via1, via2);
        auto via2_to_die2 = rules.cross_die_routes(via2, die2);

        for (auto seg1 : die1_to_via1) {
            for (auto seg2 : via1_to_via2) {
                for (auto seg3 : via2_to_die2) {
                    rules.add_cross_die_route(die1, die2, seg1 + seg2 + seg3);
                }
            }
        }
    }

    void add_xfirst_inter_iod_routes(Rules& rules)
    {
        // Die layout:
        //
        //    0 | 2
        //   ---+---
        //    1 | 3
        //
        // To achieve "X first" routing at the die level:
        //     0 -> 3 must go through 2
        //     2 -> 1 must go through 0
        //     1 -> 2 must go through 3
        //     3 -> 0 must go through 1
        //
        connect_die_transitively(rules, 0, 3, 2);
        connect_die_transitively(rules, 2, 1, 0);
        connect_die_transitively(rules, 1, 2, 3);
        connect_die_transitively(rules, 3, 0, 1);
    }

    void add_multi_hop_routes(Rules& rules)
    {
        // CCLs to non-adjacent IODs
        connect_die_transitively(rules, 4, 1, 0);
        connect_die_transitively(rules, 4, 2, 0);
        connect_die_transitively(rules, 4, 3, 0);
        connect_die_transitively(rules, 5, 1, 0);
        connect_die_transitively(rules, 5, 2, 0);
        connect_die_transitively(rules, 5, 3, 0);

        connect_die_transitively(rules, 6, 0, 1);
        connect_die_transitively(rules, 6, 2, 1);
        connect_die_transitively(rules, 6, 3, 1);
        connect_die_transitively(rules, 7, 0, 1);
        connect_die_transitively(rules, 7, 2, 1);
        connect_die_transitively(rules, 7, 3, 1);

        connect_die_transitively(rules, 8, 1, 2);
        connect_die_transitively(rules, 8, 0, 2);
        connect_die_transitively(rules, 8, 3, 2);
        connect_die_transitively(rules, 9, 1, 2);
        connect_die_transitively(rules, 9, 0, 2);
        connect_die_transitively(rules, 9, 3, 2);

        connect_die_transitively(rules, 10, 1, 3);
        connect_die_transitively(rules, 10, 2, 3);
        connect_die_transitively(rules, 10, 0, 3);
        connect_die_transitively(rules, 11, 1, 3);
        connect_die_transitively(rules, 11, 2, 3);
        connect_die_transitively(rules, 11, 0, 3);

        // CCL to CCL
        for (u8 src = 4; src < 12; ++src) {
            for (u8 tgt = 4; tgt < 12; ++tgt) {
                if (src == tgt)
                    continue;

                u8 src_adj_iod = (src - 4) / 2;
                u8 tgt_adj_iod = (tgt - 4) / 2;

                if (src_adj_iod == tgt_adj_iod)
                    connect_die_transitively(rules, src, tgt, src_adj_iod);
                else
                    connect_die_transitively(rules, src, tgt, src_adj_iod, tgt_adj_iod);
            }
        }
    }
};

TEST_F(DominoDeadlocks, DominoReqDeadlockBug)
{
    Rules rules{};
    load_model("domino_req_deadlock_bug.json", model);
    load_rules("domino_req_deadlock_bug_rules.json", rules, model);
    auto g = build_link_graph(model, rules, Channel::REQ);
    EXPECT_TRUE(g.has_cycle());
}
