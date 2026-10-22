//
// Copyright (c) 2018-2019 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//

#include "ampere/core/prefetchers/l1p_l2p_translator.h"
#include "ampere/core/prefetcher.h"

namespace ampere::core {

L1P_L2P_Translator::L1P_L2P_Translator(sc_core::sc_module_name name) : BackendPrefetcher<BackendMemReq>(name) {}

bool L1P_L2P_Translator::run()
{
    while (requests->can_take()) {
        auto req = requests->take();
        if (this->prefetch_request->can_put())
            this->prefetch_request->put(req);
        else
            overflow(req);
    }

    return true;
}

} // namespace ampere::core

/*
 * vim: sw=4:ts=4:et:si:cindent:cino=\:0,(0,g0,t0:
 */
