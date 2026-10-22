//
// Copyright (c) 2022 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//

#pragma once

#include "ampere/sc/sc.h"
#include "ampere/core/l2/crp/l2_replacement_policy.h"
#include "ampere/core/l2/crp/rrip.h"

namespace ampere::core {

using namespace ampere::sc;

class L2BRRIP : public implements<L2BRRIP, L2RRIP>
{
public:
    explicit L2BRRIP(const sc_core::sc_module_name& name);

    // Bimodal and dynamic parameters
    Param<uint64_t> brrip_long_frequency{
        "brrip_long_frequency",
        "Every N fills, fill with age (rrip_max - insert_delta). All other fills inserted with age rrip_max",
        32};

    // BRRIP only differs from RRIP in the insertion policy, all other methods inherited
    void touch(uint64_t bank,
               uint64_t set,
               uint64_t way,
               TouchOp op,
               bool demand,
               const Set& set_contents,
               MemReq req = nullptr) override;

private:
    uint64_t brrip_counter{0};
};

} // namespace ampere::core
