//
// Copyright (c) 2021 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//

#pragma once

#include "ampere/sc/sc.h"
#include "ampere/uarch_common/crp/cache_replacement_policy.h"

namespace ampere::uarch_common {

using namespace ampere::sc;

class RR : public implements<RR, CacheReplacementPolicy>
{
public:
    explicit RR(const sc_core::sc_module_name& name) : implements<RR, CacheReplacementPolicy>{name} {}

    void init(size_t sets, size_t ways) override;
    void invalidate(size_t set, size_t way) override;
    void touch(size_t set, size_t way, TouchOp op, bool nontemporal = false) override;
    size_t victim(size_t set) override;

private:
    std::vector<size_t> set_next_way;
    size_t ways{0};
};

} // namespace ampere::uarch_common
