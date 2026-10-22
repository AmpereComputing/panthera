//
// Copyright (c) 2021 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//

#ifndef AMPERE_CORE_CP_ENTANGLING_H
#define AMPERE_CORE_CP_ENTANGLING_H

#include <boost/format.hpp>
#include <exception>
#include <map>
#include "ampere/core/prefetcher.h"
#include "ampere/sc/sc.h"
#include "ampere/sc/submodule.h"

#define L1I_SET 64
#define L1I_WAY 4
// TODO: do this based off of LINE_BYTES somehow?
// VA width is 52 or 53 bits.
#define LINE_ADDR_WIDTH (53 - 6)
#define L1I_SET_BITS 6
#define L1I_PQ_SIZE 64
// Not sure if this is supposed to be 32 or 16
#define L1I_MSHR_SIZE 32

#define L1I_BB_MERGE_ENTRIES 4
// FIXME: Is this wrong? I think we actually have 7 formats defined?
#define L1I_ENTANGLED_NUM_FORMATS 6 // was also called "L1I_MAX_ENTANGLED_PER_LINE"

// Create a mask where the bottom width bits are 1s and the rest are 0s
#define _MASK(width) ((1ull << (width)) - 1ull)
// value % (2**width)
#define _LIMIT_WIDTH(value, width) (((uint64_t)(value)) & ((1ull << ((uint64_t)(width))) - 1ull))

// TODO: change to constants in namespace
// LINE AND MERGE BASIC BLOCK SIZE
#define L1I_MERGE_BBSIZE_BITS 7
#define L1I_MERGE_BBSIZE_MAX_VALUE ((1 << L1I_MERGE_BBSIZE_BITS) - 1)

// TIME AND OVERFLOWS
#define L1I_TIME_DIFF_BITS 20
#define L1I_TIME_DIFF_OVERFLOW (1ull << L1I_TIME_DIFF_BITS)
#define L1I_TIME_DIFF_MASK (L1I_TIME_DIFF_OVERFLOW - 1)

#define L1I_TIME_BITS 12
#define L1I_TIME_OVERFLOW (1ull << L1I_TIME_BITS)
#define L1I_TIME_MASK (L1I_TIME_OVERFLOW - 1)

// TIMING TABLES
#define L1I_TIMING_MSHR_SIZE (L1I_PQ_SIZE + L1I_MSHR_SIZE + 2)
#define L1I_TIMING_MSHR_TAG_MASK ((1ull << LINE_ADDR_WIDTH) - 1)
#define L1I_TIMING_CACHE_TAG_MASK ((1ull << LINE_ADDR_WIDTH) - 1)

#define ENTANGLING_TABLE_SET_BITS 8
#define L1I_ENTANGLED_TABLE_SETS (1 << ENTANGLING_TABLE_SET_BITS)
#define L1I_ENTANGLED_TABLE_WAYS 34
#define L1I_TAG_BITS (42 - ENTANGLING_TABLE_SET_BITS)
#define L1I_TAG_MASK ((1ull << L1I_TAG_BITS) - 1)
#define L1I_CONFIDENCE_COUNTER_BITS 2
#define L1I_CONFIDENCE_COUNTER_MAX_VALUE ((1 << L1I_CONFIDENCE_COUNTER_BITS) - 1)
#define L1I_CONFIDENCE_COUNTER_THRESHOLD 1

#define L1I_TRIES_AVAIL_ENTANGLED 6
#define L1I_TRIES_AVAIL_ENTANGLED_NOT_PRESENT 2

// EXTRA PREFETCH QUEUE
#define L1I_XPQ_ENTRIES 32
#define L1I_XPQ_MASK (L1I_XPQ_ENTRIES - 1)

namespace ampere::core {

using namespace ampere::sc;

struct EntanglingTableEntry
{
    uint64_t tag;                                          // src_line_addr >> ENTANGLING_TABLE_SET_BITS
    uint8_t numTargets;                                    // up to 6 target_line_addrs can be encoded
    uint32_t bb_size;                                      // L1I_MERGE_BBSIZE_BITS bits
    uint64_t encodedTargets[L1I_ENTANGLED_NUM_FORMATS];    // keep just diff
    uint32_t targetConfidences[L1I_ENTANGLED_NUM_FORMATS]; // L1I_CONFIDENCE_COUNTER_BITS bits

    uint64_t get_target_line_addr(uint64_t src_line_addr, uint8_t targetNum);
};

struct EntanglingTable
{
    EntanglingTableEntry table[L1I_ENTANGLED_TABLE_SETS][L1I_ENTANGLED_TABLE_WAYS];
    // Oldest (least recently created) way for each set
    uint32_t oldestWay[L1I_ENTANGLED_TABLE_SETS]; // log2(L1I_ENTANGLED_TABLE_WAYS) * L1I_ENTANGLED_TABLE_SETS bits

    void init();
    uint32_t get_way(uint32_t set, uint64_t src_line_addr);
    EntanglingTableEntry* get_entry(uint64_t src_line_addr);
    void entangle(uint64_t src_line_addr, uint64_t target_line_addr);
    void add_bb_size(uint64_t src_line_addr, uint32_t bb_size);
    uint64_t get_target_line_addr_if_confident(uint64_t src_line_addr, uint8_t targetIndex);
};

struct HistoryBufferEntry
{
    uint64_t line_addr; // Head of the Basic Block
    uint64_t time_diff; // L1I_TIME_DIFF_BITS bits
    uint32_t bb_size;   // L1I_MERGE_BBSIZE_BITS bits
};

class HistoryBuffer
{
public:
    static const uint32_t MAX_ENTRIES =
        1072; // FIXME: they HAVE to mean 1024, right? Also, this is ~64 times larger than it was in the paper... Hm...
    void init(uint64_t current_core_cycle);
    uint64_t find_entry(uint64_t line_addr);
    uint64_t get_bere(uint64_t line_addr, uint64_t latency, uint32_t skip, uint64_t current_core_cycle);
    void add_bb_size(uint64_t src_line_addr, uint32_t bb_size);
    uint32_t find_bb_merge(uint64_t line_addr);
    void add_entry(uint64_t line_addr, uint64_t current_core_cycle);

private:
    HistoryBufferEntry table[MAX_ENTRIES];
    uint64_t head;          // log_2 (MAX_ENTRIES)
    uint64_t headTimestamp; // 64 bits
};

// We do not have access to the MSHR, so we aproximate it using this structure
struct MSHREntry
{
    bool valid;              // 1 bit
    uint64_t tag;            // "L1I_TIMING_MSHR_TAG_BITS bits"=LINE_ADDR_WIDTH bits
    uint64_t bere_line_addr; // 58 bits
    uint64_t timestamp;      // L1I_TIME_BITS bits // time when issued
    bool accessed;           // 1 bit
};

// Add "bere_line_addr" and "accessed" to each L1I$ entry by maintaining a little side cache for that info.
struct L1ICacheEntry
{
    bool valid;              // 1 bit
    uint64_t tag;            // "L1I_TIMING_CACHE_TAG_BITS"=(LINE_ADDR_WIDTH - L1I_SET_BITS) bits
    uint64_t bere_line_addr; // 58 bits // The index into the entangling table? But 58 bits is way too many for that
                             // (only 12 needed)
};

struct PseudoL1ICache
{
    // delme    L1ICacheEntry l1i_timing_cache_table[L1I_SET][L1I_WAY];
    //  Indexed by PAs to match up with Panthera's IL1
    L1ICacheEntry table[L1I_SET][L1I_WAY];
    // Store mappings of PA:VA for lines in the IL1
    std::map<uint64_t, uint64_t> LinePA2LineVA, LineVA2LinePA;

    void init();
    void fill(uint64_t line_addr_pa, uint64_t line_addr_va); // allocate PA:VA mapping
    uint64_t evict(uint64_t line_addr_pa); // deallocate PA:VA mapping, invalidate the entry, and return bere_line_addr
    uint64_t PA2VA(uint64_t line_addr_pa);
    uint64_t VA2PA(uint64_t line_addr_pa);
    L1ICacheEntry* find_free_entry(uint64_t line_addr_pa);
    L1ICacheEntry* find_entry(uint64_t line_addr_pa);
    bool entry_exists(uint64_t line_addr_va);
};

// FIFO for storing basic blocks to prefetch.
struct XPQEntry
{
    uint64_t line_addr; // 58 bits address of the current line of this basic block to prefetch (starts at head of BB
                        // and increments while bb_size decrements)
    uint64_t entangled_addr; // 58 bits address of the line that triggered this prefetch
    uint32_t bb_size;        // L1I_MERGE_BBSIZE_BITS bits number of lines to prefetch
    Fetch parent;
};

struct Debug
{
    uint64_t nTrain, nDrop, nLaunch, nTrainRet, nFill, nDemand, nMiss, nDMiss, nDMHit, nEvict, nATE, nITME, nTrainPref,
        nTrainPrefPre, nRunPref, nRunPrefPre;
    std::vector<uint64_t> fillAddrs, fillPAs, trainAddrs, evictAddrs;
    std::map<uint64_t, uint64_t> outstandingMisses;
    void init();
};
// Maps a number of encoded targets to how many bits of difference can be stored per target.
// Note: there's an extra (6+2)x7 = 56 encoding here that wasn't included in the paper.
const uint32_t L1I_ENTANGLED_FORMATS[8] = {0xDEADBEEF, 58, 28, 18, 13, 10, 8, 6};

uint8_t get_best_format_to_entangle(uint64_t src_line_addr, uint64_t target_line_addr);

class CP_Entangling : public Prefetcher<Fetch>, implementation<CP_Entangling, Prefetcher<Fetch>>
{
public:
    explicit CP_Entangling(sc_core::sc_module_name name);
    bool run() override;
    Debug debug;

protected:
    void train_impl(Fetch req, bool hit, bool prefetch, bool demand) override;
    void fill_impl(Fetch req, bool fb_pref, bool was_alloc_by_pf) override;
    void evict_impl(PhysAddr pa, bool prefetch, bool demand) override;
    void drop_impl(Fetch req, PrefetchDropReason reason) override;
    void launch_impl(Fetch req) override;
    void flush_impl(Flush flush_info) override;

private:
    void table_print(std::ostream& os);
    uint32_t max(uint32_t, uint32_t);

    uint64_t l1i_last_basic_block;
    uint32_t l1i_consecutive_count;
    uint32_t l1i_basic_block_merge_diff;

    uint64_t current_core_cycle;

    MSHREntry l1i_timing_mshr_table[L1I_TIMING_MSHR_SIZE];
    PseudoL1ICache cache;

    EntanglingTable entanglingTable;
    HistoryBuffer histBuf;

    XPQEntry l1i_xpq[L1I_XPQ_ENTRIES];
    uint64_t l1i_xpq_head; // log_2 (L1I_XPQ_ENTRIES)

    // No Side Effects
    uint64_t l1i_find_timing_mshr_entry(uint64_t line_addr);
    bool l1i_ongoing_request(uint64_t line_addr);
    bool l1i_avail_entangled_table(uint64_t line_addr, uint64_t entangled_addr, bool insert_not_present);
    bool l1i_empty_xpq();

    // Side Effects
    Fetch xpq_pop_prefetch_request(uint64_t& entangled_addr);
    void l1i_add_timing_entry(uint64_t line_addr, uint64_t bere_line_addr);
    void l1i_invalid_timing_mshr_entry(uint64_t line_addr);
    bool l1i_invalid_timing_cache_entry(uint64_t line_addr, uint64_t& bere_line_addr);
    void l1i_add_xpq(uint64_t line_addr, uint64_t entangled_addr, uint32_t bb_size, Fetch parent);
};

} // namespace ampere::core
#endif // AMPERE_CORE_CP_ENTANGLING_H

/*
 * vim: sw=4:ts=4:et:si:cindent:cino=\:0,(0,g0,t0:
 */
