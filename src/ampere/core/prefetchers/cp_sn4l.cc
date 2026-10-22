//
// Copyright (c) 2021 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//

#include "ampere/core/prefetchers/cp_sn4l.h"

namespace ampere::core {

using namespace std;

/*************************************************************************
 * Constructor for CP_SN4L
 *************************************************************************/
CP_SN4L::CP_SN4L(sc_core::sc_module_name name) : Prefetcher<Fetch>(name)
{
    seq_table.resize(seq_table_size);
    pc_bits = log2(seq_table_size.val());
    assert(seq_table_size == exp2(pc_bits) && "table size must be power of two");

    rlu = new RLUHistory(rlu_table_size);
}

/*************************************************************************
 * Calculates entry of the table for state update and prefetch triggering
 *************************************************************************/
unsigned CP_SN4L::calculate_index(archx::PhysAddr pa)
{
    // We want the line address for indexing, so
    // calculate index as: (pa >> 6) & (table_size - 1);
    unsigned lo_bit = 6;
    unsigned hi_bit = pc_bits + lo_bit - 1;
    unsigned index = pa.extract_range(hi_bit, lo_bit);

    assert(index < seq_table_size);
    return index;
}

/*************************************************************************
 * RLU History table struct and methods
 *************************************************************************/
CP_SN4L::RLUHistory::RLUHistory(unsigned size)
{
    table.resize(size);
}

void CP_SN4L::RLUHistory::insert(CP_SN4L::Entry entry)
{
    // erase from top and add one to end, so size is always constant
    table.erase(table.begin());
    table.push_back(entry);
}

bool CP_SN4L::RLUHistory::find(PhysAddr pa)
{
    for (const auto& e : table) {
        if (e.pa == pa) {
            return true;
        }
    }
    return false;
}

/*************************************************************************
 * run method called once per cycle by clocking subsystem. akin to the
 * cycle-operate function in the academic simulators
 *************************************************************************/
bool CP_SN4L::run()
{
    // nothing to do, just come back next cycle
    if (rlu_queue.empty())
        return true;

    // go through the rlu_queue and check for duplicates and erase them
    for (auto it = rlu_queue.begin(); it != rlu_queue.end();) {
        if (rlu->find(it->pa)) {
            it = rlu_queue.erase(it);
        } else {
            ++it;
        }
    }

    // now try to launch the unique addresses
    for (auto it = rlu_queue.begin(); it != rlu_queue.end();) {
        Entry e = *it;
        if (prefetch_request->can_put()) {
            // craft prefetch request and write to port
            auto pf_target = make_child<Fetch>(e.fetch, e.pc, 0);
            prefetch_request->put(pf_target);
            ev_trigger(pf_target, e.pc);

            // insert entry into the recent look-up table
            rlu->insert(e);

            // and remove from rlu_queue
            it = rlu_queue.erase(it);

        } else {
            ++it;
        }
    }

    return true;
}

/*************************************************************************
 * Train function, called by ICF when training should occur.
 *  req: non-prefetch request which caused the lookup that initiated training
 *  prefetch: indicates line hit by lookup was installed by a prefetch
 *  demand: indicates request came in from demand fetch
 *************************************************************************/
void CP_SN4L::train_impl(Fetch req, bool hit, bool prefetch, bool demand)
{
    // extract addresses
    PhysAddr train_addr = *req->phys_addr;
    ProgramCounter pc_line = req->cache_line();
    ev_train(req, pc_line, train_addr, ev_hitmiss(hit), ev_prefetch(prefetch));

    // if we touch a block, that means the block is "demanded" and SNL status
    // bit should be set, which means that SNL prefetcher should be ready to
    // prefetch it when it is triggered next time around
    unsigned index = calculate_index(train_addr);
    seq_table[index] = make_pair(pc_line, true);

    // on miss or prefetch hit
    if (!hit || prefetch) {
        // Lookup in seq_table for next possible addresses.
        // and if we have hits, put them in the rlu_queue

        // check status of the next N lines, from A+2 to A+N+1
        for (unsigned offset = 2; offset <= depth + 1; offset++) {
            if (seq_table[index + offset].second && rlu_queue.size() < rlu_max_qsize) {
                // offset bit is set, so begin prefetch attempt
                ProgramCounter new_pc = pc_line + offset * 0x40;
                PhysAddr new_pa = train_addr + offset * 0x40;

                // content match in rlu_queue to prevent duplicates
                bool found{false};
                for (auto e : rlu_queue) {
                    if (e.pc == new_pc) {
                        found = true;
                        break;
                    }
                }
                if (!found) {
                    // craft new prefetch candidate
                    Entry pf(req, new_pc, new_pa);
                    rlu_queue.push_back(pf);
                }
            }
        }
    }
}

/*************************************************************************
 * Evict function, called by ICF when line was evicted
 *  pa: physical address of evicted line
 *  prefetch: indicates line was installed by a prefetch
 *  demand: indicates line was used by a demand fetch
 *************************************************************************/
void CP_SN4L::evict_impl(PhysAddr pa, bool prefetch, bool demand)
{
    unsigned index = calculate_index(pa);

    // we can remove useless prefetches from the table here:
    // prefetched lines that were never hit by a demand load
    if (prefetch && !demand) {
        seq_table[index] = make_pair(ProgramCounter{0}, false);
    }
}

/*************************************************************************
 * Print out debug information
 *************************************************************************/
void CP_SN4L::table_print(std::ostream& os) const
{
    os << "= CP SN4L ==================================================\n";

    fmt::print(os, "\nrlu queue:\n");
    for (auto e : rlu_queue) {
        fmt::print(os, " PC: 0x{} PA:0x{}\n", e.pc, e.pa);
    };

    fmt::print(os, "\nrlu history:\n");
    for (auto e : rlu->table) {
        fmt::print(os, " PC: 0x{} PA:0x{}\n", e.pc, e.pa);
    }

    // print out the seq_table in groups of sixteen
    //
    // Take the four highest bits and use them for the columns,
    // remainder of bits will be for the rows. For example:
    //  1024 entries = 64 rows * 16 columns

    unsigned num_rows = 1 << (pc_bits - 4);

    fmt::print(os, "\nseq_table:\n\n");
    fmt::print(os, "   N ---> 0   1   2   3   4   5   6   7   8   9   a   b   c   d   e   f\n");
    fmt::print(os, "------+------------------------------------------------------------------\n");
    for (unsigned r = 0; r < num_rows; r++) {
        fmt::print(os,
                   "0xN{:2x} | {:3} {:3} {:3} {:3} {:3} {:3} {:3} {:3} {:3} {:3} {:3} {:3} {:3} {:3} {:3} {:3} \n",
                   r,
                   (int)seq_table[num_rows * 0x0 + r].second,
                   (int)seq_table[num_rows * 0x1 + r].second,
                   (int)seq_table[num_rows * 0x2 + r].second,
                   (int)seq_table[num_rows * 0x3 + r].second,
                   (int)seq_table[num_rows * 0x4 + r].second,
                   (int)seq_table[num_rows * 0x5 + r].second,
                   (int)seq_table[num_rows * 0x6 + r].second,
                   (int)seq_table[num_rows * 0x7 + r].second,
                   (int)seq_table[num_rows * 0x8 + r].second,
                   (int)seq_table[num_rows * 0x9 + r].second,
                   (int)seq_table[num_rows * 0xa + r].second,
                   (int)seq_table[num_rows * 0xb + r].second,
                   (int)seq_table[num_rows * 0xc + r].second,
                   (int)seq_table[num_rows * 0xd + r].second,
                   (int)seq_table[num_rows * 0xe + r].second,
                   (int)seq_table[num_rows * 0xf + r].second);
    }

    vector<unsigned> counts;
    counts.resize(2);
    for (auto e : seq_table)
        counts[(unsigned)e.second]++;

    fmt::print(os,
               "\nseq_table total entries (0, 1): ({}, {}), fraction set: {:.3f}\n\n",
               counts[0],
               counts[1],
               (float)counts[1] / (counts[0] + counts[1]));

    return;
}

void CP_SN4L::table_print()
{
    table_print(std::cout);
}
void CP_SN4L::dump(std::ostream& os) const
{
    table_print(os);
}

} // namespace ampere::core

/*
 * vim: sw=4:ts=4:et:si:cindent:cino=\:0,(0,g0,t0:
 */
