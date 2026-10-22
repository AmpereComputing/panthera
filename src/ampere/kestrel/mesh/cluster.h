//
// Copyright (c) 2024 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//
#pragma once

#include "ampere/kestrel/mesh/slice.h"
#include "ampere/mesh/mesh_module.h"
#include "ampere/sc/sc.h"

namespace ampere::kestrel {

using namespace ampere::mesh;
using namespace ampere::sc;

class Cluster : public MeshModule
{
public:
    explicit Cluster(const sc_core::sc_module_name& name) : MeshModule{name} {}

private:
    Param<size_t> num_slices{"num_slices", "Number of slices in this cluster", 4};

    sc_core::sc_vector<Slice> slice{"slice", num_slices};
};

} // namespace ampere::kestrel
