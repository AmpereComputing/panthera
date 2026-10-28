//
// Copyright (c) 2022 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//

#pragma once
#include <cstdint>
#include <string>

#define USING_CAPSTONE
#if defined(USING_CAPSTONE)
#include <capstone/platform.h>
#include <capstone/capstone.h>
#endif

#define USING_ADIS
#if defined(USING_ADIS)
#include "ampere/adis/src/adis.h"
#endif

namespace ampere::disasm {

std::string disasm(std::uint32_t bytes, std::uint64_t pc);
void init(bool _use_adis);

} // namespace ampere::disasm
