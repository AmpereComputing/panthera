//
// Copyright (c) 2019-2022 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//

#pragma once

#include "ampere/core/branch_context.h"
#include "ampere/core/branch_history.h"
#include "ampere/core/branch_info.h"
#include "ampere/core/fetch.h"
#include "ampere/core/flush.h"

namespace ampere::core {

class Predictor
{
public:
    virtual void predict(Fetch fetch, BranchHistory& history, const BranchContext& context) = 0;
    virtual void train(const BranchInfo& br_info, BranchHistory& history, const BranchContext& context) = 0;
    virtual void recover(const Flush& flush_info,
                         BranchInfo* br_info,
                         BranchHistory& history,
                         const BranchContext& context)
    {
    }

    virtual void context_change(const BranchContext& old_context, const BranchContext& new_context) {}

    // "Instrumentation" predict: do prediction with no state updates such that this method
    // may be called anytime for debug or stats.
    virtual void predict_inst(Fetch fetch, BranchHistory& history, const BranchContext& context)
    {
        predict(fetch, history, context);
    }

    virtual void train_alt(const BranchInfo& br_info, BranchHistory& history, const BranchContext& context) {}

    virtual void commit(const BranchInfo& br_info, bool is_alternate) {}
};

} // namespace ampere::core
