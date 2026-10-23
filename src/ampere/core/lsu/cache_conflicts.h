//
// Copyright (c) 2018-2023 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//

#pragma once

#include <tuple>
#include <vector>
#include <utility>
#include <map>
#include <optional>

#include "ampere/archx/addr.h"
#include "ampere/sc/event.h"
#include "ampere/sc/sc.h"
#include "ampere/core/uop.h"
#include "ampere/core/lsu/ofb.h"
#include "ampere/core/lsu/cache.h"

using ampere::archx::VirtAddr;
using ampere::sc::Event;
using ampere::sc::Param;

namespace ampere::core {

using MemoryRange = AddrRange<VirtAddr>;

struct L1DFill
{
    Uop uop;
    VirtAddr addr;
    uint8_t pipe_mask{0};
    bool executes{false};
    bool completes{false};
    bool valid{false};

    inline static L1DFill from_store(const OFBEntry& ofb_entry, Uop uop, bool writeback, bool valid = true)
    {
        return {uop, ofb_entry.virt_addr, ofb_entry.pipe_mask(), writeback, writeback, valid};
    }
    inline static L1DFill from_load(const OFBEntry& ofb_entry, Uop uop, bool executes, bool valid = true)
    {
        return {uop,
                ofb_entry.virt_addr,
                ofb_entry.pipe_mask(),
                executes || ofb_entry.completes_uop(),
                ofb_entry.completes_uop(),
                valid};
    }
    inline static L1DFill from_atomic_load(const Uop& uop, uint8_t pipe_mask, bool valid = true)
    {
        return {uop, *uop->virt_addr, pipe_mask, true, true, valid};
    }
};

struct L2RefillArbitration
{
    uint8_t pipes{0};
    uint8_t cache_ports{0};
    std::vector<std::tuple<bool, bool, Uop>> uops;
    std::vector<MemoryRange> addresses;

    L2RefillArbitration() = default;
    void add(const L1DFill& fill)
    {
        uops.emplace_back(fill.valid, fill.completes, fill.uop);
        if (fill.valid) // speculative fill wakes don't cause tag conflicts
            addresses.emplace_back(fill.addr, VirtAddr::LINE_SIZE);
    }

    void add_pipe(const L1DFill& fill)
    {
        AMPERE_ASSERT(!(pipes & fill.pipe_mask), "Uop already refilling on pipe {}", fill.pipe_mask);
        pipes |= fill.pipe_mask;
        uops.emplace_back(fill.valid, fill.completes, fill.uop);
        if (fill.valid) // speculative fill wakes don't cause tag conflicts
            addresses.emplace_back(fill.addr, VirtAddr::LINE_SIZE);
    }

    void add_execution_only(const L1DFill& fill)
    {
        if (fill.executes)
            add_pipe(fill);
    }

    void add_cache_port(const L1DFill& fill, std::optional<unsigned> port = {})
    {
        if (port)
            cache_ports |= 1U << *port;
        add(fill);
    }

    explicit operator bool() const { return !uops.empty(); }
    size_t executed() const { return uops.size(); }
    bool guaranteed() const { return !addresses.empty(); }

    bool using_pipe(uint8_t pipe_mask) const { return pipes & pipe_mask; }
};

// L1D is composed of 8, 8-byte chunks/macros supporting 1 read and 1 write.
// Two reads to same chunk causes a conflict. Each macro is composed of 4 banks
// supporting 1 read or 1 write.  A read and a write to the same bank causes a
// conflict.
class LSUCacheConflicts
{
    static inline constexpr unsigned MAX_ACCESS_SIZE = 16;
    static inline constexpr unsigned BLOCK_SIZE = 16;
    static inline constexpr uint64_t BLOCK_MASK = (1ULL << BLOCK_SIZE) - 1;

    using Access = MemoryRange;

    struct PortAccess : Access
    {
        unsigned port{0};

        PortAccess() = default;
        PortAccess(VirtAddr addr, unsigned size, unsigned port) : Access(addr, size), port(port) {}
    };

    L1DCache& cache;
    std::vector<PortAccess> load_addresses;
    std::multimap<unsigned, Access> load_accesses;
    std::vector<Access> previous_load_accesses;
    std::vector<Access> previous_load_bank_accesses;
    std::vector<Access> store_retirement_addresses;
    std::set<unsigned> evictions;
    bool older_load_write_conflict{false};
    L2RefillArbitration l2c_refill_tag_conflict;
    uint64_t set_bank_mask;

    Param<unsigned> set_banks{"set_banks", "Number of sets which share a set-bank in the cache", 2};
    Param<std::optional<uint64_t>> set_bank_mask_param{
        "set_bank_mask", "Set-index mask determining set-bank, defaults to first bit(s)", {}};

    Param<unsigned> banks{"banks", "Number of banks for reading the cache", 8};
    Param<unsigned> bank_size{"bank_size", "Number of bytes in each bank", 2};

    Param<bool> enable_read_read_chunk_conflicts{
        "enable_read_read_chunk_conflicts", "loads to the same VA bits 5-3 conflict with 8B chunk", true};
    Param<bool> enable_write_read_chunk_bank_conflicts{"enable_write_read_chunk_bank_conflicts",
                                                       "loads to store-rets and the same VA bits 7-3 conflict w/ "
                                                       "chunk & macro-bank",
                                                       true};
    Param<bool> enable_fill_read_chunk_bank_conflicts{"enable_fill_read_chunk_bank_conflicts",
                                                      "loads to fill and the same VA bits 7-3 conflict w/ "
                                                      "chunk & macro-bank",
                                                      false};
    Param<bool> enable_refill_tag_conflict{
        "enable_refill_tag_conflict", "Fills conflict with reads in tag lookup", true};
    Param<bool> spec_fills_cause_conflicts{
        "spec_fills_cause_conflicts",
        "Speculative fills (L2 reads which miss L2) conflict with loads like a real fill",
        true};

    Param<bool> threecyc_conflict{"threecyc_conflict", "load/load and load/store conflicts for 3-cycle DL1", false};

    Param<bool> duplicate_loads_allowed{
        "duplicate_loads_allowed", "Two loads do not have a data chunk conflict if they are to the same line", false};
    Param<bool> duplicate_exact_loads_allowed{
        "duplicate_exact_loads_allowed",
        "Two loads do not have a data chunk conflict if they are to the same bytes",
        false};
    Param<bool> back_to_back_read_no_conflicts_other{
        "back_to_back_read_no_conflicts_other",
        "A load which reads of the same data as the previous read will not be seen as a conflict by other loads",
        false};
    Param<bool> back_to_back_read_no_conflicts_self{
        "back_to_back_read_no_conflicts_self",
        "A load which reads the same data as the previous read will not conflict with anything",
        false};
    Param<bool> back_to_back_any_way{"back_to_back_any_way", "Back-to-back can be any way", false};
    Param<bool> back_to_back_any_way_bank{
        "back_to_back_any_way_bank", "Back-to-back can be any way within required banks", false};

    Param<OneOf<unsigned, 0, BLOCK_SIZE, VirtAddr::LINE_SIZE>> combine_within{
        "combine_within", "Combine within byte granularity (0 is anything)", 0};

    Param<std::optional<unsigned>> eviction_port{"eviction_port", "Port used for evictions", {}};
    Param<bool> evictions_cause_recycle{"evictions_cause_recycle", "Evictions will cause recycles", false};
    Param<bool> enable_fill_evict_conflicts{
        "enable_fill_evict_conflicts", "Fills can cause conflicts with evictions", true};

    struct
    {
        Event load_cache_read{"load_cache_read", "Load read cache"};
        Event read_and_write_conflict{
            "read_and_write_conflict",
            "Older load had a write conflict and younger load had a read conflict in the same cycle"};
        Event back_to_back_read{"back_to_back_read", "Back-to-back read of same bytes"};
        EventData<unsigned> banks_used{"banks_used", "Banks read"};
    } event;

    bool same_set_any_way(VirtAddr set_addr, VirtAddr way_addr, PhysAddr way_phys_addr) const;
    static bool same_byte_in_block(uint64_t line_bytes);

public:
    LSUCacheConflicts(L1DCache& cache, size_t load_pipes);

    struct BackToBack
    {
        bool complete{false};
        unsigned banks_used{0};
    };

    struct Conflicts
    {
        bool read{false};
        bool write{false};
        bool fill{false};
        bool eviction{false};
        BackToBack back_to_back{};

        bool any() const { return read || write || fill || eviction; }
        bool allowed_to_lookup(bool read_conflicts, bool write_conflicts) const
        {
            return ((!read && !eviction) || !read_conflicts) && ((!write && !fill) || !write_conflicts);
        }
        void reset_conflicts() { read = write = fill = eviction = false; }
    };

    void reset();

    bool same_chunk_check(VirtAddr va1, unsigned size1, VirtAddr va2, unsigned size2) const;
    bool same_chunk_bank_check(VirtAddr va1, unsigned size1, VirtAddr va2, unsigned size2) const;
    bool same_set_bank_check(VirtAddr va1, VirtAddr va2) const;
    bool same_byte_in_set_bank_check(VirtAddr va1, unsigned size1, VirtAddr va2, unsigned size2) const;

    unsigned offset_to_bank(VirtAddr addr) const;
    unsigned banks_to_read(VirtAddr addr, unsigned size) const;
    unsigned banks_to_read_aligned(VirtAddr addr, unsigned size) const;
    uint64_t bank_offset(VirtAddr addr, unsigned bank) const;

    bool is_read_read_chunk_conflict(VirtAddr virt_addr,
                                     unsigned size,
                                     unsigned dl1_port,
                                     const std::vector<PortAccess>& inflights) const;
    bool is_write_read_chunk_bank_conflict(VirtAddr virt_addr,
                                           unsigned size,
                                           unsigned dl1_port,
                                           const std::vector<Access>& inflights) const;

    bool can_load_combine(unsigned dl1_port, const Access& access);
    void load_cache_read(
        const ConstUop& uop, VirtAddr virt_addr, unsigned datasize, unsigned dl1_port, const Conflicts& conflicts);
    BackToBack is_back_to_back_cache_access(VirtAddr virt_addr,
                                            PhysAddr phys_addr,
                                            unsigned datasize,
                                            unsigned dl1_port) const;
    Conflicts load_cache_conflicts(
        const ConstUop& uop, VirtAddr virt_addr, PhysAddr phys_addr, unsigned datasize, unsigned dl1_port);

    void assign_refill_tag_conflicts() { l2c_refill_tag_conflict = {}; }
    void assign_refill_tag_conflicts(L2RefillArbitration refills) { l2c_refill_tag_conflict = refills; }

    bool refill_conflicts() const { return enable_refill_tag_conflict || enable_fill_read_chunk_bank_conflicts; }
    bool refill_conflicts_on_pipe(unsigned pipe) const { return pipe != 0 || !enable_fill_read_chunk_bank_conflicts; }
    bool refills_participate_in_conflicts(const L2RefillArbitration& refills) const;

    bool load_refill_conflict_check() const;
    bool load_port1_refill_conflict(unsigned dl1_port) const;

    bool evictions_cause_conflicts() const { return static_cast<bool>(eviction_port); }
    std::optional<unsigned> evictions_take_port() const
    {
        if (evictions_cause_recycle)
            return {};
        return eviction_port.val();
    }
    bool try_eviction(VirtAddr virt_set_addr);

    void store_retired(VirtAddr virt_addr, unsigned datasize);
    void retired_store_wrote_cache(VirtAddr virt_addr, unsigned datasize);
};

} // namespace ampere::core

/*
 * vim: sw=4:ts=4:et:si:cindent:cino=\:0,(0,g0,t0:
 */
