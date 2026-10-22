//
// Copyright (c) 2021 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//

#include "ampere/core/prefetchers/cp_nextline.h"

namespace ampere::core {

CP_NextLine::CP_NextLine(sc_core::sc_module_name name) : Prefetcher<Fetch>(name) {}

void CP_NextLine::train_impl(Fetch req, bool hit, bool prefetch, bool demand)
{
    // only trigger on a demand miss or prefetch hit
    if (!hit || prefetch) {
        // record train event
        ev_train(req, req->program_counter, ev_hitmiss(hit));

        // check if we can write a request
        if (prefetch_request->can_put()) {
            // calculate the address
            auto pref_addr = req->program_counter.next_line_address();

            // make a new transaction parented to the triggering Fetch
            auto prefetch_target = make_child<Fetch>(req, pref_addr, 0);

            // mark this Fetch as a prefetch
            prefetch_target->prefetch = true;

            // place it on the port
            prefetch_request->put(prefetch_target);
        }
    }
}

} // namespace ampere::core

/*
 * vim: sw=4:ts=4:et:si:cindent:cino=\:0,(0,g0,t0:
 */
