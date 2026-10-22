//
// Copyright (c) 2024-2025 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//

#pragma once

#include "ampere/mesh/sam.h"
#include "ampere/sc/sc.h"

using namespace ampere::mesh;

class MockSystemAddressMap : public sc::Module, public SystemAddressMapIfc
{
public:
    explicit MockSystemAddressMap(const sc_core::sc_module_name& name) : Module{name} {}

    const SAMRegion& lookup(PhysAddr addr) const override { return default_region; }

private:
    SAMRegion default_region{SAMRegion::Type::DDR, 0_pa, (1ULL << 48), SNR8, 0};
};
