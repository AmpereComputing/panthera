//
// Copyright (c) 2019 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//

#pragma once

#include "ampere/archx/addr.h"
#include "ampere/core/uop.h"

namespace ampere::core {

core::Uop create_nop_uop();
core::Uop create_branch_uop();
core::Uop create_cond_branch_uop();
core::Uop create_load_uop(VirtAddr virt_addr = 0x0_va, PhysAddr phys_addr = 0x0_pa, ProgramCounter pc = 0_pc);
core::Uop create_store_uop(VirtAddr virt_addr = 0x0_va, PhysAddr phys_addr = 0x0_pa, ProgramCounter pc = 0_pc);
core::Uop create_core_uop(std::uint32_t opcode, ProgramCounter pc = 0_pc);

} // namespace ampere::core
