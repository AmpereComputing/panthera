//
// Copyright (c) 2019 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//

#ifndef AMPERE_CORE_L2P_MARVEL_H
#define AMPERE_CORE_L2P_MARVEL_H

#include "ampere/core/prefetcher.h"
#include "ampere/sc/sc.h"
#include "ampere/sc/submodule.h"
#include "ampere/core/prefetchers/l2p_bop.h"
#include "ampere/core/prefetchers/l2p_vldp.h"
#include "ampere/uarch_common/memreq.h"

namespace ampere::core {

using namespace ampere::sc;

class L2P_Marvel : public Prefetcher<MemReq>, implementation<L2P_Marvel, Prefetcher<MemReq>>
{
public:
    explicit L2P_Marvel(sc_core::sc_module_name name);

protected:
    class L2P_Marvel_BOP : public L2P_BOP
    {
    public:
        using L2P_BOP::fill_impl;
        using L2P_BOP::L2P_BOP;
        using L2P_BOP::train_impl;
    };
    L2P_Marvel_BOP bop{"bop"};

    class L2P_Marvel_VLDP : public L2P_VLDP
    {
    public:
        using L2P_VLDP::L2P_VLDP;
        using L2P_VLDP::train_impl;
    };
    L2P_Marvel_VLDP vldp{"vldp"};

    In<MemReq> bop_request{"bop_request_in"};
    In<MemReq> vldp_request{"vldp_request_in"};

    void train_impl(
        uint64_t index, MemReq req, bool hit, bool prefetch, bool demand, MemOp::PrefetchType prefetch_type) override;
    void fill_impl(uint64_t index, MemReq req, bool fb_pref, bool was_alloc_by_pf) override;
};

} // namespace ampere::core

#endif // AMPERE_CORE_L2P_MARVEL_H

/*
 * vim: sw=4:ts=4:et:si:cindent:cino=\:0,(0,g0,t0:
 */
