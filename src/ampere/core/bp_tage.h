//
// Copyright (c) 2019-2025 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//

#pragma once

#include <nlohmann/json.hpp>

#include "ampere/core/bp_bimodal.h"
#include "ampere/core/bp_global.h"
#include "ampere/core/bp_rsb.h"
#include "ampere/core/btb.h"
#include "ampere/core/predictor.h"

namespace ampere::core {

using json = nlohmann::json;

using namespace ampere::sc;
using namespace ampere::archx;

class BP_TAGE : public Module, public Predictor
{
public:
    explicit BP_TAGE(const sc_core::sc_module_name& name);
    explicit BP_TAGE(const sc_core::sc_module_name& name, size_t bank_bits);

    Param<size_t> num_globals{"num_globals", "number of global predictor arrays", 1};
    Param<size_t> global_sets{"global_sets", "number of set in a global array", 1024};
    Param<size_t> global_ways{"global_ways", "number of ways in a global set", 2};
    Param<size_t> global_tag_bits{"global_tag_bits", "number of bits in a global tag", 8};
    Param<size_t> globals_per_alt_counter{
        "globals_per_alt_counter", "number of global arrays which share an alternate counter", 4};

    Param<size_t> num_iglobals{"num_iglobals", "number of indirect global predictor arrays", 1};
    Param<size_t> iglobal_sets{"iglobal_sets", "number of set in an indirect global array", 256};
    Param<size_t> iglobal_ways{"iglobal_ways", "number of ways in an indirect global set", 2};
    Param<size_t> iglobal_tag_bits{"iglobal_tag_bits", "number of bits in an indirect global tag", 12};
    Param<size_t> iglobals_per_alt_counter{
        "iglobals_per_alt_counter", "number of indirect global arrays which share an alternate counter", 4};
    Param<bool> bank_iglobals{"bank_iglobals", "Bank the ITTAGE global arrays", false};

    Param<std::string> useful_policy_param{"useful_policy",
                                           "policy for useful bit management "
                                           "(hard_reset, soft_reset, force_allocate_1, force_allocate_n)",
                                           "force_allocate_1"};

    Param<int> useful_high_water{
        "useful_high_water", "threshold to begin resetting useful bits (policy: hard or soft reset)", 1024};

    Param<int> useful_low_water{"useful_low_water", "threshold to begin setting useful bits (policy: soft reset)", 256};

    Param<bool> update_useful_on_strong_alternate{"update_useful_on_strong_alternate",
                                                  "require an update cycle to reset the primary's useful bit if "
                                                  "the alternate was correct and strong",
                                                  false};

    Param<bool> elide_useful_bit_update{
        "elide_useful_bit_update", "don't bother resetting the useful bit if it was already 0", false};

    Param<bool> select_rotation_with_history{
        "select_rotation_with_history", "select global rotation using history rather than previous PC", false};
    Param<bool> update_alternate_bimodals{
        "update_alternate_bimodals", "Update the bimodal arrays when they are the alternate provider", true};

    void predict(Fetch fetch, BranchHistory& history, const BranchContext& context) override;
    void train(const BranchInfo& br_info, BranchHistory& history, const BranchContext& context) override;
    bool train_bimodal_only(const BranchInfo& br_info);
    void recover(const Flush& flush_info,
                 BranchInfo* br_info,
                 BranchHistory& history,
                 const BranchContext& context) override;

    void update_btb_lru(const BranchInfo& br_info, const BranchContext& context) { btb.update_lru(br_info, context); }
    void update_rsb(const BranchInfo& br_info, BranchHistory& history, const BranchContext& context);
    void flush_rsb(Fetch fetch);
    bool bimodal_needs_update(const BranchInfo& br_info);

    void predict_inst(Fetch fetch, BranchHistory& history, const BranchContext& context) override;

    void predict_tage(Fetch fetch, BranchHistory& history, const BranchContext& context);
    void predict_ittage(Fetch fetch, BranchHistory& history, const BranchContext& context);

    void commit_branch(const BranchInfo& br_info);

    bool primary_needs_update(const BranchInfo& br_info) const;
    bool alternate_needs_update(const BranchInfo& br_info) const;

    bool rsb_is_full();
    void clear_rsb();

    std::vector<unsigned> get_conditional_histories();
    std::vector<unsigned> get_indirect_histories();

    void save_state(json& j, const BranchContext& context);
    void restore_state(const json& j, BranchContext& context);
    void save_state(const std::string filename, const BranchContext& context);
    void restore_state(const std::string filename, BranchContext& context);

private:
    enum class UsefulPolicy {
        HARD_RESET,
        SOFT_RESET,
        FORCE_ALLOCATE_1,
        FORCE_ALLOCATE_N
    };

    BPBiModal bi_modal;

    std::vector<BPGlobalArray> arrays{num_globals};
    std::vector<BPGlobalArray> iarrays{num_iglobals};

    std::vector<std::unique_ptr<BPGlobal>> global;
    std::vector<std::unique_ptr<BPGlobal>> iglobal;

    BTB btb;
    ReturnStack rsb{"rsb"};

    UsefulPolicy useful_policy{UsefulPolicy::HARD_RESET};
    int useful_water_line{0};
    bool useful_reset_mode{false};

    static Prediction resolve_earliest_taken_branch(ProgramCounter fetch_pc, const LinePrediction& line_prediction);

    void train_conditional_predictor(const BranchInfo& br_info, BranchHistory& history, const BranchContext& context);
    void train_indirect_predictor(const BranchInfo& br_info, BranchHistory& history, const BranchContext& context);

    int get_predictor_index(Predictor* predictor);

    void allocate_on_mispredict(std::vector<std::unique_ptr<BPGlobal>>& globals,
                                int index,
                                int max_alloc,
                                const BranchInfo& br_info,
                                BranchHistory& history,
                                const BranchContext& context);
};

} // namespace ampere::core

/*
 * vim: sw=4:ts=4:et:si:cindent:cino=\:0,(0,g0,t0:
 */
