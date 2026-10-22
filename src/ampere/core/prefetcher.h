//
// Copyright (c) 2019-2021 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//

#pragma once

#include <array>
#include "ampere/archx/addr.h"
#include "ampere/core/fetch.h"
#include "ampere/core/flush.h"
#include "ampere/sc/sc.h"
#include "ampere/util/enum.h"
#include "ampere/uarch_common/memop.h"
#include "ampere/uarch_common/memreq.h"

make_ns_enum(ampere::core, PrefetchDropReason,
    EXCEPTION_LEVEL,
    PAGE_FAULT,
    PAGE_MISS,
    PAGE_MISS_NO_RESOURCES,
    CACHE_HIT,
    CACHE_HIT_PREFETCH,
    CACHE_MISS_IN_FLIGHT,
    CACHE_MISS_PREFETCH_IN_FLIGHT,
    CACHE_MISS_NO_RESOURCES,
    CACHE_MISS_NO_PREFETCH_RESOURCES
);

namespace ampere::core {

using namespace ampere::sc;
using namespace ampere::uarch_common;

template<typename T>
struct PrefetcherData
{
    static PhysAddr get_addr(T req)
    {
        static_assert(!std::is_same_v<T, T>, "Unsupported Prefetch Type");
        return req->bogus;
    }

    static PhysAddr get_phys_addr(T req)
    {
        static_assert(!std::is_same_v<T, T>, "Unsupported Prefetch Type");
        return req->bogus;
    }

    static unsigned get_type(T req)
    {
        static_assert(!std::is_same_v<T, T>, "Unsupported Prefetch Type");
        return req->bogus;
    }
};

template<typename T>
struct PrefetchTranslateRequest
{
    using prefetch_type_t = decltype(PrefetcherData<T>::get_type(std::declval<T>()));

    VirtAddr virt_addr;
    sc::Txn transaction;
    prefetch_type_t prefetch_type;
};

/*
 * In general the prefetcher receives information about target cachelines
 * via three different flags depending on event type: prefetch, demand,
 * and upgrade.  They should be interpreted via the following tables:
 *
 * +---------------------------------------------------------+
 * | Train or Evict Event                | prefetch | demand |
 * +---------------------------------------------------------+
 * | Miss or Invalid (Eviction)          |  false   | false  |
 * | Hit or Eviction was demand filled   |  false   |  true  |
 * | Hit or Eviction was prefetch filled |   true   | false  |
 * | Hit or Eviction was prefetch        |   true   |  true  |
 * |    initiated but demand filled      |          |        |
 * +---------------------------------------------------------+
 *
 * +---------------------------------------+
 * |   Fill Event             |   upgrade  |
 * +---------------------------------------+
 * |   Prefetch Fill          |   false    |
 * |   Prefetch Upgrade Fill  |   true     |
 * +---------------------------------------+
 */

// Prefetchers
template<typename T>
class Prefetcher : public Module, poly<Prefetcher<T>>
{
public:
    using request_t = T;
    using addr_t = decltype(PrefetcherData<T>::get_addr(std::declval<T>()));
    using prefetch_type_t = decltype(PrefetcherData<T>::get_type(std::declval<T>()));

    explicit Prefetcher(sc_core::sc_module_name name) : Module(name) {}

    Out<T> prefetch_request{"prefetch_request"};
    OutOpt<PrefetchTranslateRequest<T>> translate_prefetch_request{"translate_prefetch_request"};

    /*
     * Called by prefetchee when training should occur
     *  req: non-prefetch request which caused the lookup that initiated training
     *  prefetch: indicates line hit by lookup was installed by a prefetch
     *  demand: indicates line hit by lookup was a demand request prior to being filled
     *          note: both prefetch and demand may be true if a prefetch was upgraded
     *                to a demand prior to fill.
     */
    void train(uint64_t index, T req, bool hit, bool prefetch, bool demand, prefetch_type_t prefetch_type);

    /*
     * Called by prefetchee when prefetch is scheduled from prefetch queue, but is dropped.
     *  req: prefetch req which was dropped
     *  reason: reason the prefetch was dropped
     * Examples: Could already be in l2 cache, or it attached to existing request
     */
    void drop(T req, PrefetchDropReason reason);

    /*
     * Called by prefetchee when a flush event is propagating through machine
     */
    void flush(Flush flush_info);

    /*
     *  Called when prefetchee's outstanding prefetch queue overflowed
     *   req: prefetch request which was bumped from the outstanding prefetch queue
     */
    void overflow(T req);

    /* The pretched generated a fill into the prefetchee.
     *  req: prefetch request which initiated a fill
     *  fb_pref: fill buffer prefetch tag
     *  was_alloc_by_pf: this fill was the result of a prior prefetch
     */
    void fill(uint64_t index, T req, bool fb_pref, bool was_alloc_by_pf);

    /* Called by prefetchee when eviction-related training should occur
     *  pa: pa of request that originally filled evicted line
     *  prefetch: evicted line was installed by a prefetch
     *  demand: evicted line was requested by a demand prior to fill
     *          note: both prefetch and demand may be true if a prefetch was upgraded
     *                to a demand prior to fill.
     */
    void evict(PhysAddr pa, bool prefetch, bool demand);

    /*
     *  Called when prefetchee launches a prefetch request
     *   req: prefetch request which was launched
     */
    void launch(T req);

    virtual prefetch_type_t prefetch_type() const;

protected:
    virtual void train_impl(T req, bool hit, bool prefetch, bool demand);
    virtual void train_impl(T req, bool hit, bool prefetch, bool demand, prefetch_type_t prefetch_type);
    virtual void train_impl(uint64_t index, T req, bool hit, bool prefetch, bool demand, prefetch_type_t prefetch_type);
    virtual void drop_impl(T req, PrefetchDropReason reason);
    virtual void overflow_impl(T req);
    virtual void fill_impl(T req, bool fb_pref, bool was_alloc_by_pf);
    virtual void fill_impl(uint64_t index, T req, bool fb_pref, bool was_alloc_by_pf);
    virtual void evict_impl(PhysAddr pa, bool prefetch, bool demand);
    virtual void launch_impl(T req);
    virtual void flush_impl(Flush flush_info);
    /* Returns the bandwidth of the prefetchers.  This field is populated during
     * construction and can be used by composite prefetchers to set bandwidths for
     * all ports
     */
    int get_bandwidth() const;
    EventData<prefetch_type_t> ev_pf_src{"src", "Prefetcher type"};
    EventData<bool> ev_upgraded{"upgraded", "Prefetch was upgraded to demand load"};
    EventData<PrefetchDropReason> ev_drop_reason{"drop_reason", "Reason the prefetch was dropped"};

public:
    Event ev_hit{"hit", "Prefetched line was hit"};
    Event ev_fill{"fill", "Prefetched line resulted in a fill"};
    Event ev_drop{"drop", "Prefetched line was superseded by pre-existing demand request or line"};
    Event ev_overflow{"overflow", "Prefetcher queue lost oldest existing prefetch prior to scheduling"};
    Event ev_launch{"launch", "Prefetch was launched"};
};
template<typename T>
void Prefetcher<T>::train_impl(T req, bool hit, bool prefetch, bool demand)
{
}
template<typename T>
void Prefetcher<T>::train_impl(T req, bool hit, bool prefetch, bool demand, prefetch_type_t prefetch_type)
{
    return train_impl(req, hit, prefetch, demand);
}
template<typename T>
void Prefetcher<T>::train_impl(
    uint64_t index, T req, bool hit, bool prefetch, bool demand, prefetch_type_t prefetch_type)
{
    return train_impl(req, hit, prefetch, demand, prefetch_type);
}
template<typename T>
void Prefetcher<T>::drop_impl(T req, PrefetchDropReason reason)
{
}
template<typename T>
void Prefetcher<T>::flush_impl(Flush flush_info)
{
}
template<typename T>
void Prefetcher<T>::overflow_impl(T req)
{
}
template<typename T>
void Prefetcher<T>::fill_impl(T req, bool fb_pref, bool was_alloc_by_pf)
{
}
template<typename T>
void Prefetcher<T>::fill_impl(uint64_t index, T req, bool fb_pref, bool was_alloc_by_pf)
{
    return fill_impl(req, fb_pref, was_alloc_by_pf);
}
template<typename T>
void Prefetcher<T>::evict_impl(PhysAddr pa, bool prefetch, bool demand)
{
}
template<typename T>
void Prefetcher<T>::launch_impl(T req)
{
}

template<typename T>
auto Prefetcher<T>::prefetch_type() const -> prefetch_type_t
{
    if constexpr (std::is_same_v<prefetch_type_t, MemOp::PrefetchType>) {
        return MemOp::PrefetchType::NONE;
    } else {
        return {};
    }
}

template<>
struct PrefetcherData<MemReq>
{
    static PhysAddr get_addr(MemReq req) { return req->phys_addr; }
    static PhysAddr get_phys_addr(MemReq req) { return req->phys_addr; }
    static MemOp::PrefetchType get_type(MemReq req) { return req->prefetch_type; }
};

template<>
struct PrefetcherData<Fetch>
{
    static ProgramCounter get_addr(Fetch req) { return req->program_counter; }
    static PhysAddr get_phys_addr(Fetch req) { return *req->phys_addr; }
    static unsigned get_type(Fetch req) { return 0; }
};

template<typename T>
void Prefetcher<T>::train(uint64_t index, T req, bool hit, bool prefetch, bool demand, prefetch_type_t prefetch_type)
{
    if (prefetch) {
        ev_hit(req, PrefetcherData<T>::get_addr(req), ev_upgraded(demand));
    }

    train_impl(index, req, hit, prefetch, demand, prefetch_type);
}

template<typename T>
void Prefetcher<T>::fill(uint64_t index, T req, bool fb_pref, bool was_alloc_by_pf)
{
    if (was_alloc_by_pf && PrefetcherData<T>::get_type(req) == prefetch_type()) {
        ev_fill(req, PrefetcherData<T>::get_addr(req), ev_upgraded(!fb_pref));
    }
    fill_impl(index, req, fb_pref, was_alloc_by_pf);
}

template<typename T>
void Prefetcher<T>::flush(Flush flush_info)
{
    flush_impl(flush_info);
}

template<typename T>
void Prefetcher<T>::drop(T req, PrefetchDropReason reason)
{
    ev_drop(req, PrefetcherData<T>::get_addr(req), ev_drop_reason(reason));
    drop_impl(req, reason);
}

template<typename T>
void Prefetcher<T>::overflow(T req)
{
    ev_overflow(req, PrefetcherData<T>::get_addr(req));
    overflow_impl(req);
}

template<typename T>
void Prefetcher<T>::evict(PhysAddr pa, bool prefetch, bool demand)
{
    evict_impl(pa, prefetch, demand);
}

template<typename T>
void Prefetcher<T>::launch(T req)
{
    ev_launch(req, ev_pf_src(PrefetcherData<T>::get_type(req)), PrefetcherData<T>::get_addr(req));
    launch_impl(req);
}

template<typename T>
int Prefetcher<T>::get_bandwidth() const
{
    int bandwidth = 1;

    if constexpr (std::is_same_v<prefetch_type_t, MemOp::PrefetchType>) {
        if (auto bw = get_environment()->parameters.match_setting("core.l2c.prefetch_request_port.bandwidth"); bw)
            bandwidth = from_json_T<int>(bw->value());
    }

    return bandwidth;
}

// model no prefetching by passing L2P_None as the type.
class L2P_None : public Prefetcher<MemReq>, implementation<L2P_None, Prefetcher<MemReq>>
{
public:
    explicit L2P_None(sc_core::sc_module_name name) : Prefetcher<MemReq>(name) {}
};

class CP_None : public Prefetcher<Fetch>, implementation<CP_None, Prefetcher<Fetch>>
{
public:
    explicit CP_None(sc_core::sc_module_name name) : Prefetcher<Fetch>(name) {}
};

} // namespace ampere::core

/*
 * vim: sw=4:ts=4:et:si:cindent:cino=\:0,(0,g0,t0:
 */
