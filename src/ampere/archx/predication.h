//
// Copyright (c) 2023 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//
#pragma once
#include "ampere/archx/uop.h"
#include "ampere/archx/instruction.h"
namespace ampere::archx {

bool is_predicated_off(std::shared_ptr<ampere::archx::Instruction>& instruction, const std::unique_ptr<Uop>& archx_uop);

} // namespace ampere::archx
