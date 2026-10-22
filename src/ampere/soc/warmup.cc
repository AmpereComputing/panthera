/*
 * Copyright (c) 2024-2025 Ampere Computing. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "warmup.h"
#include "ampere/core/core.h"

// #define AMPERE_TRACE_WARMUP
#ifdef AMPERE_TRACE_WARMUP
#define WTRACE(txn, fstr, ...)           \
    if ((txn)->trace) {                  \
        fmt::print((fstr), __VA_ARGS__); \
    }
#else
#define WTRACE(...)
#endif

namespace ampere::soc {

using namespace ampere::mesh;

constexpr chi::FinalState cache_state_to_final_state(MeshInjector::CacheEntry::State cache_state)
{
    switch (cache_state) {
    case MeshInjector::CacheEntry::State::I:
        return chi::FinalState::I;
    case MeshInjector::CacheEntry::State::SC:
        return chi::FinalState::SC;
    case MeshInjector::CacheEntry::State::SD:
        return chi::FinalState::SD;
    case MeshInjector::CacheEntry::State::UC:
        return chi::FinalState::UC;
    case MeshInjector::CacheEntry::State::UD:
        return chi::FinalState::UD;
    }
    return chi::FinalState::I;
}

constexpr MeshInjector::CacheEntry::State final_state_to_cache_state(chi::FinalState final_state)
{
    switch (final_state) {
    case chi::FinalState::I:
        return MeshInjector::CacheEntry::State::I;
    case chi::FinalState::SC:
        return MeshInjector::CacheEntry::State::SC;
    case chi::FinalState::SD:
        return MeshInjector::CacheEntry::State::SD;
    case chi::FinalState::UC:
        return MeshInjector::CacheEntry::State::UC;
    case chi::FinalState::UD:
        return MeshInjector::CacheEntry::State::UD;
    }
    return MeshInjector::CacheEntry::State::I;
}

void Warmup::end_of_elaboration()
{
    for (const auto& top : sc_core::sc_get_top_level_objects()) {
        find_warmable_objects(*top);
    }
}

void Warmup::find_warmable_objects(sc_core::sc_object& obj)
{
    if (auto* inj = dynamic_cast<MeshInjector*>(&obj); inj) {
        l2_map[inj->node_id.val()] =
            std::make_unique<MeshInjectorCacheInterface<decltype(MeshInjector::cache)>>(&inj->cache);
        l3_map[inj->node_id.val()] =
            std::make_unique<MeshInjectorCacheInterface<decltype(MeshInjector::l3c)>>(&inj->l3c);
        sam_map.emplace(inj->node_id.val(), *inj->sam.get());
    }

    if (auto* core = dynamic_cast<core::Core*>(&obj); core) {
        l2_map[core->get_node_id()] = std::make_unique<CoreL2Interface>(&core->l2c.banks, core::CacheType::L2);
        l3_map[core->get_node_id()] = std::make_unique<CoreL2Interface>(&core->l2c.banks, core::CacheType::L3);
        sam_map.emplace(core->get_node_id(), *core->l2c.sam.get());
    }

    if (auto* pha = dynamic_cast<PHA*>(&obj); pha) {
        sf_map[pha->node_id.val()] = &pha->snoop_filter;
        choose_stash_map[pha->node_id.val()] = std::bind_front(&PHA::choose_stash_node, pha);
        sam_map.emplace(pha->node_id.val(), *pha->sam.get());
    }

    for (const auto& child : obj.get_child_objects())
        find_warmable_objects(*child);
}

void Warmup::warmstart_from_warmstarters()
{
    uint64_t threshold = 0;
    uint64_t total = 0;

    // auto inj_1 = dynamic_cast<MeshInjector*>(sc_core::sc_find_object("sys.soc_0.meshinjector_1"));
    // assert(inj_1);

    for (auto& ws : warmstarters) {
        if (ws.num_transactions > threshold)
            threshold = ws.num_transactions;
    }

    while (threshold) {
        for (auto& ws : warmstarters) {
            if (ws.num_transactions == threshold) {
                auto wup = ws.gen_warmup();
                AMPERE_ASSERT(wup, "unexpectedly ran out of warmup transactions");
                AMPERE_ASSERT(wup->from.nodeid_type() == NodeId::NodeId_Type::Global, "expecting Global NodeID");
                warm(wup->address, wup->action, wup->from);
                --ws.num_transactions;
                ++total;
            }
        }

        --threshold;
    }

    if (threshold == 0) {
        for (auto& ws : warmstarters) {
            auto wup = ws.gen_warmup();
            while (wup.has_value()) {
                AMPERE_ASSERT(wup, "unexpectedly ran out of warmup transactions");
                AMPERE_ASSERT(wup->from.nodeid_type() == NodeId::NodeId_Type::Global, "expecting Global NodeID");
                warm(wup->address, wup->action, wup->from);
                wup = ws.gen_warmup();
                total++;
            }
        }
    }

    if (total) {
        AMPERE_INFO("{}: Warmed up with {} transactions from {} warmstarters", name(), total, warmstarters.size());
        AMPERE_INFO("{}: Lines filled: L2={} L3={}, Lines victimized: L2={} L3={}, Lines invalidated: L2={} L3={}",
                    name(),
                    cache_lines_filled_l2,
                    cache_lines_filled_l3,
                    cache_lines_victimized_l2,
                    cache_lines_victimized_l3,
                    cache_lines_invalidated_l2,
                    cache_lines_invalidated_l3);

        AMPERE_ASSERT(cache_lines_filled_l2 >= cache_lines_victimized_l2, "L2 victims exceed fills");
        AMPERE_ASSERT(cache_lines_filled_l3 >= cache_lines_victimized_l3, "L3 victims exceed fills");

        auto net_warmed = cache_lines_filled_l2 + cache_lines_filled_l3
                          - (cache_lines_victimized_l2 + cache_lines_victimized_l3 + cache_lines_invalidated_l2
                             + cache_lines_invalidated_l3);
        AMPERE_INFO("{}: Net warmed memory = {} Bytes", name(), net_warmed * 64);
    }

    // inj_1->dump_l3();
    // exit(1);
}

void Warmup::warm(PhysAddr addr, WarmupInfo::Action action, chi::NodeId requester_node)
{
    auto txn = make<WarmupTxn>(requester_node, addr, trace_addr != 0_pa && addr == trace_addr);

    if (action == WarmupInfo::READ)
        readshared(txn, addr, chi::FinalState::SC, requester_node);
    else if (action == WarmupInfo::READ_UNIQUE)
        readunique(txn, addr, chi::FinalState::UC, requester_node);
    else if (action == WarmupInfo::WRITE)
        readunique(txn, addr, chi::FinalState::UD, requester_node);
}

void Warmup::readunique(WarmupTxn& txn, PhysAddr addr, chi::FinalState state, chi::NodeId requester_node)
{
    auto hit_l2 = get_l2(requester_node).lookup(txn, addr);
    auto hit_l3 = get_l3(requester_node).lookup(txn, addr);

    bool miss = !hit_l2 && !hit_l3;

    WTRACE(txn,
           "Warmup::readunique({}, {}, {}) -> L2={} L3={}\n",
           addr,
           state,
           requester_node,
           hit_l2 ? "hit" : "miss",
           hit_l3 ? "hit" : "miss");

    if (hit_l2 && hit_l2->is_unique())
        return;

    bool hit_but_not_unique = ((!hit_l2 && (hit_l3 && !hit_l3->is_unique())) || (hit_l2 && !hit_l2->is_unique()));

    if (miss || hit_but_not_unique) {
        auto fwd_state =
            readunique_to_pha(txn, sam_map.at(requester_node).lookup(addr), true, addr, state, requester_node);

        AMPERE_ASSERT(fwd_state == chi::FinalState::UC || fwd_state == chi::FinalState::UD,
                      "unexpected fwd_state {} in readunique",
                      fwd_state);

        if (fwd_state == chi::FinalState::UD)
            state = chi::FinalState::UD;
    }

    auto victim = get_l2(requester_node).refill(txn, addr, final_state_to_cache_state(state));
    ++cache_lines_filled_l2;

    if (victim) {
        auto wb_txn =
            make_child<WarmupTxn>(txn, requester_node, victim->addr, trace_addr != 0_pa && victim->addr == trace_addr);
        WTRACE(wb_txn, "Warmup::writeback({}, {}, {})\n", victim->addr, victim->state, requester_node);
        auto victim_state = cache_state_to_final_state(victim->state);
        writeback_to_pha(wb_txn,
                         sam_map.at(requester_node).lookup(victim->addr),
                         true,
                         victim->addr,
                         victim_state,
                         requester_node,
                         false);
        ++cache_lines_victimized_l2;
    }
}

chi::FinalState Warmup::readunique_to_pha(WarmupTxn& txn,
                                          chi::NodeId pha_node,
                                          bool is_proxy,
                                          PhysAddr addr,
                                          chi::FinalState state,
                                          chi::NodeId requester_node)
{
    auto& sf = get_sf(pha_node);
    auto [sf_set, sf_way] = sf.lookup(txn, addr, false);
    bool clean = false;
    bool missed = false;
    bool goto_unique = false;

    if (sf_way == -1) {
        missed = true;

        sf_way = sf.find_empty_way(txn, sf_set);

        if (sf_way == -1) {
            sf_way = sf.find_victim(txn, sf_set);
            clean = true;
        }
    } else {
        goto_unique = true;
    }

    WTRACE(txn,
           "Warmup::readunique_to_p{}ha({}, {}, {}, {}) -> missed={} goto_unique={} clean={}\n",
           is_proxy ? "x" : "m",
           pha_node,
           addr,
           state,
           requester_node,
           missed,
           goto_unique,
           clean);

    assert(sf_way != -1);

    if (clean) {
        if (is_proxy) {
            clean_sf(txn, sf, sf_set, sf_way);
            auto victim_addr = sf.get_address(sf_set, sf_way);
            auto pmha_node = sam_map.at(pha_node).lookup(victim_addr);
            cleaninvalid_to_pmha(txn, pmha_node, pha_node, victim_addr);
        } else {
            clean_sf_from_pmha(txn, sf, sf_set, sf_way);
        }
    }

    if (goto_unique) {
        if (is_proxy)
            state = snp_from_pxha(txn, sf, sf_set, sf_way, chi::FinalState::UC);
        else
            state = snp_from_pmha(txn, sf, sf_set, sf_way, chi::FinalState::UC);
    }

    if (missed || state == chi::FinalState::I) {
        if (is_proxy) {
            auto pmha_node = sam_map.at(pha_node).lookup(addr);
            state = readunique_to_pha(txn, pmha_node, false, addr, state, pha_node);
        } else {
            state = chi::FinalState::UC; // memory always returns UC
        }
    }

    // our requester is now the primary snoop target and the new state is UC
    SnoopStatus snp_status;
    snp_status.set_state(requester_node, state, false);
    if (is_proxy && (state == FinalState::UC || state == FinalState::UD))
        snp_status.set_proxy_owned(true);
    sf.fill(txn, sf_set, sf_way, addr, snp_status, false);

    WTRACE(txn,
           "Warmup::readunique_to_p{}ha({}, {}, {}, {}) -> update SF {}\n",
           is_proxy ? "x" : "m",
           pha_node,
           addr,
           state,
           requester_node,
           snp_status);

    return state;
}

void Warmup::clean_sf(WarmupTxn& parent_txn, SnoopFilter& sf, int set, int way)
{
    auto snp_addr = sf.get_address(set, way);
    auto snp_status = sf.get_data(set, way);

    auto txn = make_child<WarmupTxn>(parent_txn, *parent_txn);
    txn->trace = false;
    if (trace_addr != 0_pa && trace_addr == snp_addr)
        txn->trace = true;

    WTRACE(txn, "Warmup::clean_sf({} set={} way={}) -> initiated by {}\n", snp_addr, set, way, parent_txn->addr);

    auto stvec = snp_status.core_valid_as_set();
    auto pst = snp_status.primary_snoop_target();
    if (pst)
        stvec.insert(*pst);

    for (const auto nid : stvec) {
        auto snp_hit_l2 = get_l2(nid).lookup(txn, snp_addr);
        auto snp_hit_l3 = get_l3(nid).lookup(txn, snp_addr);

        if (snp_hit_l2) {
            get_l2(nid).invalidate(txn, snp_addr);
            ++cache_lines_invalidated_l2;
            WTRACE(txn, "Warmup::clean_sf({}) -> invalidating L2 of {}\n", snp_addr, nid);
        }

        if (snp_hit_l3) {
            get_l3(nid).invalidate(txn, snp_addr);
            ++cache_lines_invalidated_l3;
            WTRACE(txn, "Warmup::clean_sf({}) -> invalidating L3 of {}\n", snp_addr, nid);
        }
    }

    snp_status.reset();
    sf.fill(txn, set, way, snp_addr, snp_status, false);

    WTRACE(txn, "Warmup::clean_sf({}) -> update SF {}\n", snp_addr, snp_status);
}

void Warmup::clean_sf_from_pmha(WarmupTxn& parent_txn, SnoopFilter& sf, int set, int way)
{
    auto snp_addr = sf.get_address(set, way);
    auto snp_status = sf.get_data(set, way);

    auto txn = make_child<WarmupTxn>(parent_txn, *parent_txn);
    txn->trace = false;
    if (trace_addr != 0_pa && trace_addr == snp_addr)
        txn->trace = true;

    WTRACE(txn, "Warmup::clean_sf_from_pmha({}) -> initiated by {}\n", snp_addr, parent_txn->addr);

    auto stvec = snp_status.core_valid_as_set();
    auto pst = snp_status.primary_snoop_target();
    if (pst)
        stvec.insert(*pst);

    for (const auto nid : stvec) {
        auto& pxha_sf = get_sf(nid);
        auto [pxha_sf_set, pxha_sf_way] = pxha_sf.lookup(txn, snp_addr, false);

        if (pxha_sf_way != -1) {
            WTRACE(txn, "Warmup::clean_sf_from_pmha({}) -> cleaning PxHA {}\n", snp_addr, nid);
            clean_sf(txn, pxha_sf, pxha_sf_set, pxha_sf_way);
        }
    }

    snp_status.reset();
    sf.fill(txn, set, way, snp_addr, snp_status, false);

    WTRACE(txn, "Warmup::clean_sf_from_pmha({}) -> update SF {}\n", snp_addr, snp_status);
}

void Warmup::writeback_to_pha(WarmupTxn& txn,
                              chi::NodeId pha_node,
                              bool is_proxy,
                              PhysAddr addr,
                              chi::FinalState state,
                              chi::NodeId requester_node,
                              bool from_l3)
{
    auto& sf = get_sf(pha_node);
    auto [sf_set, sf_way] = sf.lookup(txn, addr, false);

    WTRACE(txn,
           "Warmup::writeback_to_p{}ha({}, {}, {}, {}, from_l3={})\n",
           is_proxy ? "x" : "m",
           pha_node,
           addr,
           state,
           requester_node,
           from_l3);

    AMPERE_ASSERT(sf_way != -1, "writeback_to_pha: expected SF hit for address {}", addr);

    auto snp_status = sf.get_data(sf_set, sf_way);
    snp_status.set_state(requester_node, chi::FinalState::I, false);

    if (!from_l3) {
        if (state != chi::FinalState::SC || snp_status.get_state() == chi::FinalState::I) {
            NodeId starting_stash_nid = NodeId{};
            bool allow_victim = high_stash_effort ? false : true;

            while (true) {
                // stash this line in another core
                auto stash_nid = choose_stash_node(pha_node)(requester_node, addr);

                if (!starting_stash_nid.is_valid()) {
                    starting_stash_nid = stash_nid;
                } else {
                    if (stash_nid == starting_stash_nid)
                        allow_victim = true;
                }

                if (stash(txn, stash_nid, addr, state, allow_victim)) {
                    snp_status.set_state(stash_nid, state, true);
                    break;
                }
            }
        }
    } else if (is_proxy) {
        writeback_to_pha(txn, sam_map.at(pha_node).lookup(addr), false, addr, state, pha_node, from_l3);
    }

    sf.fill(txn, sf_set, sf_way, addr, snp_status, false);

    WTRACE(txn,
           "Warmup::writeback_to_p{}ha({}, {}, {}, {}, from_l3={}) -> update SF {}\n",
           is_proxy ? "x" : "m",
           pha_node,
           addr,
           state,
           requester_node,
           from_l3,
           snp_status);
}

bool Warmup::stash(WarmupTxn& txn, chi::NodeId stash_node, PhysAddr addr, chi::FinalState state, bool allow_victim)
{
    WTRACE(txn, "Warmup::stash({}, {}, {}, allow_victim={})\n", stash_node, addr, state, allow_victim);

    if (!allow_victim) {
        if (get_l3(stash_node).would_fill_without_victim(addr)) {
            get_l3(stash_node).refill(txn, addr, final_state_to_cache_state(state));
            ++cache_lines_filled_l3;
            return true;
        }

        return false;
    } else {
        auto victim = get_l3(stash_node).refill(txn, addr, final_state_to_cache_state(state));
        ++cache_lines_filled_l3;

        if (victim) {
            auto wb_txn =
                make_child<WarmupTxn>(txn, stash_node, victim->addr, trace_addr != 0_pa && victim->addr == trace_addr);
            WTRACE(wb_txn, "Warmup::writeback({}, {}, {}, from_l3=true)\n", victim->addr, victim->state, stash_node);
            auto victim_state = cache_state_to_final_state(victim->state);
            writeback_to_pha(wb_txn,
                             sam_map.at(stash_node).lookup(victim->addr),
                             true,
                             victim->addr,
                             victim_state,
                             stash_node,
                             true);
            ++cache_lines_victimized_l3;
        }

        return true;
    }
}

void Warmup::readshared(WarmupTxn& txn, PhysAddr addr, chi::FinalState state, chi::NodeId requester_node)
{
    auto hit_l2 = get_l2(requester_node).lookup(txn, addr);
    auto hit_l3 = get_l3(requester_node).lookup(txn, addr);

    WTRACE(txn,
           "Warmup::readshared({}, {}, {}) -> L2={} L3={}\n",
           addr,
           state,
           requester_node,
           hit_l2 ? "hit" : "miss",
           hit_l3 ? "hit" : "miss");

    if (hit_l2)
        return;

    if (!hit_l2 && !hit_l3) {
        state = readshared_to_pha(txn, sam_map.at(requester_node).lookup(addr), true, addr, state, requester_node);
    }

    auto victim = get_l2(requester_node).refill(txn, addr, final_state_to_cache_state(state));
    ++cache_lines_filled_l2;

    if (victim) {
        auto wb_txn =
            make_child<WarmupTxn>(txn, requester_node, victim->addr, trace_addr != 0_pa && victim->addr == trace_addr);
        auto victim_state = cache_state_to_final_state(victim->state);
        writeback_to_pha(wb_txn,
                         sam_map.at(requester_node).lookup(victim->addr),
                         true,
                         victim->addr,
                         victim_state,
                         requester_node,
                         false);
        ++cache_lines_victimized_l2;
    }
}

chi::FinalState Warmup::readshared_to_pha(WarmupTxn& txn,
                                          chi::NodeId pha_node,
                                          bool is_proxy,
                                          PhysAddr addr,
                                          chi::FinalState state,
                                          chi::NodeId requester_node)
{
    auto& sf = get_sf(pha_node);
    auto [sf_set, sf_way] = sf.lookup(txn, addr, false);
    bool clean = false;
    bool missed = false;
    bool goto_shared = false;

    if (sf_way == -1) {
        missed = true;

        sf_way = sf.find_empty_way(txn, sf_set);

        if (sf_way == -1) {
            sf_way = sf.find_victim(txn, sf_set);
            clean = true;
        }
    } else {
        state = sf.get_data(sf_set, sf_way).get_state();
        if (state != chi::FinalState::SC) // state will get overwritten below
            goto_shared = true;
    }

    assert(sf_way != -1);

    WTRACE(txn,
           "Warmup::readshared_to_p{}ha {}({}, {}, {}) -> missed={} goto_shared={} clean={}\n",
           is_proxy ? "x" : "m",
           pha_node,
           addr,
           state,
           requester_node,
           missed,
           goto_shared,
           clean);

    if (clean) {
        if (is_proxy) {
            clean_sf(txn, sf, sf_set, sf_way);
            auto victim_addr = sf.get_address(sf_set, sf_way);
            auto pmha_node = sam_map.at(pha_node).lookup(victim_addr);
            cleaninvalid_to_pmha(txn, pmha_node, pha_node, victim_addr);
        } else {
            clean_sf_from_pmha(txn, sf, sf_set, sf_way);
        }
    }

    if (goto_shared) {
        if (is_proxy)
            state = snp_from_pxha(txn, sf, sf_set, sf_way, chi::FinalState::SC);
        else
            state = snp_from_pmha(txn, sf, sf_set, sf_way, chi::FinalState::SC);
    }

    if (missed || state == chi::FinalState::I) {
        if (is_proxy) {
            auto pmha_node = sam_map.at(pha_node).lookup(addr);
            state = readshared_to_pha(txn, pmha_node, false, addr, state, pha_node);
        } else {
            state = chi::FinalState::UC; // memory always returns UC
        }
    }

    auto snp_status = sf.get_data(sf_set, sf_way);
    snp_status.set_state(requester_node, state, false);
    sf.fill(txn, sf_set, sf_way, addr, snp_status, false);

    WTRACE(txn,
           "Warmup::readshared_to_pha({}, is_proxy={}, {}, {}, {}) -> SF update {}\n",
           pha_node,
           is_proxy,
           addr,
           state,
           requester_node,
           snp_status);

    return state;
}

chi::FinalState Warmup::snp_from_pxha(WarmupTxn& txn, SnoopFilter& sf, int set, int way, chi::FinalState fwd_state)
{
    bool hit = false;   // -> true if we hit in any cache
    bool dirty = false; // -> true if we hit dirty state in any cache

    MeshInjector::CacheEntry::State cache_state;
    chi::FinalState sf_state;

    if (fwd_state == chi::FinalState::UC) {
        sf_state = chi::FinalState::I;
        cache_state = MeshInjector::CacheEntry::State::I;
    } else if (fwd_state == chi::FinalState::SC) {
        sf_state = chi::FinalState::SC;
        cache_state = MeshInjector::CacheEntry::State::SC;
    } else {
        throw std::runtime_error(fmt::format("unexpected fwd_state {}", fwd_state));
    }

    auto snp_addr = sf.get_address(set, way);
    auto snp_status = sf.get_data(set, way);

    auto stvec = snp_status.core_valid_as_set();
    auto pst = snp_status.primary_snoop_target();
    if (pst)
        stvec.insert(*pst);

    for (const auto nid : stvec) {
        auto& l2 = get_l2(nid);
        auto& l3 = get_l3(nid);

        auto snp_hit_l2 = l2.lookup(txn, snp_addr);
        auto snp_hit_l3 = l3.lookup(txn, snp_addr);

        WTRACE(txn,
               "Warmup::snp_from_pxha({}) -> {} hit_l2={} hit_l3={}\n",
               snp_addr,
               nid,
               (bool)snp_hit_l2,
               (bool)snp_hit_l3);

        if (snp_hit_l2) {
            hit = true;
            if (snp_hit_l2->is_dirty())
                dirty = true;
            if (cache_state == MeshInjector::CacheEntry::State::I)
                l2.invalidate(txn, snp_addr);
            else
                l2.refill(txn, snp_addr, cache_state);
            snp_status.set_state(nid, sf_state, false);
        }

        if (snp_hit_l3) {
            hit = true;
            if (snp_hit_l2->is_dirty())
                dirty = true;
            if (cache_state == MeshInjector::CacheEntry::State::I)
                l3.invalidate(txn, snp_addr);
            else
                l3.refill(txn, snp_addr, cache_state);
            snp_status.set_state(nid, sf_state, false);
        }
    }

    sf.fill(txn, set, way, snp_addr, snp_status, false);
    WTRACE(txn, "Warmup::snp_from_pxha({}) -> SF update {}\n", snp_addr, snp_status);

    if (hit) {
        if (dirty) {
            if (fwd_state == chi::FinalState::UC)
                fwd_state = chi::FinalState::UD;
            else if (fwd_state == chi::FinalState::SC)
                fwd_state = chi::FinalState::SD;
        }
    } else {
        // we can think we're going to hit, and end up not hitting; indicate
        // that back to the caller with I state
        fwd_state = chi::FinalState::I;
    }

    return fwd_state;
}

chi::FinalState Warmup::snp_from_pmha(WarmupTxn& txn, SnoopFilter& sf, int set, int way, chi::FinalState fwd_state)
{
    bool hit = false;
    bool dirty = false;

    chi::FinalState sf_state;

    if (fwd_state == chi::FinalState::UC)
        sf_state = chi::FinalState::I;
    else if (fwd_state == chi::FinalState::SC)
        sf_state = chi::FinalState::SC;
    else
        throw std::runtime_error(fmt::format("unexpected fwd_state {}", fwd_state));

    auto snp_addr = sf.get_address(set, way);
    auto snp_status = sf.get_data(set, way);

    auto stvec = snp_status.core_valid_as_set();
    auto pst = snp_status.primary_snoop_target();
    if (pst)
        stvec.insert(*pst);

    for (const auto nid : stvec) {
        auto& pxha_sf = get_sf(nid);
        auto [pxha_sf_set, pxha_sf_way] = pxha_sf.lookup(txn, snp_addr, false);

        if (pxha_sf_way != -1) {
            auto state = snp_from_pxha(txn, pxha_sf, pxha_sf_set, pxha_sf_way, fwd_state);

            if (state == chi::FinalState::I) {
                snp_status.set_state(nid, chi::FinalState::I, false);
            } else {
                hit = true;
                snp_status.set_state(nid, sf_state, false);
                if (state == chi::FinalState::UD || state == chi::FinalState::SD)
                    dirty = true;
            }
        }
    }

    sf.fill(txn, set, way, snp_addr, snp_status, false);
    WTRACE(txn, "Warmup::snp_from_pmha({}) -> SF update {}\n", snp_addr, snp_status);

    if (hit) {
        if (dirty) {
            if (fwd_state == chi::FinalState::UC)
                fwd_state = chi::FinalState::UD;
            else if (fwd_state == chi::FinalState::SC)
                fwd_state = chi::FinalState::SD;
        }
    } else {
        // we can think we're going to hit, and end up not hitting; indicate
        // that back to the caller with I state
        fwd_state = chi::FinalState::I;
    }

    return fwd_state;
}

void Warmup::cleaninvalid_to_pmha(WarmupTxn& parent_txn, chi::NodeId pmha_node, chi::NodeId pxha_node, PhysAddr addr)
{
    auto txn = make_child<WarmupTxn>(parent_txn, *parent_txn);
    txn->trace = false;
    if (trace_addr != 0_pa && trace_addr == addr)
        txn->trace = true;

    auto& sf = get_sf(pmha_node);
    auto [sf_set, sf_way] = sf.lookup(txn, addr, false);

    if (sf_way == -1)
        throw std::runtime_error(fmt::format("Warmup::cleaninvalid({}, {}): expected to hit SF", pmha_node, addr));

    auto snp_status = sf.get_data(sf_set, sf_way);
    snp_status.clear_stvec(pxha_node);
    if (snp_status.count() == 0)
        snp_status.set_state(FinalState::I);

    WTRACE(txn,
           "Warmup::cleaninvalid_to_pmha(pmha={}, pxha={}, addr={}) -> Update SF status {}\n",
           pmha_node,
           pxha_node,
           addr,
           snp_status);
    sf.fill(txn, sf_set, sf_way, addr, snp_status, false);
}

} // namespace ampere::soc
