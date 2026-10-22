//
// Copyright (c) 2020 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//

#ifndef AMPERE_ARCHX_BRANCH_TARGET_H
#define AMPERE_ARCHX_BRANCH_TARGET_H

#include "ampere/archx/addr.h"

namespace ampere::archx {

const ProgramCounter NON_CANONICAL_TARGET{0xaaaaaaaaabadc0de};

ProgramCounter branch_target_if_taken(ProgramCounter pc, std::uint32_t opcode);
} // namespace ampere::archx

#endif // AMPERE_ARCHX_BRANCH_TARGET_H
