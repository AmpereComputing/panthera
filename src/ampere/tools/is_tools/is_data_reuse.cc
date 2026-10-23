//
// Copyright (c) 2020 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//
#include <fcntl.h>
#include <iostream>
#include <sys/types.h>
#include <unistd.h>

#include "ampere/archx/instruction_is.h"
#include "ampere/archx/translation.h"
#include "ampere/archx/sysreg.h"
#include "ampere/instruction_stream/instruction_stream.h"
#include "chip-decoder-map.h"

using namespace ampere;
using namespace ampere::is;

// Hacked version of is_validate.cc for analysis of data reuse
int main(int argc, char* argv[])
{
    int optll_size = 100;
    int optsl_size = 100;
    int option;
    while ((option = getopt(argc, argv, ":l:s:h")) != -1) {
        switch (option) {
        case 'l':
            optll_size = atoi(optarg);
            break;
        case 's':
            optsl_size = atoi(optarg);
            break;
        case 'h':
            std::cout << "Usage: is_data_reuse {-l size -s size} <instruction_stream>" << std::endl;
            return 0;
            break;
        }
    }

    if (optind == argc) {
        std::cout << "instruction stream required." << std::endl;
        return 0;
    }

    uint64_t instruction_count = 0;
    uint64_t ll_size = (uint64_t)optll_size;
    uint64_t sl_size = (uint64_t)optsl_size;

    // Brets stuff
    std::deque<Instruction> load_list = {};  // stack of whole load instructions
    std::deque<Instruction> store_list = {}; // stack of whole store instructions

    struct minfo
    {
        uint64_t count, iid, mid;
    }; // count of hits to PA, instruction num, load/store count-num
    std::unordered_map<uint64_t, minfo> load_hash = {};
    std::unordered_map<uint64_t, minfo> store_hash = {};
    std::unordered_map<uint64_t, uint64_t> ll_idist = {}; // histograms - instruction dist, count
    std::unordered_map<uint64_t, uint64_t> ls_idist = {};
    std::unordered_map<uint64_t, uint64_t> ll_ldist = {}; // load distance, count
    std::unordered_map<uint64_t, uint64_t> ls_sdist = {}; // store distance, count
    uint64_t ll_hits{0};
    uint64_t ls_hits{0};
    uint64_t sl_hits{0}; // don't care about stores that hit loads, but count to keep it symmetric
    uint64_t ss_hits{0};
    uint64_t load_count{0};
    uint64_t store_count{0};
    // uint64_t last_load{0};
    // uint64_t last_store{0};

    std::vector<std::string> decoder_names = is_tools::get_decoders("max");
    archx::Instruction::set_decoders(decoder_names);
    archx::ArchState state;
    InstructionStreamReader reader(argv[optind]);
    Event event;

    while (reader.read(event)) {
        if (event.has_instruction()) { // while there are instructions to be processed
            instruction_count++;
            Instruction instr = event.instruction(); // grab instruction and setup the decode
            archx::InstructionIS is_instr(instr, archx::inum_t(instruction_count), archx::inum_t(0), state);

            // Mem instruction only - and be srue there is at least one memop included
            if ((is_instr.is_load() || is_instr.is_store()) && !is_instr.is_barrier() && !is_instr.is_clrex()) {
                //
                //  **LOAD**
                // Handle the valid va & pa if this is a load (and has at least 1 memop)
                if (is_instr.is_load() && (instr.memop_size() > 0)) {
                    load_count++;
                    // add instruction for this load to the ordered list
                    load_list.push_front(instr);

                    uint64_t prevload = 0;   // for tracking which prior load this hit if any
                    uint64_t prevloadln = 0; // load number of previously hit load
                    uint64_t pa = load_list.front().memop(0).pa();
                    // if the PA is not in the list, add it
                    auto hinfo = load_hash.find(pa);
                    if (hinfo == load_hash.end()) { // PA not on list so add it
                        minfo minst = {1, instruction_count, load_count};
                        load_hash.emplace(pa, minst);
                    } else {                            // PA is there so increment count
                        prevload = hinfo->second.iid;   // capture previous inst id for load that was hit
                        prevloadln = hinfo->second.mid; // and load number
                        hinfo->second.count++;
                        // std::cout << "hit count " << hinfo->second.count << " prior iid " << hinfo->second.iid << "
                        // prior mid " << hinfo->second.mid << std::endl;
                        hinfo->second.iid = instruction_count;
                        hinfo->second.mid = load_count;
                    }

                    if (load_list.size() > (ll_size)) { // one too many, so remove the oldest one from the list
                        // first decrement the count in the hash then remove if it was the last
                        auto hinfo = load_hash.find(load_list.back().memop(0).pa());
                        hinfo->second.count--;
                        // std::cout << "decremented to count " << hinfo->second.count << " iid " << hinfo->second.iid
                        // << " mid " << hinfo->second.mid << std::endl;
                        if (hinfo->second.count == 0) {
                            // here we need to remove the PA from the load_hash
                            // std::cout << "removing has for pa " << hinfo->first << " iid " << hinfo->second.iid << "
                            // mid " << hinfo->second.mid << std::endl;
                            load_hash.extract(hinfo->first);
                        }
                        // now remove from the list
                        load_list.pop_back();
                    }

                    // check if this load hits a prior store or load and which is most recent
                    //  Always track the most recent store if the load hit a store
                    //  Only track the most recent hit load if it is younger than any hit store
                    //  (in SLL sequence, count the second load as both hitting the store and the prior load)
                    auto sinfo = store_hash.find(pa);
                    uint64_t store_inum = 0;         // instr count of hit store - 0 if no hit
                    uint64_t store_snum = 0;         // store count of hit store - 0 if no hit
                    if (sinfo != store_hash.end()) { // load PA is on the store list, so track LS hit
                        ls_hits++;
                        // update LS histogram - increment distance or add if not there
                        uint64_t dist = instruction_count - sinfo->second.iid;
                        store_inum = sinfo->second.iid;
                        auto lsid = ls_idist.find(dist);
                        if (lsid != ls_idist.end()) { // this distance has occurred previously
                            lsid->second++;
                        } else {
                            ls_idist.emplace(dist, 1);
                        }
                        // do the same for the store distance histogram
                        // store_snum is store number of hit store - store_count is number of most recent store
                        store_snum = sinfo->second.mid;
                        uint64_t sdist = store_count - store_snum + 1;
                        auto lssd = ls_sdist.find(sdist);
                        if (lssd != ls_sdist.end()) { // this distance has occurred previously
                            lssd->second++;
                        } else {
                            ls_sdist.emplace(sdist, 1);
                        }
                    }
                    // check if this load hit a prior load, but only track if there was no store in between
                    if (prevload > 0) { // the load hit a previous load
                        if (prevload
                            > store_inum) { // either store wasn't hit (store_inum=0) or hit load is younger than store
                            ll_hits++;
                            // update LL instruction histogram - increment distance or add if not there
                            uint64_t dist = instruction_count - prevload;
                            auto llid = ll_idist.find(dist);
                            if (llid != ll_idist.end()) { // this distance has occurred previously
                                llid->second++;
                            } else {
                                ll_idist.emplace(dist, 1);
                            }
                            // update LL load distance histogram
                            uint64_t ldist = load_count - prevloadln; // distance from this load to previously hit load
                            auto llld = ll_ldist.find(ldist);
                            if (llld != ll_ldist.end()) { // this distance has occurred previously
                                llld->second++;
                            } else {
                                ll_ldist.emplace(ldist, 1);
                            }
                        }
                    }

                    // capture the last PA, just to see if I can.
                    // last_load = pa;
                }

                //
                // **STORE**
                // Do the same if this is a store
                if (is_instr.is_store() && (instr.memop_size() > 0)) {
                    store_count++;
                    // add instruction for this store to the ordered list
                    store_list.push_front(instr);

                    uint64_t prevstore = 0; // for tracking which prior store this hit if any
                    uint64_t pa = store_list.front().memop(0).pa();
                    // if the PA is not in the list, add it
                    auto hinfo = store_hash.find(pa);
                    if (hinfo == store_hash.end()) { // PA not on list so add it
                        minfo minst = {1, instruction_count, store_count};
                        store_hash.emplace(pa, minst);
                    } else {                           // PA is there so increment count
                        prevstore = hinfo->second.iid; // capture previous inst id for store that was hit
                        hinfo->second.count++;
                        // std::cout << "hit count " << hinfo->second.count << " prior iid " << hinfo->second.iid << "
                        // prior mid " << hinfo->second.mid << std::endl;
                        hinfo->second.iid = instruction_count;
                        hinfo->second.mid = store_count;
                    }

                    if (store_list.size() > (sl_size)) { // one too many, so remove the oldest one from the list
                        // first decrement the count in the hash then remove if it was the last
                        auto hinfo = store_hash.find(store_list.back().memop(0).pa());
                        hinfo->second.count--;
                        // std::cout << "decremented to count " << hinfo->second.count << " iid " << hinfo->second.iid
                        // << " mid " << hinfo->second.mid << std::endl;
                        if (hinfo->second.count == 0) {
                            // here we need to remove the PA from the store_hash
                            // std::cout << "removing has for pa " << hinfo->first << " iid " << hinfo->second.iid << "
                            // mid " << hinfo->second.mid << std::endl;
                            store_hash.extract(hinfo->first);
                        }
                        // now remove from the list
                        store_list.pop_back();
                    }

                    // check if this store hits a prior store or load and which is most recent
                    auto linfo = load_hash.find(pa);
                    if (linfo
                        != load_hash
                               .end()) { // store PA is on the load list, so figure if store or load was more recent
                        if (linfo->second.iid
                            > prevstore) { // hit load is younger than hit store or there wasn't a store hit
                            sl_hits++;
                        } else if (prevstore > 0) { // if store was hit it must be younger than the load hit
                            ss_hits++;
                        }
                    } else if (prevstore > 0) { // didn't hit load, but did hit a store
                        ss_hits++;
                    }

                    // capture the last PA, just to see if I can.
                    // last_store = pa;
                }
            }
        }
        event.Clear();
    }

    std::cout << instruction_count << " total instructions" << std::endl;
    std::cout << load_count << " load instructions" << std::endl;
    std::cout << store_count << " store instructions" << std::endl;
    std::cout << ls_hits << " loads hit prior stores within buffer" << std::endl;
    std::cout << ll_hits << " loads hit prior loads within buffer" << std::endl;
    std::cout << ss_hits << " stores hit prior stores within buffer" << std::endl;
    // std::cout << sl_hits << " stores hit prior loads that were in list (not that anyone cares)" << std::endl;
    // std::cout << last_load << " is PA of last load in this trace" << std::endl;
    // std::cout << last_store << " is PA of last store in this trace" << std::endl;
    std::cout << load_list.size() << " is the load list size" << std::endl;
    std::cout << store_list.size() << " is the store list size" << std::endl;
    // std::cout << load_list.size() << " " << load_hash.size() << " size of load_list/load_hash" << std::endl;
    // std::cout << store_list.size() << " " << store_hash.size() << " size of store_list/store_hash"<< std::endl;

    // just for visual inspection, try printing them out in pairs
    // uint64_t checksum=0;
    // for (const auto &[key, count] : ls_idist) {
    //     std::cout << key << ", " << count << std::endl;
    //     checksum = checksum + count;
    // }
    // std::cout << "checksum is " << checksum << " ls_hits is " << ils_hits << std::endl;

    std::cout << "Store to load - instruction distance histogram" << std::endl;
    uint64_t counter = 0;
    // for (const auto &[key, count] : ls_idist) { //row of keys
    for (const auto& item : ls_idist) { // row of keys
        counter++;
        if (counter < ls_idist.size())
            std::cout << item.first << ",";
        else
            std::cout << item.first << std::endl;
    }
    counter = 0;
    for (const auto& item : ls_idist) { // row of counts
        counter++;
        if (counter < ls_idist.size())
            std::cout << item.second << ",";
        else
            std::cout << item.second << std::endl;
    }

    std::cout << "Store to load - Store distance histogram" << std::endl;
    counter = 0;
    for (const auto& item : ls_sdist) { // row of keys
        counter++;
        if (counter < ls_sdist.size())
            std::cout << item.first << ",";
        else
            std::cout << item.first << std::endl;
    }
    counter = 0;
    for (const auto& item : ls_sdist) { // row of counts
        counter++;
        if (counter < ls_sdist.size())
            std::cout << item.second << ",";
        else
            std::cout << item.second << std::endl;
    }

    std::cout << "Load to load - instruction distance histogram" << std::endl;
    counter = 0;
    for (const auto& item : ll_idist) { // row of keys
        counter++;
        if (counter < ll_idist.size())
            std::cout << item.first << ",";
        else
            std::cout << item.first << std::endl;
    }
    counter = 0;
    for (const auto& item : ll_idist) { // row of counts
        counter++;
        if (counter < ll_idist.size())
            std::cout << item.second << ",";
        else
            std::cout << item.second << std::endl;
    }

    std::cout << "Load to load - load distance histogram" << std::endl;
    counter = 0;
    for (const auto& item : ll_ldist) { // row of keys
        counter++;
        if (counter < ll_ldist.size())
            std::cout << item.first << ",";
        else
            std::cout << item.first << std::endl;
    }
    counter = 0;
    for (const auto& item : ll_ldist) { // row of counts
        counter++;
        if (counter < ll_ldist.size())
            std::cout << item.second << ",";
        else
            std::cout << item.second << std::endl;
    }

    while (load_list.size() > 0) {
        // print out PA's for all the loads still on the list at the end of trace
        // std::cout << "PA is " << load_list.front().memop(0).pa() << std::endl;
        load_list.pop_front();
    }

    while (store_list.size() > 0) {
        store_list.pop_front();
    }

} // End of main

/*
 * vim: sw=4:ts=4:et:si:cindent:cino=\:0,(0,g0,t0:
 */
