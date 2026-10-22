// Copyright (c) 2020-2023 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//

#pragma once
#include <vector>
#include "ampere/archx/branch_type.h"
#include "ampere/archx/barrier_type.h"
#include "ampere/archx/pmu.h"

namespace ampere::archx {

enum class DivSource {
    // DivSource used to figure out the number and location of the
    // divide and sqrt sources for the early-out condition detection.
    INVALID,
    ADVSIMD_FDIV,
    ADVSIMD_SQRT,
    GPR_IDIV,
    SVE_IDIV,
    SVE_FDIV,
    SVE_SQRT
};

enum class MemoryTaggingCheck {
    UNKNOWN = 0, // not all xlat source have tag check annotated
    CHECK_IF_NOT_SP,
    ALWAYS_CHECKED,
};

enum class MoveType {
    NONE,
    ORR64,
    ADD64,
    ORR32,
    MOVZ
};

struct InstInfo
{ // structure layout must match initialization code in inst_info.py
    BranchType branch_type{BranchType::NONE};
    Barrier_Type barrier_type{Barrier_Type::NONE};
    bool is_load{false};
    bool is_store{false};
    bool is_prefetch{false};
    bool is_acquire{false};
    bool is_release{false};
    bool is_movprfx_fusible_predicated{false};
    bool is_movprfx_fusible_unpredicated{false};
    MoveType move_type{MoveType::NONE};
    std::vector<PMU> pmu_type;
    DivSource div_srcs{DivSource::INVALID};
    bool is_fusion_delay_hint{false};
    MemoryTaggingCheck mem_tag_check{MemoryTaggingCheck::UNKNOWN};
};

struct InstInfoFusion
{ // structure layout must match initialization code in inst_info.py
    std::vector<PMU> pmu_type;
};

} // namespace ampere::archx
