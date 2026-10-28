//
// Copyright (c) 2023 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//

#include "ampere/tigris/tigris.h"

#include <algorithm>
#include <boost/smart_ptr/local_shared_ptr.hpp>
#include <cstdlib>
#include <memory>
#include <random>
#include <unordered_map>
#include <utility>

#include "ampere/archx/addr.h"
#include "ampere/archx/barrier_type.h"
#include "ampere/archx/page_size.h"
#include "ampere/archx/sysreg.h"
#include "ampere/archx/translation.h"
#include "ampere/sc/event.h"
#include "ampere/sc/report.h"
#include "ampere/uarch_common/cache.h"
#include "ampere/uarch_common/memop.h"
#include "ampere/uarch_common/memreq.h"

#include "ampere/core/prefetcher.h"
#include "ampere/core/prefetchers/oracle.h"
#include "ampere/core/tlb.h"
#include "ampere/tigris/helpers.h"

namespace ampere::tigris {

void BiAddrTranslator::add(VirtAddr virt_addr, PhysAddr phys_addr)
{
    auto virt_addr_la = virt_addr.line_address();
    auto phys_addr_la = phys_addr.line_address();

    // we use zero to mean not valid address
    assert(virt_addr_la != 0_va);
    assert(phys_addr_la != 0_pa);

    // update maps
    virt2phys[virt_addr_la] = phys_addr_la;
    phys2virt[phys_addr_la] = virt_addr_la;
}
VirtAddr BiAddrTranslator::get_virt_addr(PhysAddr phys_addr) const
{
    if (!phys2virt.contains(phys_addr.line_address())) {
        assert(0_pa == phys_addr);
    }
    // note, PhysAddr(0) is used for unititialized virt_addrs. In warmup they aren't in table, but in l2
    return phys_addr == 0_pa ? 0_va : phys2virt.at(phys_addr.line_address());
}
/*
 * pc2va is defined in addr.h. It's just a cast
 */
VirtAddr BiAddrTranslator::get_virt_addr(ProgramCounter pc) const
{
    return pc2va(pc.line_address());
}
ProgramCounter BiAddrTranslator::get_pc(VirtAddr virt_addr) const
{
    return ProgramCounter(virt_addr.value());
}
ProgramCounter BiAddrTranslator::get_pc(PhysAddr phys_addr) const
{
    return get_pc(get_virt_addr(phys_addr));
}
PhysAddr BiAddrTranslator::get_phys_addr(VirtAddr virt_addr) const
{
    // note, VirtAddr(0) is used for unititialized virt_addrs. In warmup they aren't in table, but in l2
    if (!virt2phys.contains(virt_addr.line_address())) {
        assert(0_va == virt_addr);
    }
    return virt_addr == 0_va ? 0_pa : virt2phys.at(virt_addr.line_address());
}
PhysAddr BiAddrTranslator::get_phys_addr(ProgramCounter pc) const
{
    return get_phys_addr(get_virt_addr(pc.line_address()));
}
PhysAddr BiAddrTranslator::get_phys_addr(PhysAddr phys_addr) const
{
    return phys_addr;
}
void BiAddrTranslator::dump() const
{
    std::ofstream output_file("tlbMapVa2Pa.txt");
    output_file << "VA2PA" << std::endl;
    for (const auto& pair : virt2phys) {
        output_file << pair.first.value() << ": " << pair.second.value() << std::endl;
    }
    output_file << "PA2VA" << std::endl;
    for (const auto& pair : phys2virt) {
        output_file << pair.first.value() << ": " << pair.second.value() << std::endl;
    }
}

template<typename addr_t, typename value>
uint64_t PhysicallyTaggedCache<addr_t, value>::compute_tag(addr_t address) const
{
    return cacheAddrTranslator->get_phys_addr(address).extract_range(this->ms_tag_bit,
                                                                     this->line_bits + this->set_bits);
}

template<typename upper_addr_t, typename prefetcher_t>
bool Tigris::CacheLevel<upper_addr_t, prefetcher_t>::should_train(bool hit, const TMemReq& req) const
{
    return req->getMemReq()->barrier_type == BAR_NONE && (train_on_non_temporal || !req->getMemReq()->nontemporal)
           && ((train_on_loads && req->getMemReq()->type == READ)
               || (train_on_stores && req->getMemReq()->type == WRITE))
           && (!train_on_misses_only || !hit);
}

template<typename upper_addr_t, typename prefetcher_t>
template<typename addr_to_extract_t>
addr_to_extract_t Tigris::CacheLevel<upper_addr_t, prefetcher_t>::extract_addr(const TMemReq& req) const
{
    auto memreq = req->getMemReq();
    if constexpr (std::is_same<addr_to_extract_t, VirtAddr>::value) {
        return memreq->virt_addr;
    } else if constexpr (std::is_same<addr_to_extract_t, PhysAddr>::value) {
        return memreq->phys_addr;
    } else if constexpr (std::is_same<addr_to_extract_t, ProgramCounter>::value) {
        return memreq->pc;
    } else {
        throw std::runtime_error(
            "address translation is only implemented to pc, physical, or virtual address. You're translating to "
            "something else. You must add this functionality in this function");
    }
}

template<typename upper_addr_t, typename prefetcher_t>
const Tigris::CacheLevel<upper_addr_t, prefetcher_t>::prefetch_req_t&
Tigris::CacheLevel<upper_addr_t, prefetcher_t>::extract_prefetch_req(const TMemReq& req) const
{
    if constexpr (std::is_same<prefetch_req_t, BackendMemReq>::value) {
        return req->getBackendMemReq();
    } else if constexpr (std::is_same<prefetch_req_t, MemReq>::value) {
        return req->getMemReq();
    }
}

template<typename upper_addr_t, typename prefetcher_t>
const Tigris::CacheLevel<upper_addr_t, prefetcher_t>::prefetch_t
Tigris::CacheLevel<upper_addr_t, prefetcher_t>::extract_prefetch_type(const TMemReq& req) const
{
    if constexpr (std::is_same<prefetch_req_t, BackendMemReq>::value) {
        return req->getBackendMemReq().prefetch_type;
    } else if constexpr (std::is_same<prefetch_req_t, MemReq>::value) {
        return req->getMemReq()->prefetch_type;
    }
}

template<typename upper_addr_t, typename prefetcher_t>
template<typename from_addr_t, typename to_addr_t>
to_addr_t Tigris::CacheLevel<upper_addr_t, prefetcher_t>::translate_addr(from_addr_t addr)
{
    if constexpr (std::is_same<from_addr_t, to_addr_t>::value) {
        return addr;
    } else if constexpr (std::is_same<to_addr_t, VirtAddr>::value) {
        return magic_addr_map->get_virt_addr(addr);
    } else if constexpr (std::is_same<to_addr_t, PhysAddr>::value) {
        return magic_addr_map->get_phys_addr(addr);
    } else {
        throw std::runtime_error(
            "address translation is only implemented to physical or virtual address. You're translating to something "
            "else. You must add this functionality in magic address translator");
    }
}

template<typename upper_addr_t, typename prefetcher_t>
Tigris::CacheLevel<upper_addr_t, prefetcher_t>::CacheLevel(const sc_core::sc_module_name& name,
                                                           Requester type,
                                                           bool physically_tagged,
                                                           bool aligned)
    : Module(name), aligned(aligned), request_type(type)
{
    if (physically_tagged) {
        cache = std::make_unique<PhysicallyTaggedCache<addr_t, Line>>("cache", magic_addr_map);
    } else {
        cache = std::make_unique<uarch_common::Cache<addr_t, Line>>("cache");
    }
    connect(prefetcher->prefetch_request, prefetch_request)
        .capacity(incoming_per_cycle * 2)
        .bandwidth(incoming_per_cycle)
        .latency(1)
        .name("prefetcher_requests");
}

template<typename upper_addr_t, typename prefetcher_t>
void Tigris::CacheLevel<upper_addr_t, prefetcher_t>::setup(const Tigris& tigris)
{
    core::OraclePrefetcher<prefetch_req_t>::try_owned_setup(prefetcher.get(), [&]() { return tigris.make_driver(); });
}

template<typename upper_addr_t, typename prefetcher_t>
void Tigris::CacheLevel<upper_addr_t, prefetcher_t>::connect_source(const std::string& name,
                                                                    OutOpt<MemoryRequest>& requests,
                                                                    InOpt<MemoryRequest>& responses,
                                                                    LevelSettings settings)
{
    auto& level = lower.emplace(Requester::ANY, std::make_unique<Lower>(name, settings)).first->second;
    connect(requests, level->request)
        .bandwidth(incoming_per_cycle)
        .capacity(incoming_per_cycle)
        .latency(1)
        .name(name + "_to_cache_requests");

    connect(level->response, responses)
        .bandwidth(incoming_per_cycle)
        .capacity(incoming_per_cycle)
        .latency(1)
        .name(name + "_to_source_responses");
}

template<typename upper_addr_t, typename prefetcher_t>
template<typename upper_level_upper_addr_t, typename upper_level_prefetcher_t>
void Tigris::CacheLevel<upper_addr_t, prefetcher_t>::connect_upper(
    Tigris::CacheLevel<upper_level_upper_addr_t, upper_level_prefetcher_t>& upper_level, Requester type)
{
    using upper_level_addr_t = typename upper_level_prefetcher_t::addr_t;
    static_assert(std::is_same<upper_level_addr_t, upper_addr_t>::value,
                  "upper.lower addr type does not match addr type of cache!");
    const std::string lower_name = basename();
    LevelSettings merged;
    merged.needs_load_responses =
        fill_loads || std::any_of(lower.begin(), lower.end(), [](auto& e) { return e.second->needs_load_responses; });
    merged.needs_store_responses =
        fill_stores || std::any_of(lower.begin(), lower.end(), [](auto& e) { return e.second->needs_store_responses; });

    auto& level =
        upper_level.lower
            .emplace(type,
                     std::make_unique<typename CacheLevel<upper_level_upper_addr_t, upper_level_prefetcher_t>::Lower>(
                         lower_name, merged))
            .first->second;
    connect(fill_request, level->request)
        .bandwidth(upper_level.incoming_per_cycle)
        .capacity(outstanding)
        .latency(request_latency)
        .return_latency(1)
        .name(lower_name + "_lower_to_upper_requests");

    connect(level->response, fill_response)
        .bandwidth(upper_level.incoming_per_cycle * 2)
        .capacity(outstanding)
        .latency(fill_latency)
        .return_latency(1)
        .name(lower_name + "_upper_to_lower_responses");

    level->evict_request = std::make_unique<Out<upper_level_addr_t>>((lower_name + "_evict_request").c_str());
    upper_evict_request = std::make_unique<In<upper_addr_t>>((lower_name + "_upper_evict_request").c_str());

    connect(*level->evict_request, *upper_evict_request)
        .bandwidth(upper_level.incoming_per_cycle * 2)
        .capacity(outstanding)
        .latency(1)
        .name(lower_name + "upper_to_lower_evicts");
}

template<typename addr_t, typename prefetcher_t>
void Tigris::CacheLevel<addr_t, prefetcher_t>::connect_memory()
{
    connect(fill_request, fill_response)
        .bandwidth(incoming_per_cycle)
        .capacity(outstanding)
        .latency(request_latency + fill_latency)
        .return_latency(1)
        .name("memory_responses");
}

template<typename addr_t, typename prefetcher_t>
bool Tigris::CacheLevel<addr_t, prefetcher_t>::can_put_outstanding() const
{
    return !outstanding || (outstanding_requests.size() < outstanding);
}

template<typename upper_addr_t, typename prefetcher_t>
bool Tigris::CacheLevel<upper_addr_t, prefetcher_t>::can_request(const TMemReq& req) const
{
    return ((req && !should_fill(req)) || can_put_outstanding()) && fill_request->can_put();
}

template<typename upper_addr_t, typename prefetcher_t>
void Tigris::CacheLevel<upper_addr_t, prefetcher_t>::preload(addr_t addr, unsigned int size, bool write, bool code)
{
    // TODO not using warmup now, but will want this later
    // if (!warmup)
    //     return;

    // if (auto hit = cache->lookup({}, addr, true, true); !hit) {
    //     //TODO this doesn't make any sense. Gotta fix it
    //     auto req = sc::trans_make<TMemReq>(0, addr, code ? ICF : LSU, write ? WRITE : READ);
    //     //prefetcher->fill(0, req, false, false);
    //     if (auto victim = cache->refill({}, addr, Line{addr.line_address(), {}, false, true}, true); victim){}
    //         //prefetcher->evict(victim->addr, false, true);
    // }
}

template<typename upper_addr_t, typename prefetcher_t>
std::tuple<uarch_common::CacheResult<typename Tigris::CacheLevel<upper_addr_t, prefetcher_t>::Line>, bool, bool>
Tigris::CacheLevel<upper_addr_t, prefetcher_t>::lookup_and_request(PhysAddr phys_addr,
                                                                   VirtAddr virt_addr,
                                                                   const TMemReq& req,
                                                                   Lower* requester)
{
    bool prefetch = !requester;
    bool should_fill_request = prefetch || should_fill(req);
    bool should_write_through = write_through && req->getMemReq()->type == WRITE;
    bool request_needs_response = requester && needs_response(*requester, req);
    addr_t req_addr = extract_addr<addr_t>(req);
    upper_addr_t upper_addr = extract_addr<upper_addr_t>(req);

    // if hit in cache
    if (auto hit = cache->lookup(req->getMemReq(), req_addr, true, false, prefetch); hit && !should_write_through) {
        // cache hit
        if (!prefetch) {
            if (!hit->used && hit->prefetch_type) {
                hit->used = true;
                event.prefetch_useful(req->getMemReq());
            }
            if (request_needs_response)
                requester->response->put({req, false});
        } else {
            event.prefetch_already_present(req->getMemReq());
        }

        return {hit, false, false};
    }

    // if hit in ofb
    if (!should_write_through) {
        if (auto e = outstanding_requests.find(upper_addr.line_address()); e != outstanding_requests.end()) {
            bool upgraded = false;
            if (!prefetch) {
                auto& entry = e->second;
                if (entry.opportunisitic) {
                    entry.req = req;
                    entry.needed = req->getMemReq()->origin;
                    entry.opportunisitic = false;
                    event.prefetch_upgrade(req->getMemReq());
                    upgraded = true;
                }

                if (request_needs_response) {
                    if (should_fill_request) {
                        entry.needed |= req->getMemReq()->origin;
                        entry.responses.emplace_back(req);
                    } else {
                        requester->response->put({req, false});
                    }
                }
            }

            event.fill_buffer_hit(req->getMemReq());
            return {{}, false, upgraded};
        }
    }

    // if miss
    bool requested = fill_request->can_put();
    AMPERE_ASSERT(prefetch || requested, "Unable to request something that cannot be dropped");
    if (requested) {
        // create a new tigris request who's memreq attribute is a child
        // of req->getMemReq()
        MemReq outgoing_memreq = make_child<MemReq>(const_cast<MemReq&>(req->getMemReq()),
                                                    req->getMemReq()->id,
                                                    phys_addr.line_address(),
                                                    request_type,
                                                    should_fill_request ? READ : req->getMemReq()->type,
                                                    PhysAddr::LINE_SIZE,
                                                    false,
                                                    req->getMemReq()->prefetch_type,
                                                    req->getMemReq()->barrier_type,
                                                    ATOMIC_NONE,
                                                    SNOOP_NONE,
                                                    req->getMemReq()->page_size,
                                                    0,
                                                    virt_addr.line_address(),
                                                    req->getMemReq()->pc);
        TMemReq outgoing_req(new TigrisMemReq(outgoing_memreq));

        if (should_fill_request && can_put_outstanding()) {
            unsigned needed = prefetch ? 0 : req->getMemReq()->origin;
            auto& entry =
                outstanding_requests
                    .emplace(upper_addr.line_address(), Outstanding{req, extract_prefetch_type(req), prefetch, needed})
                    .first->second;

            if (!prefetch && request_needs_response)
                entry.responses.emplace_back(req);
        } else if (!prefetch && request_needs_response) {
            requester->response->put({req, false});
        }

        fill_request->put({outgoing_req, false});
    }

    return {{}, requested, false};
}

template<typename upper_addr_t, typename prefetcher_t>
bool Tigris::CacheLevel<upper_addr_t, prefetcher_t>::can_accept(Requester type) const
{
    return lower.at(type)->response->can_put() && can_put_outstanding();
}

template<typename upper_addr_t, typename prefetcher_t>
void Tigris::CacheLevel<upper_addr_t, prefetcher_t>::handle_request(Lower& requester, const TMemReq& req, bool split)
{
    auto [hit, requested, upgraded] =
        lookup_and_request(req->getMemReq()->phys_addr, req->getMemReq()->virt_addr, req, &requester);
    auto prefetch = hit ? hit->prefetch_type.value_or(get_none_prefetch_t()) : get_none_prefetch_t();
    auto index = cache->compute_set(extract_addr<addr_t>(req));

    (req->getMemReq()->type == WRITE ? event.store_access : event.load_access)(
        req->getMemReq(), event.access_hit(static_cast<bool>(hit)));

    if (!split && should_train(static_cast<bool>(hit), req))
        prefetcher->train(
            index, extract_prefetch_req(req), static_cast<bool>(hit) || !requested, upgraded, true, prefetch);
}

template<typename upper_addr_t, typename prefetcher_t>
void Tigris::CacheLevel<upper_addr_t, prefetcher_t>::handle_prefetch_requests()
{
    // TODO used to be !drop_prefetches_on_fill_resources. My testing
    // has been using that version. This fix was recently identified, but we
    // haven't tested it thouroughly
    while (prefetch_request->can_take() && (drop_prefetches_on_fill_resources || can_request())) {
        prefetch_req_t prefetch_req = prefetch_request->take();
        TMemReq req(new TigrisMemReq(prefetch_req));
        if (can_request()) {
            VirtAddr virt_addr = req->getMemReq()->virt_addr;
            PhysAddr phys_addr = req->getMemReq()->phys_addr;
            if (auto [hit, requested, _upgraded] = lookup_and_request(phys_addr, virt_addr, req); hit || !requested) {
                core::PrefetchDropReason reason = core::PrefetchDropReason::CACHE_MISS_NO_RESOURCES;
                if (auto e = outstanding_requests.find(phys_addr.line_address()); e != outstanding_requests.end()) {
                    if (e->second.opportunisitic) {
                        reason = core::PrefetchDropReason::CACHE_MISS_PREFETCH_IN_FLIGHT;
                        event.prefetch_triggered_again(req->getMemReq());
                    } else {
                        reason = core::PrefetchDropReason::CACHE_MISS_IN_FLIGHT;
                        event.prefetch_triggered_late(req->getMemReq());
                    }
                    e->second.prefetch_hit = true;
                }
                prefetcher->drop(extract_prefetch_req(req), reason);
            } else {
                prefetcher->launch(extract_prefetch_req(req));
            }
        } else {
            prefetcher->overflow(extract_prefetch_req(req));
        }
    }
}

template<typename upper_addr_t, typename prefetcher_t>
void Tigris::CacheLevel<upper_addr_t, prefetcher_t>::handle_requests()
{
    for (auto& [type, level] : lower) {
        while (level->request->can_take() && level->response->can_put() && can_request()) {
            auto [req, split] = level->request->take();
            handle_request(*level, req, split);
        }
    }
}

template<typename upper_addr_t, typename prefetcher_t>
void Tigris::CacheLevel<upper_addr_t, prefetcher_t>::evict(const Line& victim, const TMemReq& req)
{
    auto prefetched = static_cast<bool>(victim.prefetch_type);
    if (!victim.used && !victim.warmup && prefetched) {
        event.evicted_unused(req->getMemReq());
        unused_eviction.emplace(victim.addr);
    }
    // This line isn't well tested. Used in some prefetchers we haven't
    // tested. Signature is phys_addr even if prefetcher operates on
    // virtual addrs
    prefetcher->evict(magic_addr_map->get_phys_addr(victim.addr), prefetched, !prefetched || victim.used);

    for (auto& [type, level] : lower) {
        if (level->evict_request) {
            (*level->evict_request)->put(victim.addr);
        }
    }
}

template<typename upper_addr_t, typename prefetcher_t>
bool Tigris::CacheLevel<upper_addr_t, prefetcher_t>::can_evict() const
{
    for (const auto& [type, level] : lower)
        if (level->evict_request && !(*level->evict_request)->can_put())
            return false;

    return true;
}

template<typename upper_addr_t, typename prefetcher_t>
void Tigris::CacheLevel<upper_addr_t, prefetcher_t>::handle_invalidates()
{
    if (!upper_evict_request)
        return;

    while ((*upper_evict_request)->can_take() && can_evict()) {
        upper_addr_t upper_addr = (*upper_evict_request)->take();
        addr_t addr = translate_addr<upper_addr_t, addr_t>(upper_addr);
        if (auto victim = cache->invalidate({}, addr); victim)
            evict(*victim, {});
    }
}

template<typename upper_addr_t, typename prefetcher_t>
void Tigris::CacheLevel<upper_addr_t, prefetcher_t>::handle_responses()
{
    for (auto it = remaining_responses.begin(); it != remaining_responses.end();) {
        auto lit = lower.find((*it)->getMemReq()->origin);
        auto& level = lit != lower.end() ? lit->second : lower.at(Requester::ANY);
        if (level->response->can_put()) {
            level->response->put({*it, false});
            it = remaining_responses.erase(it);
        } else {
            ++it;
        }
    }
}

template<typename upper_addr_t, typename prefetcher_t>
bool Tigris::CacheLevel<upper_addr_t, prefetcher_t>::can_fill() const
{
    if (!fill_response->can_take() || !can_evict())
        return false;

    const auto& [req, _split] = fill_response->peek();
    upper_addr_t upper_addr = extract_addr<upper_addr_t>(req);
    if (auto e = outstanding_requests.find(upper_addr.line_address()); e != outstanding_requests.end()) {
        for (const auto& [type, level] : lower) {
            if ((e->second.needed & type) != 0 && !level->response->can_put())
                return false;
        }
    }

    return true;
}

template<typename upper_addr_t, typename prefetcher_t>
bool Tigris::CacheLevel<upper_addr_t, prefetcher_t>::should_fill(const TMemReq& req) const
{
    return req->getMemReq()->type == WRITE ? fill_stores : fill_loads;
}

template<typename upper_addr_t, typename prefetcher_t>
bool Tigris::CacheLevel<upper_addr_t, prefetcher_t>::needs_response(Lower& requester, const TMemReq& req) const
{
    return req->getMemReq()->type == WRITE ? requester.needs_store_responses : requester.needs_load_responses;
}

template<typename upper_addr_t, typename prefetcher_t>
void Tigris::CacheLevel<upper_addr_t, prefetcher_t>::handle_fills()
{
    while (can_fill()) {
        auto [req, split] = fill_response->take();
        addr_t cache_addr = extract_addr<addr_t>(req);
        upper_addr_t upper_addr = extract_addr<upper_addr_t>(req);

        bool from_prefetch = false;
        bool was_outstanding = false;
        auto index = cache->compute_set(cache_addr);
        bool prefetch = false;

        // If we have the address requested
        if (auto e = outstanding_requests.find(upper_addr.line_address()); e != outstanding_requests.end()) {
            auto& entry = e->second;
            if (!entry.opportunisitic) {
                for (const auto& [type, level] : lower) {
                    for (auto it = entry.responses.begin(); it != entry.responses.end();) {
                        if (((*it)->getMemReq()->origin & type) == 0) {
                            ++it;
                        } else {
                            if (level->response->can_put()) {
                                level->response->put({*it, false});
                            } else
                                remaining_responses.emplace_back(*it);
                            it = entry.responses.erase(it);
                        }
                    }
                }
            }

            was_outstanding = true;
            prefetch = entry.opportunisitic;
            from_prefetch = entry.prefetch_type != get_none_prefetch_t();
            outstanding_requests.erase(e);
        }
        if (was_outstanding) {
            Line e{cache_addr.line_address(), extract_prefetch_type(req), !prefetch};
            if (auto victim = cache->refill(req->getMemReq(), cache_addr, e); victim)
                evict(*victim, req);

            if (prefetch)
                event.refill_prefetch(req->getMemReq());
            else if (req->getMemReq()->type == WRITE)
                event.refill_store(req->getMemReq());
            else
                event.refill_load(req->getMemReq());

            if (auto u = unused_eviction.find(cache_addr.line_address()); u != unused_eviction.end()) {
                event.unused_brought_back(req->getMemReq());
                unused_eviction.erase(u);
            }

            if (!split)
                prefetcher->fill(index, extract_prefetch_req(req), prefetch, prefetch || from_prefetch);
        }
    }
}

template<typename upper_addr_t, typename prefetcher_t>
bool Tigris::CacheLevel<upper_addr_t, prefetcher_t>::run()
{
    handle_invalidates();
    handle_responses();
    handle_fills();
    handle_requests();
    handle_prefetch_requests();
    return true;
}

void Tigris::reorder_requests()
{
    if (reorder_amount == 0 || reorder_probability == 0 || !memory_op_request->can_put()
        || !memory_model->can_accept_data() || requests.size() < 2)
        return;

    for (size_t i = reordered_until; i < requests_per_cycle && i < requests.size(); ++i, ++reordered_until) {
        if (probabilty(rp) > reorder_probability)
            continue;

        bool negative = i > (requests.size() / 2);
        int offset = static_cast<int>(distance(rd) + 1.0F) * (negative ? -1 : 1);
        int j = static_cast<int>(i) + offset;
        j = std::min<int>(std::max(0, j), requests.size() - 1);

        std::swap(requests[i], requests.at(j));
        event.memory_request_reordered(event.reorder_distance(offset));
    }
}

void Tigris::handle_code_requests()
{
    size_t i = 0;
    for (; i < code_requests_per_cycle && i < code_requests.size(); ++i) {
        auto& inst = code_requests[i];
        if (!handle_code(*inst))
            break;
    }

    for (size_t j = 0; j < i; ++j)
        code_requests.pop_front();
}

void Tigris::handle_requests()
{
    if (!memory_op_request->can_put() || !memory_model->can_accept_data()
        || (require_any_response() && waiting_requests.size() >= commit_depth))
        return;

    size_t i = 0;
    for (; i < requests_per_cycle && i < requests.size();) {
        auto& [inst, index, split_hi_half] = requests[i];
        if (auto [req, complete] = handle_memory_op(*inst, index, split_hi_half); req) {
            if (req->getMemReq()->type == WRITE ? require_store_response() : require_load_response()) {
                waiting_requests.emplace(req->getMemReq()->id, std::make_pair(req, false));
                last_activity_cycle = get_cycle();
            }

            // either move on to next request or process the hi-half of the split
            if (complete)
                ++i;
            else
                split_hi_half = true;
        } else {
            break;
        }
    }

    for (size_t j = 0; j < i; ++j)
        requests.pop_front();

    reordered_until = (reordered_until < i) ? 0 : reordered_until - i;
}

void Tigris::handle_code_responses()
{
    while (code_memory_response->can_take())
        code_memory_response->take();
}

void Tigris::handle_responses()
{
    while (memory_op_response->can_take()) {
        auto [req, _split] = memory_op_response->take();
        if (!require_any_response())
            continue;

        if (auto waiting = waiting_requests.find(req->getMemReq()->id); waiting != waiting_requests.end()) {
            waiting->second.second = true;
        } else {
            AMPERE_WARNING("Unexpected {} response request id: {}, addr: {:#018x} at cycle {}",
                           waiting_requests.begin()->second.first->getMemReq()->type,
                           waiting_requests.begin()->first,
                           waiting_requests.begin()->second.first->getMemReq()->phys_addr.value(),
                           get_cycle());
        }
    }

    if (commit_depth) {
        unsigned committed = 0;
        for (auto it = waiting_requests.begin(); it != waiting_requests.end() && it->second.second; ++committed) {
            it = waiting_requests.erase(it);
        }

        AMPERE_ASSERT_ERROR(waiting_requests.empty() || committed > 0 || !last_activity_cycle
                                || (get_cycle() - last_activity_cycle) < braindead_threshold,
                            "Braindead on {} request id: {}, addr: {:#018x} at cycle {}",
                            waiting_requests.begin()->second.first->getMemReq()->type,
                            waiting_requests.begin()->first,
                            waiting_requests.begin()->second.first->getMemReq()->phys_addr.value(),
                            get_cycle());

        if (committed)
            last_activity_cycle = get_cycle();
    }
}

bool Tigris::run()
{
    if (memory_model->accepts_code()) {
        handle_code_responses();
        handle_code_requests();
    }

    if (memory_model->accepts_data()) {
        handle_responses();
        reorder_requests();
        handle_requests();
    }

    return true;
}

BarrierType Tigris::barrier_type(archx::Barrier_Type type)
{
    switch (type) {
    case ampere::archx::Barrier_Type::DMB:
        return BarrierType::BAR_DMB;
    default:
        return BarrierType::BAR_NONE;
    }
}

core::MemOp::Type Tigris::memop_type(const archx::Instruction& instruction)
{
    if (instruction.is_load())
        return core::MemOp::READ;
    if (instruction.is_store())
        return core::MemOp::WRITE;
    return core::MemOp::NO_TYPE;
}

uint32_t Tigris::memop_size(const archx::Instruction& instruction, unsigned index) const
{
    auto size = memory_op_size(instruction, index);
    if (size > 1 && !is_memory_op_pair(instruction)) {
        if (auto parts = scatter_gather_parts(instruction); parts > 0) {
            auto difference = instruction.virtual_address(1) - instruction.virtual_address(0);
            return difference * instruction.num_addresses() / parts;
        }
    }
    return size;
}

Tigris::TMemReq Tigris::handle_code(const archx::Instruction& instruction)
{
    if (!code_memory_request->can_put() || !memory_model->can_accept_code())
        return nullptr;

    auto page_size = translate_page(instruction, VirtAddr(instruction.program_counter()));
    auto memreq = make<MemReq>(memory_request_id++,
                               instruction.program_counter_pa(),
                               Requester::ICF,
                               READ,
                               4,
                               false,
                               PrefetchType::NONE,
                               BAR_NONE,
                               ATOMIC_NONE,
                               SNOOP_NONE,
                               page_size,
                               0,
                               VirtAddr(instruction.program_counter()),
                               instruction.program_counter());
    TMemReq req(new TigrisMemReq(memreq));
    ProgramCounter pc = instruction.program_counter();
    VirtAddr virt_addr = magic_addr_map->get_virt_addr(pc);
    PhysAddr phys_addr = instruction.program_counter_pa();
    magic_addr_map->add(virt_addr, phys_addr);

    event.code_request(req->getMemReq());
    code_memory_request->put({req, false});
    return req;
}

Tigris::MemoryRequest Tigris::handle_memory_op(const archx::Instruction& instruction,
                                               unsigned index,
                                               bool split_hi_half)
{
    if (!memory_op_request->can_put() || !memory_model->can_accept_data())
        return {nullptr, false};

    auto virt_addr = instruction.virtual_address(index);
    auto phys_addr = instruction.physical_address(index);
    auto page_size = translate_page(instruction, virt_addr);
    auto data_size = memop_size(instruction, index);
    auto size_to_end = VirtAddr::LINE_SIZE - virt_addr.line_offset();
    bool line_split = !phys_addr.same_cache_line(phys_addr + data_size - 1);

    if (split_hi_half) {
        data_size -= size_to_end;
        virt_addr = virt_addr.next_line_address();
        if (phys_addr.next_line_address().same_page(phys_addr, page_size)) {
            phys_addr = phys_addr.next_line_address();
        } else {
            auto tr = translate(instruction, virt_addr);
            phys_addr = tr.result_address();
            page_size = tr.faulted() ? archx::PageSize::Size::PS_4KB : tr.result().second.value();
        }
    } else if (line_split) {
        data_size = size_to_end;
    }

    // update the global hashtable magic tlb with the addrs
    magic_addr_map->add(virt_addr, phys_addr);

    auto memreq = make<MemReq>(memory_request_id++,
                               phys_addr,
                               Requester::LSU,
                               memop_type(instruction),
                               data_size,
                               false,
                               PrefetchType::NONE,
                               barrier_type(instruction.barrier_type()),
                               ATOMIC_NONE,
                               SNOOP_NONE,
                               page_size,
                               0,
                               virt_addr,
                               instruction.program_counter());
    BackendMemReq backend_req = BackendMemReq(memreq);
    TMemReq req(new TigrisMemReq(memreq, backend_req));

    if (!split_hi_half) {
        if (instruction.is_load())
            event.load_execute(req->getMemReq());
        if (instruction.is_store())
            event.store_execute(req->getMemReq());
    }

    event.memory_request(req->getMemReq());
    memory_op_request->put({req, split_hi_half});

    return {req, split_hi_half || !line_split};
}

bool Tigris::supported_memory_op(const archx::Instruction& instruction)
{
    if (!needs_memory_updates() || !needs_sysregs_updates())
        return !instruction.is_cache_maint() && !instruction.is_prefetch();
    return true;
}

void Tigris::warmup_memory(PhysAddr addr, unsigned int size, bool write, bool code)
{
    // TODO required to add warmup
    // if (memory_model->accepts_code() || memory_model->accepts_data())
    //     memory_model->preload(addr, size, write, code);
}

unsigned Tigris::scatter_gather_parts(const archx::Instruction& instruction) const
{
    if (instruction.num_addresses() <= 2)
        return 0;

    auto difference = instruction.virtual_address(1) - instruction.virtual_address(0);
    auto total_size = difference * instruction.num_addresses();
    return (total_size + request_granularity - 1) / request_granularity;
}

bool Tigris::should_execute_memop(const archx::Instruction& instruction, unsigned int index) const
{
    if (!valid_memory_operation(instruction, index))
        return false;

    // skip parts of scatter/gather
    if (auto parts = scatter_gather_parts(instruction); parts > 0)
        return (index % (instruction.num_addresses() / parts)) == 0;

    return true;
}

void Tigris::handle_instruction(const boost::local_shared_ptr<archx::Instruction>& instruction)
{
    if (memory_model->accepts_code()
        && (code_requests.empty()
            || !code_requests.back()->program_counter().same_cache_line(instruction->program_counter())))
        code_requests.emplace_back(instruction);

    if (memory_model->accepts_data()) {
        if (!supported_memory_op(*instruction))
            return;

        for (unsigned i = 0; i < instruction->num_addresses(); ++i)
            if (should_execute_memop(*instruction, i))
                requests.emplace_back(instruction, i, false);
    }
}

void Tigris::end_of_simulation()
{
    if (magic_tlb_dump_at_end) {
        magic_addr_map->dump();
    }
}

// explicit template instantiations
template struct Tigris::CacheLevel<PhysAddr, Prefetcher<MemReq>>;
template struct Tigris::CacheLevel<PhysAddr, core::BackendPrefetcher<BackendMemReq>>;
} // namespace ampere::tigris
