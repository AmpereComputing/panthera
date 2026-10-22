//
// Copyright (c) 2021 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//
#pragma once
#include "ampere/archx/addr.h"

namespace ampere::archx {
VirtAddr prefetch_immediate_address(uint32_t opcode, uint64_t rn);
VirtAddr prefetch_literal_address(ProgramCounter pc, uint32_t opcode);
VirtAddr prefetch_register_address(uint32_t opcode, uint64_t rn, uint64_t rm);
VirtAddr prefetch_unscaled_address(uint32_t opcode, uint64_t rn);
} // namespace ampere::archx

