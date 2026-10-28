//
// Copyright (c) 2021 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//

#include "ampere/core/prefetchers/cp_mana.h"

namespace ampere::core {

CP_Mana::CP_Mana(sc_core::sc_module_name name) : Prefetcher<Fetch>(name)
{
    statHeadFound = 0;
    statHeadMissing = 0;
    statStreamBufferHit = 0;
    statPrefetchEntryFound = 0;
    statCompactorMatch = 0;
    statStreamTrackerLookup = 0;
    statRecord = 0;
    statGetPointer = 0;
    statEnqueuePrefetch = 0;
    statL1iLookups = 0;
    statStreamBufferLookups = 0;
    statCompactorLookups = 0;
    statPrefetchQueueIsFull = 0;

    next_region_correct = 0;
    next_region_wrong = 0;
    last_region = 0;

    donot_train_set.clear();
    initialize();
}

void CP_Mana::initialize(void)
{
    AMPERE_UNCATEGORIZED_DEBUG("MANA initialize");
    if (std::ceil(log2(MANA_TABLE_SINGLE_SETS)) != std::floor(log2(MANA_TABLE_SINGLE_SETS))) {
        // assert(false && "num_of_sets is not a power of two");
        AMPERE_ASSERT(false, "num_of_sets is not a power of two");
    }
    if (std::ceil(log2(MANA_TABLE_MULTIPLE_SETS)) != std::floor(log2(MANA_TABLE_MULTIPLE_SETS))) {
        // assert(false && "num_of_sets is not a power of two");
        AMPERE_ASSERT(false, "num_of_sets is not a power of two");
    }

    MANA_tables = new MANA_TABLES(HOBPT,
                                  MANA_TABLE_SINGLE_SETS,
                                  MANA_TABLE_SINGLE_WAYS,
                                  MANA_TABLE_SINGLE_CIRCULAR_HISTORY_SIZE,
                                  MANA_TABLE_SINGLE_TAG_DOMAIN,
                                  MANA_TABLE_MULTIPLE_SETS,
                                  MANA_TABLE_MULTIPLE_WAYS,
                                  MANA_TABLE_MULTIPLE_CIRCULAR_HISTORY_SIZE,
                                  MANA_TABLE_MULTIPLE_TAG_DOMAIN,
                                  MANA_SUPPORT_MULTIPLE_TABLES,
                                  theBlockSize,
                                  FIXED_REGION_SHIFT_OFFSET,
                                  FLOATED_BACKWARD_REGION_SIZE,
                                  FLOATED_FORWARD_REGION_SIZE);

    AMPERE_UNCATEGORIZED_DEBUG("tds: {} tdm: {}", MANA_TABLE_SINGLE_TAG_DOMAIN, MANA_TABLE_MULTIPLE_TAG_DOMAIN);
    streamTracker = new StreamTracker(theStreamCount,
                                      theTrackerSize,
                                      theLookahead,
                                      FIXED_REGION_SHIFT_OFFSET,
                                      FLOATED_BACKWARD_REGION_SIZE,
                                      FLOATED_FORWARD_REGION_SIZE);
    for (int i = 0; i < theSRQSize; i++) {
        SRQ.emplace_back(
            i + 1, false, FIXED_REGION_SHIFT_OFFSET, FLOATED_BACKWARD_REGION_SIZE, FLOATED_FORWARD_REGION_SIZE);
    }
    traceLineBuffer = 0;
    thePrefetchQueueSize = 64;

    report_prefetcher_storage_cost();
}

void CP_Mana::report_prefetcher_storage_cost()
{
    // we have assumed a 64-bit address space
    uint64_t address_width = 64;

    // HOBP is an instruction address that:
    // 6 bits are removed because of block offset,
    // log2(MANA_TABLE_SINGLE_SETS) bits are removed because of set offset in MANA_TABLE,
    // MANA_TABLE_SINGLE_TAG_DOMAIN bits are removed because of the partial tag,
    // log2(HOBPT_sets) bits are removed because of set offset in HOBPT
    uint64_t HOBP = address_width - 6 - log2(MANA_TABLE_SINGLE_SETS) - MANA_TABLE_SINGLE_TAG_DOMAIN - log2(HOBPT_sets);

    // successor ptr should be able to address all MANA_TABLE entries
    uint64_t successor_ptr = std::ceil(log2(MANA_TABLE_SINGLE_SETS * MANA_TABLE_SINGLE_WAYS));
    if (MANA_SUPPORT_MULTIPLE_TABLES) {
        // max(): successor_ptr should be large enough to address both MANA_TABLEs
        // '+1' : is for a single bit that indicates the pointer belongs to which MANA_TABLE
        successor_ptr = std::max(std::ceil(log2(MANA_TABLE_SINGLE_SETS * MANA_TABLE_SINGLE_WAYS)),
                                 std::ceil(log2(MANA_TABLE_MULTIPLE_SETS * MANA_TABLE_MULTIPLE_WAYS)))
                        + 1;
    }

    // HOBP_index should be large enough to address all HOBPT entries
    uint64_t HOBP_index = log2(HOBPT_sets * HOBPT_ways);

    // the size of MANA_TABLE_SINGLE is calculated as follows: (it is also the same for MULTIPLE)
    // (the number of all entries * the size of each entry)
    // the number of all entries: (MANA_TABLE_SINGLE_SETS * MANA_TABLE_SINGLE_WAYS)
    // each entry has:
    //  1) partial tag = MANA_TABLE_SINGLE_TAG_DOMAIN
    //  2) HOBP_index
    //  3) footprints: regionSize * MANA_TABLE_SINGLE_CIRCULAR_HISTORY_SIZE
    //  4) a pointer to successor: successor_ptr
    //  5) bits for replacement policy: ceil(MANA_TABLE_SINGLE_WAYS)
    double size_MANA_table =
        (MANA_TABLE_SINGLE_SETS * MANA_TABLE_SINGLE_WAYS)
        * (MANA_TABLE_SINGLE_TAG_DOMAIN + HOBP_index + regionSize
           + MANA_TABLE_SINGLE_CIRCULAR_HISTORY_SIZE * successor_ptr + std::ceil(log2(MANA_TABLE_SINGLE_WAYS)))
        / 8.0 / 1024.0;

    // AMPERE_INFO("entry size: {} {} {} {} {} {}", MANA_TABLE_SINGLE_TAG_DOMAIN, HOBP_index, regionSize,
    //             MANA_TABLE_SINGLE_CIRCULAR_HISTORY_SIZE, successor_ptr, std::ceil(MANA_TABLE_SINGLE_WAYS));

    double size_MANA_table_2 = 0;

    if (MANA_SUPPORT_MULTIPLE_TABLES) {
        size_MANA_table_2 =
            (MANA_TABLE_MULTIPLE_SETS * MANA_TABLE_MULTIPLE_WAYS)
            * (MANA_TABLE_MULTIPLE_TAG_DOMAIN + HOBP_index + regionSize
               + std::ceil(log2(MANA_TABLE_MULTIPLE_CIRCULAR_HISTORY_SIZE))
               + successor_ptr * MANA_TABLE_MULTIPLE_CIRCULAR_HISTORY_SIZE + std::ceil(log2(MANA_TABLE_MULTIPLE_WAYS)))
            / 8.0 / 1024.0;
    }

    // the size of HOBPT is calculated as follows:
    // the number of entries * the size of each entry
    // the number of entries = (HOBPT_sets * HOBPT_ways)
    // each entry:
    //  1) HOBP
    //  2) bits for replacement policy = ceil(log2(HOBPT_ways))
    double size_HOBPT = (HOBPT_sets * HOBPT_ways) * (HOBP + ceil(log2(HOBPT_ways))) / 8.0 / 1024.0;

    // the size of SRQ: the number of SRQ entries * the size of each SRQ entry
    // the number of SRQ entries: theSRQSize
    // the size of each SRQ entry: (a trigger address = address_width - 6) + (a footprint = regionSize)
    double size_SRQ = theSRQSize * (address_width - 6 + regionSize) / 8.0 / 1024.0;

    // the size of SABs = the number of SABs * the number of entries in each SAB * the size of each SAB entry
    double size_SABs = (theStreamCount * theTrackerSize * (address_width - 6 + regionSize)) / 8.0 / 1024.0;
    double size_prefetch_queue = thePrefetchQueueSize * (address_width - 6) / 8.0 / 1024.0;

    {
        AMPERE_INFO("size MANA SINGLE: {}KB", size_MANA_table);
        AMPERE_INFO("size MANA MULTIPLE: {}KB", size_MANA_table_2);
        AMPERE_INFO("size HOBPT: {}KB", size_HOBPT);
        AMPERE_INFO("size SRQ: {}KB", size_SRQ);
        AMPERE_INFO("size SABs: {}KB", size_SABs);
        AMPERE_INFO("size prefetch queue: {}KB", size_prefetch_queue);
        AMPERE_INFO("total: {}KB",
                    (size_HOBPT + size_MANA_table + size_MANA_table_2 + size_SRQ + size_SABs + size_prefetch_queue));
    }
}

void CP_Mana::finalize(void) {}

void CP_Mana::retire(uint64_t theAddress)
{
    bool prefetched = false;

    // update an evaluation counter
    statStreamBufferLookups += (theStreamCount * theTrackerSize * 2);

    // Lookup the SABs
    Range aRange = Range();
    bool streamHit = streamTracker->lookup(theAddress, prefetched, aRange, ACTUAL_REGION_TYPE);
    statStreamTrackerLookup++;

    // Do the following if the new block misses in SABs
    if (!streamHit) {
        // Lookup MANA_TABLEs to know whether this block is the trigger address of an already observed spatial region
        TABLE_PTR table_ptr = MANA_tables->getPtr(theAddress);
        statGetPointer++;

        // allocate a new stream if a valid entry is found in MANA_TABLEs
        if (table_ptr.set != ULLONG_MAX && table_ptr.way != ULLONG_MAX) {
            statHeadFound++;
            aRange = streamTracker->allocate(table_ptr);
        } else {
            statHeadMissing++;
        }
    } else {
        if (prefetched) {
            statStreamBufferHit++;
        }
    }

    // aRange.theLegth determines the number of spatial regions that should be prefetched to provide the sufficient
    // lookahead ahead of the fetch stream
    if (aRange.theLength) {
        // start pointer chasing!
        for (int i = 0; i < aRange.theLength; i++) {
            // Read the spatial region using the pointer that is kept in the SAB
            StreamEntry aPrefetchEntry(
                FIXED_REGION_SHIFT_OFFSET, FLOATED_BACKWARD_REGION_SIZE, FLOATED_FORWARD_REGION_SIZE);
            bool readHit = MANA_tables->read(aRange, aPrefetchEntry);

            if (!readHit) {
                break;
            }

            // extract the prefetch candidates of the new fetched spatial region
            std::vector<uint64_t> prefetch_candidates = aPrefetchEntry.getPrefetchCandidates();
            bool issuedAll = true;
            for (uint32_t j = 0; j < prefetch_candidates.size(); j++) {
                // push the prefetch candidates into 'thePrefetchQueue'
                if (thePrefetchQueue.size() >= thePrefetchQueueSize) {
                    issuedAll = false;
                    break;
                }
                thePrefetchQueue.push_back(prefetch_candidates[j]);

                // update some evaluation counters
                statEnqueuePrefetch++;
                statL1iLookups += 8;
            }

            if (!issuedAll) {
                statPrefetchQueueIsFull++;
                break;
            }

            // a new spatial region is fetched from MANA_TABLEs, push it to the SAB
            statPrefetchEntryFound++;
            streamTracker->push_back(aRange.thePtr, aPrefetchEntry);

            // chase the pointer, go to the successor spatial region
            MANA_tables->incrementPointer(aRange);
        }
    }

    // update an evaluation counter
    statCompactorLookups += (theSRQSize * 2);

    // update the SRQ
    bool evict = true;
    // iterate over the SRQ entries
    for (std::list<StreamEntry>::iterator i = SRQ.begin(); i != SRQ.end(); i++) {
        bool prefetched;
        // check whether the new observed block falls in the address space covered by a tracked spatial region in the
        // SRQ
        if (i->inRange(theAddress, prefetched, ACTUAL_REGION_TYPE)) {
            std::pair<int, bool> index = i->getIndex(theAddress);
            // set the bit corresponds to the arriving block in the spatial region's footprint
            if (index.second) {
                i->bits.set(index.first);
            }

            // update an evaluation counter
            statCompactorMatch++;

            // should not evict an entry, it is matched
            evict = false;
            break;
        }
    }

    // no match found in SRQ, an entry should be evicted, the victim should be recorded in the MANA_TABLEs,
    // and a new spatial regions should be tracked in the SRQ
    if (evict) {
        // the victim is in front of the SRQ
        StreamEntry victim = SRQ.front();
        SRQ.pop_front();

        // the new spatial region is pushed into the SRQ
        SRQ.emplace_back(theAddress,
                         ACTUAL_REGION_TYPE,
                         FIXED_REGION_SHIFT_OFFSET,
                         FLOATED_BACKWARD_REGION_SIZE,
                         FLOATED_FORWARD_REGION_SIZE); // add the new address to the SRQ

        // record the victim in the MANA_TABLEs
        MANA_tables->record(victim);

        // The following code block updates some evaluation counters
        {
            RegionBases.insert(victim.theRegionBase >> LOG2_BLOCK_SIZE);
            if (next_region.find(last_region) != next_region.end()) {
                if (next_region[last_region] == victim.theRegionBase) {
                    next_region_correct++;
                } else {
                    next_region_wrong++;
                }
            }
            next_region[last_region] = victim.theRegionBase;
            last_region = victim.theRegionBase;
            statRecord++;
        }
    }
}

void CP_Mana::pushRetireIn(uint64_t theAddress)
{
    if (traceLineBuffer == (theAddress & theBlockMask)) {
        return;
    }
    traceLineBuffer = theAddress & theBlockMask;

    // inform MANA that a new block is observed
    retire(theAddress & theBlockMask);
}

uint64_t CP_Mana::pullPrefetchOut()
{
    if (!thePrefetchQueue.empty()) {
        uint64_t thePrefetchAddress = thePrefetchQueue.front();
        // thePrefetchQueue.pop_front();
        return thePrefetchAddress;
    } else {
        return 0;
    }
}

void CP_Mana::popPrefetch()
{
    thePrefetchQueue.pop_front();
}

void CP_Mana::printMANATables()
{
    MANA_tables->printMANATable();
}

void CP_Mana::getStats()
{
    fmt::print(std::cout, "statHeadFound:                  {}\n", statHeadFound);
    fmt::print(std::cout, "statHeadMissing:                {}\n", statHeadMissing);
    fmt::print(std::cout, "statStreamBufferHit:            {}\n", statStreamBufferHit);
    fmt::print(std::cout, "statPrefetchEntryFound:         {}\n", statPrefetchEntryFound);
    fmt::print(std::cout, "statCompactorMatch:             {}\n", statCompactorMatch);
    fmt::print(std::cout, "statStreamTrackerLookup:        {}\n", statStreamTrackerLookup);
    fmt::print(std::cout, "statRecord:                     {}\n", statRecord);
    fmt::print(std::cout, "statGetPointer:                 {}\n", statGetPointer);
    fmt::print(std::cout, "statEnqueuePrefetch:            {}\n", statEnqueuePrefetch);
    fmt::print(std::cout, "statPrefetchQueueIsFull:        {}\n", statPrefetchQueueIsFull);

    fmt::print(
        std::cout, "StreamBufferHitRate:            {}\n", (double)statStreamBufferHit / statStreamTrackerLookup);
    fmt::print(std::cout, "Regions' size:                  {}\n", RegionBases.size());
    fmt::print(std::cout, "next_region_correct:            {}\n", next_region_correct);
    fmt::print(std::cout, "next_region_wrong:              {}\n", next_region_wrong);
    fmt::print(std::cout,
               "next_region_correct_prediction: {}\n",
               (double)next_region_correct / (next_region_correct + next_region_wrong));
    fmt::print(std::cout, "statStreamBufferLookups:        {}\n", statStreamBufferLookups);
    fmt::print(std::cout, "statL1iLookups:                 {}\n", statL1iLookups);
    fmt::print(std::cout, "statCompactorLookups:           {}\n", statCompactorLookups);
}

void CP_Mana::train_impl(Fetch req, bool hit, bool prefetch, bool demand)
{
    auto addr = req->program_counter.value();

    // if (donot_train_set.find(addr & theBlockMask) != donot_train_set.end())
    //     return;

    pushRetireIn(addr);
    if (prefetch_request->can_put()) {
        auto prefetch_line = pullPrefetchOut();
        if (prefetch_line != 0) {
            auto prefetch_target = make_child<Fetch>(req, ProgramCounter{prefetch_line}, 0);
            prefetch_request->put(prefetch_target);
            // fmt::print(std::cout, "{:x},{:x},{}\n", (addr & theBlockMask), prefetch_line, prefetch);
            popPrefetch();
        }
    }
}

void CP_Mana::evict_impl(PhysAddr pa, bool prefetch, bool demand)
{
    auto addr = (pa.value() & theBlockMask);
    if (prefetch && (!demand)) {
        donot_train_set.insert(addr);
    } else {
        std::set<uint64_t>::iterator it = donot_train_set.find(addr);
        if (it != donot_train_set.end())
            donot_train_set.erase(addr);
    }
}

void CP_Mana::table_print(std::ostream& os)
{
    return;
}

} // namespace ampere::core

/*
 * vim: sw=4:ts=4:et:si:cindent:cino=\:0,(0,g0,t0:
 */
