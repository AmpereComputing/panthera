//
// Copyright (c) 2024 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//
#pragma once
namespace adis {
// create a human-understandable shift count from the encoding fields
unsigned shift_opnd(unsigned tszh, unsigned tszl, unsigned imm3);
unsigned shift2_opnd(unsigned tszh, unsigned tszl, unsigned imm3);
} // namespace
