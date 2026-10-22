//
// Copyright (c) 2020 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//

#include "ampere/core/bp_global.h"
#include "ampere/core/branch_history.h"

namespace ampere::core {

void to_json(json& j, const StrengthCounter& counter)
{
    j = counter.get_strength();
}

void from_json(const json& j, StrengthCounter& counter)
{
    counter = j.get<BranchStrength>();
}

void to_json(json& j, const UseAlternateCounter& counter)
{
    j = counter.get_value();
}

void from_json(const json& j, UseAlternateCounter& counter)
{
    counter.set_value(j.get<unsigned>());
}

void to_json(json& j, const BPGlobalEntry& entry)
{
    j = json{
        {"tag", entry.tag},
        {"el", entry.el},
        {"offset", entry.offset},
        {"branch_type", entry.type},
        {"target", entry.target},
        {"target_nextline", entry.target_nextline},
        {"strength", entry.strength},
        {"taken", entry.taken},
        {"useful", entry.useful},
        {"most_recently_allocated", entry.most_recently_allocated},
    };
}

void from_json(const json& j, BPGlobalEntry& entry)
{
    j.at("tag").get_to(entry.tag);
    j.at("el").get_to(entry.el);
    j.at("offset").get_to(entry.offset);
    j.at("branch_type").get_to(entry.type);
    j.at("target").get_to(entry.target);
    j.at("target_nextline").get_to(entry.target_nextline);
    j.at("strength").get_to(entry.strength);
    j.at("taken").get_to(entry.taken);
    j.at("useful").get_to(entry.useful);
    j.at("most_recently_allocated").get_to(entry.most_recently_allocated);
}

BPGlobal::BPGlobal(const sc_core::sc_module_name& name,
                   std::vector<BPGlobalArray>& physical_arrays,
                   int set_bits,
                   int tag_bits,
                   size_t bank_bits)
    : Module{name}, physical_arrays{physical_arrays}, set_bits{set_bits}, tag_bits{tag_bits}, bank_bits{bank_bits}
{
    AMPERE_ASSERT(
        bank_bits <= size_t(set_bits),
        "The number of bank bits in a TAGE global array must be less than or equal to the total number of index bits!");
}

void BPGlobal::predict(Fetch fetch, BranchHistory& history, const BranchContext& context)
{
    if (clock_gated)
        return;

    auto [set, index] = find_set(fetch, fetch->program_counter, history);

    for (auto& entry : set) {
        if (entry.tag == index.tag && (!el_tagged || entry.el == context.el)) {
            auto& p = fetch->line_prediction[entry.offset];
            auto ium_iter = ium.find({index.set, entry.tag, el_tagged ? context.el : archx::EL0, entry.offset});
            if (predict_entry(p, entry, ium_iter)) {
                auto ium_alloc_id = (ium_iter != ium.end()) ? ium_iter->second.alloc_id : 0;
                if (ium_iter != ium.end() && p.primary.ium && entry.taken != ium_iter->second.taken)
                    br_ium_override(fetch);

                br_global_lookup(fetch,
                                 fetch->program_counter,
                                 br_taken(p.primary.taken),
                                 br_target(entry.target),
                                 br_target_nextline(entry.target_nextline),
                                 br_strength(static_cast<int>(p.primary.strength)),
                                 br_useful(p.primary.useful),
                                 br_ium(ium_alloc_id),
                                 br_global_array(index.array),
                                 br_global_set(index.set),
                                 br_global_tag(index.tag),
                                 br_global_offset(entry.offset));
            }
        }
    }
    br_global_read(fetch);

    // Increment the number of predictions we have seen since a correct
    // prediction was made from this array, or an allocation was made into it.
    // If this number of predictions is greater than a given threshold, begin
    // clock gating this array.
    predictions_since_usage++;
    if (gate_after_predictions_since_usage > 0 && predictions_since_usage > gate_after_predictions_since_usage) {
        clock_gated = true;
    }
}

bool BPGlobal::predict_entry(SlotPrediction& p, const BPGlobalEntry& entry, IUMIter ium_it)
{
    auto primary_strength = entry.strength.get_strength();
    bool use_alternate = primary_strength == BranchStrength::VERYWEAK && use_alternate_counter->get_value() >= 16;

    p.conditional_alternate_predictor = p.conditional_primary_predictor;
    p.conditional_alternate_taken = p.conditional_primary_taken;
    p.conditional_primary_taken = entry.taken;
    p.conditional_primary_predictor = this;
    p.conditional_use_alternate = use_alternate;

    if (ium_it != ium.end())
        p.conditional_primary_taken = ium_it->second.taken;

    if (is_conditional(p.branch_type) || p.branch_type == BranchType::NONE) {
        p.alternate = p.primary;
        p.primary.predictor = this;
        p.primary.strength = primary_strength;
        p.primary.taken = entry.taken;
        p.primary.useful = entry.useful;
        p.primary.ium = false;
        p.use_alternate = use_alternate;

        if (ium_it != ium.end()) {
            p.primary.taken = ium_it->second.taken;
            p.primary.strength = ium_it->second.strength.get_strength();
            p.primary.ium = true;
            ++ium_it->second.age;
        }

        return true;
    }

    return false;
}

bool BPGlobalIndirect::predict_entry(SlotPrediction& p, const BPGlobalEntry& entry, IUMIter ium_it)
{
    if (entry.type != BranchType::NONE && is_indirect(p.branch_type)) {
        p.alternate = p.primary;
        p.primary.predictor = this;
        p.primary.strength = entry.strength.get_strength();
        p.primary.taken = true;
        p.primary.target = entry.target;
        p.primary.target_nextline = entry.target_nextline;
        p.primary.useful = entry.useful;
        p.use_alternate = (p.alternate.target.has_value() && p.primary.strength == BranchStrength::VERYWEAK
                           && use_alternate_counter->get_value() >= 16);
        p.ittage_hit = true;

        if (ium_it != ium.end()) {
            p.primary.target = ium_it->second.target;
            p.primary.target_nextline = false; // IUM can't predict something as target-nextline
            p.primary.strength = ium_it->second.strength.get_strength();
            p.primary.ium = true;
            ++ium_it->second.age;
        }

        return true;
    }

    return false;
}

void BPGlobal::train(const BranchInfo& br_info, BranchHistory& history, const BranchContext& context)
{
    auto [entry, index] = find_entry(br_info.fetch, br_info.pc, history, context);

    if (entry) {
        train_entry(br_info, *entry);

        br_global_update(br_info.fetch,
                         br_info.pc,
                         br_taken(br_info.taken),
                         br_target(br_info.target),
                         br_target_nextline(entry->target_nextline),
                         br_useful(entry->useful),
                         br_global_array(index.array),
                         br_global_set(index.set),
                         br_global_tag(index.tag),
                         br_global_offset(index.offset),
                         br_strength(static_cast<int>(entry->strength.get_strength())));
    } else {
        br_global_update_miss(br_info.fetch,
                              br_info.pc,
                              br_taken(br_info.taken),
                              br_target(br_info.target),
                              br_target_nextline(br_info.target_nextline),
                              br_global_array(index.array),
                              br_global_set(index.set),
                              br_global_tag(index.tag));
    }
}

void BPGlobal::clear_ium(std::uint64_t alloc_id)
{
    for (IUMIter ium_it = ium.begin(); ium_it != ium.end();) {
        if (ium_it->second.alloc_id <= alloc_id)
            ium_it = ium.erase(ium_it);
        else
            ++ium_it;
    }
}

void BPGlobal::train_entry(const BranchInfo& br_info, BPGlobalEntry& entry)
{
    bool primary_correct = br_info.taken == br_info.slot.primary.taken && !br_info.slot.primary.ium;
    bool alternate_correct = br_info.taken == br_info.slot.alternate.taken && !br_info.slot.alternate.ium;

    if (primary_correct) {
        entry.strength++;
    } else {
        if (entry.strength.get_strength() == BranchStrength::VERYWEAK)
            entry.taken = br_info.taken;
        else
            entry.strength--;
    }

    update_useful(
        entry, primary_correct, alternate_correct, entry.strength.get_strength(), br_info.slot.alternate.strength);
    update_use_alternate_counter(primary_correct, alternate_correct);
}

void BPGlobalIndirect::train_entry(const BranchInfo& br_info, BPGlobalEntry& entry)
{
    bool primary_correct = br_info.target == br_info.slot.primary.target && !br_info.slot.primary.ium;
    bool alternate_correct = br_info.target == br_info.slot.alternate.target && !br_info.slot.alternate.ium;

    if (primary_correct) {
        entry.strength++;
    } else {
        if (entry.strength.get_strength() == BranchStrength::VERYWEAK) {
            entry.target = br_info.target;
            entry.target_nextline = br_info.target_nextline;
        } else {
            entry.strength--;
        }
    }

    update_useful(
        entry, primary_correct, alternate_correct, entry.strength.get_strength(), br_info.slot.alternate.strength);
    update_use_alternate_counter(primary_correct, alternate_correct);
}

void BPGlobal::update_useful(BPGlobalEntry& entry,
                             bool primary_correct,
                             bool alternate_correct,
                             BranchStrength primary_strength,
                             BranchStrength alternate_strength)
{
    if (primary_strength == BranchStrength::VERYWEAK)
        entry.useful = false;
    else if (alternate_correct && alternate_strength == BranchStrength::VERYSTRONG)
        entry.useful = false;
    else if (primary_correct && !alternate_correct)
        entry.useful = true;
}

void BPGlobal::update_use_alternate_counter(bool primary_correct, bool alternate_correct)
{
    if (primary_correct != alternate_correct) {
        if (alternate_correct)
            (*use_alternate_counter)++;
        else
            (*use_alternate_counter)--;
    }
}

void BPGlobal::train_alt(const BranchInfo& br_info, BranchHistory& history, const BranchContext& context)
{
    // If the primary was both incorrect and weak, then we go ahead and
    // adjust the alternate's counter.
    //
    // I'm not clear on the logic of this, but I'm just following Andre's
    // code at the moment.
    //
    bool primary_correct = br_info.taken == br_info.slot.primary.taken;

    if (!primary_correct && br_info.slot.primary.strength == BranchStrength::VERYWEAK) {
        auto [entry, index] = find_entry(br_info.fetch, br_info.pc, history, context);

        if (entry) {
            bool alternate_correct = br_info.taken == br_info.slot.alternate.taken;

            if (alternate_correct) {
                entry->strength++;
            } else {
                if (entry->strength.get_strength() == BranchStrength::VERYWEAK)
                    entry->taken = br_info.taken;
                else
                    entry->strength--;
            }

            br_global_update(br_info.fetch,
                             br_info.pc,
                             br_taken(br_info.taken),
                             br_target(br_info.target),
                             br_useful(entry->useful),
                             br_global_array(index.array),
                             br_global_set(index.set),
                             br_global_tag(index.tag),
                             br_global_offset(index.offset),
                             br_strength(static_cast<int>(entry->strength.get_strength())));
        }
    }
}

void BPGlobalIndirect::train_alt(const BranchInfo& br_info, BranchHistory& history, const BranchContext& context)
{
    bool primary_correct = br_info.target == br_info.slot.primary.target;

    if (!primary_correct && br_info.slot.primary.strength == BranchStrength::VERYWEAK) {
        auto [entry, index] = find_entry(br_info.fetch, br_info.pc, history, context);

        if (entry) {
            bool alternate_correct = br_info.target == br_info.slot.alternate.target;

            if (alternate_correct) {
                entry->strength++;
            } else {
                if (entry->strength.get_strength() == BranchStrength::VERYWEAK) {
                    entry->target = br_info.target;
                    entry->target_nextline = br_info.target_nextline;
                } else {
                    entry->strength--;
                }
            }

            br_global_update(br_info.fetch,
                             br_info.pc,
                             br_taken(br_info.taken),
                             br_target(br_info.target),
                             br_target_nextline(entry->target_nextline),
                             br_useful(entry->useful),
                             br_global_array(index.array),
                             br_global_set(index.set),
                             br_global_tag(index.tag),
                             br_global_offset(index.offset),
                             br_strength(static_cast<int>(entry->strength.get_strength())));
        }
    }
}

void BPGlobal::recover(const Flush& flush_info,
                       BranchInfo* br_info,
                       BranchHistory& history,
                       const BranchContext& context)
{
    if (ium_entries == 0)
        return;

    auto index = compute_index(br_info->fetch, br_info->pc, history);
    archx::EL ium_el = el_tagged ? context.el : archx::EL0;
    auto ium_it = ium.find({index.set, index.tag, ium_el, index.offset});

    if (ium_it != ium.end()) {
        if (ium_it->second.strength.get_strength() == BranchStrength::VERYWEAK) {
            ium_it->second.taken = br_info->taken;
            ium_it->second.target = br_info->target;
        } else {
            ium_it->second.strength--;
        }

        ium_it->second.alloc_id = br_info->alloc_id;

        br_ium_allocate(br_pc(br_info->pc),
                        br_taken(ium_it->second.taken),
                        br_target(br_info->target),
                        br_strength(static_cast<int>(ium_it->second.strength.get_strength())),
                        br_global_set(index.set),
                        br_global_tag(index.tag),
                        br_global_offset(index.offset));
        return;
    }

    if (ium.size() == ium_entries) {
        std::uint64_t oldest = std::numeric_limits<std::uint64_t>::max();
        IUMIter victim = ium.end();

        for (IUMIter it = ium.begin(); it != ium.end(); ++it) {
            if (it->second.age < oldest) {
                victim = it;
                oldest = it->second.age;
            }
        }

        assert(victim != ium.end());
        ium.erase(victim);
    }

    assert(ium.size() < ium_entries);

    auto& p = (this == br_info->slot.primary.predictor) ? br_info->slot.primary : br_info->slot.alternate;

    bool taken = p.taken;
    ProgramCounter target = p.target.has_value() ? *p.target : br_info->target;
    StrengthCounter strength{p.strength};

    if (strength.get_strength() == BranchStrength::VERYWEAK) {
        taken = br_info->taken;
        target = br_info->target;
    } else {
        --strength;
    }

    br_ium_allocate(br_pc(br_info->pc),
                    br_taken(taken),
                    br_target(target),
                    br_strength(static_cast<int>(strength.get_strength())),
                    br_global_set(index.set),
                    br_global_tag(index.tag),
                    br_global_offset(index.offset));

    ium.emplace(
        std::piecewise_construct,
        std::forward_as_tuple(index.set, index.tag, ium_el, index.offset),
        std::forward_as_tuple(
            index.set, index.tag, context.el, index.offset, taken, target, strength.get_strength(), br_info->alloc_id));
}

static void set_most_recently_allocated(BPGlobalSet& set, int way)
{
    for (int w = 0; w < (int)set.size(); ++w)
        set[w].most_recently_allocated = w == way;
}

bool BPGlobal::allocate(
    const BranchInfo& br_info, BranchHistory& history, const BranchContext& context, bool force, bool reset_useful)
{
    auto [set, index] = find_set(br_info.fetch, br_info.pc, history);

    int victim_way = -1;

    // Reset the array-gating counter for this global to 0 because we allocated
    // into it, indicating we may need to use it soon. If we were already
    // clock-gating this array, stop.
    predictions_since_usage = 0;
    clock_gated = false;

    for (int way = 0; way < (int)set.size(); ++way) {
        auto& entry = set[way];

        if (reset_useful)
            entry.useful = false;

        if (entry.tag == index.tag && entry.offset == index.offset && (!el_tagged || entry.el == context.el)) {
            // This entry already exists; this can happen due to the delay between prediction and
            // training. Treat this like an update instead.
            //
            train_entry(br_info, entry);
            set_most_recently_allocated(set, way);

            br_global_update(br_info.fetch,
                             br_pc(br_info.pc),
                             br_taken(br_info.taken),
                             br_target(br_info.target),
                             br_target_nextline(br_info.target_nextline),
                             br_useful(entry.useful),
                             br_global_array(index.array),
                             br_global_set(index.set),
                             br_global_tag(index.tag),
                             br_global_offset(index.offset),
                             br_strength(static_cast<int>(entry.strength.get_strength())));

            return true;
        } else if (!entry.useful && entry.strength.get_strength() < BranchStrength::WEAK) {
            if (victim_way == -1 || !entry.most_recently_allocated)
                victim_way = way;
        }
    }

    if (victim_way == -1) {
        for (auto& entry : set) {
            if (!entry.useful)
                --entry.strength;
        }

        if (!force)
            return false;

        // If we're forcing allocation, simply choose the way that is not
        // the most recently allocated.
        //
        for (int way = 0; way < (int)set.size(); ++way) {
            if (!set[way].most_recently_allocated) {
                victim_way = way;
                break;
            }
        }
    }

    auto& entry = set[victim_way];

    entry.tag = index.tag;
    entry.el = context.el;
    entry.offset = index.offset;
    entry.type = br_info.type;
    entry.target = br_info.target;
    entry.target_nextline = br_info.target_nextline && is_indirect(br_info.type);
    entry.strength = BranchStrength::VERYWEAK;
    entry.taken = br_info.taken;
    entry.useful = false;
    set_most_recently_allocated(set, victim_way);

    br_global_allocate(br_info.fetch,
                       br_pc(br_info.pc),
                       br_taken(br_info.taken),
                       br_target(br_info.target),
                       br_target_nextline(br_info.target_nextline),
                       br_global_array(index.array),
                       br_global_set(index.set),
                       br_global_tag(index.tag),
                       br_global_offset(index.offset));

    return true;
}

void BPGlobal::commit(const BranchInfo& br_info, bool is_alternate)
{
    bool is_provider = (is_alternate == br_info.slot.use_alternate);
    bool mispredicted = br_info.mispredicted_direction || br_info.mispredicted_target;

    if (is_provider && !mispredicted) {
        predictions_since_usage = 0;
    }
}

std::pair<BPGlobalSet&, BPIndex> BPGlobal::find_set(Fetch fetch, ProgramCounter branch_pc, BranchHistory& history)
{
    auto index = compute_index(fetch, branch_pc, history);
    return {physical_arrays[index.array][index.set], index};
}

std::pair<BPGlobalEntry*, BPIndex> BPGlobal::find_entry(Fetch fetch,
                                                        ProgramCounter branch_pc,
                                                        BranchHistory& history,
                                                        const BranchContext& context)
{
    auto [set, index] = find_set(fetch, branch_pc, history);

    for (auto& entry : set) {
        if (entry.tag == index.tag && entry.offset == index.offset && (!el_tagged || entry.el == context.el))
            return {&entry, index};
    }

    return {nullptr, index};
}

BPIndex BPGlobal::compute_index(const Fetch& fetch, ProgramCounter branch_pc, BranchHistory& history) const
{
    auto [set, tag] = history.hash(branch_pc, history_size, set_bits - bank_bits, tag_bits);
    auto bank = fetch->bank & ((1UL << bank_bits) - 1);
    BPSet bank_set_index = (bank << (set_bits - bank_bits)) | set;
    return {select_rotation(fetch, branch_pc, history), bank_set_index, tag, int(branch_pc.extract_range(5, 2))};
}

void BPGlobal::set_rotation_select(RotationSelect func)
{
    select_rotation = func;
}

void BPGlobal::set_use_alternate_counter(std::shared_ptr<UseAlternateCounter> counter)
{
    use_alternate_counter = counter;
}

} // namespace ampere::core
