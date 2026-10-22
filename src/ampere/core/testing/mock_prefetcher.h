//
// Copyright (c) 2021 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//

#pragma once

#include "ampere/core/prefetcher.h"
#include "ampere/sc/sc.h"

namespace ampere::core {

// model no prefetching by passing L3P_None as the type.
class L2P_Dummy : public Prefetcher<MemReq>, poly<L2P_Dummy>
{
public:
    explicit L2P_Dummy(sc_core::sc_module_name name) : Prefetcher(name) {}

    void train_impl(MemReq req, bool hit, bool prefetch, bool demand) override
    {
        trains.emplace_back(req, hit, prefetch, demand);
    }
    void drop_impl(MemReq req, PrefetchDropReason reason) override { drops.push_back(req); }
    void overflow_impl(MemReq req) override { overflows.push_back(req); }
    void fill_impl(MemReq req, bool fb_pref, bool was_alloc_by_pf) override
    {
        fills.emplace_back(req, fb_pref, was_alloc_by_pf);
    }
    void evict_impl(PhysAddr pa, bool prefetch, bool demand) override { evicts.emplace_back(pa, prefetch, demand); }
    void launch_impl(MemReq req) override { launches.push_back(req); }
    void exercise(MemReq req) { train_addresses.push_back(req); }

    bool run() override
    {
        if (!train_addresses.empty()) {
            prefetch_request->put(train_addresses.front());
            train_addresses.pop_front();
        }

        return true;
    }

    void clear()
    {
        train_addresses.clear();
        trains.clear();
        drops.clear();
        overflows.clear();
        fills.clear();
        evicts.clear();
        launches.clear();
    }

    std::deque<MemReq> train_addresses;
    std::deque<std::tuple<MemReq, bool, bool, bool>> trains;
    std::deque<MemReq> drops;
    std::deque<MemReq> overflows;
    std::deque<std::tuple<MemReq, bool, bool>> fills;
    std::deque<std::tuple<PhysAddr, bool, bool>> evicts;
    std::deque<MemReq> launches;
};

} // namespace ampere::core

/*
 * vim: sw=4:ts=4:et:si:cindent:cino=\:0,(0,g0,t0:
 */
