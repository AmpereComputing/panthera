//
// Copyright (c) 2019-2025 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//

#include "ampere/archx/flush_type.h"
#include "ampere/util/compressed_fstream.h"
#include "ampere/core/bp_tage.h"
#include "ampere/core/bob_entry.h"

namespace ampere::core {

std::string line_prediction_as_string(const LinePrediction& line_prediction);

std::pair<int, int> bits_for_rotation_select(size_t num_arrays);
size_t prev_pc_to_rotation(std::uint64_t index, size_t num_arrays, int spread);

template<typename GlobalType>
void build_globals(std::vector<std::unique_ptr<BPGlobal>>& global,
                   std::vector<BPGlobalArray>& physical_arrays,
                   size_t num_globals,
                   size_t sets,
                   size_t ways,
                   size_t tag_bits,
                   size_t globals_per_alt_counter,
                   const char* prefix,
                   bool select_rotation_with_history,
                   size_t bank_bits);

BP_TAGE::BP_TAGE(const sc_core::sc_module_name& name, size_t bank_bits)
    : Module{name}, bi_modal("bi_modal", bank_bits), btb("btb", bank_bits)
{
    build_globals<BPGlobal>(global,
                            arrays,
                            num_globals,
                            global_sets,
                            global_ways,
                            global_tag_bits,
                            globals_per_alt_counter,
                            "global",
                            select_rotation_with_history,
                            bank_bits);
    build_globals<BPGlobalIndirect>(iglobal,
                                    iarrays,
                                    num_iglobals,
                                    iglobal_sets,
                                    iglobal_ways,
                                    iglobal_tag_bits,
                                    iglobals_per_alt_counter,
                                    "iglobal",
                                    select_rotation_with_history,
                                    bank_iglobals ? bank_bits : 0);

    if (useful_policy_param.val() == "hard_reset")
        useful_policy = UsefulPolicy::HARD_RESET;
    else if (useful_policy_param.val() == "soft_reset")
        useful_policy = UsefulPolicy::SOFT_RESET;
    else if (useful_policy_param.val() == "force_allocate_1")
        useful_policy = UsefulPolicy::FORCE_ALLOCATE_1;
    else if (useful_policy_param.val() == "force_allocate_n")
        useful_policy = UsefulPolicy::FORCE_ALLOCATE_N;
    else
        AMPERE_ERROR("unknown useful policy {}", useful_policy_param.val());
}

BP_TAGE::BP_TAGE(const sc_core::sc_module_name& name) : BP_TAGE(name, 0) {}

template<typename GlobalType>
void build_globals(std::vector<std::unique_ptr<BPGlobal>>& global,
                   std::vector<BPGlobalArray>& physical_arrays,
                   size_t num_globals,
                   size_t sets,
                   size_t ways,
                   size_t tag_bits,
                   size_t globals_per_alt_counter,
                   const char* prefix,
                   bool select_rotation_with_history,
                   size_t bank_bits)
{
    for (auto& array : physical_arrays) {
        array.resize(sets);

        for (auto& set : array) {
            set.resize(ways);

            BPTag tag{0};

            for (auto& entry : set) {
                entry.tag = tag;
                tag = ~tag & util::mask(tag_bits);
            }

            set.back().most_recently_allocated = true;
        }
    }

    std::shared_ptr<UseAlternateCounter> counter;
    auto [bits, spread] = bits_for_rotation_select(num_globals);

    for (size_t i = 0; i < num_globals; i++) {
        auto gp = std::make_unique<GlobalType>(
            sc_core::sc_gen_unique_name(prefix), physical_arrays, util::bitsize(sets - 1), tag_bits, bank_bits);

        global.push_back(std::move(gp));

        RotationSelect select_rotation;

        if (num_globals == 1) {
            select_rotation = [](Fetch fetch, ProgramCounter pc, BranchHistory& history) -> size_t {
                return 0;
            };
        } else if (select_rotation_with_history) {
            select_rotation = [i, num_globals, bits = bits](
                                  Fetch fetch, ProgramCounter pc, BranchHistory& history) -> size_t {
                history.start_hashing(fetch->fetch_id);
                auto starting_array = history.hash(2, bits);
                return (starting_array + i) % num_globals;
            };
        } else {
            select_rotation = [i, num_globals, bits = bits, spread = spread](
                                  Fetch fetch, ProgramCounter pc, BranchHistory& history) -> size_t {
                auto starting_array =
                    prev_pc_to_rotation(fetch->prev_program_counter.extract_range(bits + 5, 6), num_globals, spread);
                return (starting_array + i) % num_globals;
            };
        }

        global[i]->set_rotation_select(select_rotation);

        if ((i % globals_per_alt_counter) == 0)
            counter = std::make_shared<UseAlternateCounter>();

        global[i]->set_use_alternate_counter(counter);
    }
}

void BP_TAGE::predict(Fetch fetch, BranchHistory& history, const BranchContext& context)
{
    btb.predict(fetch, history, context);
    bi_modal.predict(fetch, history, context);

    history.start_hashing(fetch->fetch_id);

    for (auto& g : global)
        g->predict(fetch, history, context);

    for (auto& g : iglobal)
        g->predict(fetch, history, context);

    fetch->update_prediction(resolve_earliest_taken_branch(fetch->program_counter, fetch->line_prediction));

    if (fetch->get_prediction().is_taken())
        rsb.predict(fetch, history, context);
}

void BP_TAGE::predict_tage(Fetch fetch, BranchHistory& history, const BranchContext& context)
{
    btb.predict(fetch, history, context);
    bi_modal.predict(fetch, history, context);

    history.start_hashing(fetch->fetch_id);

    for (auto& g : global)
        g->predict(fetch, history, context);

    fetch->update_prediction(resolve_earliest_taken_branch(fetch->program_counter, fetch->line_prediction));

    if (fetch->get_prediction().is_taken())
        rsb.predict(fetch, history, context);
}

void BP_TAGE::predict_ittage(Fetch fetch, BranchHistory& history, const BranchContext& context)
{
    history.start_hashing(fetch->fetch_id);

    for (auto& g : iglobal)
        g->predict(fetch, history, context);

    fetch->update_prediction(resolve_earliest_taken_branch(fetch->program_counter, fetch->line_prediction));
}

void BP_TAGE::predict_inst(Fetch fetch, BranchHistory& history, const BranchContext& context)
{
    btb.predict_inst(fetch, history, context);
    bi_modal.predict_inst(fetch, history, context);

    history.start_hashing(fetch->fetch_id);

    for (auto& g : global)
        g->predict_inst(fetch, history, context);

    resolve_earliest_taken_branch(fetch->program_counter, fetch->line_prediction);
}

Prediction BP_TAGE::resolve_earliest_taken_branch(ProgramCounter fetch_pc, const LinePrediction& line_prediction)
{
    for (size_t offset = fetch_pc.extract_range(5, 2); offset < line_prediction.size(); ++offset) {
        auto& p = line_prediction[offset];
        bool taken = p.use_alternate ? p.alternate.taken : p.primary.taken;

        if (is_unconditional(p.branch_type))
            assert(taken);

        if (is_branch(p.branch_type) && taken) {
            ProgramCounter branch_pc = fetch_pc.line_address() + offset * 4;
            std::optional<ProgramCounter> target = p.use_alternate ? p.alternate.target : p.primary.target;
            bool target_nextline = p.use_alternate ? p.alternate.target_nextline : p.primary.target_nextline;

            if (!target.has_value()) {
                target = branch_pc + 4;
                target_nextline = false;
            }

            return {p.branch_type, branch_pc, *target, target_nextline};
        }
    }

    return {};
}

bool BP_TAGE::train_bimodal_only(const BranchInfo& br_info)
{
    // Check if we should train the bimodal arrays. We attempt to train if this
    // is a conditional branch, and the array used to make the original
    // prediction was the bimodal array
    if (bimodal_needs_update(br_info)) {
        // return true if this is only a strengthening update
        bool primary_bimodal = (!br_info.slot.primary.predictor || br_info.slot.primary.predictor == &bi_modal);
        return bi_modal.strengthen(br_info, !primary_bimodal);
    }
    return true;
}

void BP_TAGE::train(const BranchInfo& br_info, BranchHistory& history, const BranchContext& context)
{
    btb.train(br_info, history, context);

    if (is_conditional(br_info.type)) {
        history.start_hashing(br_info.fetch->fetch_id);
        train_conditional_predictor(br_info, history, context);
    } else if (is_indirect(br_info.type)) {
        history.start_hashing(br_info.fetch->fetch_id);
        train_indirect_predictor(br_info, history, context);
    }

    if (is_call(br_info.type) || is_return(br_info.type))
        rsb.train(br_info, history, context);
}

void BP_TAGE::train_conditional_predictor(const BranchInfo& br_info,
                                          BranchHistory& history,
                                          const BranchContext& context)
{
    auto predictor = br_info.slot.primary.predictor;
    auto alt_predictor = br_info.slot.alternate.predictor;

    if (!predictor)
        predictor = &bi_modal;
    if (update_alternate_bimodals && !alt_predictor && predictor != &bi_modal)
        alt_predictor = &bi_modal;

    predictor->train(br_info, history, context);

    // Update the alternate predictor (but only update the bimodal when its the
    // alternate if the alternate was actually used to make the prediction)
    if (alt_predictor && (alt_predictor != &bi_modal || bimodal_needs_update(br_info)))
        alt_predictor->train_alt(br_info, history, context);

    if (br_info.mispredicted_direction) {
        bool primary_was_weak_correct =
            br_info.slot.primary.taken == br_info.taken && br_info.slot.primary.strength == BranchStrength::VERYWEAK;

        if (!primary_was_weak_correct) {
            int index = get_predictor_index(br_info.slot.primary.predictor);
            allocate_on_mispredict(global, index, 2, br_info, history, context);
        }
    }
}

/*
 * Return true if the bimodal array needs updating. Every conditional branch
 * which was predicted out of the bimodals (i.e. didn't hit in TAGE) must
 * update the bimodal arrays because the hysteresis(strength) bits are not read
 * at prediction-time.
 */
bool BP_TAGE::bimodal_needs_update(const BranchInfo& br_info)
{
    if (is_conditional(br_info.type)) {
        auto predictor = br_info.slot.use_alternate ? br_info.slot.alternate.predictor : br_info.slot.primary.predictor;
        if (!predictor)
            predictor = &bi_modal;

        AMPERE_ASSERT(!(br_info.slot.use_alternate
                        && (!br_info.slot.primary.predictor || br_info.slot.primary.predictor == &bi_modal)),
                      "If we're using the alternate predictor, the primary cannot be bimodal");

        return predictor == &bi_modal;
    }
    return false;
}

void BP_TAGE::train_indirect_predictor(const BranchInfo& br_info, BranchHistory& history, const BranchContext& context)
{
    auto predictor = br_info.slot.primary.predictor;

    // If we didn't know about this branch at all the first time, it will have the bi_modal
    // as it's primary. To make some of the checks below more uniform, I just switch it here
    // to the BTB, which is the true "base predictor" for indirects.
    //
    if (predictor == &bi_modal)
        predictor = &btb;

    if (predictor && predictor != &btb && predictor != &rsb)
        predictor->train(br_info, history, context);

    if (br_info.slot.alternate.predictor && br_info.slot.alternate.predictor != &btb && predictor != &rsb)
        br_info.slot.alternate.predictor->train_alt(br_info, history, context);

    // Avoid allocating a global if we missed the BTB and the BTB is capable of storing this
    // branch target. It will likely be right the next time around.
    //
    bool alloc_in_btb = !br_info.slot.btb_hit && btb.can_provide_target(br_info.type, br_info.pc, br_info.target);

    if (br_info.mispredicted_target && !alloc_in_btb) {
        bool primary_was_weak_correct =
            br_info.slot.primary.target == br_info.target && br_info.slot.primary.strength == BranchStrength::VERYWEAK;

        if (!primary_was_weak_correct) {
            int index = get_predictor_index(predictor);
            allocate_on_mispredict(iglobal, index, 1, br_info, history, context);
        }
    }
}

void BP_TAGE::commit_branch(const BranchInfo& br_info)
{
    auto predictor = br_info.slot.primary.predictor;

    if (predictor)
        predictor->commit(br_info, false /*not alternate*/);

    if (br_info.slot.alternate.predictor && br_info.slot.alternate.predictor != &btb && predictor != &rsb)
        br_info.slot.alternate.predictor->commit(br_info, true /*alternate predictor*/);

    for (auto& g : global)
        g->clear_ium(br_info.alloc_id);

    for (auto& g : iglobal)
        g->clear_ium(br_info.alloc_id);
}

int BP_TAGE::get_predictor_index(Predictor* predictor)
{
    if (predictor == nullptr || predictor == &btb || predictor == &bi_modal || predictor == &rsb)
        return -1;

    for (size_t i = 0; i < num_globals; i++) {
        if (predictor == global[i].get())
            return i;
    }

    for (size_t i = 0; i < num_iglobals; i++) {
        if (predictor == iglobal[i].get())
            return i;
    }

    throw std::runtime_error{"could not find predictor"};
}

void BP_TAGE::allocate_on_mispredict(std::vector<std::unique_ptr<BPGlobal>>& globals,
                                     int index,
                                     int max_alloc,
                                     const BranchInfo& br_info,
                                     BranchHistory& history,
                                     const BranchContext& context)
{
    int num_allocated = 0;
    int penalty = 0;

    for (size_t i = index + 1; i < globals.size() && num_allocated < max_alloc; i++) {
        auto gp = globals[i].get();

        if (gp->allocate(br_info, history, context, false, useful_reset_mode))
            ++num_allocated;
        else
            penalty += globals == global ? global_ways : iglobal_ways;
    }

    if (useful_policy == UsefulPolicy::HARD_RESET || useful_policy == UsefulPolicy::SOFT_RESET) {
        useful_water_line += penalty - 2 * num_allocated;

        if (useful_water_line < 0) {
            useful_water_line = 0;
        } else if (useful_water_line > useful_high_water) {
            if (useful_policy == UsefulPolicy::HARD_RESET) {
                for (auto& array : arrays) {
                    for (auto& set : array) {
                        for (auto& way : set)
                            way.useful = false;
                    }
                }
                useful_water_line = 0;
            } else {
                // SOFT_RESET
                useful_reset_mode = true;
            }
        } else if (useful_water_line < useful_low_water) {
            useful_reset_mode = false;
        }
    } else if (num_allocated == 0) {
        if (useful_policy == UsefulPolicy::FORCE_ALLOCATE_1 && ((size_t)index + 1) < globals.size()) {
            globals[index + 1]->allocate(br_info, history, context, true);
        } else if (useful_policy == UsefulPolicy::FORCE_ALLOCATE_N) {
            for (size_t i = index + 1; i < globals.size() && num_allocated < max_alloc; i++) {
                auto gp = globals[i].get();

                if (gp->allocate(br_info, history, context, true))
                    ++num_allocated;
            }
        }
    }
}

void BP_TAGE::recover(const Flush& flush_info,
                      BranchInfo* br_info,
                      BranchHistory& history,
                      const BranchContext& context)
{
    auto fetch_id = flush_info->fetch->fetch_id;
    rsb.flush(fetch_id);

    // A non-null br_info means this is a back-end flush (after allocation
    // into the BOB).
    //
    if (br_info) {
        auto branch_type = br_info->type;

        if (is_call(branch_type)) {
            rsb.spec_call(br_info->fetch, br_info->pc, context);
        } else if (is_return(branch_type)) {
            rsb.spec_ret(br_info->fetch, context);
        } else if (is_conditional(branch_type)) {
            auto predictor =
                (br_info->slot.use_alternate ? br_info->slot.alternate.predictor : br_info->slot.primary.predictor);
            if (predictor && predictor != &btb && predictor != &bi_modal)
                predictor->recover(flush_info, br_info, history, context);
        }

        if (flush_info->is_context_change) {
            // A back-end flush which is a context-switch---clear the IUM & RSB.
            for (auto& g : global)
                g->clear_ium();

            for (auto& g : iglobal)
                g->clear_ium();

            rsb.clear();
        }

        return;
    }

    // The remaining code handles "branch checks", that is corrections from
    // the ICF at instruction decode time. Here we have to worry about maintenance
    // of the RSB in case we missed a call or return, and clearing the BTB in
    // some cases when we know aliasing is occurring.

    if (flush_info->type != FlushType::BRANCH_CHECK_WRONG_TYPE) {
        auto inst_index = (flush_info->flushing_pc - flush_info->fetch->cache_line()) / 4;
        assert(inst_index < 16);
        auto& inst_pred = flush_info->fetch->line_prediction.at(inst_index);
        inst_pred.miss_all = true;
    }

    if (flush_info->type == FlushType::BRANCH_CHECK_WRONG_TYPE || flush_info->type == FlushType::BRANCH_CHECK_TARGET) {
        btb.invalidate_line(flush_info->fetch, context);
    }

    auto& prediction = flush_info->fetch->get_prediction();
    if (is_call(prediction.branch_type) && flush_info->flushing_pc == flush_info->fetch->exit_pc()) {
        rsb.spec_call(flush_info->fetch, flush_info->flushing_pc, context);
    } else if (is_return(prediction.branch_type) && flush_info->flushing_pc == flush_info->fetch->exit_pc()) {
        auto maybe_return_pc = rsb.spec_ret(flush_info->fetch, context);
        auto inst_index = (flush_info->flushing_pc - flush_info->fetch->cache_line()) / 4;
        auto& inst_pred = flush_info->fetch->line_prediction.at(inst_index);
        inst_pred.primary.predictor = &rsb;
        if (maybe_return_pc.has_value())
            inst_pred.rsb_hit = true;
        auto return_pc = maybe_return_pc.value_or(NON_CANONICAL_TARGET);
        inst_pred.primary.target = return_pc;
        flush_info->fetch->update_prediction_target(return_pc);
        flush_info->pc = return_pc;
    }
}

bool BP_TAGE::primary_needs_update(const BranchInfo& br_info) const
{
    auto predictor = br_info.slot.primary.predictor;
    bool predictor_is_global = predictor != &bi_modal && predictor != &btb && predictor != nullptr;

    if (predictor_is_global
        && (br_info.slot.primary.strength != BranchStrength::VERYSTRONG || br_info.slot.primary.ium))
        return true;

    if (update_useful_on_strong_alternate && br_info.slot.alternate.predictor) {
        bool alternate_direction_correct = br_info.taken == br_info.slot.alternate.taken;
        bool alternate_target_correct = !br_info.taken || (br_info.target == br_info.slot.alternate.target);
        bool alternate_correct = alternate_direction_correct && alternate_target_correct;

        if (alternate_correct && br_info.slot.alternate.strength == BranchStrength::VERYSTRONG
            && !(elide_useful_bit_update && br_info.slot.primary.useful == false))
        {
            return true;
        }
    }

    return false;
}

bool BP_TAGE::alternate_needs_update(const BranchInfo& br_info) const
{
    auto predictor = br_info.slot.alternate.predictor;

    if (predictor) {
        bool primary_direction_correct = br_info.taken == br_info.slot.primary.taken;
        bool primary_target_correct = !br_info.taken || (br_info.target == br_info.slot.primary.target);
        bool primary_correct = primary_direction_correct && primary_target_correct;

        if (!primary_correct && br_info.slot.primary.strength == BranchStrength::VERYWEAK) {
            if (br_info.slot.alternate.strength != BranchStrength::VERYSTRONG || br_info.slot.alternate.ium)
                return true;
        }
    }

    return false;
}

void BP_TAGE::save_state(json& j, const BranchContext& context)
{
    json btb_json, bi_modal_json, tage_globals, ittage_globals;

    btb.save_state(btb_json);
    bi_modal.save_state(bi_modal_json);

    tage_globals["num_globals"] = (unsigned)num_globals;
    tage_globals["num_sets"] = (unsigned)global_sets;
    tage_globals["num_ways"] = (unsigned)global_ways;
    tage_globals["tag_bits"] = (unsigned)global_tag_bits;
    tage_globals["arrays"] = arrays;

    ittage_globals["num_globals"] = (unsigned)num_iglobals;
    ittage_globals["num_sets"] = (unsigned)iglobal_sets;
    ittage_globals["num_ways"] = (unsigned)iglobal_ways;
    ittage_globals["tag_bits"] = (unsigned)iglobal_tag_bits;
    ittage_globals["arrays"] = iarrays;

    j["context"] = context;
    j["L2BTB"] = btb_json;
    j["bimodals"] = bi_modal_json;
    j["TAGE"] = tage_globals;
    j["ITTAGE"] = ittage_globals;
}

void BP_TAGE::restore_state(const json& j, BranchContext& context)
{
    j.at("context").get_to(context);
    btb.restore_state(j["L2BTB"]);
    bi_modal.restore_state(j["bimodals"]);

    json tage_globals = j["TAGE"];
    AMPERE_ASSERT(tage_globals["num_globals"].get<unsigned>() == num_globals,
                  "TAGE number of globals does not match parameters");
    AMPERE_ASSERT(tage_globals["num_sets"].get<unsigned>() == global_sets,
                  "TAGE number of global sets does not match parameters");
    AMPERE_ASSERT(tage_globals["num_ways"].get<unsigned>() == global_ways,
                  "TAGE number of global ways does not match parameters");
    AMPERE_ASSERT(tage_globals["tag_bits"].get<unsigned>() == global_tag_bits,
                  "TAGE number of global tag bits does not match parameters");
    for (unsigned g = 0; g < num_globals; g++)
        for (unsigned s = 0; s < global_sets; s++)
            for (unsigned w = 0; w < global_ways; w++)
                arrays.at(g).at(s).at(w) = tage_globals["arrays"][g][s][w];

    json ittage_globals = j["ITTAGE"];
    AMPERE_ASSERT(ittage_globals["num_globals"].get<unsigned>() == num_iglobals,
                  "TAGE number of iglobals does not match parameters");
    AMPERE_ASSERT(ittage_globals["num_sets"].get<unsigned>() == iglobal_sets,
                  "TAGE number of iglobal sets does not match parameters");
    AMPERE_ASSERT(ittage_globals["num_ways"].get<unsigned>() == iglobal_ways,
                  "TAGE number of iglobal ways does not match parameters");
    AMPERE_ASSERT(ittage_globals["tag_bits"].get<unsigned>() == iglobal_tag_bits,
                  "TAGE number of iglobal tag bits does not match parameters");
    for (unsigned g = 0; g < num_iglobals; g++)
        for (unsigned s = 0; s < iglobal_sets; s++)
            for (unsigned w = 0; w < iglobal_ways; w++)
                iarrays.at(g).at(s).at(w) = ittage_globals["arrays"][g][s][w];
}

void BP_TAGE::save_state(const std::string filename, const BranchContext& context)
{
    fmt::print("BP_TAGE: Saving current prediction structure state to {}.\n", filename);
    json json_state;
    save_state(json_state, context);
    auto json_stream = ampere::util::open_compressed_ostream(filename.c_str());
    *json_stream << json_state.dump(1 /*indent*/, ' ' /*indent_char*/);
}

void BP_TAGE::restore_state(const std::string filename, BranchContext& context)
{
    fmt::print("BP_TAGE: Restoring current prediction structure state from {}.\n", filename);
    auto json_stream = ampere::util::open_compressed_istream(filename.c_str());
    json json_state = json::parse(*json_stream);
    restore_state(json_state, context);
}

bool BP_TAGE::rsb_is_full()
{
    return rsb.is_full();
}

void BP_TAGE::clear_rsb()
{
    rsb.clear();
}

void BP_TAGE::update_rsb(const BranchInfo& br_info, BranchHistory& history, const BranchContext& context)
{
    if (is_call(br_info.type) || is_return(br_info.type))
        rsb.train(br_info, history, context);
}

void BP_TAGE::flush_rsb(Fetch fetch)
{
    rsb.flush(fetch->fetch_id + 1);
}

static std::vector<unsigned> get_histories(std::vector<std::unique_ptr<BPGlobal>>& gs)
{
    std::vector<unsigned> histories;

    for (auto& g : gs)
        histories.push_back(g->history_size);

    return histories;
}

std::vector<unsigned> BP_TAGE::get_conditional_histories()
{
    return get_histories(global);
}

std::vector<unsigned> BP_TAGE::get_indirect_histories()
{
    return get_histories(iglobal);
}

std::pair<int, int> bits_for_rotation_select(size_t num_arrays)
{
    size_t encompassing_power_of_two = 1;

    while (encompassing_power_of_two < num_arrays)
        encompassing_power_of_two <<= 1;

    int spread;

    if (encompassing_power_of_two == num_arrays)
        spread = 1;
    else
        spread = num_arrays / (encompassing_power_of_two - num_arrays);

    return {util::bitsize(encompassing_power_of_two - 1), spread};
}

size_t prev_pc_to_rotation(std::uint64_t index, size_t num_arrays, int spread)
{
    size_t index_mod = index % num_arrays;

    if (index <= num_arrays)
        return index_mod;
    else
        return index_mod * spread;
}

} // namespace ampere::core

/*
 * vim: sw=4:ts=4:et:si:cindent:cino=\:0,(0,g0,t0:
 */
