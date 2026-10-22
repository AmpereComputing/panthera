//
// Copyright (c) 2019 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//

#ifndef AMPERE_CORE_L2P_VLDP_H
#define AMPERE_CORE_L2P_VLDP_H

#include "ampere/core/prefetcher.h"
#include "ampere/sc/sc.h"
#include "ampere/sc/submodule.h"

namespace ampere::core {

using namespace ampere::sc;

class L2P_VLDP : public Prefetcher<MemReq>, implementation<L2P_VLDP, Prefetcher<MemReq>>
{
public:
    explicit L2P_VLDP(sc_core::sc_module_name name);

    // ****************************************************************
    // Parameters
    // ****************************************************************
    Param<int> num_dpt_levels{"num_dpt_levels", "Number of levels for DPT", 3};
    Param<int> num_deltas_tracked{"num_deltas_tracked", "Number of deltas tracked per page", num_dpt_levels + 1};
    Param<uint> num_pfb_entries{"num_pfb_entries", "Number of entries in PFB", 32};
    Param<uint> num_dhb_entries{"num_dhb_entries", "Number of entries in DHB", 64};
    Param<uint> num_dpt_entries{"num_dpt_entries", "Number of entries in DPT", 32};
    Param<int> num_opt_entries{"num_opt_entries", "Number of entries in OPT", 64};
    Param<int> max_dpt_accuracy{"max_dpt_accuracy", "Max possible value for DPT accuracy", 4};
    Param<int> max_pref_degree{"max_pref_degree", "Max number of prefetches that can be issued in a cycle", 4};
    Param<int> has_opt{"has_opt", "Include OPT for more perf", 1};

    // ****************************************************************
    // Variables
    // ****************************************************************
    archx::PhysAddr phys_addr;
    archx::PhysAddr line_addr;
    archx::PhysAddr page_number;
    archx::PhysAddr page_offset;

    // Used for debug
    archx::PhysAddr pref_line_addr;
    int num_pref_issued = 0;
    int num_pref_hit = 0;

    // ****************************************************************
    // Structures
    // ****************************************************************
    // Structure declarations
    struct DeltaVector;
    struct DHBEntry;
    struct OPTEntry;
    struct DPTEntry;
    struct PFBEntry;

    // Table definitions
    std::map<archx::PhysAddr, DHBEntry> delta_history_buffer;
    std::vector<OPTEntry> offset_prediction_table;
    std::vector<std::deque<DPTEntry>> delta_prediction_tables;
    std::map<archx::PhysAddr, PFBEntry> prefetch_buffer;

    // ****************************************************************
    // Functions
    // ****************************************************************
    DHBEntry* lookup_dhb(archx::PhysAddr page_number);
    DHBEntry* update_dhb(archx::PhysAddr page_number, archx::PhysAddr page_offset, archx::PhysAddr line_addr);
    int lookup_opt(int page_offset);
    void update_opt(int page_offset, int dhb_delta);
    std::pair<std::pair<bool, int>, DPTEntry*> lookup_dpts(int table_num,
                                                           bool lookup_all_tables,
                                                           bool prefetch,
                                                           DeltaVector dhb_deltas);
    void update_dpt(DeltaVector dhb_deltas, int last_predictor, int num_times_page_was_touched);
    void add_dpt_entry(DeltaVector dhb_deltas, int table_num);
    void update_dhb_mru();
    void update_dpt_mru(int);
    void table_print(std::ostream& os) const;
    void table_print();
    void dump(std::ostream& os) const override;

protected:
    void train_impl(MemReq req, bool hit, bool prefetch, bool demand) override;

}; // class L2P_VLDP

// Vector of Deltas with a shift-register-like function for updating delta values
struct L2P_VLDP::DeltaVector
{
    std::vector<int> data;

    // Init vector for deltas that is sized and initialized to zeros
    static DeltaVector init(int vector_length)
    {
        DeltaVector init_vector;
        init_vector.data.resize(vector_length);

        for (int i = 0; i < vector_length; i += 1) {
            init_vector.data[i] = 0;
        }

        return init_vector;
    }

    // Logical shift-left register used for adding new deltas
    DeltaVector shift_left(DeltaVector input_vector, int new_delta, int vector_length)
    {
        DeltaVector shifted_vector;
        shifted_vector.data.resize(vector_length);

        // Shift left only if there is a non-zero delta to add to the vector
        if (new_delta != 0) {
            shifted_vector.data[0] = new_delta;

            for (int i = 0; i < vector_length - 1; i += 1) {
                shifted_vector.data[i + 1] = input_vector.data[i];
            }
        } else {
            shifted_vector = input_vector;
        }

        return shifted_vector;
    }
};

// Delta History Buffer Entry
struct L2P_VLDP::DHBEntry
{
    archx::PhysAddr offset{0};         // Page offset of the last address accessed in this page (6 bits)
    DeltaVector deltas;                // Sequence of recently observed deltas (num_deltas_tracked * 6 bits)
    archx::PhysAddr line_addr{0};      // Line address (for debug only)
    int last_predictor{0};             // DPT level used for the latest delta prediction (log2(num_dpt_levels) bits)
    int num_times_page_was_touched{1}; // Number of times this page has been touched (2 bits)
    int mru{1};                        // MRU bit (1 bit)
}; // (48 + 6 + (num_dpt_levels+1)*6 + log2(num_dpt_levels) + 2 + 1) bits
   // If num_dpt_levels = 2, each DHB entry has (52 + 6 + 3*6 + 2 + 2 + 1) = 81 bits
   // If num_dpt_levels = 3, each DHB entry has (52 + 6 + 4*6 + 2 + 2 + 1) = 87 bits
   // If num_dpt_levels = 4, each DHB entry has (52 + 6 + 5*6 + 2 + 2 + 1) = 93 bits

// Offset Prediction Table Entry
struct L2P_VLDP::OPTEntry
{
    archx::PhysAddr offset{0}; // Page offset from the first lookup to the page (6 bits)
    int predicted_delta{0};    // Predicted next offset (6 bits)
    int accuracy{0};           // Accuracy (1 bit)
}; // (6 + 6 + 1) = 13 bits

// Delta Prediction Table Entry
struct L2P_VLDP::DPTEntry
{
    DeltaVector deltas;     // Sequence of stored deltas ((dpt_level+1) * 6 bits)
    int predicted_delta{0}; // Next predicted delta (6 bits)
    int accuracy{0};        // Confidence on the delta prediction (2 bits)
    int num_times_used{0};  // Number of times this entry has been accessed (2 bits)
    int mru{1};             // MRU bit (1 bit)
}; // ((dpt_level+1)*6 + 6 + 2 + 2 + 1) bits
   // For dpt_level = 0, each DPT entry has (1*6 + 6 + 2 + 2 + 1) = 17 bits
   // For dpt_level = 1, each DPT entry has (2*6 + 6 + 2 + 2 + 1) = 23 bits
   // For dpt_level = 2, each DPT entry has (3*6 + 6 + 2 + 2 + 1) = 29 bits
   // For dpt_level = 3, each DPT entry has (4*6 + 6 + 2 + 2 + 1) = 35 bits

// Prefetch Buffer Entry (for debug only)
struct L2P_VLDP::PFBEntry
{
    archx::PhysAddr pae_line_addr{0};
    archx::PhysAddr pref_page{0};
    int pref_delta{0};
    Cycle pref_cycle{0};
    Cycle hit_cycle{0};
    std::string pref_provider{"DPT"};
    bool pref_hit{false};
};

} // namespace ampere::core

#endif // AMPERE_CORE_L2P_VLDP_H

/*
 * vim: sw=4:ts=4:et:si:cindent:cino=\:0,(0,g0,t0:
 */
