//
// Copyright (c) 2020, 2022, 2023 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//

#include "ampere/core/bp_bimodal.h"
#include "ampere/core/bpu.h"
#include "ampere/uarch_common/cache.h"

namespace ampere::core {

using namespace ampere::uarch_common;

bool taken(BiModalDirection direction)
{
    return direction == BiModalDirection::TAKEN;
}

void BPBiModal::predict(Fetch fetch, BranchHistory& history, const BranchContext& context)
{
    size_t set_idx = compute_index(fetch, fetch->program_counter);
    auto& set = sets[set_idx];

    for (size_t offset = 0; offset < set.direction.size(); offset++) {
        auto& p = fetch->line_prediction[offset];
        bool pred_taken = taken(set.direction.at(offset));

        if (is_conditional(p.branch_type) || p.branch_type == BranchType::NONE) {
            p.primary.predictor = this;
            p.primary.taken = pred_taken;
        }
        p.conditional_primary_predictor = this;
        p.conditional_primary_taken = pred_taken;
    }
}

void BPBiModal::train_impl(const BranchInfo& br_info,
                           BranchHistory& history,
                           const BranchContext& context,
                           bool is_alternate)
{
    size_t set_idx = compute_index(br_info.fetch, br_info.pc);
    auto& set = sets[set_idx];
    int offset = br_info.pc.extract_range(5, 2);
    int hys_offset = offset / 4;

    bool pred_taken = taken(set.direction[offset]);

    if (br_info.taken == pred_taken) {
        set.hysteresis[hys_offset] = BiModalHysteresis::STRONG;
    } else {
        if (set.hysteresis[hys_offset] == BiModalHysteresis::WEAK) {
            set.direction[offset] = br_info.taken ? BiModalDirection::TAKEN : BiModalDirection::NOT_TAKEN;
        } else {
            set.hysteresis[hys_offset] = BiModalHysteresis::WEAK;
        }
    }
    ev_full_update(br_info.fetch,
                   br_info.pc,
                   ev_set(set_idx),
                   ev_offset(offset),
                   ev_hys_offset(hys_offset),
                   ev_taken(static_cast<int>(set.direction[offset])),
                   ev_strength(static_cast<int>(set.hysteresis[hys_offset])),
                   ev_is_alternate(is_alternate));
}

/*
 * Attempt to strengthen the bimodal counter if the direction matches our
 * existing direction. Fail to do so and return `false` if we could not due to
 * the direction not matching - this indicates we need a full update.
 */
bool BPBiModal::strengthen(const BranchInfo& br_info, bool is_alternate)
{
    int set_idx = compute_index(br_info.fetch, br_info.pc);
    auto& set = sets[set_idx];
    int offset = br_info.pc.extract_range(5, 2);
    int hys_offset = offset / 4;

    if (!br_info.mispredicted_direction) {
        set.hysteresis[hys_offset] = BiModalHysteresis::STRONG;
        ev_strengthening_update(br_info.fetch,
                                br_info.pc,
                                ev_set(set_idx),
                                ev_offset(offset),
                                ev_hys_offset(hys_offset),
                                ev_taken(br_info.taken),
                                ev_strength(static_cast<int>(set.hysteresis[hys_offset])),
                                ev_is_alternate(is_alternate));
        return true;
    } else {
        return false;
    }
}

size_t BPBiModal::compute_index(const Fetch& fetch, ProgramCounter pc) const
{
    size_t set_idx = pc.line_number() % (1UL << (set_bits - bank_bits));
    return (fetch->bank << (set_bits - bank_bits)) | set_idx;
}

void to_json(json& j, const BiModalSet& set)
{
    j["direction"] = set.direction;
    j["hysteresis"] = set.hysteresis;
}

void from_json(const json& j, BiModalSet& set)
{
    j["direction"].get_to(set.direction);
    j["hysteresis"].get_to(set.hysteresis);
}

void BPBiModal::save_state(json& j)
{
    j["num_sets"] = unsigned(num_sets);
    j["sets"] = json();
    for (unsigned set = 0; set < num_sets; set++) {
        j["sets"][set] = sets.at(set);
    }
}

void BPBiModal::restore_state(const json& j)
{
    AMPERE_ASSERT(j["num_sets"].get<unsigned>() == num_sets,
                  "BPBiModal number of sets in JSON do not match parameters");

    json json_set = j["sets"];
    for (unsigned set = 0; set < num_sets; set++) {
        sets.at(set) = json_set[set].get<BiModalSet>();
    }
}

} // namespace ampere::core
