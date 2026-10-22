
//
// Copyright (c) 2019-2021 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//

#pragma once

#include "ampere/core/backend_prefetcher.h"
#include "ampere/core/prefetcher.h"

namespace ampere::core {

class L1P_L2P_Translator : public BackendPrefetcher<BackendMemReq>,
                           public implementation<L1P_L2P_Translator, BackendPrefetcher<BackendMemReq>>
{
public:
    In<PrefetchTranslateRequest<MemReq>> requests;

    explicit L1P_L2P_Translator(sc_core::sc_module_name name);

    prefetch_type_t prefetch_type() const override { return PrefetchType::VA_L2PF; }
    bool can_replay() const override { return true; }
    bool can_prefetch_page(BackendMemReq req) const override { return true; }
    bool can_allocate_pob(BackendMemReq req, archx::EL current_level) const override { return true; }
    bool can_allocate_ofb() const override { return false; }

    bool run() override;
};

} // namespace ampere::core

/*
 * vim: sw=4:ts=4:et:si:cindent:cino=\:0,(0,g0,t0:
 */
