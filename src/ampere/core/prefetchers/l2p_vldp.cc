//
// Copyright (c) 2019 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//

#include "ampere/core/prefetchers/l2p_vldp.h"
#include "ampere/uarch_common/memreq.h"

namespace ampere::core {

L2P_VLDP::L2P_VLDP(sc_core::sc_module_name name) : Prefetcher<MemReq>(name)
{
    // Sizing of cascaded Delta Prediction Tables
    delta_prediction_tables.resize(num_dpt_levels);
    // Sizing of Offset Prediction Table
    offset_prediction_table.resize(num_opt_entries);
}

// *****************************************************************************************
// Trains the VLDP prefetcher
// *****************************************************************************************
void L2P_VLDP::train_impl(MemReq req, bool hit, bool prefetch, bool demand)
{
    // Ignore i-side requests for now
    switch (req->origin) {
    case MemOp::LSU:
    case MemOp::LSU_PREFETCH:
    case MemOp::DMMU:
    case MemOp::PREFETCH:
    case MemOp::L2C:
        break;
    default:
        return;
    }

    phys_addr = req->phys_addr;
    // Line address is bits [63:6] of physical address
    line_addr = PhysAddr(phys_addr.line_number());
    // Page number is bits [63:12] of physical address for 4K pages
    page_number = PhysAddr(phys_addr.page_number_4k());
    // Page offset is bits [11:6] of physical address for 4K pages
    page_offset = PhysAddr(phys_addr.page_offset_4k());

    bool opt_pref_ready = false;
    bool dpt_pref_ready = false;
    int opt_pref_offset = 0;
    int dpt_pref_offset = 0;

    if (!hit || prefetch) {
        // Update DHB for a PAE and return a matching or new entry pointer
        DHBEntry* dhb_entry_ptr = update_dhb(page_number, page_offset, line_addr);

        // On the first access to a page, the OPT is looked up using the page offset, and if the accuracy bit is set
        // for this entry, a prefetch is issued with the predicted delta
        if (dhb_entry_ptr->num_times_page_was_touched == 1) {
            opt_pref_offset = lookup_opt(page_offset.value());

            if (has_opt && (opt_pref_offset != 0)) {
                opt_pref_ready = true;
            }
        }
        // On the second access to a page, a delta can be computed and compared with the contents of the OPT
        else if (dhb_entry_ptr->num_times_page_was_touched == 2)
        {
            update_opt(page_offset.value(), dhb_entry_ptr->deltas.data[0]);
        }
        // On the third access to the same page, update DPT
        else if (dhb_entry_ptr->num_times_page_was_touched >= 3)
        {
            update_dpt(dhb_entry_ptr->deltas, dhb_entry_ptr->last_predictor, dhb_entry_ptr->num_times_page_was_touched);
        }

        archx::PhysAddr pref_addr;
        DeltaVector deltas = dhb_entry_ptr->deltas;

        // Increasing prefetching depth/degree by generating multiple prefetch requests at a time
        for (int i = 0; i < max_pref_degree && prefetch_request->can_put(); i++) {
            // Check if the list of deltas matches any of the DPT levels
            // For multi-degree prefetches, update accuracy (last predictor) only for the one that issued the original
            // prefetch
            if (i == 0) {
                std::pair<std::pair<bool, int>, DPTEntry*> dpt_entry_for_dhb_update =
                    lookup_dpts(0, true, false, deltas);
                bool dpt_entry_for_dhb_update_exists = dpt_entry_for_dhb_update.first.first;

                // Update the last predictor in DHB with the DPT level that supplied the prediction
                if (dpt_entry_for_dhb_update_exists) {
                    dhb_entry_ptr->last_predictor = dpt_entry_for_dhb_update.first.second;
                    // Also update the prediction if the accuracy is low
                    if (dpt_entry_for_dhb_update.second->accuracy == 0) {
                        dpt_entry_for_dhb_update.second->predicted_delta = deltas.data[0];
                    }
                }
            }

            // Now look for a prefetch worthy offset from the DPT
            std::pair<std::pair<bool, int>, DPTEntry*> dpt_entry_for_prefetch = lookup_dpts(0, true, true, deltas);
            bool dpt_entry_for_prefetch_exists = dpt_entry_for_prefetch.first.first;

            // If the prediction is fairly accurate, generate and send a prefetch request
            if (dpt_entry_for_prefetch_exists) {
                if (dpt_entry_for_prefetch.second->accuracy == max_dpt_accuracy) {
                    dpt_pref_ready = true;
                    dpt_pref_offset = dpt_entry_for_prefetch.second->predicted_delta;
                }
            }

            deltas = deltas.shift_left(deltas, dpt_pref_offset, num_deltas_tracked);

            // // Crossing the 4k page boundary, no OPT prefetch request issued
            // if (!line_addr.same_cache_line(line_addr + opt_pref_offset, 6)) {
            //     return;
            // }
            // Crossing the 4k page boundary, no DPT prefetch request issued
            if (!line_addr.same_cache_line(line_addr + dpt_pref_offset)) {
                return;
            }

            if (opt_pref_ready || dpt_pref_ready) {
                int pref_offset;
                if (opt_pref_ready)
                    pref_offset = opt_pref_offset;
                else
                    pref_offset = dpt_pref_offset;

                // Generate prefetch request address
                if (i == 0)
                    pref_addr = PhysAddr{(line_addr.value() + pref_offset) << 6};
                else
                    pref_addr += (pref_offset << 6);

                // Generate and send prefetch request
                auto pref_req = make_child<MemReq>(
                    req,
                    MemReqObj::prefetch(pref_addr.value(), pref_addr, MemOp::Requester::PREFETCH, prefetch_type()));
                prefetch_request->put(pref_req);
                // num_pref_issued++;

                // std::cout<<"i: "<<i<<" pf: "<<pref_addr<<std::endl;

                // Add latest prefetch address to a queue for debug purposes
                if (i == 0)
                    pref_line_addr = PhysAddr{line_addr.value() + pref_offset};
                else
                    pref_line_addr += pref_offset;

                PFBEntry new_pref_data = {line_addr, page_number, pref_offset, get_cycle()};
                if (opt_pref_ready)
                    new_pref_data.pref_provider = "OPT";

                if (prefetch_buffer.size() == num_pfb_entries)
                    prefetch_buffer.erase(prefetch_buffer.begin());

                prefetch_buffer.emplace_hint(prefetch_buffer.end(),
                                             std::pair<archx::PhysAddr, PFBEntry>(pref_line_addr, new_pref_data));
            }
        }

        // If the incoming line address was a prefetch that hit in the cache, mark it as a prefetch hit in the PFB
        std::map<archx::PhysAddr, PFBEntry>::iterator it;
        it = prefetch_buffer.find(line_addr);
        if (it != prefetch_buffer.end()) {
            if (prefetch) {
                it->second.pref_hit = true;
                // num_pref_hit++;
            }
            it->second.hit_cycle = get_cycle();
        }
    }
} // L2P_VLDP::train_impl()

// *****************************************************************************************
// Lookup the DHB and return the delta history, last predictor and number of times page was
// accessed
// *****************************************************************************************
L2P_VLDP::DHBEntry* L2P_VLDP::lookup_dhb(archx::PhysAddr page_number)
{
    std::map<archx::PhysAddr, DHBEntry>::iterator it;
    it = delta_history_buffer.find(page_number);
    if (it != delta_history_buffer.end())
        return &it->second;
    return nullptr;

} // L2P_VLDP::lookup_dhb()

// *****************************************************************************************
// Updates the Delta History Buffer (DHB)
// The DHB is accessed via CAM lookup using incoming request's page number. If there is a
// match, the matching entry's offset field is updated with the new page offset, the delta
// history is shifted and the new delta is added as the most recent. and the MRU field is
// updated.
// *****************************************************************************************
L2P_VLDP::DHBEntry* L2P_VLDP::update_dhb(archx::PhysAddr page_number,
                                         archx::PhysAddr page_offset,
                                         archx::PhysAddr line_addr)
{
    // Fully associative search to find an entry with the matching page number
    DHBEntry* entry_ptr = lookup_dhb(page_number);

    if (entry_ptr) {
        auto calculated_delta = page_offset - entry_ptr->offset;
        if (calculated_delta != 0) {
            entry_ptr->deltas = entry_ptr->deltas.shift_left(entry_ptr->deltas, calculated_delta, num_deltas_tracked);
            entry_ptr->offset = page_offset;
            entry_ptr->num_times_page_was_touched++;
            entry_ptr->mru = 1;
        } else {
            // TODO: Do we want to decrement num_times counter?
            // if (entry_ptr->num_times_page_was_touched != 0) {
            //     entry_ptr->num_times_page_was_touched -= 1;
            // }
            // else {
            //     entry_ptr->num_times_page_was_touched = 0;
            // }
        }
    }
    // If no matching entry is found (DHB miss), then a not-Most Recently Used (nMRU) DHB entry is evicted and
    // assigned to the new page number
    else {
        // Update DHB MRU
        update_dhb_mru();
        // Add new entry to DHB
        DHBEntry new_entry = {page_offset, DeltaVector::init(num_deltas_tracked), line_addr};
        delta_history_buffer.insert(std::pair<archx::PhysAddr, DHBEntry>(page_number, new_entry));
        // Lookup again to return pointer to recently added entry
        entry_ptr = lookup_dhb(page_number);
        // TODO: Add an assertion here to check that entry is found
    }

    return entry_ptr;

} // L2P_VLDP::update_dhb()

// *****************************************************************************************
// Lookup the OPT for new page accesses to get predicted delta
// *****************************************************************************************
int L2P_VLDP::lookup_opt(int page_offset)
{
    int delta = 0;
    // TODO: Change the index range based on table size
    if (offset_prediction_table[page_offset].accuracy == 1) {
        delta = offset_prediction_table[page_offset].predicted_delta;
    }

    return delta;

} // L2P_VLDP::lookup_opt()

// *****************************************************************************************
// Update accuracy of OPT
// *****************************************************************************************
void L2P_VLDP::update_opt(int page_offset, int dhb_delta)
{
    int first_offset = page_offset - dhb_delta;
    OPTEntry* opt_entry_ptr = &offset_prediction_table[first_offset];
    int predicted_delta = opt_entry_ptr->predicted_delta;

    // If OPT prediction matches the observed delta, the accuracy bit is set to 1
    if ((dhb_delta == predicted_delta) && (dhb_delta != 0)) {
        opt_entry_ptr->accuracy = 1;
    }
    // If the accuracy bit was already 0, the old predicted delta is replaced with the new observed delta,
    // and the accuracy bit remains 0
    else if (opt_entry_ptr->accuracy == 0)
    {
        opt_entry_ptr->predicted_delta = dhb_delta;
    }
    // If OPT prediction does not match the observed delta, the accuracy bit is set to 0
    else {
        opt_entry_ptr->predicted_delta = dhb_delta;
        opt_entry_ptr->accuracy = 0;
    }

} // L2P_VLDP::update_opt()

// *****************************************************************************************
// Looks up one or multiple DPTs and returns entry pointer
// *****************************************************************************************
std::pair<std::pair<bool, int>, L2P_VLDP::DPTEntry*> L2P_VLDP::lookup_dpts(int table_num,
                                                                           bool lookup_all_tables,
                                                                           bool prefetch,
                                                                           DeltaVector dhb_deltas)
{
    bool match_exists = false;
    int dpt_level = 0;
    std::deque<DPTEntry>::iterator it;
    DPTEntry* ptr = nullptr;
    int skew;

    if (prefetch)
        skew = 0;
    else
        skew = 1;

    int start_level;
    int end_level;

    if (lookup_all_tables) {
        start_level = num_dpt_levels - 1;
        end_level = 0;
    } else {
        start_level = table_num;
        end_level = table_num;
    }

    for (int level = start_level; (level >= end_level) && !match_exists; level--) {
        for (it = delta_prediction_tables[level].begin(); (it != delta_prediction_tables[level].end()) && !match_exists;
             it++)
        {
            for (int i = 0; i <= level; i++) {
                if ((i == 0) | match_exists) {
                    if (it->deltas.data[i] == dhb_deltas.data[i + skew]) {
                        match_exists = true;
                        dpt_level = level;
                        ptr = &(*it);
                    } else {
                        match_exists = false;
                    }
                } else {
                    break;
                }
            }
        }
    }

    return std::make_pair(std::make_pair(match_exists, dpt_level), ptr);

} // L2P_VLDP::lookup_dpts()

// *****************************************************************************************
// Update the Delta Prediction Tables (DPTs) for PAEs.
// PAEs can cause DPT to be updated in 3 ways:
// 1.   Any new delta patterns will be allocated in the DPT and older entry will be evicted
//      by nMRU
// 2.   Each PAE is compared with previous delta prediction and accuracy is updated
// 3.   If prediction accuracy is too low, the delta prediction field is updated to
//      reflect the new delta
// *****************************************************************************************
void L2P_VLDP::update_dpt(DeltaVector dhb_deltas, int last_predictor, int num_times_page_was_touched)
{
    int latest_dhb_delta = dhb_deltas.data[0];

    // Search for entry in DPT that matches delta history from DHB
    std::pair<std::pair<bool, int>, DPTEntry*> matched_last_dpt_entry =
        lookup_dpts(last_predictor, false, false, dhb_deltas);
    bool found_entry_in_last_dpt = matched_last_dpt_entry.first.first;
    // bool found_entry_in_some_dpt = false;
    DPTEntry* dpt_entry_ptr = matched_last_dpt_entry.second;

    int stored_dpt_pred;

    // If matching entry is found in DPT, accuracy and predicted delta fields are updated
    if (found_entry_in_last_dpt) {
        stored_dpt_pred = dpt_entry_ptr->predicted_delta;

        // Update accuracy of DPT
        if (latest_dhb_delta == stored_dpt_pred) {
            if (dpt_entry_ptr->accuracy != max_dpt_accuracy) {
                dpt_entry_ptr->accuracy++;
            } else {
                dpt_entry_ptr->accuracy = max_dpt_accuracy;
            }
            dpt_entry_ptr->num_times_used++; // TODO: Check if this is the right place to increment
        } else {
            if (dpt_entry_ptr->accuracy == 0) {
                dpt_entry_ptr->accuracy = 0;
            } else {
                dpt_entry_ptr->accuracy -= 1;
            }

            // If the prediction accuracy is sufficiently low, the delta prediction field will be updated to
            // reflect the new delta.
            if (dpt_entry_ptr->accuracy == 0) {
                dpt_entry_ptr->predicted_delta = latest_dhb_delta;
            }

            // Inaccurate predictions in DPT table T will prompt promotion of delta pattern to the next table T+1.
            // Promotions can only happen if a page was touched more than 3 times, this is to avoid writing 0s into the
            // table
            if ((last_predictor != num_dpt_levels - 1) && (num_times_page_was_touched > 3)) {
                // Check if an entry exists in next table and update prediction if it does
                std::pair<std::pair<bool, int>, DPTEntry*> matched_next_dpt_entry =
                    lookup_dpts(last_predictor + 1, false, false, dhb_deltas);
                bool found_entry_in_next_dpt = matched_next_dpt_entry.first.first;

                // If matching entry exists in next table, update prediction
                if (found_entry_in_next_dpt) {
                    matched_next_dpt_entry.second->predicted_delta = latest_dhb_delta;
                }
                // If matching entry does not exist, add a new entry in table T+1
                else {
                    add_dpt_entry(dhb_deltas, last_predictor + 1);
                }
            }
        }
    }
    // If the delta pattern is not found in the last predictor, search all the tables for a matching pattern
    else {
        std::pair<std::pair<bool, int>, DPTEntry*> matched_some_dpt_entry = lookup_dpts(0, true, false, dhb_deltas);
        bool found_entry_in_some_dpt = matched_some_dpt_entry.first.first;

        DPTEntry* dpt_entry_ptr = matched_some_dpt_entry.second;

        // If matching entry is found in DPT, accuracy and predicted delta fields are updated
        if (found_entry_in_some_dpt) {
            stored_dpt_pred = dpt_entry_ptr->predicted_delta;

            if (latest_dhb_delta != stored_dpt_pred) {
                // If the prediction accuracy is sufficiently low, the delta prediction field will be updated to
                // reflect the new delta.
                if (dpt_entry_ptr->accuracy == 0) {
                    dpt_entry_ptr->predicted_delta = latest_dhb_delta;
                }
            }
        } else {
            // If NO matching entry is found in any table, allocate a new entry in the shortest-history table (DPT0) for
            // the latest delta
            add_dpt_entry(dhb_deltas, 0);
        }
    }

} // L2P_VLDP::update_dpt()

// *****************************************************************************************
// Adds a new entry to specified DPT
// *****************************************************************************************
void L2P_VLDP::add_dpt_entry(DeltaVector dhb_deltas, int table_num)
{
    // Update DPT MRU
    update_dpt_mru(table_num);

    DPTEntry new_dpt_entry = {DeltaVector::init(table_num + 1), dhb_deltas.data[0]};
    // Update the delta value with second-from-latest delta value from the DHB
    for (int i = 0; i <= table_num; i++) {
        new_dpt_entry.deltas.data[i] = dhb_deltas.data[i + 1];
    }
    delta_prediction_tables[table_num].push_back(new_dpt_entry);

} // L2P_VLDP::add_dpt_entry()

// *****************************************************************************************
// Check if the max size of DHB is reached and evict an entry by MRU
// *****************************************************************************************
void L2P_VLDP::update_dhb_mru()
{
    bool mru_entry_found = false;

    if (delta_history_buffer.size() == num_dhb_entries) {
        for (std::map<archx::PhysAddr, DHBEntry>::iterator it = delta_history_buffer.begin();
             (it != delta_history_buffer.end()) && !mru_entry_found;
             it++)
        {
            // Remove the entry that has not been touched ever
            if (it->second.num_times_page_was_touched == 0) {
                delta_history_buffer.erase(it);
                mru_entry_found = true;
                // Else look for an entry which is not recently used
            } else if (it->second.mru == 0) {
                delta_history_buffer.erase(it);
                mru_entry_found = true;
            } else {
                mru_entry_found = false;
            }
        }
    }

    // If no entry is worthy of eviction, flip all MRU bits to 0 and start again
    if ((delta_history_buffer.size() == num_dhb_entries) && !mru_entry_found) {
        for (std::map<archx::PhysAddr, DHBEntry>::iterator it = delta_history_buffer.begin();
             it != delta_history_buffer.end();
             it++)
        {
            it->second.mru = 0;
        }
        delta_history_buffer.erase(delta_history_buffer.begin());
    }

} // L2P_VLDP::update_dhb_mru()

// *****************************************************************************************
// Check if the max size of a DPT is reached and evict an entry by MRU
// *****************************************************************************************
void L2P_VLDP::update_dpt_mru(int table_num)
{
    bool mru_entry_found = false;

    if (delta_prediction_tables[table_num].size() == num_dpt_entries) {
        for (std::deque<DPTEntry>::iterator it = delta_prediction_tables[table_num].begin();
             (it != delta_prediction_tables[table_num].end()) && !mru_entry_found;
             it++)
        {
            // Remove the entry that has not been touched ever
            if (it->num_times_used == 0) {
                delta_prediction_tables[table_num].erase(it);
                mru_entry_found = true;
                // Else look for an entry which is not recently used
            } else if (it->mru == 0) {
                delta_prediction_tables[table_num].erase(it);
                mru_entry_found = true;
                // // Else look for an entry whose accuracy is low
                // // This could cause a recently added entry to get evicted
                // } else if (it->accuracy == 0) {
                //     delta_prediction_tables[table_num].erase(it);
                //     mru_entry_found = true;
            } else {
                mru_entry_found = false;
            }
        }
    }

    // If no entry is worthy of eviction, flip all MRU bits to 0 and start again
    if ((delta_prediction_tables[table_num].size() == num_dpt_entries) && !mru_entry_found) {
        for (std::deque<DPTEntry>::iterator it = delta_prediction_tables[table_num].begin();
             (it != delta_prediction_tables[table_num].end());
             it++)
        {
            it->mru = 0;
        }
        // Randomly picking the first entry to evict
        delta_prediction_tables[table_num].erase(delta_prediction_tables[table_num].begin());
    }

} // L2P_VLDP::update_dpt_mru()

// *****************************************************************************************
// Prints debug information
// *****************************************************************************************
void L2P_VLDP::table_print(std::ostream& os) const
{
    /////////////////////////////////////////////////////////////////////////////////
    // PRINT CONTENTS OF PREFETCH BUFFER
    /////////////////////////////////////////////////////////////////////////////////
    fmt::print(os, "\n{:^113}", "PREFETCH BUFFER");
    fmt::print(os, "\n{:-^113}", "-");
    fmt::print(os,
               "\n{0:>3} | {1:>8} | {2:>8} | {3:>15} | {4:>15} | {5:>13} | {6:>10} | {7:>8} | {8:>9}\n",
               "#",
               "pref_cyc",
               "hit_cyc",
               "pref_line_addr",
               "pae_line_addr",
               "pref_page",
               "pref_delta",
               "pref_hit",
               "pref_prov");
    fmt::print(os, "{:-^113}\n", "-");

    int pfb_entry_num = 0;
    // Loop through all entries in PFB
    for (std::map<archx::PhysAddr, PFBEntry>::const_iterator it = prefetch_buffer.begin(); it != prefetch_buffer.end();
         it++)
    {
        fmt::print(os,
                   "{0:>3} | {1:>8} | {2:>8} | {3:>15} | {4:>15} | {5:>13} | {6:>10} | {7:>8} | {8:>9}\n",
                   pfb_entry_num++,
                   it->second.pref_cycle,
                   it->second.hit_cycle,
                   it->first,
                   it->second.pae_line_addr,
                   it->second.pref_page,
                   it->second.pref_delta,
                   it->second.pref_hit,
                   it->second.pref_provider);
    }

    fmt::print(os, "{:-^113}\n", "-");

    /////////////////////////////////////////////////////////////////////////////////
    // PRINT CONTENTS OF DELTA HISTORY BUFFER
    /////////////////////////////////////////////////////////////////////////////////
    fmt::print(os, "\n{:^91}", "DELTA HISTORY BUFFER");
    fmt::print(os, "\n{:-^91}", "-");
    fmt::print(os,
               "\n{0:>2} | {1:>15} | {2:>13} | {3:>6} | {4:>15} | {5:>8} | {6:>8} | {7:>3}\n",
               "#",
               "line_addr",
               "page",
               "offset",
               "stored_deltas",
               "last_dpt",
               "num_used",
               "mru");
    fmt::print(os, "{:-^91}\n", "-");

    int dhb_entry_num = 0;
    // Loop through all entries in DHB
    for (std::map<archx::PhysAddr, DHBEntry>::const_iterator it = delta_history_buffer.begin();
         it != delta_history_buffer.end();
         it++)
    {
        fmt::print(os,
                   "{0:>2} | {1:>15} | {2:>13} | {3:>6} |",
                   dhb_entry_num++,
                   it->second.line_addr,
                   it->first,
                   it->second.offset);

        auto size = it->second.deltas.data.size();

        // Loop through all deltas in each DHB entry
        for (uint i = 0; i < size; i++) {
            fmt::print(os, "{0:>4x}", it->second.deltas.data[size - i - 1]);
        }

        fmt::print(os,
                   " | {0:>8} | {1:>8} | {2:>3}\n",
                   it->second.last_predictor,
                   it->second.num_times_page_was_touched,
                   it->second.mru);
    }
    fmt::print(os, "{:-^91}\n", "-");

    /////////////////////////////////////////////////////////////////////////////////
    // PRINT CONTENTS OF OFFSET PREDICTION TABLE
    /////////////////////////////////////////////////////////////////////////////////
    fmt::print(os, "\n{:^92}\n", "OFFSET PREDICTION TABLE");

    int opt_entry_num = 0;
    int col = 4;

    for (int i = 0; i < col; i++) {
        fmt::print(os, "{:-^15}        ", "-");
    }
    fmt::print(os, "{}", "\n");
    for (int i = 0; i < col; i++) {
        fmt::print(os, "{0:>2} | {1:>4} | {2:>3}        ", "#", "pred", "acc");
    }
    fmt::print(os, "{}", "\n");
    for (int i = 0; i < col; i++) {
        fmt::print(os, "{:-^15}        ", "-");
    }
    fmt::print(os, "{}", "\n");
    for (int i = 0; i < num_opt_entries / col; i++) {
        for (int j = 0; j < col; j++) {
            opt_entry_num = i + j * (num_opt_entries / col);
            OPTEntry opt_entry = offset_prediction_table[opt_entry_num];
            fmt::print(os,
                       "{0:>2x} | {1:>4} | {2:>3}        ",
                       opt_entry_num++,
                       opt_entry.predicted_delta,
                       opt_entry.accuracy);
        }
        fmt::print(os, "{}", "\n");
    }
    for (int i = 0; i < col; i++) {
        fmt::print(os, "{:-^15}        ", "-");
    }
    fmt::print(os, "{}", "\n");

    /////////////////////////////////////////////////////////////////////////////////
    // PRINT CONTENTS OF DELTA PREDICTION TABLES
    /////////////////////////////////////////////////////////////////////////////////
    std::vector<int> dpt_entry_num;
    dpt_entry_num.resize(delta_prediction_tables.size());

    for (uint table_num = 0; table_num < delta_prediction_tables.size(); table_num++) {
        dpt_entry_num[table_num] = 0;

        // fmt::print(os, "\n{0:^48}", "DELTA PREDICTION TABLE\n", table_num);
        fmt::print(os, "\n{:^48}", "DELTA PREDICTION TABLE");
        fmt::print(os, "\n{:-^48}", "-");
        fmt::print(os,
                   "\n{0:>3} | {1:>11} | {2:>4} | {3:>4} | {4:>8} | {5:>3}\n",
                   "#",
                   "deltas",
                   "pred",
                   "acc",
                   "num_used",
                   "mru");
        fmt::print(os, "{:-^48}\n", "-");

        // Loop through all entries in DPT
        for (DPTEntry dpt_entry : delta_prediction_tables[table_num]) {
            fmt::print(os, "{0:>3} |", dpt_entry_num[table_num]++);

            auto size = dpt_entry.deltas.data.size();
            auto spacing = num_dpt_levels - size;

            // Silly way to align the column width with the header width
            for (uint i = 0; i < spacing; i++) {
                fmt::print(os, "{0: >4}", " ");
            }

            // Loop through all deltas in each DPT entry
            for (uint i = 0; i < size; i++) {
                fmt::print(os, "{0:>4x}", dpt_entry.deltas.data[size - i - 1]);
            }

            fmt::print(os,
                       " | {0:>4x} | {1:>4} | {2:>8} | {3:>3}\n",
                       dpt_entry.predicted_delta,
                       dpt_entry.accuracy,
                       dpt_entry.num_times_used,
                       dpt_entry.mru);
        }

        fmt::print(os, "{:-^48}\n", "-");
    }

} // L2P::VLDP:table_print()

void L2P_VLDP::table_print()
{
    table_print(std::cout);
};

void L2P_VLDP::dump(std::ostream& os) const
{
    fmt::print(os, "\n{:=^113}\n", " L2P_VLDP ");

    fmt::print(os, "\nCONFIGURATION:");
    fmt::print(os, "\n==============\n");
    fmt::print(os, "num_dhb_entries    = {}\n", num_dhb_entries);
    if (has_opt)
        fmt::print(os, "num_opt_entries    = {}\n", num_opt_entries);
    fmt::print(os, "num_dpt_entries    = {}\n", num_dpt_entries);
    fmt::print(os, "num_dpt_levels     = {}\n", num_dpt_levels);
    fmt::print(os, "num_deltas_tracked = {}\n", num_deltas_tracked);
    fmt::print(os, "max_pref_degree    = {}\n", max_pref_degree);
    fmt::print(os, "max_dpt_accuracy   = {}\n", max_dpt_accuracy);

    // fmt::print(os, "num_pref_issued    = {}\n", num_pref_issued);
    // fmt::print(os, "num_pref_hit       = {}\n", num_pref_hit);

    int dhb_entry_size = 52 + 6 + (num_deltas_tracked) * 6 + ceil(log2(num_dpt_levels.val())) + 2 + 1;
    float dhb_size = num_dhb_entries * dhb_entry_size;

    int opt_entry_size = 6 + 6 + 1;
    float opt_size = 0;
    if (has_opt)
        opt_size = num_opt_entries * opt_entry_size;

    std::vector<float> dpt_size;
    dpt_size.resize(num_dpt_levels);

    float total_pref_size = dhb_size + opt_size;

    for (int i = 0; i < num_dpt_levels; i++) {
        int dpt_entry_size = (i + 1) * 6 + 6 + 2 + 2 + 1;
        dpt_size[i] = num_dpt_entries * dpt_entry_size;
        total_pref_size += dpt_size[i];
    }

    fmt::print(os, "\nTABLE SIZES:");
    fmt::print(os, "\n============\n");
    fmt::print(os, "DHB size   = {0:>5} bits = {1} Kb \n", dhb_size, dhb_size / 1024);

    if (has_opt)
        fmt::print(os, "OPT size   = {0:>5} bits = {1} Kb\n", opt_size, opt_size / 1024);

    for (int i = 0; i < num_dpt_levels; i++)
        fmt::print(os, "DPT{0} size  = {1:>5} bits = {2} Kb\n", i, dpt_size[i], dpt_size[i] / 1024);

    fmt::print(os, "TOTAL size = {0:>5} bits = {1} Kb\n", total_pref_size, total_pref_size / 1024);

    table_print(os);

    fmt::print(os, "\n{:=^113}\n", "=");

} // L2P_VLDP::dump()

} // namespace ampere::core
