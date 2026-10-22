/*
 * Copyright (c) 2025 Ampere Computing. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#pragma once

#include <cstdint>
#include <map>

#include "ampere/mesh/pha.h"
#include "ampere/mesh/sam.h"
#include "ampere/sc/sc.h"
#include "ampere/soc/warmup_cache_interface.h"
#include "ampere/soc/warmup_info.h"

namespace ampere::soc {

class WarmupTxnObj : public sc::Transaction
{
public:
    WarmupTxnObj(mesh::chi::NodeId requester, sc::PhysAddr addr, bool trace = false)
        : requester{requester}, addr{addr}, trace{trace}
    {
    }

    WarmupTxnObj(const WarmupTxnObj&) = default;

    mesh::chi::NodeId requester;
    sc::PhysAddr addr;
    bool trace;
};

using WarmupTxn = sc::transaction_ptr<WarmupTxnObj>;

class Warmup : public sc::Module
{
public:
    explicit Warmup(const sc_core::sc_module_name& name) : sc::Module{name} {}

    void end_of_elaboration() override;
    void warmstart_from_warmstarters();

    void register_warmup(uint64_t num_transactions, std::function<std::optional<WarmupInfo>()> gen_warmup)
    {
        warmstarters.push_back(Warmstarter{num_transactions, gen_warmup});
    }

    sc::Param<sc::PhysAddr> trace_addr{"trace_addr", "trace activities on this address", sc::PhysAddr{0}};
    sc::Param<bool> high_stash_effort{
        "high_stash_effort", "try hard to find a free way in a stash node before victimizing", false};

private:
    struct Warmstarter
    {
        uint64_t num_transactions;
        std::function<std::optional<WarmupInfo>()> gen_warmup;
    };

    std::vector<Warmstarter> warmstarters;

    using SFType = decltype(mesh::PHA::snoop_filter);

    std::map<mesh::chi::NodeId, std::unique_ptr<CacheInterface>> l2_map;
    std::map<mesh::chi::NodeId, std::unique_ptr<CacheInterface>> l3_map;
    std::map<mesh::chi::NodeId, SFType*> sf_map;
    std::map<mesh::chi::NodeId, std::function<mesh::chi::NodeId(mesh::chi::NodeId, sc::PhysAddr)>> choose_stash_map;

    std::map<mesh::chi::NodeId, const mesh::SAM&> sam_map;

    uint64_t cache_lines_filled_l2{0};
    uint64_t cache_lines_filled_l3{0};
    uint64_t cache_lines_victimized_l2{0};
    uint64_t cache_lines_victimized_l3{0};
    uint64_t cache_lines_invalidated_l2{0};
    uint64_t cache_lines_invalidated_l3{0};

    CacheInterface& get_l2(mesh::chi::NodeId nid) { return *l2_map.at(nid); }
    CacheInterface& get_l3(mesh::chi::NodeId nid) { return *l3_map.at(nid); }
    SFType& get_sf(mesh::chi::NodeId nid) { return *sf_map.at(nid); }

    std::function<mesh::chi::NodeId(mesh::chi::NodeId, sc::PhysAddr)> choose_stash_node(mesh::chi::NodeId nid)
    {
        return choose_stash_map.at(nid);
    }

    void find_warmable_objects(sc_core::sc_object& obj);
    void warm(sc::PhysAddr addr, WarmupInfo::Action action, mesh::chi::NodeId requester_node);
    void readunique(WarmupTxn& WarmupTxn,
                    sc::PhysAddr addr,
                    mesh::chi::FinalState state,
                    mesh::chi::NodeId requester_node);
    mesh::chi::FinalState readunique_to_pha(WarmupTxn& WarmupTxn,
                                            mesh::chi::NodeId pha_node,
                                            bool is_proxy,
                                            sc::PhysAddr addr,
                                            mesh::chi::FinalState state,
                                            mesh::chi::NodeId requester_node);
    void clean_sf(WarmupTxn& WarmupTxn, mesh::SnoopFilter& sf, int set, int way);
    void clean_sf_from_pmha(WarmupTxn& WarmupTxn, mesh::SnoopFilter& sf, int set, int way);
    void writeback_to_pha(WarmupTxn& WarmupTxn,
                          mesh::chi::NodeId pha_node,
                          bool is_proxy,
                          sc::PhysAddr addr,
                          mesh::chi::FinalState state,
                          mesh::chi::NodeId requester_node,
                          bool from_l3);
    bool stash(WarmupTxn& WarmupTxn,
               mesh::chi::NodeId stash_node,
               sc::PhysAddr addr,
               mesh::chi::FinalState state,
               bool allow_victim);
    void readshared(WarmupTxn& WarmupTxn,
                    sc::PhysAddr addr,
                    mesh::chi::FinalState state,
                    mesh::chi::NodeId requester_node);
    mesh::chi::FinalState readshared_to_pha(WarmupTxn& WarmupTxn,
                                            mesh::chi::NodeId pha_node,
                                            bool is_proxy,
                                            sc::PhysAddr addr,
                                            mesh::chi::FinalState state,
                                            mesh::chi::NodeId requester_node);
    mesh::chi::FinalState snp_from_pxha(
        WarmupTxn& WarmupTxn, mesh::SnoopFilter& sf, int set, int way, mesh::chi::FinalState fwd_state);
    mesh::chi::FinalState snp_from_pmha(
        WarmupTxn& WarmupTxn, mesh::SnoopFilter& sf, int set, int way, mesh::chi::FinalState fwd_state);
    void cleaninvalid_to_pmha(WarmupTxn& txn,
                              mesh::chi::NodeId pmha_node,
                              mesh::chi::NodeId pxha_node,
                              sc::PhysAddr addr);
};

} // namespace ampere::soc
