//
// Copyright (c) 2022 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//

#pragma once

#include <cstdint>
#include <map>
#include <string>

#include "disargs.h"
typedef std::uint_fast32_t uint_t;
typedef std::map<std::uint64_t, std::string> symbols_t;

void dump_instr(const args_t& args, symbols_t& symbols, std::uint8_t* f, uint_t n, std::uint64_t va);
