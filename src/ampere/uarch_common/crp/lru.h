//
// Copyright (c) 2021 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//

#pragma once

#include "ampere/sc/sc.h"
#include "ampere/uarch_common/crp/cache_replacement_policy.h"

namespace ampere::uarch_common {

using namespace ampere::sc;

class LRU : public implements<LRU, CacheReplacementPolicy>
{
public:
    explicit LRU(const sc_core::sc_module_name& name) : implements<LRU, CacheReplacementPolicy>{name} {}

    void init(size_t sets, size_t ways) override;
    void invalidate(size_t set, size_t way) override;
    void touch(size_t set, size_t way, TouchOp op, bool nontemporal = false) override;
    size_t victim(size_t set) override;

private:
    uint64_t age{0};

    std::vector<std::vector<uint64_t>> set_way_age;
    size_t sets{0};
    size_t ways{0};
};

} // namespace ampere::uarch_common
