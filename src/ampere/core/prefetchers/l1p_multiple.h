
//
// Copyright (c) 2019-2021 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//

#pragma once

#include "ampere/core/backend_prefetcher.h"
#include "ampere/core/prefetcher.h"
#include "ampere/core/prefetchers/multiple.h"
#include "ampere/sc/event.h"

namespace ampere::core {

template<typename T>
class MultipleBackendPrefetchers : public MultiplePrefetcherBase<T, BackendPrefetcher<T>>
{
    using Base = MultiplePrefetcherBase<T, BackendPrefetcher<T>>;
    using prefetch_type_t = typename Base::prefetch_type_t;

public:
    explicit MultipleBackendPrefetchers(sc_core::sc_module_name name)
        : MultiplePrefetcherBase<T, BackendPrefetcher<T>>(name)
    {
    }

protected:
    using train_func_t = void (BackendPrefetcher<T>::*)(uint64_t, T, bool, bool, bool, prefetch_type_t);
    using fill_func_t = void (BackendPrefetcher<T>::*)(uint64_t, T, bool, bool);

    void allocate_impl(uint64_t id, T req) override
    {
        this->call_each_and_forward(&BackendPrefetcher<T>::allocate, id, req);
    }

    void issue_impl(uint64_t id, T req) override { this->call_each_and_forward(&BackendPrefetcher<T>::issue, id, req); }

    void execute_impl(
        uint64_t index, T req, bool hit, bool prefetch, bool demand, prefetch_type_t prefetch_type) override
    {
        this->call_each_and_forward(&BackendPrefetcher<T>::execute, index, req, hit, prefetch, demand, prefetch_type);
    }

    void writeback_impl(uint64_t id, T req) override
    {
        this->call_each_and_forward(&BackendPrefetcher<T>::writeback, id, req);
    }

    bool commit_impl(uint64_t id, T req) override
    {
        bool can_commit_next = true;
        for (auto &prefetcher : this->prefetchers) {
            can_commit_next &= prefetcher.module->commit(id, req);
            this->forward_requests(prefetcher);
        }

        return can_commit_next;
    }

    void train_impl(T req, bool hit, bool prefetch, bool demand) override
    {
        if constexpr (std::is_same_v<prefetch_type_t, MemOp::PrefetchType>) {
            this->call_each_and_forward(static_cast<train_func_t>(&BackendPrefetcher<T>::train),
                                        0ULL,
                                        req,
                                        hit,
                                        prefetch,
                                        demand,
                                        MemOp::PrefetchType::NONE);
        } else {
            this->call_each_and_forward(static_cast<train_func_t>(&BackendPrefetcher<T>::train),
                                        0ULL,
                                        req,
                                        hit,
                                        prefetch,
                                        demand,
                                        prefetch_type_t{});
        }
    }

    void train_impl(T req, bool hit, bool prefetch, bool demand, prefetch_type_t prefetch_type) override
    {
        this->call_each_and_forward(
            static_cast<train_func_t>(&BackendPrefetcher<T>::train), 0ULL, req, hit, prefetch, demand, prefetch_type);
    }

    void train_impl(uint64_t index, T req, bool hit, bool prefetch, bool demand, prefetch_type_t prefetch_type) override
    {
        this->call_each_and_forward(
            static_cast<train_func_t>(&BackendPrefetcher<T>::train), index, req, hit, prefetch, demand, prefetch_type);
    }

    void drop_impl(T req, PrefetchDropReason reason) override
    {
        this->call_requested(&BackendPrefetcher<T>::drop, true, req, reason);
    }

    void overflow_impl(T req) override { this->call_requested(&BackendPrefetcher<T>::overflow, true, req); }

    void fill_impl(T req, bool fb_pref, bool was_alloc_by_pf) override
    {
        this->prefetchers_requested(req, true);
        this->call_each(static_cast<fill_func_t>(&BackendPrefetcher<T>::fill), 0ULL, req, fb_pref, was_alloc_by_pf);
    }

    void fill_impl(uint64_t index, T req, bool fb_pref, bool was_alloc_by_pf) override
    {
        this->prefetchers_requested(req, true);
        this->call_each(static_cast<fill_func_t>(&BackendPrefetcher<T>::fill), index, req, fb_pref, was_alloc_by_pf);
    }

    void evict_impl(PhysAddr pa, bool prefetch, bool demand) override
    {
        this->call_each(&BackendPrefetcher<T>::evict, pa, prefetch, demand);
    }

    void launch_impl(T req) override { this->call_requested(&BackendPrefetcher<T>::launch, false, req); }

    void flush_impl(Flush flush_info) override { this->call_each(&BackendPrefetcher<T>::flush, flush_info); }

    bool can_replay() const override
    {
        return std::any_of(this->prefetchers.begin(), this->prefetchers.end(), [](const auto &pref) {
            return pref.module->can_replay();
        });
    }
    bool can_prefetch_page(T req) const override
    {
        return std::any_of(this->prefetchers.begin(), this->prefetchers.end(), [&](const auto &pref) {
            return pref.module->prefetch_type() == PrefetcherData<T>::get_type(req)
                   && pref.module->can_prefetch_page(req);
        });
    }
    bool can_allocate_pob(T req, archx::EL current_level) const override
    {
        return std::any_of(this->prefetchers.begin(), this->prefetchers.end(), [&](const auto &pref) {
            return pref.module->prefetch_type() == PrefetcherData<T>::get_type(req)
                   && pref.module->can_allocate_pob(req, current_level);
        });
    }

    bool can_issue_prefetch() const override
    {
        return std::any_of(this->prefetchers.begin(), this->prefetchers.end(), [&](const auto &pref) {
            return pref.module->can_issue_prefetch();
        });
    }
    bool can_issue_prefetch(T req) const override
    {
        return std::any_of(this->prefetchers.begin(), this->prefetchers.end(), [&](const auto &pref) {
            return pref.module->prefetch_type() == PrefetcherData<T>::get_type(req)
                   && pref.module->can_issue_prefetch();
        });
    }

    bool can_allocate_ofb() const override
    {
        return std::any_of(this->prefetchers.begin(), this->prefetchers.end(), [&](const auto &pref) {
            return pref.module->can_issue_prefetch();
        });
    }

    bool can_allocate_ofb(T req) const override
    {
        return std::any_of(this->prefetchers.begin(), this->prefetchers.end(), [&](const auto &pref) {
            return pref.module->prefetch_type() == PrefetcherData<T>::get_type(req)
                   && pref.module->can_allocate_ofb(req);
        });
    }

    bool arbitrate_prefetchers(unsigned a, unsigned b) const override
    {
        if (!this->prefetchers_by_priority.at(a)->can_issue_prefetch())
            return false;
        if (!this->prefetchers_by_priority.at(b)->can_issue_prefetch())
            return true;
        return Base::arbitrate_prefetchers(a, b);
    }
};

class L1P_Multiple : public MultipleBackendPrefetchers<BackendMemReq>,
                     public implementation<L1P_Multiple, BackendPrefetcher<BackendMemReq>>
{
public:
    explicit L1P_Multiple(sc_core::sc_module_name name);
};

} // namespace ampere::core

/*
 * vim: sw=4:ts=4:et:si:cindent:cino=\:0,(0,g0,t0:
 */
