//
// Copyright (c) 2021 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//

#include "ampere/core/prefetchers/cp_entangling.h"
#include "ampere/archx/addr.h"

namespace ampere::core {

void Debug::init()
{
    nLaunch = 0;
    nTrain = 0;
    nFill = 0;
    nDrop = 0;
    nDemand = 0;
    nMiss = 0;
    nDMiss = 0;
    nDMHit = 0;
    nEvict = 0;
    nATE = 0;
    nITME = 0;
    nTrainPref = 0;
    nRunPref = 0;
    nTrainPrefPre = 0;
    nRunPrefPre = 0;
}

void PseudoL1ICache::init()
{
    for (auto& i : table)
        for (auto& j : i)
            j.valid = false;
}

void PseudoL1ICache::fill(uint64_t line_addr_pa, uint64_t line_addr_va)
{
    LinePA2LineVA[line_addr_pa] = line_addr_va;
    LineVA2LinePA[line_addr_va] = line_addr_pa;
}

uint64_t PseudoL1ICache::PA2VA(uint64_t line_addr_pa)
{
    if (LinePA2LineVA.contains(line_addr_pa)) {
        return LinePA2LineVA[line_addr_pa];
    } else {
        throw std::runtime_error((boost::format("lost track of physical line 0x%1$x .") % line_addr_pa).str());
    }
}

uint64_t PseudoL1ICache::VA2PA(uint64_t line_addr_va)
{
    if (LineVA2LinePA.contains(line_addr_va)) {
        return LineVA2LinePA[line_addr_va];
    } else {
        throw std::runtime_error((boost::format("lost track of virtual line 0x%1$x .") % line_addr_va).str());
    }
}

uint64_t PseudoL1ICache::evict(uint64_t line_addr_pa)
{
    L1ICacheEntry* entry = find_entry(line_addr_pa);
    assert(entry != NULL);
    entry->valid = false;
    LineVA2LinePA.erase(PA2VA(line_addr_pa));
    LinePA2LineVA.erase(line_addr_pa);
    return entry->bere_line_addr;
}

L1ICacheEntry* PseudoL1ICache::find_free_entry(uint64_t line_addr_pa)
{
    uint32_t set = line_addr_pa % L1I_SET;
    for (uint32_t way = 0; way < L1I_WAY; way++) {
        if (!table[set][way].valid) {
            return &table[set][way];
        }
    }
    return nullptr;
}

L1ICacheEntry* PseudoL1ICache::find_entry(uint64_t line_addr_pa)
{
    uint64_t i = line_addr_pa % L1I_SET;
    for (uint32_t j = 0; j < L1I_WAY; j++) {
        if (table[i][j].tag == ((line_addr_pa >> L1I_SET_BITS) & L1I_TIMING_CACHE_TAG_MASK) && table[i][j].valid) {
            return &table[i][j];
        }
    }
    return nullptr;
}

bool PseudoL1ICache::entry_exists(uint64_t line_addr_va)
{
    if (LineVA2LinePA.contains(line_addr_va)) {
        return find_entry(VA2PA(line_addr_va)) != nullptr;
    } else {
        return false;
    }
}

void HistoryBuffer::init(uint64_t current_core_cycle)
{
    head = 0; // current hist table index
    headTimestamp = current_core_cycle;
    for (uint32_t i = 0; i < this->MAX_ENTRIES; i++) {
        table[i].line_addr = 0;
        table[i].time_diff = 0;
        table[i].bb_size = 0;
    }
}

// Find index of hbuf entry whose BB head is line_addr. Returns HistoryBuffer::MAX_ENTRIES if not found.
uint64_t HistoryBuffer::find_entry(uint64_t line_addr)
{
    uint32_t i = (head - 1) % HistoryBuffer::MAX_ENTRIES;
    while (true) {
        if (table[i].line_addr == line_addr)
            return i;
        if (i == head)
            return MAX_ENTRIES; // tag not in hist table
        i = (i - 1) % MAX_ENTRIES;
    }
}

// Increases the bb_size of src_line_addr's entry to bb_size.
void HistoryBuffer::add_bb_size(uint64_t src_line_addr, uint32_t bb_size)
{
    uint64_t index = this->find_entry(src_line_addr);
    assert(index < MAX_ENTRIES);
    table[index].bb_size = bb_size & L1I_MERGE_BBSIZE_MAX_VALUE; // = bb_size % 128
}

// "It can have duplicated entries if the line was evicted in between"
void HistoryBuffer::add_entry(uint64_t line_addr, uint64_t current_core_cycle)
{
    // Insert empty addresses in hist not to have timediff overflows
    while (current_core_cycle - headTimestamp >= L1I_TIME_DIFF_OVERFLOW) {
        table[head].line_addr = 0;
        table[head].time_diff = L1I_TIME_DIFF_MASK;
        table[head].bb_size = 0;
        head = (head + 1) % this->MAX_ENTRIES;
        headTimestamp += L1I_TIME_DIFF_MASK;
    }

    // Allocate a new entry (evict old one if necessary)
    table[head].line_addr = line_addr;
    table[head].time_diff = (current_core_cycle - headTimestamp) & L1I_TIME_DIFF_MASK;
    table[head].bb_size = 0;
    head = (head + 1) % this->MAX_ENTRIES;
    headTimestamp = current_core_cycle;
}

// TODO: Inline this? (only 1 call)
uint32_t HistoryBuffer::find_bb_merge(uint64_t line_addr)
{
    // assert(L1I_BB_MERGE_ENTRIES < MAX_ENTRIES)
    for (uint32_t count = 0; count < L1I_BB_MERGE_ENTRIES; count++) {
        uint32_t i = (head - 1 - count) % this->MAX_ENTRIES;
        if (line_addr > table[i].line_addr && (line_addr - table[i].line_addr) <= table[i].bb_size) {
            return line_addr - table[i].line_addr;
        }
    }
    return 0;
}

// return BeRe (best request -- entangled address)
uint64_t HistoryBuffer::get_bere(uint64_t line_addr, uint64_t latency, uint32_t skip, uint64_t current_core_cycle)
{
    assert(line_addr); // the first 4k page of VAs is never mapped
    uint64_t time_i = headTimestamp;
    uint64_t req_time = 0;
    uint32_t num_skipped = 0;
    for (uint32_t count = 0; count < this->MAX_ENTRIES; count++) {
        uint32_t i = (head - 1 - count) % this->MAX_ENTRIES;
        // Against the time overflow
        if (req_time == 0 && table[i].line_addr == line_addr
            && time_i + latency >= current_core_cycle) { // Its me (miss or late prefetcher)
            req_time = time_i;
        } else if (req_time) { // Not me (check only older than me)
            if (table[i].line_addr == line_addr) {
                return 0; // Second time it appeared (it was evicted in between) or many for the same set. No entangle
            }
            if (time_i + latency <= req_time && table[i].line_addr) {
                if (skip == num_skipped) {
                    return table[i].line_addr;
                } else {
                    num_skipped++;
                }
            }
        }
        time_i -= table[i].time_diff;
    }
    return 0;
}

// Decode a target_line_addr from an entangling entry given the src_line_addr and which target to decode
uint64_t EntanglingTableEntry::get_target_line_addr(uint64_t src_line_addr, uint8_t targetIndex)
{
    assert(numTargets > 0);
    return (src_line_addr & ~_MASK(L1I_ENTANGLED_FORMATS[numTargets]))
           | (encodedTargets[targetIndex] & _MASK(L1I_ENTANGLED_FORMATS[numTargets]));
}

// Returns the format that can encode the most targets while still encoding the mapping from src_line_addr to
// target_line_addr
uint8_t get_best_format_to_entangle(uint64_t src_line_addr, uint64_t target_line_addr)
{
    for (uint8_t i = L1I_ENTANGLED_NUM_FORMATS; i != 0; i--) {
        if ((src_line_addr >> L1I_ENTANGLED_FORMATS[i]) == (target_line_addr >> L1I_ENTANGLED_FORMATS[i])) {
            return i;
        }
    }
    assert(false);
    return 0;
}

void EntanglingTable::init()
{
    for (uint32_t i = 0; i < L1I_ENTANGLED_TABLE_SETS; i++) {
        for (uint32_t j = 0; j < L1I_ENTANGLED_TABLE_WAYS; j++) {
            table[i][j].tag = 0;
            table[i][j].numTargets = 1; // FIXME: why not 0?
            for (uint32_t k = 0; k < L1I_ENTANGLED_NUM_FORMATS; k++) {
                table[i][j].encodedTargets[k] = 0;
                table[i][j].targetConfidences[k] = 0;
            }
            table[i][j].bb_size = 0;
        }
        oldestWay[i] = 0;
    }
}

// Returns the target_line_addr at targetIndex in the targets for src_line_addr in the entangling table if the entry
// exists and the confidence is high enough, otherwise return 0.
uint64_t EntanglingTable::get_target_line_addr_if_confident(uint64_t src_line_addr, uint8_t targetIndex)
{
    EntanglingTableEntry* entry = this->get_entry(src_line_addr);
    if (entry != nullptr) {
        if (entry->targetConfidences[targetIndex] >= L1I_CONFIDENCE_COUNTER_THRESHOLD) {
            return entry->get_target_line_addr(src_line_addr, targetIndex);
        }
    }
    return 0;
}

// Increases the bb_size of src_line_addr's entry to bb_size. Creates an entry for src_line_addr if none exists.
void EntanglingTable::add_bb_size(uint64_t src_line_addr, uint32_t bb_size)
{
    uint32_t set = src_line_addr % L1I_ENTANGLED_TABLE_SETS;
    uint32_t way = this->get_way(set, src_line_addr);
    EntanglingTableEntry* entry;
    if (way == L1I_ENTANGLED_TABLE_WAYS) {
        way = oldestWay[set];
        oldestWay[set] = (oldestWay[set] + 1) % L1I_ENTANGLED_TABLE_WAYS;

        entry = &(table[set][way]);
        entry->tag = src_line_addr >> ENTANGLING_TABLE_SET_BITS;
        entry->numTargets = 1;
        for (uint32_t k = 0; k < L1I_ENTANGLED_NUM_FORMATS; k++) {
            entry->encodedTargets[k] = 0;
            entry->targetConfidences[k] = 0;
        }
        entry->bb_size = 0;
    } else {
        entry = &(table[set][way]);
    }
    if (bb_size > entry->bb_size) {
        entry->bb_size = bb_size & L1I_MERGE_BBSIZE_MAX_VALUE;
    }
}

EntanglingTableEntry* EntanglingTable::get_entry(uint64_t src_line_addr)
{
    uint32_t set = src_line_addr % L1I_ENTANGLED_TABLE_SETS;
    uint32_t way = this->get_way(set, src_line_addr);
    if (way < L1I_ENTANGLED_TABLE_WAYS) {
        return &(table[set][way]);
    } else {
        return nullptr;
    }
}

uint32_t EntanglingTable::get_way(uint32_t set, uint64_t src_line_addr)
{
    uint64_t tag = src_line_addr >> ENTANGLING_TABLE_SET_BITS;
    for (int i = 0; i < L1I_ENTANGLED_TABLE_WAYS; i++) {
        if (table[set][i].tag == tag) {
            return i;
        }
    }
    return L1I_ENTANGLED_TABLE_WAYS;
}

bool CP_Entangling::l1i_avail_entangled_table(uint64_t src_line_addr,
                                              uint64_t target_line_addr,
                                              bool insert_not_present)
{
    EntanglingTableEntry* entry = entanglingTable.get_entry(src_line_addr);
    if (entry == nullptr)
        return insert_not_present;
    for (uint32_t k = 0; k < L1I_ENTANGLED_NUM_FORMATS; k++) {
        if (entry->targetConfidences[k] >= L1I_CONFIDENCE_COUNTER_THRESHOLD
            && entry->get_target_line_addr(src_line_addr, k) == target_line_addr)
        {
            return true;
        }
    }
    // Check for availability
    uint32_t min_format = get_best_format_to_entangle(src_line_addr, target_line_addr);
    uint32_t num_valid = 1;
    for (uint32_t k = 0; k < L1I_ENTANGLED_NUM_FORMATS; k++) {
        if (entry->targetConfidences[k] >= L1I_CONFIDENCE_COUNTER_THRESHOLD) {
            num_valid++;
            uint32_t format_k =
                get_best_format_to_entangle(src_line_addr, entry->get_target_line_addr(src_line_addr, k));
            if (format_k < min_format) {
                min_format = format_k;
            }
        }
    }
    if (num_valid > min_format) { // Eviction is necessary
        return false;
    } else {
        return true;
    }
}

// TODO: change function style back?
CP_Entangling::CP_Entangling(sc_core::sc_module_name name) : Prefetcher<Fetch>(name)
{
    debug.init();
    // The current basic block to be added to the history buffer when interrupted
    l1i_last_basic_block = 0;
    l1i_consecutive_count = 0;

    l1i_basic_block_merge_diff = 0; // ?

    histBuf.init(current_core_cycle);

    // l1i_init_timing_tables();
    for (auto& e : l1i_timing_mshr_table) {
        e.valid = false;
    }

    cache.init();
    entanglingTable.init();

    // l1i_init_xpq();
    l1i_xpq_head = 0;
    for (auto& e : l1i_xpq) {
        e.line_addr = 0;
        e.entangled_addr = 0;
        e.bb_size = 0;
    }
}

void CP_Entangling::table_print(std::ostream& os) {}

// when the machine is handling a flush, swe need to clear out all
// outstanding requests from the timing_mshr_entry and debug table
void CP_Entangling::flush_impl(Flush flush_info)
{
    for (auto line_addr_entry : debug.outstandingMisses) {
        l1i_invalid_timing_mshr_entry(line_addr_entry.first);
    }
    debug.outstandingMisses.clear();
}

void CP_Entangling::evict_impl(PhysAddr physAddr, bool prefetch, bool demand)
{
    debug.nEvict++;
    uint64_t physLine = physAddr.line_number();
    uint64_t evicted_line_addr = cache.PA2VA(physLine);

    uint64_t src_line_addr = cache.evict(physLine);
    if (prefetch) {
        bool accessed = demand; // whether the line being evicted was demanded at any point before eviction
        // 1. Find the entry in the entangling table that causes a prefetch of evicted_line_addr upon seeing
        // src_line_addr.
        // 2. if (accessed) { increase confidence } else { decrease confidence } of the entry
        { // l1i_update_confidence_entangled_table(src_line_addr, evicted_line_addr, accessed);
            EntanglingTableEntry* entry = entanglingTable.get_entry(src_line_addr);
            if (entry != nullptr) {
                for (uint32_t k = 0; k < L1I_ENTANGLED_NUM_FORMATS; k++) {
                    if (entry->targetConfidences[k] >= L1I_CONFIDENCE_COUNTER_THRESHOLD
                        && entry->get_target_line_addr(src_line_addr, k) == evicted_line_addr)
                    {
                        if (accessed && entry->targetConfidences[k] < L1I_CONFIDENCE_COUNTER_MAX_VALUE) {
                            entry->targetConfidences[k]++;
                        }
                        if (!accessed && entry->targetConfidences[k] > 0) {
                            entry->targetConfidences[k]--;
                        }
                    }
                }
            }
        }
    }
}

void CP_Entangling::drop_impl(Fetch req, PrefetchDropReason reason)
{
    debug.nDrop++;
    // uint64_t line_addr = req->cache_line().value();
    // l1i_invalid_timing_mshr_entry(line_addr);
    // debug.outstandingMisses.erase(line_addr);
}

void CP_Entangling::launch_impl(Fetch req)
{
    debug.nLaunch++;
    // uint64_t line_addr = req->cache_line().value();
}

void CP_Entangling::fill_impl(Fetch req, bool fb_pref, bool was_alloc_by_pf)
{
    if (!req->phys_addr) {
        throw std::runtime_error((boost::format("No PhysAddr provided for Fetch id %1%.") % req->fetch_id).str());
    }
    debug.nFill++;
    uint64_t line_addr = req->cache_line().value();
    uint64_t line_addr_pa = req->phys_addr->line_number();
    debug.fillAddrs.push_back(line_addr);
    debug.fillPAs.push_back(line_addr_pa);
    cache.fill(line_addr_pa, line_addr);
    assert(debug.outstandingMisses.count(line_addr));
    debug.outstandingMisses.erase(line_addr);
    // Used to check evictions here

    uint64_t latency; // = l1i_get_latency_timing_mshr(line_addr);
    {
        uint32_t index = l1i_find_timing_mshr_entry(line_addr);
        if (index == L1I_TIMING_MSHR_SIZE || !l1i_timing_mshr_table[index].accessed) {
            latency = 0;
        } else {
            latency = (current_core_cycle - l1i_timing_mshr_table[index].timestamp)
                      & L1I_TIME_MASK; // (current_cycle - timestamp) % (2**L1I_TIME_BITS);
        }
    }

    // l1i_timing_cache_table has nothing to do with timing. It's the map of lines in the cache to accessed bit and
    // entangled source. Take the MSHR entry and move it to said map, unless it was a demand request that wasn't in the
    // MSHR, then just add an entry in that map to note that it was accessed. Actually, this whole function has nothing
    // to do with timing.
    { // l1i_move_timing_entry(line_addr);
        L1ICacheEntry* entry = cache.find_free_entry(line_addr_pa);
        assert(entry != NULL); // It must have a free entry
        // end l1i_get_invalid_timing_cache_entry(line_addr);
        entry->valid = true;
        entry->tag = (line_addr >> L1I_SET_BITS) & L1I_TIMING_CACHE_TAG_MASK;

        uint32_t index_mshr = l1i_find_timing_mshr_entry(line_addr);
        if (index_mshr != L1I_TIMING_MSHR_SIZE) {
            entry->bere_line_addr = l1i_timing_mshr_table[index_mshr].bere_line_addr;
            l1i_invalid_timing_mshr_entry(line_addr);
        }
    }

    // Get and update entangled
    if (latency) {
        bool inserted = false;
        for (uint32_t i = 0; i < L1I_TRIES_AVAIL_ENTANGLED; i++) {
            uint64_t bere = histBuf.get_bere(line_addr, latency, i, current_core_cycle);
            if (bere && line_addr != bere) {
                if (l1i_avail_entangled_table(bere, line_addr, false)) {
                    entanglingTable.entangle(bere, line_addr);
                    inserted = true;
                    break;
                }
            }
        }
        if (!inserted) {
            for (uint32_t i = 0; i < L1I_TRIES_AVAIL_ENTANGLED_NOT_PRESENT; i++) {
                uint64_t bere = histBuf.get_bere(line_addr, latency, i, current_core_cycle);
                if (bere && line_addr != bere) {
                    if (l1i_avail_entangled_table(bere, line_addr, true)) {
                        entanglingTable.entangle(bere, line_addr);
                        inserted = true;
                        break;
                    }
                }
            }
        }
        if (!inserted) {
            uint64_t bere = histBuf.get_bere(line_addr, latency, 0, current_core_cycle);
            if (bere && line_addr != bere) {
                entanglingTable.entangle(bere, line_addr);
            }
        }
    }
}

bool CP_Entangling::run()
{ // FIXME: we can't check if the PQ is full. Handle overflow?
    // Do prefetches
    while (/*L1I.PQ.is_not_full() &&*/ prefetch_request->can_put() && !l1i_empty_xpq()) { // no side effects
        uint64_t entangled_addr = 0;
        Fetch pf_target = xpq_pop_prefetch_request(entangled_addr); // SIDE EFFECTS!
        debug.nRunPrefPre++;
        if (!l1i_ongoing_request(pf_target->cache_line().value())) { // no side effects
            debug.nRunPref++;
            prefetch_request->put(pf_target);
            l1i_add_timing_entry(pf_target->cache_line().value(), entangled_addr); // SIDE EFFECTS!
        }
    }
    return true; // run next cycle
}

uint32_t CP_Entangling::max(uint32_t a, uint32_t b)
{
    if (a > b)
        return a;
    else
        return b;
}

void CP_Entangling::train_impl(Fetch req, bool hit, bool prefetch, bool demand)
{
    debug.nTrain++;
    if (!hit)
        debug.nMiss++;
    if (demand)
        debug.nDemand++;
    if (demand && !hit)
        debug.nDMiss++;

    bool cache_hit = hit; // FIXME: not 100% sure this shouldn't be (hit && demand) instead.
    uint64_t line_addr = req->cache_line().value();
    if (debug.outstandingMisses.contains(line_addr)) {
        debug.nDMHit++;
    }
    if (demand && !hit) {
        debug.outstandingMisses[line_addr] = current_core_cycle;
    }
    debug.trainAddrs.push_back(line_addr);

    // BEGIN ORIGINAL l1i_prefetcher_cache_operate //
    bool consecutive = false;
    if (l1i_last_basic_block + l1i_consecutive_count == line_addr) { // Same
        debug.nTrainRet++;
        return;
    } else if (l1i_last_basic_block + l1i_consecutive_count + 1 == line_addr) { // Consecutive
        l1i_consecutive_count++;                                                // SIDE EFFECT!
        consecutive = true;
    }

    // Queue basic block prefetches
    EntanglingTableEntry* entry = entanglingTable.get_entry(line_addr);
    if (entry != nullptr) { // having an entry means it's the head of a basic block. TODO: confirm if this is true
        l1i_add_xpq(line_addr + 1, 0, entry->bb_size, req); // prefetch the rest of this basic block
    }

    // Queue entangled and basic block of entangled prefetches
    for (uint32_t k = 0; k < L1I_ENTANGLED_NUM_FORMATS; k++) {
        uint64_t entangled_addr = entanglingTable.get_target_line_addr_if_confident(line_addr, k); // no side effects
        if (entangled_addr && (entangled_addr != line_addr)) {
            EntanglingTableEntry* entry = entanglingTable.get_entry(entangled_addr);
            uint32_t bb_size = 0;
            if (entry != nullptr)
                bb_size = entry->bb_size;
            l1i_add_xpq(entangled_addr, line_addr, bb_size + 1, req); // SIDE EFFECTS!
        }
    }

    if (!consecutive) { // New basic block found
        EntanglingTableEntry* entry = entanglingTable.get_entry(l1i_last_basic_block);
        uint32_t max_bb_size = 0;
        if (entry != nullptr)
            max_bb_size = entry->bb_size;

        // Check for merging bb opportunities
        if (l1i_consecutive_count) { // single blocks no need to merge
            if (l1i_basic_block_merge_diff > 0) {
                entanglingTable.add_bb_size(l1i_last_basic_block - l1i_basic_block_merge_diff,
                                            l1i_consecutive_count + l1i_basic_block_merge_diff); // SIDE EFFECTS!
                histBuf.add_bb_size(l1i_last_basic_block - l1i_basic_block_merge_diff,
                                    l1i_consecutive_count + l1i_basic_block_merge_diff); // SIDE EFFECTS!
            } else {
                entanglingTable.add_bb_size(l1i_last_basic_block,
                                            max(max_bb_size, l1i_consecutive_count));               // SIDE EFFECTS!
                histBuf.add_bb_size(l1i_last_basic_block, max(max_bb_size, l1i_consecutive_count)); // SIDE EFFECTS!
            }
        }
    }

    if (!consecutive) {                   // New basic block found
        l1i_consecutive_count = 0;        // SIDE EFFECT!
        l1i_last_basic_block = line_addr; // SIDE EFFECT!
    }

    if (!consecutive) {
        l1i_basic_block_merge_diff = histBuf.find_bb_merge(l1i_last_basic_block); // SIDE EFFECT!
    }

    // Add the request in the history buffer
    if (!consecutive && l1i_basic_block_merge_diff == 0) {
        if ((histBuf.find_entry(line_addr) == HistoryBuffer::MAX_ENTRIES)) { // no side effects
            histBuf.add_entry(line_addr, current_core_cycle);                // SIDE EFFECTS!
        } else {
            if (!cache_hit && !(l1i_ongoing_request(line_addr) && demand)) { // no side effects
                histBuf.add_entry(line_addr, current_core_cycle);            // SIDE EFFECTS!
            }
        }
    }

    // Add miss in the latency table
    if (!cache_hit && !l1i_ongoing_request(line_addr)) { // no side effects
        l1i_add_timing_entry(
            line_addr,
            0); // SIDE EFFECTS! // FIXME: crashes on this call because there are no free (invalid) MSHR entries to use
    }
    { // l1i_access_timing_entry(line_addr)
        uint32_t index = l1i_find_timing_mshr_entry(line_addr);
        if (index < L1I_TIMING_MSHR_SIZE) {
            if (!l1i_timing_mshr_table[index].accessed) {
                l1i_timing_mshr_table[index].accessed = true;
            }
        }
    }

    // Do prefetches
    // FIXME: we can't check if the PQ is full. Handle overflow?
    while (/*L1I.PQ.is_not_full() &&*/ prefetch_request->can_put() && !l1i_empty_xpq()) { // no side effects
        uint64_t entangled_addr = 0;
        Fetch pf_target = xpq_pop_prefetch_request(entangled_addr); // SIDE EFFECTS!
        debug.nTrainPrefPre++;
        if (!l1i_ongoing_request(pf_target->cache_line().value())) { // no side effects
            debug.nTrainPref++;
            prefetch_request->put(pf_target);
            l1i_add_timing_entry(pf_target->cache_line().value(), entangled_addr); // SIDE EFFECTS!
        }
    }
}

//// ORIGINAL FUNCTIONS ////
// Original authors: Alberto Ros (aros@ditec.um.es), Alexandra Jimborean (alexandra.jimborean@um.es)

uint64_t CP_Entangling::l1i_find_timing_mshr_entry(uint64_t line_addr)
{
    for (uint32_t i = 0; i < L1I_TIMING_MSHR_SIZE; i++) {
        if (l1i_timing_mshr_table[i].tag == (line_addr & L1I_TIMING_MSHR_TAG_MASK) && l1i_timing_mshr_table[i].valid)
            return i;
    }
    return L1I_TIMING_MSHR_SIZE;
}

void CP_Entangling::l1i_add_timing_entry(uint64_t line_addr, uint64_t bere_line_addr)
{
    debug.nATE++;
    // First find for coalescing
    if (l1i_find_timing_mshr_entry(line_addr) < L1I_TIMING_MSHR_SIZE)
        return;
    if (cache.entry_exists(line_addr))
        return;

    uint32_t i;
    { // i = l1i_get_invalid_timing_mshr_entry();
        for (i = 0; i < L1I_TIMING_MSHR_SIZE; i++) {
            if (!l1i_timing_mshr_table[i].valid)
                break;
        }
        if (i == L1I_TIMING_MSHR_SIZE) {
            assert(false); // It must return a free entry
        }
    }

    l1i_timing_mshr_table[i].valid = true;
    l1i_timing_mshr_table[i].tag = line_addr & L1I_TIMING_MSHR_TAG_MASK;
    l1i_timing_mshr_table[i].bere_line_addr = bere_line_addr;
    l1i_timing_mshr_table[i].timestamp = current_core_cycle & L1I_TIME_MASK;
    l1i_timing_mshr_table[i].accessed = false;
}

void CP_Entangling::l1i_invalid_timing_mshr_entry(uint64_t line_addr)
{
    debug.nITME++;
    uint32_t index = l1i_find_timing_mshr_entry(line_addr);
    assert(index < L1I_TIMING_MSHR_SIZE);
    l1i_timing_mshr_table[index].valid = false;
}

bool CP_Entangling::l1i_ongoing_request(uint64_t line_addr)
{
    return l1i_find_timing_mshr_entry(line_addr) < L1I_TIMING_MSHR_SIZE;
}

// Add a new src_line_addr->target_line_addr entry or increase the confidence of an existing entry
void EntanglingTable::entangle(uint64_t src_line_addr, uint64_t target_line_addr)
{
    uint32_t set = src_line_addr % L1I_ENTANGLED_TABLE_SETS;
    uint32_t way = this->get_way(set, src_line_addr);
    EntanglingTableEntry* entry;
    if (way == L1I_ENTANGLED_TABLE_WAYS) {
        // New entry, overwrite the oldest way and return
        way = oldestWay[set];
        oldestWay[set] = (oldestWay[set] + 1) % L1I_ENTANGLED_TABLE_WAYS;
        entry = &(table[set][way]);

        entry->tag = src_line_addr >> ENTANGLING_TABLE_SET_BITS;
        entry->numTargets = 1;
        entry->bb_size = 0;
        entry->encodedTargets[0] = target_line_addr & _MASK(L1I_ENTANGLED_FORMATS[entry->numTargets]);
        entry->targetConfidences[0] = L1I_CONFIDENCE_COUNTER_MAX_VALUE;
        for (uint32_t k = 1; k < L1I_ENTANGLED_NUM_FORMATS; k++) {
            entry->encodedTargets[k] = 0;
            entry->targetConfidences[k] = 0;
        }
        return;
    } else {
        // Add to existing entry
        entry = &(table[set][way]);
    }

    // If target_line_addr is already in this entry, increase its confidence and return
    for (uint32_t k = 0; k < L1I_ENTANGLED_NUM_FORMATS; k++) {
        if (entry->targetConfidences[k] >= L1I_CONFIDENCE_COUNTER_THRESHOLD
            && entry->get_target_line_addr(src_line_addr, k) == target_line_addr)
        {
            entry->targetConfidences[k] = L1I_CONFIDENCE_COUNTER_MAX_VALUE;
            return;
        }
    }

    // If target_line_addr is too far from src_line_addr, we may have to evict multiple older targets to make room to
    // add the new larger differnce. Repeatedly evict the lowest-confidence target until the new one fits.
    uint32_t maxTargets;
    while (true) {
        maxTargets = get_best_format_to_entangle(src_line_addr, target_line_addr);
        uint32_t numValid = 1; // the one we're trying to add
        uint32_t worstConfidence = L1I_CONFIDENCE_COUNTER_MAX_VALUE + 1;
        uint32_t worstConfidenceIndex = 0;
        for (uint32_t i = 0; i < L1I_ENTANGLED_NUM_FORMATS; i++) {
            if (entry->targetConfidences[i] >= L1I_CONFIDENCE_COUNTER_THRESHOLD) {
                numValid++;
                uint32_t newMaxTargets =
                    get_best_format_to_entangle(src_line_addr, entry->get_target_line_addr(src_line_addr, i));
                if (newMaxTargets < maxTargets) {
                    maxTargets = newMaxTargets;
                }
                if (entry->targetConfidences[i] < worstConfidence) {
                    worstConfidence = entry->targetConfidences[i];
                    worstConfidenceIndex = i;
                }
            }
        }
        if (numValid > maxTargets) {
            entry->targetConfidences[worstConfidenceIndex] = 0; // Evict worstConfidenceIndex
        } else {
            break;
        }
    }

    // Re-encode the targets
    for (uint32_t i = 0; i < L1I_ENTANGLED_NUM_FORMATS; i++) {
        if (entry->targetConfidences[i] >= L1I_CONFIDENCE_COUNTER_THRESHOLD) {
            entry->encodedTargets[i] =
                entry->get_target_line_addr(src_line_addr, i) & _MASK(L1I_ENTANGLED_FORMATS[maxTargets]);
        }
    }
    entry->numTargets = maxTargets;

    // Find a free/evicted slot and insert the new target
    for (uint32_t k = 0; k < L1I_ENTANGLED_NUM_FORMATS; k++) {
        if (entry->targetConfidences[k] < L1I_CONFIDENCE_COUNTER_THRESHOLD) {
            entry->encodedTargets[k] = target_line_addr & _MASK(L1I_ENTANGLED_FORMATS[entry->numTargets]);
            entry->targetConfidences[k] = L1I_CONFIDENCE_COUNTER_MAX_VALUE;
            return;
        }
    }
}

void CP_Entangling::l1i_add_xpq(uint64_t line_addr, uint64_t entangled_addr, uint32_t bb_size, Fetch parent)
{
    assert(bb_size > 0);

    // Merge if possible
    if (l1i_xpq[l1i_xpq_head].bb_size) { // XPQ is full
        uint32_t i = (l1i_xpq_head - 1) % L1I_XPQ_ENTRIES;
        for (uint32_t count = 0; count < L1I_XPQ_ENTRIES; count++) {
            if (line_addr == l1i_xpq[i].line_addr) {
                if (l1i_xpq[l1i_xpq_head].bb_size < bb_size) {
                    l1i_xpq[l1i_xpq_head].bb_size = bb_size;
                    return;
                }
            }
            i = (i - 1) % L1I_XPQ_ENTRIES;
        }
    }

    l1i_xpq[l1i_xpq_head].line_addr = line_addr;
    l1i_xpq[l1i_xpq_head].entangled_addr = entangled_addr;
    l1i_xpq[l1i_xpq_head].bb_size = bb_size;
    l1i_xpq[l1i_xpq_head].parent = parent;
    l1i_xpq_head = (l1i_xpq_head + 1) % L1I_XPQ_ENTRIES;
}

bool CP_Entangling::l1i_empty_xpq()
{
    return l1i_xpq[(l1i_xpq_head + L1I_XPQ_MASK) % L1I_XPQ_ENTRIES].bb_size == 0;
}

// Returns next line to prefetch
Fetch CP_Entangling::xpq_pop_prefetch_request(uint64_t& entangled_addr)
{
    assert(!l1i_empty_xpq());

    // find tail
    uint32_t tail;
    for (tail = l1i_xpq_head; tail != (l1i_xpq_head - 1) % L1I_XPQ_ENTRIES; tail = (tail + 1) % L1I_XPQ_ENTRIES) {
        if (l1i_xpq[tail].bb_size) {
            break;
        }
    }

    entangled_addr = l1i_xpq[tail].entangled_addr;
    // Create prefetch request to return
    auto pf_pc_line_addr = ProgramCounter{l1i_xpq[tail].line_addr * ProgramCounter::LINE_SIZE};
    auto pf = make_child<Fetch>(l1i_xpq[tail].parent, pf_pc_line_addr, 0);

    // update queue
    l1i_xpq[tail].bb_size--;
    if (l1i_xpq[tail].bb_size == 0) {
        return pf;
    }
    l1i_xpq[tail].line_addr++;
    l1i_xpq[tail].entangled_addr = 0;
    return pf;
}

} // namespace ampere::core

/*
 * vim: sw=4:ts=4:et:si:cindent:cino=\:0,(0,g0,t0:
 */
