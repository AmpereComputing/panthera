//
// Copyright (c) 2022 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//

#pragma once

#include "ampere/sc/sc.h"
#include "ampere/core/l2/crp/l2_replacement_policy.h"

namespace ampere::core {

using namespace ampere::sc;

class NRU : public implements<NRU, L2ReplacementPolicy>
{
public:
    explicit NRU(const sc_core::sc_module_name& name) : implements<NRU, L2ReplacementPolicy>(name) {}

    Param<bool> prefetch_fills_act_like_demand{
        "prefetch_fills_act_like_demand", "L2 prefetcher fills affect replacement like demand fills", false};

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
    int pick_way(uint64_t bank, unsigned victim);

    uint64_t sets{0};
    uint64_t ways{0};
    uint64_t banks{0};
    uint64_t ways_mask{0xff};

    std::vector<uint64_t> start_ptr;
    std::vector<std::vector<std::vector<bool>>> r_value;
};

} // namespace ampere::core
