/*
 * Copyright (c) 2024 Ampere Computing. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include <fstream>
#include <nlohmann/json.hpp>

#include "component.h"
#include "location.h"
#include "link_graph.h"
#include "lynx.enum.h"
#include "model.h"
#include "rules.h"

using namespace ampere::lynx;

void load_model(const std::string& fname, Model& model)
{
    std::ifstream ifs{fname[0] == '/' ? fname : std::string{TEST_FIXTURES} + "/" + fname};
    nlohmann::json j;
    ifs >> j;
    j.get_to(model);
}

void load_rules(const std::string& fname, Rules& rules, const Model& model)
{
    std::ifstream ifs{fname[0] == '/' ? fname : std::string{TEST_FIXTURES} + "/" + fname};
    nlohmann::json j;
    ifs >> j;
    from_json(j, rules, model);
}

void load_graph(const std::string& fname, LinkGraph& graph, const Model& model)
{
    std::ifstream ifs{fname[0] == '/' ? fname : std::string{TEST_FIXTURES} + "/" + fname};
    nlohmann::json j;
    ifs >> j;
    from_json(j, graph, model);
}

/* Mesh 1x1 looks like this:
 *
 *   PxHA       PmHA
 *        \   /
 *         0,0
 *        /   \
 *   Core      MCU
 */
void setup_mesh1x1(Model& model, u8 die_id)
{
    model.add_component(Component{ComponentType::RTR, Location{die_id, 0, 0}});

    model.add_component(Component{ComponentType::Core, Location{die_id, 0, 0, 0}});
    model.add_component(Component{ComponentType::PxHA, Location{die_id, 0, 0, 1}});
    model.add_component(Component{ComponentType::PmHA, Location{die_id, 0, 0, 2}});
    model.add_component(Component{ComponentType::MCU, Location{die_id, 0, 0, 3}});
}

/* Mesh 2x1 looks like this:
 *
 *               PxHA     PmHA
 *                   \   /
 *         0,0 ------ 1,0
 *        /          /   \
 *   Core        PxHA     MCU
 */
void setup_mesh2x1(Model& model, u8 die_id)
{
    model.add_component(Component{ComponentType::RTR, Location{die_id, 0, 0}});
    model.add_component(Component{ComponentType::RTR, Location{die_id, 1, 0}});

    model.add_component(Component{ComponentType::Core, Location{die_id, 0, 0, 0}});

    model.add_component(Component{ComponentType::PxHA, Location{die_id, 1, 0, 0}});
    model.add_component(Component{ComponentType::PxHA, Location{die_id, 1, 0, 1}});
    model.add_component(Component{ComponentType::PmHA, Location{die_id, 1, 0, 2}});
    model.add_component(Component{ComponentType::MCU, Location{die_id, 1, 0, 3}});
}

/* Mesh 2x2 looks like this:
 *
 *   PxHA        PmHA     MCU
 *        \          \   /
 *         0,1 ------ 1,1
 *        / |        / | \
 *   Core   |    Core  |  ADI
 *          |          |
 *   PxHA   |    PmHA  |  MCU
 *        \ |        \ | /
 *         0,0 ------ 1,0
 *        /          /   \
 *   Core        Core     ADI
 */
void setup_mesh2x2(Model& model, u8 die_id)
{
    model.add_component(Component{ComponentType::RTR, Location{die_id, 0, 0}});
    model.add_component(Component{ComponentType::RTR, Location{die_id, 0, 1}});
    model.add_component(Component{ComponentType::RTR, Location{die_id, 1, 0}});
    model.add_component(Component{ComponentType::RTR, Location{die_id, 1, 1}});

    model.add_component(Component{ComponentType::Core, Location{die_id, 0, 0, 0}});
    model.add_component(Component{ComponentType::PxHA, Location{die_id, 0, 0, 1}});

    model.add_component(Component{ComponentType::Core, Location{die_id, 1, 0, 0}});
    model.add_component(Component{ComponentType::PmHA, Location{die_id, 1, 0, 1}});
    model.add_component(Component{ComponentType::MCU, Location{die_id, 1, 0, 2}});
    model.add_component(Component{ComponentType::ADI, Location{die_id, 1, 0, 3}});

    model.add_component(Component{ComponentType::Core, Location{die_id, 0, 1, 0}});
    model.add_component(Component{ComponentType::PxHA, Location{die_id, 0, 1, 1}});

    model.add_component(Component{ComponentType::Core, Location{die_id, 1, 1, 0}});
    model.add_component(Component{ComponentType::PmHA, Location{die_id, 1, 1, 1}});
    model.add_component(Component{ComponentType::MCU, Location{die_id, 1, 1, 2}});
    model.add_component(Component{ComponentType::ADI, Location{die_id, 1, 1, 3}});
}

void setup_routers(Model& model, u8 die_id, int cols, int rows)
{
    for (u8 x = 0; x < cols; ++x) {
        for (u8 y = 0; y < rows; ++y)
            model.add_component(Component{ComponentType::RTR, Location{die_id, x, y}});
    }
}

void setup_domino_iod(Model& model, u8 die_id)
{
    setup_routers(model, die_id, 4, 3);
    model.add_component(Component{ComponentType::PmHA, Location{die_id, 0, 1, 2, 0}});
    model.add_component(Component{ComponentType::PmHA, Location{die_id, 1, 0, 1, 0}});
    model.add_component(Component{ComponentType::PmHA, Location{die_id, 1, 1, 0, 0}});
    model.add_component(Component{ComponentType::PmHA, Location{die_id, 1, 1, 2, 0}});
    model.add_component(Component{ComponentType::PmHA, Location{die_id, 1, 2, 0, 0}});
    model.add_component(Component{ComponentType::PmHA, Location{die_id, 2, 0, 1, 0}});
    model.add_component(Component{ComponentType::PmHA, Location{die_id, 2, 1, 0, 0}});
    model.add_component(Component{ComponentType::PmHA, Location{die_id, 2, 1, 2, 0}});
    model.add_component(Component{ComponentType::PmHA, Location{die_id, 2, 2, 0, 0}});
    model.add_component(Component{ComponentType::PmHA, Location{die_id, 3, 0, 1, 0}});
    model.add_component(Component{ComponentType::PmHA, Location{die_id, 3, 1, 0, 0}});
    model.add_component(Component{ComponentType::PmHA, Location{die_id, 3, 2, 0, 0}});
    model.add_component(Component{ComponentType::MCU, Location{die_id, 3, 0, 2, 0}});
    model.add_component(Component{ComponentType::MCU, Location{die_id, 3, 1, 2, 0}});
    model.add_component(Component{ComponentType::MCU, Location{die_id, 3, 1, 3, 0}});
    model.add_component(Component{ComponentType::MCU, Location{die_id, 3, 2, 3, 0}});
    model.add_component(Component{ComponentType::ADI, Location{die_id, 0, 2, 0}, "v0"});
    model.add_component(Component{ComponentType::ADI, Location{die_id, 0, 2, 1}, "v1"});
    model.add_component(Component{ComponentType::ADI, Location{die_id, 1, 2, 2}, "h0"});
    model.add_component(Component{ComponentType::ADI, Location{die_id, 2, 2, 2}, "h1"});
    model.add_component(Component{ComponentType::ADI, Location{die_id, 0, 0, 3}, "b0"});
    model.add_component(Component{ComponentType::ADI, Location{die_id, 1, 0, 3}, "b1"});
    model.add_component(Component{ComponentType::ADI, Location{die_id, 2, 0, 3}, "a0"});
    model.add_component(Component{ComponentType::ADI, Location{die_id, 3, 0, 0}, "a1"});
}

void setup_domino_ccl(Model& model, u8 die_id)
{
    setup_routers(model, die_id, 4, 4);
    model.add_component(Component{ComponentType::Core, Location{die_id, 0, 0, 2, 0}});
    model.add_component(Component{ComponentType::Core, Location{die_id, 0, 1, 2, 0}});
    model.add_component(Component{ComponentType::Core, Location{die_id, 0, 2, 2, 0}});
    model.add_component(Component{ComponentType::Core, Location{die_id, 0, 3, 2, 0}});
    model.add_component(Component{ComponentType::Core, Location{die_id, 1, 0, 1, 0}});
    model.add_component(Component{ComponentType::Core, Location{die_id, 1, 1, 1, 0}});
    model.add_component(Component{ComponentType::Core, Location{die_id, 1, 2, 1, 0}});
    model.add_component(Component{ComponentType::Core, Location{die_id, 1, 3, 1, 0}});
    model.add_component(Component{ComponentType::Core, Location{die_id, 2, 0, 1, 0}});
    model.add_component(Component{ComponentType::Core, Location{die_id, 2, 1, 1, 0}});
    model.add_component(Component{ComponentType::Core, Location{die_id, 2, 2, 1, 0}});
    model.add_component(Component{ComponentType::Core, Location{die_id, 2, 3, 1, 0}});
    model.add_component(Component{ComponentType::Core, Location{die_id, 3, 0, 1, 0}});
    model.add_component(Component{ComponentType::Core, Location{die_id, 3, 1, 1, 0}});
    model.add_component(Component{ComponentType::Core, Location{die_id, 3, 2, 1, 0}});
    model.add_component(Component{ComponentType::Core, Location{die_id, 3, 3, 1, 0}});
    model.add_component(Component{ComponentType::PxHA, Location{die_id, 1, 0, 2, 0}});
    model.add_component(Component{ComponentType::PxHA, Location{die_id, 1, 1, 2, 0}});
    model.add_component(Component{ComponentType::PxHA, Location{die_id, 1, 2, 2, 0}});
    model.add_component(Component{ComponentType::PxHA, Location{die_id, 1, 3, 2, 0}});
    model.add_component(Component{ComponentType::PxHA, Location{die_id, 2, 0, 2, 0}});
    model.add_component(Component{ComponentType::PxHA, Location{die_id, 2, 1, 2, 0}});
    model.add_component(Component{ComponentType::PxHA, Location{die_id, 2, 2, 2, 0}});
    model.add_component(Component{ComponentType::PxHA, Location{die_id, 2, 3, 2, 0}});
    model.add_component(Component{ComponentType::ADI, Location{die_id, 0, 0, 1}, "a1"});
    model.add_component(Component{ComponentType::ADI, Location{die_id, 0, 1, 1}, "a0"});
    model.add_component(Component{ComponentType::ADI, Location{die_id, 0, 2, 1}, "b1"});
    model.add_component(Component{ComponentType::ADI, Location{die_id, 0, 3, 1}, "b0"});
}

void setup_domino_half_quad(Model& model)
{
    setup_domino_iod(model, 0);
    setup_domino_ccl(model, 4);

    model.connect_components(model.find_component(Location{4, 0, 0, 1}), model.find_component(Location{0, 2, 0, 3}));
    model.connect_components(model.find_component(Location{4, 0, 1, 1}), model.find_component(Location{0, 3, 0, 0}));
}

void setup_domino_4iod(Model& model)
{
    setup_domino_iod(model, 0);
    setup_domino_iod(model, 1);
    setup_domino_iod(model, 2);
    setup_domino_iod(model, 3);

    model.connect_components(model.find_by_tag(0, "v0"), model.find_by_tag(1, "v0"));
    model.connect_components(model.find_by_tag(0, "v1"), model.find_by_tag(1, "v1"));

    model.connect_components(model.find_by_tag(0, "h0"), model.find_by_tag(2, "h0"));
    model.connect_components(model.find_by_tag(0, "h1"), model.find_by_tag(2, "h1"));

    model.connect_components(model.find_by_tag(2, "v0"), model.find_by_tag(3, "v0"));
    model.connect_components(model.find_by_tag(2, "v1"), model.find_by_tag(3, "v1"));

    model.connect_components(model.find_by_tag(3, "h0"), model.find_by_tag(1, "h0"));
    model.connect_components(model.find_by_tag(3, "h1"), model.find_by_tag(1, "h1"));
}

void setup_domino(Model& model)
{
    setup_domino_4iod(model);

    for (u8 i = 4; i < 12; ++i)
        setup_domino_ccl(model, i);

    model.connect_components(model.find_by_tag(4, "a0"), model.find_by_tag(0, "a0"));
    model.connect_components(model.find_by_tag(4, "a1"), model.find_by_tag(0, "a1"));
    model.connect_components(model.find_by_tag(5, "b0"), model.find_by_tag(0, "b0"));
    model.connect_components(model.find_by_tag(5, "b1"), model.find_by_tag(0, "b1"));

    model.connect_components(model.find_by_tag(6, "a0"), model.find_by_tag(1, "a0"));
    model.connect_components(model.find_by_tag(6, "a1"), model.find_by_tag(1, "a1"));
    model.connect_components(model.find_by_tag(7, "b0"), model.find_by_tag(1, "b0"));
    model.connect_components(model.find_by_tag(7, "b1"), model.find_by_tag(1, "b1"));

    model.connect_components(model.find_by_tag(8, "a0"), model.find_by_tag(2, "a0"));
    model.connect_components(model.find_by_tag(8, "a1"), model.find_by_tag(2, "a1"));
    model.connect_components(model.find_by_tag(9, "b0"), model.find_by_tag(2, "b0"));
    model.connect_components(model.find_by_tag(9, "b1"), model.find_by_tag(2, "b1"));

    model.connect_components(model.find_by_tag(10, "a0"), model.find_by_tag(3, "a0"));
    model.connect_components(model.find_by_tag(10, "a1"), model.find_by_tag(3, "a1"));
    model.connect_components(model.find_by_tag(11, "b0"), model.find_by_tag(3, "b0"));
    model.connect_components(model.find_by_tag(11, "b1"), model.find_by_tag(3, "b1"));
}
