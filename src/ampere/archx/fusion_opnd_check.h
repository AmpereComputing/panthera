//
// Copyright (c) 2019-2022 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//
#pragma once

#include <utility>
#include <cstdint>

#include "ampere/archx/decoder.h"
#include "ampere/archx/uop.h"

namespace ampere::archx {
std::pair<FusionType, UopSet> fusion_opnd_check(UopSet& uops,
                                                std::uint32_t* insn_bytes,
                                                bool skip_checks); // skip SOME checks
}
