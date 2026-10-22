//
// Copyright (c) 2019 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//

#pragma once

#include "ampere/core/prefetcher.h"
#include "ampere/sc/sc.h"
#include "ampere/sc/submodule.h"

namespace ampere::core {

using namespace ampere::sc;
using namespace ampere::archx;

class L2P_Spatial : public Prefetcher<MemReq>, implementation<L2P_Spatial, Prefetcher<MemReq>>
{
public:
    explicit L2P_Spatial(sc_core::sc_module_name name);

protected:
    void train_impl(MemReq req, bool hit, bool prefetch, bool demand) override;
    MemOp::PrefetchType prefetch_type() const override;

private:
    void table_print(std::ostream &os);
};

} // namespace ampere::core

/*
 * vim: sw=4:ts=4:et:si:cindent:cino=\:0,(0,g0,t0:
 */
