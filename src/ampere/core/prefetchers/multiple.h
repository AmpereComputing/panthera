
//
// Copyright (c) 2019-2021 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//

#pragma once

#include <cstdint>
#include <list>
#include <map>
#include <optional>
#include <stdexcept>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>
#include <fmt/format.h>
#include <sysc/communication/sc_port.h>

#include "ampere/archx/addr.h"
#include "ampere/archx/prefetch_type.h"
#include "ampere/sc/poly.h"
#include "ampere/sc/port_multiplex.h"
#include "ampere/uarch_common/memreq.h"
#include "ampere/core/prefetcher.h"
#include "ampere/sc/submodule.h"
#include "ampere/uarch_common/memop.h"

namespace ampere::core {

// Multiple prefetchers in the JSON format:
// {
//     "prefetcher_name": {
//         "type": <string>,     # prefetcher type
//         "order": <number>,    # construction order (lower is earlier)
//         "priority": <number>, # queue priority (lower is higher priority)
//     }
// }
//
// After that, parameters can be provided as usual for "prefetcher.prefetcher_name.*"
template<typename T, typename PT = Prefetcher<T>>
class MultiplePrefetcherBase : public PT
{
public:
    using typename PT::addr_t;
    using typename PT::prefetch_type_t;

protected:
    Param<bool> unified_queue{"unified_queue", "Unified prefetch request queue", false};

    struct SubPrefetcher
    {
        unsigned id;
        Submodule<PT> module;
        In<T> requests;
        InOpt<PrefetchTranslateRequest<T>> translation_requests;
        explicit SubPrefetcher(size_t id, const char *name)
            : id(id),
              module(name),
              requests(fmt::format("{}_prefetch_requests", name).c_str()),
              translation_requests(fmt::format("{}_prefetch_translation_requests", name).c_str())
        {
        }

        uint64_t mask() const { return 1ULL << id; }
    };

    std::list<SubPrefetcher> prefetchers;
    std::map<unsigned, PT *> prefetchers_by_priority;
    std::unordered_map<addr_t, uint64_t> requested;
    bool translation_requests_allowed = false;

    using prefetch_mux_t = typename TakerMultiplexer<T>::Setup;
    using translate_mux_t =
        typename TakerMultiplexer<PrefetchTranslateRequest<T>, sc_core::SC_ZERO_OR_MORE_BOUND>::Setup;

    void add_prefetcher(std::string name,
                        const json &params,
                        prefetch_mux_t *prefetch_request_mux,
                        translate_mux_t *translate_prefetch_request_mux)
    {
        auto &parameters = this->get_environment()->parameters;
        if (params.is_object()) {
            for (auto [key, value] : params.items()) {
                if (key != "name" && key != "order" && key != "priority")
                    parameters.add_setting(fmt::format("{}.{}.{}", this->name(), name, key), value, "param");
            }
        } else {
            parameters.add_setting(fmt::format("{}.{}.type", this->name(), name), params, "param");
        }

        unsigned priority_index = prefetchers.size();
        auto &sub = prefetchers.emplace_back(priority_index, name.c_str());

        if (unified_queue) {
            connect_from(sub.module->prefetch_request, sub.requests).name("prefetch_requests").latency(0);
            connect_from(sub.module->translate_prefetch_request, sub.translation_requests)
                .name("translation_requests")
                .latency(0);
        } else {
            std::optional<unsigned> priority;
            if (params.contains("priority")) {
                priority.emplace(params["priority"]);
                priority_index = *priority;
            }

            connect_from(sub.module->prefetch_request, sub.requests).name("prefetch_requests").potentially_lossy();
            connect_from(sub.module->translate_prefetch_request, sub.translation_requests)
                .name("translation_requests")
                .potentially_lossy();

            prefetch_request_mux->connect_in(sub.requests, priority);
            translate_prefetch_request_mux->connect_in(sub.translation_requests, priority);
        }

        prefetchers_by_priority[priority_index] = sub.module.get();
    }

    auto addr_of(T req) const { return PrefetcherData<T>::get_addr(req); }

    void forward_requests(SubPrefetcher &prefetcher)
    {
        if (!unified_queue)
            return;

        while (prefetcher.requests->can_take()) {
            auto request = prefetcher.requests->take();
            auto addr = addr_of(request);
            auto previous = requested.find(addr);
            if (this->prefetch_request->can_put() || previous != requested.end()) {
                if (previous == requested.end()) {
                    previous = requested.emplace(addr, 0).first;
                    this->prefetch_request->put(request);
                }

                previous->second |= prefetcher.mask();
            } else {
                prefetcher.module->overflow(request);
            }
        }

        while (prefetcher.translation_requests->can_take()) {
            auto request = prefetcher.translation_requests->take();
            // only forward the request given drop/overflow/etc. cannot be called on these
            if (translation_requests_allowed && this->translate_prefetch_request->can_put())
                this->translate_prefetch_request->put(request);
        }
    }

    void forward_all_requests()
    {
        if (!unified_queue)
            return;

        for (auto &prefetcher : prefetchers)
            forward_requests(prefetcher);
    }

    uint64_t prefetchers_requested(T req, bool remove = false)
    {
        uint64_t mask = UINT64_MAX;
        if (unified_queue) {
            if (auto it = requested.find(addr_of(req)); it != requested.end()) {
                mask = it->second;
                if (remove)
                    requested.erase(it);
            }
        } else if (auto type = PrefetcherData<T>::get_type(req); type != prefetch_type_t{}) {
            mask = 0;
            for (auto it = prefetchers.crbegin(); it != prefetchers.crend(); ++it)
                mask = (mask << 1) | (type == it->module->prefetch_type() ? 1ULL : 0);
        }
        return mask;
    }

    explicit MultiplePrefetcherBase(sc_core::sc_module_name name) : PT(name)
    {
        auto &parameters = this->get_environment()->parameters;
        if (auto *settings = parameters.match_setting(fmt::format("{}.prefetchers", this->name())); settings) {
            AMPERE_ASSERT(settings->value().is_array() || settings->value().is_object(),
                          "Expected array or dict of prefetchers, got {}",
                          settings->value().type_name());

            std::unique_ptr<prefetch_mux_t> prefetch_request_mux;
            std::unique_ptr<translate_mux_t> translate_prefetch_request_mux;

            if (!unified_queue) {
                prefetch_request_mux =
                    std::make_unique<prefetch_mux_t>(multiplex("prefetch_requests_mux", this->prefetch_request));
                translate_prefetch_request_mux = std::make_unique<translate_mux_t>(
                    multiplex("translate_prefetch_request_mux", this->translate_prefetch_request));

                auto arbiter = [this](unsigned a, unsigned b) {
                    return this->arbitrate_prefetchers(a, b);
                };

                prefetch_request_mux->arbiter(arbiter);
                translate_prefetch_request_mux->arbiter(arbiter);
            }

            auto prefetcher_settings = settings->value();
            if (prefetcher_settings.is_array()) {
                for (const auto &params : prefetcher_settings) {
                    AMPERE_ASSERT(params.is_object(), "Expected dict in array, got {}", params.type_name());
                    if (!params["type"].is_null())
                        add_prefetcher(
                            params["name"], params, prefetch_request_mux.get(), translate_prefetch_request_mux.get());
                }
            } else if (prefetcher_settings.is_object()) {
                std::vector<std::pair<unsigned, decltype(prefetcher_settings.begin())>> ordered_settings;
                unsigned order = 0;
                for (auto it = prefetcher_settings.begin(); it != prefetcher_settings.end(); ++it) {
                    if (it.key() != "type" && !it.value().is_null()
                        && (!it.value().is_object() || !it.value()["type"].is_null()))
                    {
                        const auto &s = ordered_settings.emplace_back(it.value().value("order", order), it);
                        order = s.first + 1;
                    }
                }

                std::sort(ordered_settings.begin(), ordered_settings.end());

                for (const auto &[_, it] : ordered_settings)
                    add_prefetcher(
                        it.key(), it.value(), prefetch_request_mux.get(), translate_prefetch_request_mux.get());
            }

            if (!unified_queue) {
                std::unordered_set<prefetch_type_t> types;
                for (auto &prefetcher : prefetchers) {
                    auto type = prefetcher.module->prefetch_type();
                    AMPERE_ASSERT(!types.count(type), "Can only have 1 prefetcher of each type");
                    types.emplace(type);
                }
            }
        }
    }

    template<typename P, typename... Args>
    void call_each(P func, Args &&...args)
    {
        for (auto &prefetcher : prefetchers)
            ((*prefetcher.module).*func)(std::forward<Args>(args)...);
    }

    template<typename P, typename... Args>
    void call_each_and_forward(P func, Args &&...args)
    {
        for (auto &prefetcher : prefetchers) {
            ((*prefetcher.module).*func)(std::forward<Args>(args)...);
            forward_requests(prefetcher);
        }
    }

    template<typename P, typename... Args>
    void call_requested(P func, bool remove, T req, Args &&...args)
    {
        auto mask = prefetchers_requested(req, remove);
        for (auto &prefetcher : prefetchers)
            if (mask & prefetcher.mask())
                ((*prefetcher.module).*func)(req, std::forward<Args>(args)...);
    }

public:
    bool run() override
    {
        forward_all_requests();
        return true;
    }

    virtual bool arbitrate_prefetchers(unsigned a, unsigned b) const { return a < b; }

    void end_of_elaboration() override
    {
        translation_requests_allowed = this->translate_prefetch_request.bind_count() > 0;
    }
};

template<typename T>
class MultiplePrefetchers : public MultiplePrefetcherBase<T, Prefetcher<T>>
{
    using Base = MultiplePrefetcherBase<T, Prefetcher<T>>;
    using prefetch_type_t = typename Base::prefetch_type_t;

public:
    explicit MultiplePrefetchers(sc_core::sc_module_name name) : MultiplePrefetcherBase<T, Prefetcher<T>>(name) {}

    prefetch_type_t prefetch_type() const override { return {}; }

protected:
    using train_func_t = void (Prefetcher<T>::*)(uint64_t, T, bool, bool, bool, prefetch_type_t);
    using fill_func_t = void (Prefetcher<T>::*)(uint64_t, T, bool, bool);

    void train_impl(T req, bool hit, bool prefetch, bool demand) override
    {
        if constexpr (std::is_same_v<prefetch_type_t, MemOp::PrefetchType>) {
            this->call_each_and_forward(static_cast<train_func_t>(&Prefetcher<T>::train),
                                        0ULL,
                                        req,
                                        hit,
                                        prefetch,
                                        demand,
                                        MemOp::PrefetchType::NONE);
        } else {
            this->call_each_and_forward(
                static_cast<train_func_t>(&Prefetcher<T>::train), 0ULL, req, hit, prefetch, demand, prefetch_type_t{});
        }
    }

    void train_impl(T req, bool hit, bool prefetch, bool demand, prefetch_type_t prefetch_type) override
    {
        this->call_each_and_forward(
            static_cast<train_func_t>(&Prefetcher<T>::train), 0ULL, req, hit, prefetch, demand, prefetch_type);
    }

    void train_impl(uint64_t index, T req, bool hit, bool prefetch, bool demand, prefetch_type_t prefetch_type) override
    {
        this->call_each_and_forward(
            static_cast<train_func_t>(&Prefetcher<T>::train), index, req, hit, prefetch, demand, prefetch_type);
    }

    void drop_impl(T req, PrefetchDropReason reason) override
    {
        this->call_requested(&Prefetcher<T>::drop, true, req, reason);
    }

    void overflow_impl(T req) override { this->call_requested(&Prefetcher<T>::overflow, true, req); }

    void fill_impl(T req, bool fb_pref, bool was_alloc_by_pf) override
    {
        this->prefetchers_requested(req, true);
        this->call_each(static_cast<fill_func_t>(&Prefetcher<T>::fill), 0ULL, req, fb_pref, was_alloc_by_pf);
    }

    void fill_impl(uint64_t index, T req, bool fb_pref, bool was_alloc_by_pf) override
    {
        this->prefetchers_requested(req, true);
        this->call_each(static_cast<fill_func_t>(&Prefetcher<T>::fill), index, req, fb_pref, was_alloc_by_pf);
    }

    void evict_impl(PhysAddr pa, bool prefetch, bool demand) override
    {
        this->call_each(&Prefetcher<T>::evict, pa, prefetch, demand);
    }

    void launch_impl(T req) override { this->call_requested(&Prefetcher<T>::launch, false, req); }

    void flush_impl(Flush flush_info) override { this->call_each(&Prefetcher<T>::flush, flush_info); }
};

class CP_Multiple : public MultiplePrefetchers<Fetch>, public implementation<CP_Multiple, Prefetcher<Fetch>>
{
public:
    explicit CP_Multiple(sc_core::sc_module_name name);
};

class L2P_Multiple : public MultiplePrefetchers<MemReq>, public implementation<L2P_Multiple, Prefetcher<MemReq>>
{
public:
    explicit L2P_Multiple(sc_core::sc_module_name name);
};

} // namespace ampere::core

/*
 * vim: sw=4:ts=4:et:si:cindent:cino=\:0,(0,g0,t0:
 */
