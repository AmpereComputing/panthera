//
// Copyright (c) 2020-2021 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//

#pragma once

#include "ampere/sc/sc.h"
#include "ampere/uarch_common/crp/cache_replacement_policy.h"

namespace ampere::uarch_common {

using namespace ampere::sc;

class RRIP : public implements<RRIP, CacheReplacementPolicy>
{
public:
    explicit RRIP(const sc_core::sc_module_name& name) : implements<RRIP, CacheReplacementPolicy>(name) {}

    Param<size_t> rrip_max{"rrip_max", "Maximum RRIP age value (usually 2^n-1)", 3};
    Param<size_t> insert_delta{"insert_delta", "When not filling with age rrip_max, fill with age (rrip_max - N)", 1};
    Param<size_t> brrip_long_frequency{
        "brrip_long_frequency",
        "Every N fills, fill with age (rrip_max - insert_delta). Value of 1 means SRRIP (never insert at rrip_max)",
        1};

    void init(size_t sets, size_t ways) override;
    void touch(size_t set, size_t way, TouchOp op, bool nontemporal = false) override;
    size_t victim(size_t set) override;
    void invalidate(size_t set, size_t way) override;

private:
    size_t sets{0};
    size_t ways{0};
    std::vector<std::vector<unsigned>> rrpv;
    uint64_t brrip_counter{0};
};

} // namespace ampere::uarch_common
