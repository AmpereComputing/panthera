//
// Copyright (c) 2024 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//
#pragma once
#include <cstdint>
#include "ampere/kestrel/aice-sim/aice_instruction.h"

namespace ampere::kestrel::aice {

[[maybe_unused]] static aice_instruction_obj_t test_add1(addr_t pc)
{
    return make_add(pc, 1, 2, 3);
}

} // namespace ampere::kestrel::aice
