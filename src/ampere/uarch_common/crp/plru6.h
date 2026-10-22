//
// Copyright (c) 2020-2021 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//

#pragma once

#include "ampere/sc/sc.h"
#include "ampere/uarch_common/crp/cache_replacement_policy.h"

namespace ampere::uarch_common {

using namespace ampere::sc;

class PLRU6 : public implements<PLRU6, CacheReplacementPolicy>
{
public:
    explicit PLRU6(const sc_core::sc_module_name& name) : implements<PLRU6, CacheReplacementPolicy>(name) {}

    void init(size_t sets, size_t ways) override;
    void touch(size_t set, size_t way, TouchOp op, bool nontemporal = false) override;
    size_t victim(size_t set) override;
    void invalidate(size_t set, size_t way) override;

private:
    std::vector<std::vector<unsigned>> lru_node;
    std::vector<unsigned> lpu_ptr;
    size_t sets{0};
    size_t ways{0};
};

} // namespace ampere::uarch_common
