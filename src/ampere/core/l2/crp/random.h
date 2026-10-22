//
// Copyright (c) 2022 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//

#pragma once

#include "ampere/sc/sc.h"
#include "ampere/core/l2/crp/l2_replacement_policy.h"

namespace ampere::core {

using namespace ampere::sc;

class L2Random : public implements<L2Random, L2ReplacementPolicy>
{
public:
    explicit L2Random(const sc_core::sc_module_name& name)
        : implements<L2Random, L2ReplacementPolicy>(name), random(seed)
    {
    }

    Param<uint64_t> seed{"seed", "Random seed", 1};

    void init(uint64_t banks, uint64_t sets, uint64_t ways, uint64_t ways_mask) override;
    void invalidate(uint64_t bank, uint64_t set, uint64_t way) override;
    void touch(uint64_t bank,
               uint64_t set,
               uint64_t way,
               TouchOp op,
               bool demand,
               const Set& set_contents,
               MemReq req = nullptr) override;
    uint64_t victim(uint64_t bank, uint64_t set, const Set& set_contents, uint64_t way_mask) override;

private:
    LocalRandom random;
    uint64_t sets{0};
    uint64_t ways{0};
    uint64_t banks{0};
    uint64_t ways_mask{0xff};
};

} // namespace ampere::core
