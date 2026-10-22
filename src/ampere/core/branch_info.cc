//
// Copyright (c) 2019 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//

#include "ampere/core/branch_info.h"

namespace ampere::core {

BranchInfo::BranchInfo(BranchType type, ProgramCounter pc, Fetch fetch, std::uint64_t alloc_id)
    : type{type},
      pc{pc},
      mispredicted_direction{false},
      mispredicted_target{false},
      target_nextline{false},
      fetch{fetch},
      slot{fetch->line_prediction[pc.extract_range(5, 2)]},
      alloc_id{alloc_id}
{
    // The target and taken are initialized based on the prediction, but will
    // be updated by the BPU if and when mispredicted. Consider this branch as
    // having been predicted taken if the branch type (from decoding the
    // instruction) and PC match that of the final prediction produced by the
    // BPU.
    const auto& prediction = fetch->get_prediction();
    taken = prediction.branch_type == type && prediction.branch_pc == pc;
    target = taken ? prediction.target : pc + 4;

    btb_miss = !slot.btb_hit;
    rsb_hit = slot.rsb_hit;
    ittage_hit = slot.ittage_hit;
    miss_all = slot.miss_all;
}

} // namespace ampere::core
