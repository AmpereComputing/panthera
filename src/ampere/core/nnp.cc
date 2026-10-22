//
// Copyright (c) 2020-2024 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//
#include "ampere/core/nnp.h"

namespace ampere::core {

NNP::NNP(const sc_core::sc_module_name& name) : CacheConfiguration{name}
{
    array.resize(num_sets);

    for (auto& set : array)
        set.resize(num_ways);

    crp->init(num_sets, num_ways);
}
void NNP::end_of_elaboration()
{
    AMPERE_ASSERT(!act_as_np || !act_as_nnnp, "Cannot act both as NP and NNNP simultaneously");
}

/**
 * @brief Lookup an NNP entry at prediction time.
 *
 * @param fetch                        The Fetch to look up.
 * @return boost::optional<NNPEntry&>  The entry, if we hit, or boost::none otherwise.
 *
 * Most of the heavy lifting is done by `lookup_internal`. We just need to return the
 * corresponding entry on a hit.
 */
boost::optional<NNPEntry&> NNP::lookup(Fetch fetch)
{
    auto [index, way] = lookup_internal(fetch);
    fetch->nnp_hit = (way != -1);
    fetch->nnp_set = index;
    fetch->nnp_way = way;

    if (way != -1) {
        crp->touch(index, way, CacheReplacementPolicy::TouchOp::LOOKUP);
        auto& entry = array[index][way];

        access_event(fetch,
                     nnp_hit(true),
                     nnp_set(index),
                     nnp_way(way),
                     nnp_tag(entry.tag),
                     useful_bit(entry.useful),
                     entrance_pc(fetch->program_counter),
                     br_pc(fetch->program_counter.line_address() + entry.offset),
                     br_target(entry.target),
                     br_target_nextline(entry.target_nextline));

        return entry;
    }

    access_event(fetch, nnp_hit(false), nnp_set(index), entrance_pc(fetch->program_counter));

    return boost::none;
}

/**
 * @brief Set the useful bit for an entry.
 *
 * @param fetch The Fetch.
 *
 * This is how we update for the NNP in the case that it was correct.
 */
void NNP::set_useful(Fetch fetch)
{
    uint64_t index = fetch->nnp_set;
    int way = fetch->nnp_way;
    bool hit = fetch->nnp_hit;

    if (hit) {
        auto& entry = array[index][way];
        entry.useful = true;
        crp->touch(index, way, CacheReplacementPolicy::TouchOp::UPDATE);

        set_useful_event(fetch, nnp_set(index), nnp_way(way), nnp_tag(entry.tag), useful_bit(entry.useful));
    }
}

void NNP::weaken_useful(Fetch& fetch, uint64_t index, int way)
{
    auto& entry = array[index][way];
    entry.useful = false;

    update_event(fetch, nnp_set(index), nnp_way(way), nnp_tag(entry.tag), useful_bit(entry.useful));
}

void NNP::allocate(Fetch& fetch, const Prediction& pred, uint64_t index, int way)
{
    auto& entry = array[index][way];
    entry.branch_type = pred.branch_type;
    entry.target = pred.target;
    entry.target_nextline = pred.target_nextline;
    entry.offset = pred.branch_pc.extract_range(5, 2);
    entry.tag = compute_tag(fetch->program_counter);
    entry.useful = allocate_useful;

    crp->touch(index, way, CacheReplacementPolicy::TouchOp::UPDATE);

    allocate_event(fetch,
                   nnp_set(index),
                   nnp_way(way),
                   nnp_tag(entry.tag),
                   useful_bit(entry.useful),
                   entrance_pc(fetch->program_counter),
                   br_pc(pred.branch_pc),
                   br_target(entry.target),
                   br_target_nextline(entry.target_nextline));
}

void NNP::invalidate(Fetch& fetch, uint64_t index, int way)
{
    auto& entry = array[index][way];
    entry.branch_type = BranchType::NONE;
    crp->invalidate(index, way);

    invalidate_event(fetch, nnp_set(index), nnp_way(way), nnp_tag(entry.tag), useful_bit(entry.useful));
}

/**
 * @brief Train the NNP whenever its prediction was incorrect.
 *
 * @param fetch  The Fetch.
 * @param value  The corrected prediction (as best we know so far...)
 *
 * Three(ish) cases:
 * 1) NNP predicted sequential, BTB predicted taken
 *   - weaken victim way if useful, otherwise allocate into victim way
 * 2) NNP predicted taken, BTB predicted sequential
 *   - weaken hit-upon way if useful, otherwise invalidate hit-upon way
 * 3) NNP predicted taken, BTB predicted different taken (Connor variant)
 *   a) originally hit-upon way is still a tag match
 *     - weaken hit-upon way if useful, otherwise allocate into hit-upon way
 *   b) originally hit-upon way is NOT still a tag match
 *     - weaken victim way if useful, otherwise allocate into victim way
 * 3) NNP predicted taken, BTB predicted different taken (pre-Connor variant)
 *   - weaken hit-upon way if useful, otherwise invalidate hit-upon way
 *   - weaken victim way if useful, otherwise allocate into victim way
 *
 * Note that it is possible to invalidate a hit-upon way during the same update
 * as allocating into a victim (if update_hit_before_victim is not set)!
 */
void NNP::full_update(Fetch fetch, const Prediction& value)
{
    uint64_t index = fetch->nnp_set;
    int way = fetch->nnp_way;
    bool hit = fetch->nnp_hit;
    bool btb_taken = value.branch_type != BranchType::NONE;

    // We identify the victim way prior to ANY updates, because we don't want
    // it being allowed to select a way that is invalidated during this update
    // as the victim if it wouldn't already be - doing so is not faithful to
    // the RTL design.
    int victim_way = crp->victim(index);
    auto& victim_entry = array[index][victim_way];

    if (!hit && btb_taken) {
        // If the NNP predicted sequential and the BTB predicted taken, weaken
        // the victim if it is useful, allocate if its useful bit is not set
        if (victim_entry.useful) {
            weaken_useful(fetch, index, victim_way);
        } else {
            allocate(fetch, value, index, victim_way);
        }
    } else if (hit && !btb_taken) {
        // If the NNP predicted taken and the BTB predicted sequential, weaken
        // the hit-upon way if it is useful, invalidate if its useful bit is
        // not set
        auto& hit_entry = array[index][way];
        if (hit_entry.useful) {
            weaken_useful(fetch, index, way);
        } else {
            invalidate(fetch, index, way);
        }
    } else if (/* hit && btb_taken && */ update_hit_before_victim) { // both taken, but different predictions (Connor
                                                                     // variant)
        unsigned offset = fetch->program_counter.extract_range(5, 2);
        auto tag = compute_tag(fetch->program_counter);
        auto& hit_entry = array[index][way];
        bool tag_match = hit_entry.tag == tag && hit_entry.offset >= offset;
        if (tag_match) {
            // hit-upon way is still a tag match, so weaken hit-upon way if
            // useful, otherwise allocate into hit-upon way
            if (hit_entry.useful) {
                weaken_useful(fetch, index, way);
            } else {
                allocate(fetch, value, index, way);
            }
        } else {
            // originally hit-upon way is NOT still a tag match, so weaken
            // victim way if useful, otherwise allocate into victim way
            if (victim_entry.useful) {
                weaken_useful(fetch, index, victim_way);
            } else {
                allocate(fetch, value, index, victim_way);
            }
        }
    } else /* if (hit && btb_taken) */ { // both taken, but different predictions (pre-Connor variant)
        auto& hit_entry = array[index][way];
        // Weaken the hit-upon way if it is useful, otherwise invalidate it.
        // Simultaneously, weaken the victim way if it is useful, otherwise
        // allocate into it.
        if (hit_entry.useful) {
            weaken_useful(fetch, index, way);
        } else {
            invalidate(fetch, index, way);
        }
        if (victim_entry.useful) {
            weaken_useful(fetch, index, victim_way);
        } else {
            allocate(fetch, value, index, victim_way);
        }
    }
}

/**
 * @brief Find a potentially matching set index and way for a Fetch.
 *
 * @param fetch                       The Fetch (we need its PC and previous PC)
 * @return std::pair<uint64_t, int>   The matching set index/way. way == -1 is a miss.
 *
 * The algorithm for lookup in the NNP is a little different than most caches. First we
 * find a set based on the current entry point PC/previous entry point PC, then we need
 * to find the way with the matching tag that has the smallest branch offset not less
 * than the offset of the entry point PC.
 *
 * This is because we can store more than one branch per cache line, and we want to
 * discover the next taken branch relative to where we entered.
 */
std::pair<uint64_t, int> NNP::lookup_internal(Fetch fetch) const
{
    auto index_pc = fetch->prev_program_counter_uncorrected;
    if (act_as_np) {
        index_pc = fetch->program_counter;
    } else if (act_as_nnnp) {
        index_pc = correct_prev_pc ? fetch->prev_prev_program_counter : fetch->prev_prev_program_counter_uncorrected;
    } else if (correct_prev_pc) {
        index_pc = fetch->prev_program_counter;
    }
    auto index = compute_set(index_pc);
    auto tag = compute_tag(fetch->program_counter);

    unsigned offset = fetch->program_counter.extract_range(5, 2);

    unsigned hit_offset = 16;
    int hit_way = -1;

    const auto& set = array.at(index);

    for (unsigned way = 0; way < num_ways; ++way) {
        const auto& entry = set[way];

        // Since we only store taken branches, we can use BranchType::NONE to signify invalid
        // ways.
        if (entry.branch_type != BranchType::NONE && entry.tag == tag && entry.offset >= offset
            && entry.offset < hit_offset)
        {
            hit_offset = entry.offset;
            hit_way = way;
        }
    }

    return {index, hit_way};
}

void NNP::update(Fetch fetch, bool useful)
{
    if (useful)
        set_useful(fetch);
    else
        full_update(fetch, fetch->get_prediction());
}

/**
 * @brief Extract the set index from a PC, ignoring cache line offset bits (5:0)
 */
uint64_t NNP::compute_set(ProgramCounter address) const
{
    uint64_t index = address.extract_range(bottom_set_bit + set_bits - 1, bottom_set_bit);
    if (connor_index_hash) {
        // The Connor index hash should work out to the following with set_bits=7, bottom_set_bit=6:
        // PC[19:13] ^ PC[12:6] ^ (PC[5:2] << 3)
        AMPERE_ASSERT(bottom_set_bit > 2, "The Connor NNP index hash requires 'bottom_set_bit' to be greater than 2");
        index ^= (address.extract_range(bottom_set_bit - 1, 2) << (set_bits - (bottom_set_bit - 2)));
        index ^= address.extract_range(bottom_set_bit + set_bits * 2 - 1, bottom_set_bit + set_bits);
    }
    return index;
}

/**
 * @brief Extract the tag bits from a PC, ignoring the 2 LSBs which are always 0.
 */
uint64_t NNP::compute_tag(ProgramCounter address) const
{
    return address.extract_range(2 + tag_bits_param - 1, 2);
}

} // namespace ampere::core
