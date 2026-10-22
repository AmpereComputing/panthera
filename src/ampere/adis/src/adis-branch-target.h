// Copyright (c) 2021-2025 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause

#pragma once
#include <cstdint>
#include "adis-encoding-enum.h"

namespace adis {

std::uint64_t branch_target(encoding_enum_t inum, std::uint32_t itext, std::uint64_t branch_pc);

} // namespace
