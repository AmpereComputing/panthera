//
// Copyright (c) 2024 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//
#include "ampere/kestrel/mesh/slice.h"
#include "ampere/memory/memory.h"
#include "ampere/mesh/cpx.h"
#include "ampere/mesh/injector/mesh_injector.h"
#include "ampere/mesh/pha.h"

namespace ampere::kestrel {

Slice::Slice(const sc_core::sc_module_name& name) : MeshModule{name}
{
    for (size_t i = 0; i < num_tiles; ++i) {
        auto* tile = make_submod<Tile>("tile", {{"slice_id", slice_id.val()}, {"tile_id", i}});
        tile->get_mesh_port().connect(crossbar.ports.at(DIR_COUNT + i));
    }

    for (size_t i = 0; i < num_pxha_clusters; ++i) {
        auto* cpx = make_submod<CPX>("cpx", {{"n", num_pxhas_per_cluster.val()}, {"nrsp", 2}});

        for (size_t j = 0; j < num_pxhas_per_cluster; ++j) {
            auto* pxha = make_submod<PxHA>("pxha");
            pxha->get_mesh_port().connect(cpx->device_port[j],
                                          fmt::format("{}_to_{}", cpx->basename(), pxha->basename()));

            auto* l3 = make_submod<MeshInjector>("l3");
            pxha->l3_port.connect(l3->get_mesh_port(), fmt::format("{}_to_{}", pxha->basename(), l3->basename()));
        }

        cpx->mesh_port.connect(crossbar.ports.at(DIR_COUNT + num_tiles + i));
    }

    AMPERE_ASSERT(num_pmha_clusters == 2, "can't handle anything other than 2 PmHA clusters for now");

    for (size_t i = 0; i < num_pmha_clusters; ++i) {
        auto* cpx = make_submod<CPX>("cpx", {{"n", num_pmhas_per_cluster.val()}, {"nrsp", 2}});

        for (size_t j = 0; j < num_pmhas_per_cluster; ++j) {
            auto* pmha = make_submod<PmHA>("pmha");
            pmha->get_mesh_port().connect(cpx->device_port[j],
                                          fmt::format("{}_to_{}", cpx->basename(), pmha->basename()));
        }

        if (i == 0)
            cpx->mesh_port.connect(rtr_0.ports.at(DIR_COUNT));
        else
            cpx->mesh_port.connect(rtr_1.ports.at(DIR_COUNT));
    }

    auto* mem_0 = make_submod<memory::Memory>("memory");
    auto* mem_1 = make_submod<memory::Memory>("memory");

    mem_0->get_mesh_port().connect(rtr_0.ports.at(DIR_COUNT + 1), "mem_0_mp");
    mem_1->get_mesh_port().connect(rtr_1.ports.at(DIR_COUNT + 1), "mem_1_mp");

    crossbar.ports[NORTH].connect(rtr_0.ports[SOUTH]);
    crossbar.ports[EAST].connect(rtr_1.ports[SOUTH]);
}

} // namespace ampere::kestrel
