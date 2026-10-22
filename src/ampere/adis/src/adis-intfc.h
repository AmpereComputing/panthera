//
// Copyright (c) 2022 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//

#pragma once
#include <cstdint>
#include <string>
#include "adis-encoding-enum.h"
namespace adis {
// Interface functions

// adis_decode() returns an instruction number (inum) that drives the
// other disassmebly-related operations.
encoding_enum_t decode(std::uint32_t itext);

// disassemble the instruction given the itext, inum and virtual
// address.
std::string disasm(std::uint32_t itext, encoding_enum_t inum, std::uint64_t vaddr=0);
}
