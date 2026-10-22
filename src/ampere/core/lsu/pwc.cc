//
// Copyright (c) 2018-2023 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//

#include "ampere/core/lsu/pwc.h"
#include <algorithm>
#include <iterator>
#include <ranges>

namespace ampere::core {

const LSUPWCEntry &LSUPWC::allocate(MemReq req, bool write_through, bool requires_combine)
{
    auto &entry = const_cast<LSUPWCEntry &>(Base::allocate(req, requires_combine));
    entry.write_through = write_through;
    return entry;
}

void LSUPWC::deallocate(PhysAddr addr)
{
    if (auto it = find_oldest(addr); it != entries.end())
        deallocate(it);
}

void LSUPWC::deallocate(const pwc_iterator &entry)
{
    auto reason = entry->draining ? static_cast<DrainReason>(*entry->draining) : DrainReason::TIMEOUT;
    store_deallocate->put({entry->id, entry->phys_addr()});
    Base::deallocate_no_longer_pending();
    Base::deallocate(entry, reason);
}

void LSUPWC::handle_responses()
{
    while (store_response->can_take()) {
        auto resp = store_response->take();
        auto entry = find(resp.id);
        event.response(entry->req,
                       entry->phys_addr(),
                       Base::event.pwc_id(Base::age_to_pwc_id(entry->id)),
                       event.action(resp.action));

        switch (resp.action) {
        case ResponseAction::PREFETCHED:
            entry->prefetched = true;
        case ResponseAction::NO_BANDWIDTH:
        case ResponseAction::NO_OFB:
        case ResponseAction::FILLED_NOT_UNIQUE:
            Base::deallocate_no_longer_pending();
            entry->retry();
            break;
        case ResponseAction::MISSED:
            entry->miss();
            break;
        case ResponseAction::STORED:
        case ResponseAction::FILLED:
        case ResponseAction::L2:
            deallocate(entry);
            break;
        }
    }
}

void LSUPWC::schedule_and_request()
{
    if (entries.empty() || !want_to_drain())
        return;

    auto reason = *drain_reason();
    unsigned depth = 0;
    for (auto it = entries.begin(); it != entries.end() && (!schedule_depth || depth < schedule_depth); ++it, ++depth) {
        if (!store_request->can_put())
            break;

        auto &entry = *it;
        if (entry.fills() && entry.filling()) {
            if (drain_in_order && !prefetch_misses)
                break;
            continue;
        }

        if (it == entries.begin()
            || std::find_if(std::next(std::make_reverse_iterator(it)),
                            entries.rend(),
                            [&](const auto &e) { return e.phys_addr().same_cache_line(entry.phys_addr()); })
                   == entries.rend())
        {
            bool prefetch = prefetch_misses && it != entries.begin();
            if (prefetch && !entry.can_prefetch(write_allocate))
                continue;

            entry.schedule(reason);
            event.schedule(entry.req,
                           entry.phys_addr(),
                           Base::event.drain_reason(reason),
                           Base::event.pwc_id(Base::age_to_pwc_id(entry.id)),
                           event.write_through(entry.write_through),
                           event.prefetch_only(prefetch));
            store_request->put({entry, write_allocate, prefetch});
            Base::deallocate_pending();
        }
    }
}

bool LSUPWC::run()
{
    Base::run();
    handle_responses();
    schedule_and_request();

    return true;
}

} // namespace ampere::core

/*
 * vim: sw=4:ts=4:et:si:cindent:cino=\:0,(0,g0,t0:
 */
