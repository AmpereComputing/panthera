//
// Copyright (c) 2019 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//

#ifndef AMPERE_CORE_BRANCH_INFO_H
#define AMPERE_CORE_BRANCH_INFO_H

#include "ampere/archx/branch_type.h"
#include "ampere/archx/addr.h"
#include "ampere/core/fetch.h"
#include "ampere/core/prediction.h"

namespace ampere::core {

using ampere::archx::ProgramCounter;

class BranchInfo
{
public:
    BranchInfo(BranchType type, ProgramCounter pc, Fetch fetch, std::uint64_t alloc_id);

    BranchType type;
    ProgramCounter pc;
    ProgramCounter target;
    bool taken;

    bool mispredicted_direction;
    bool mispredicted_target;
    bool target_nextline;

    bool btb_miss;
    bool rsb_hit;
    bool ittage_hit;
    bool miss_all;

    Fetch fetch;
    SlotPrediction& slot;
    std::uint64_t alloc_id;

    uint64_t instrs_since_last_branch{0};
};

} // namespace ampere::core

#endif // AMPERE_CORE_BRANCH_INFO_H
