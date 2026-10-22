//
// Copyright (c) 2024 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//
#pragma once
#include <memory>
#include "ampere/archx/instruction_is.h"

namespace ampere::archx::emulate {

// update instr.arch_state_output with the output of this instruction,
// using the pc, itext/opcode, & fetch_state. The driver_is will then
// add intr.arch_state_output to the fetch state for the next wrong
// path.
void emulate(archx::InstructionIS* instr);
void emulate_correct_path(archx::InstructionIS* instr);
} // namespace ampere::archx::emulate
