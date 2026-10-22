//
// Copyright (c) 2022 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//
#pragma once

#include "ampere/mesh/mesh_config.h"
#include "ampere/sc/sc.h"

namespace ampere::soc {

using namespace mesh;
using namespace sc;

class SocBase : public Module, public MeshConfigIfc
{
public:
    SocBase(const sc_core::sc_module_name& name) : Module{name} {}
    SocBase(const sc_core::sc_module_name& name, Clock* clock, Environment* env) : Module{name, clock, env} {}
};

} // namespace ampere::soc
