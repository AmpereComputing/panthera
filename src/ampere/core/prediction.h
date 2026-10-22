//
// Copyright (c) 2018-2019, 2022 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//

#pragma once

#include "ampere/archx/addr.h"
#include "ampere/archx/instruction.h"
#include "ampere/util/enum.h"

make_ns_enum_without_class(ampere::core, ClearType,
    SEQUENTIAL = 0,                // NNP predicts a taken branch. BTB+TAGE predicts no taken branch
    DIFFERENT_TARGET = 1,          // NNP predicts a taken branch. BTB+TAGE predicts a different taken branch target
    // Note: for Furiosa, changes to both indirect targets and direct targets
    // are both reported as DIFFERENT_TARGET (there will be no
    // DIFFERENT_INDIRECT_TARGET reported for Furiosa)
    DIFFERENT_INDIRECT_TARGET = 2, // NNP predicts a taken branch. ITTAGE predicts a different taken branch target (RSB corrections are counted by DIFFERENT_TARGET, above)
    TAKEN = 3,                     // NNP predicts no branch. BTB+TAGE predicts a taken branch
    LOOP_START = 4,                // Started looping, clearing fetch
    RSB_FULL = 5                   // RSB is full, stall fetch and clear BTB
);

make_ns_enum(ampere::core, BranchStrength,
    VERYWEAK = 0,
    WEAK = 1,
    STRONG = 2,
    VERYSTRONG = 3
);

namespace ampere::core {

using archx::BranchType;
using archx::ProgramCounter;

class Predictor;

struct SlotPrediction
{
    SlotPrediction() = default;
    SlotPrediction(const SlotPrediction&) = default;
    SlotPrediction& operator=(const SlotPrediction&) = default;

    BranchType branch_type{BranchType::NONE};

    struct PredictionInfo
    {
        Predictor* predictor{nullptr};
        std::optional<ProgramCounter> target{};
        bool target_nextline{false};
        BranchStrength strength{BranchStrength::VERYWEAK};
        bool taken{false};
        bool useful{false};
        bool ium{false};
    };

    PredictionInfo primary;
    PredictionInfo alternate;

    bool use_alternate{false};
    bool btb_hit{false};
    bool rsb_hit{false};
    bool ittage_hit{false};
    bool miss_all{false};

    // conditional_*_taken hold the taken/not-taken prediction originally made
    // for this slot by the bimodals and TAGE, even if it is later overridden
    // by BTB/ITTAGE. This original T/NT prediction is used if we find the
    // BTB/ITTAGE prediction was wrong by discovering a conditional branch at
    // branch check time.
    Predictor* conditional_primary_predictor{nullptr};
    Predictor* conditional_alternate_predictor{nullptr};
    bool conditional_primary_taken{false};
    bool conditional_alternate_taken{false};
    bool conditional_use_alternate{false};
};

using LinePrediction = std::array<SlotPrediction, 16>;

struct Prediction
{
    BranchType branch_type{BranchType::NONE};
    ProgramCounter branch_pc{0};
    ProgramCounter target{0};
    bool target_nextline{false}; // The prediction at our target is nextline

    bool is_taken() const { return branch_type != BranchType::NONE; }
};

inline bool prediction_changed(const Prediction& old_prediction, const Prediction& new_prediction)
{
    return (new_prediction.target != old_prediction.target) || (new_prediction.branch_pc != old_prediction.branch_pc)
           || (new_prediction.branch_type != old_prediction.branch_type);
}

inline ClearType clear_type(const Prediction& old_prediction, const Prediction& new_prediction)
{
    assert(prediction_changed(old_prediction, new_prediction));

    if (old_prediction.is_taken() && !new_prediction.is_taken()) {
        return ClearType::SEQUENTIAL;
    } else if (!old_prediction.is_taken() && new_prediction.is_taken()) {
        return ClearType::TAKEN;
    } else {
        return ClearType::DIFFERENT_TARGET;
    }
}

} // namespace ampere::core

/*
 * vim: sw=4:ts=4:et:si:cindent:cino=\:0,(0,g0,t0:
 */
