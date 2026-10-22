//
// Copyright (c) 2022-2024 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//

#pragma once
#include <cstdint>
#include <optional>
#include <utility>

namespace adis {
using u64optpair_t = std::optional<std::pair<std::uint64_t, std::uint64_t>>;
using u32optpair_t = std::optional<std::pair<std::uint32_t, std::uint32_t>>;

// returns an optional pair (wmask, tmask)
u64optpair_t dec_imm13_64(std::uint32_t imm13, bool logical);
u32optpair_t dec_imm13_32(std::uint32_t imm13, bool logical);
}
