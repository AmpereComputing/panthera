//
// Copyright (c) 2024 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//
// A Kestrel Slice is one unit of hierarchy containing a number of Tiles
// (nominally 4) connected through a cross-bar to some PxHAs and from
// there to some PmHAs and HBM.
//
// The main goal of a Slice is to be able to deliver all the available
// HBM bandwidth to the contained Tiles. Slices can also communicate
// "sideways" to other Slices, but this pathway is of lesser concern
// bandwidth-wise.
//
#pragma once

#include "ampere/kestrel/mesh/tile.h"
#include "ampere/mesh/mesh_module.h"
#include "ampere/mesh/mesh_node.h"
#include "ampere/sc/sc.h"

namespace ampere::kestrel {

using namespace ampere::mesh;

class Slice : public MeshModule
{
public:
    explicit Slice(const sc_core::sc_module_name& name);

private:
    Param<uint8_t> slice_id{"slice_id", "ID number of this slice", 0};
    Param<size_t> num_tiles{"num_tiles", "number of Tiles in the slice", 4};
    Param<size_t> num_pxha_clusters{"num_pxha_clusters", "number of PxHA clusters in the slice", 4};
    Param<size_t> num_pxhas_per_cluster{"num_pxhas_per_cluster", "number of PxHAs in one cluster", 2};
    Param<size_t> num_pmha_clusters{"num_pmha_clusters", "number of PmHA clusters in the slice", 2};
    Param<size_t> num_pmhas_per_cluster{"num_pmhas_per_cluster", "number of PmHAs in one cluster", 2};

    MeshNode crossbar{"crossbar",
                      static_cast<unsigned int>(slice_id * 2),
                      static_cast<unsigned int>(num_tiles),
                      static_cast<unsigned int>(num_tiles + num_pxha_clusters),
                      0,
                      0};
    MeshNode rtr_0{"rtr_0", static_cast<unsigned int>(slice_id * 2), static_cast<unsigned int>(num_tiles + 1), 2, 0, 0};
    MeshNode rtr_1{
        "rtr_1", static_cast<unsigned int>(slice_id * 2 + 1), static_cast<unsigned int>(num_tiles + 1), 2, 0, 0};
};

} // namespace ampere::kestrel
