//
// Copyright (c) 2019 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//

#include "ampere/core/btb.h"
#include "ampere/core/bob_entry.h"
#include "ampere/core/branch_history.h"

namespace ampere::core {

using namespace archx;
using namespace uarch_common;

extern std::uint64_t fnv_hash_word(std::uint64_t data, std::uint64_t h = 14695981039346656037ull);

BTB::BTB(const sc_core::sc_module_name& name, size_t _bank_bits) : Module{name}
{
    index_bits = bitsize(sets - 1);
    AMPERE_ASSERT(_bank_bits <= index_bits,
                  "The number of bank bits in the BTB must be less than or equal to the total number of index bits!");
    bank_bits = _bank_bits;
    set_bits = index_bits - bank_bits;
    array.resize(sets);

    for (auto& set : array)
        set.resize(ways);

    crp->init(sets, ways);
}

BTB::BTB(const sc_core::sc_module_name& name) : BTB{name, 0} {}

std::tuple<unsigned, unsigned> BTB::compute_index(ProgramCounter pc, BPUBank bank) const
{
    unsigned index;
    unsigned tag;

    int adj = use_half_cache_line_bit_in_index ? -1 : 0;

    // For Furiosa (12 tag bits, 11 set bits):
    //    tag = PC[52:41] ^ PC[40:29] ^ PC[28:17]
    //    set = PC[27:17] ^ PC[16:6]
    //
    // For Ripley (10 tag bits, 11 set bits):
    //    tag = PC[52:46] ^ PC[45:36] ^ PC[35:26] ^ PC[25:16]
    //    set = PC[26:16] ^ PC[15:5]
    //
    // For Connor (8 tag bits, 11 set bits):
    //    tag = PC[52:48] ^ PC[47:40] ^ PC[39:32] ^ PC[31:24] ^ PC[23:16]
    //    set = PC[26:16] ^ PC[15:5]
    //
    tag = util::fold_to_n_bits(pc.extract_range(52, line_bits + set_bits + adj), tag_bits);
    auto set_index =
        util::fold_to_n_bits(pc.extract_range(line_bits + 2 * set_bits + adj - 1, line_bits + adj), set_bits);
    index = (bank << set_bits) | set_index;

    return {index, tag};
}

void BTB::do_predict(Fetch fetch, BranchHistory& history, const BranchContext& context, bool post_event)
{
    auto [set_index, tag] = compute_index(fetch->program_counter, fetch->bank);
    auto& set = array.at(set_index);
    bool hit = false;

    for (unsigned way = 0; way < ways; way++) {
        auto& entry = set[way];
        if (entry.is_valid && entry.tag == tag && (!el_tagged || entry.el == context.el)) {
            auto& p = fetch->line_prediction[entry.offset];
            auto branch_pc = fetch->program_counter.line_address() + entry.offset * 4;

            p.branch_type = entry.branch_type;
            p.btb_hit = true;
            hit = true;

            if (can_provide_target(p.branch_type, branch_pc, entry.target)) {
                p.primary.target = entry.target;
                p.primary.target_nextline = entry.target_nextline;
            }

            if (is_unconditional(p.branch_type))
                p.primary.taken = true;
            crp->touch(set_index, way, CacheReplacementPolicy::TouchOp::LOOKUP);

            if (post_event) {
                match_event(fetch,
                            fetch->program_counter,
                            br_type(static_cast<int>(entry.branch_type)),
                            br_pc(branch_pc),
                            br_target(entry.target),
                            btb_set(set_index),
                            btb_tag(tag));
            }
        }
    }

    if (post_event)
        read_event(fetch, fetch->program_counter, hit_data(hit), btb_set(set_index), btb_tag(tag));
}

bool BTB::can_provide_target(BranchType branch_type, ProgramCounter branch_pc, ProgramCounter target_pc) const
{
    if (is_direct(branch_type)) {
        return true;
    } else {
        auto upper_branch = branch_pc.extract_range(63, target_bits + 2);
        auto upper_target = target_pc.extract_range(63, target_bits + 2);

        return (upper_target == upper_branch || upper_target == upper_branch + 1 || upper_target == upper_branch - 1);
    }
}

void BTB::train(const BranchInfo& br_info, BranchHistory& history, const BranchContext& context)
{
    if (!br_info.taken)
        return;

    auto [set_index, tag] = compute_index(br_info.fetch->program_counter, br_info.fetch->bank);
    auto& set = array.at(set_index);

    unsigned offset = br_info.pc.extract_range(5, 2);

    auto [way, is_alloc] = pick_way(set_index, tag, context.el, offset);
    bool is_update = is_alloc || set[way].branch_type != br_info.type || set[way].target != br_info.target;

    // If a direct branch, check that the trained target can be represented by
    // the ISA. Bits above bit 29 must be shared (this is a loose for some
    // branch types, but will catch those that are so wrong they can't be
    // represented in RTL's BTB.
    if (is_direct(br_info.type)) {
        ProgramCounter diff{
            uint64_t(br_info.pc > br_info.target ? br_info.pc - br_info.target : br_info.target - br_info.pc)};
        AMPERE_ASSERT(diff.extract_range(63, 30) == 0,
                      "Attempting to insert direct branch into BTB with too big a difference between the branch pc"
                      "({}) and the target ({})",
                      br_info.pc,
                      br_info.target);
    }

    set[way] = {tag, context.el, offset, br_info.type, br_info.target, br_info.target_nextline, br_info.pc};
    set[way].is_valid = true;

    if (is_update) {
        update_event(br_info.fetch,
                     br_pc(br_info.pc),
                     br_type(static_cast<int>(br_info.type)),
                     br_target(br_info.target),
                     br_target_nextline(br_info.target_nextline),
                     btb_set(set_index),
                     btb_tag(tag),
                     br_offset(offset));
    }
}

void BTB::update_lru(const BranchInfo& br_info, const BranchContext& context)
{
    auto [set_index, tag] = compute_index(br_info.fetch->program_counter, br_info.fetch->bank);
    auto& set = array.at(set_index);

    unsigned offset = br_info.pc.extract_range(5, 2);

    int way = pick_matching_way(set_index, tag, context.el, offset);

    if (way >= 0) {
        set[way].is_valid = true;
        crp->touch(set_index, way, CacheReplacementPolicy::TouchOp::LOOKUP);
        update_lru_event(br_info.fetch, br_pc(br_info.pc), btb_set(set_index), btb_tag(tag), br_offset(offset));
    }
}

std::pair<int, bool> BTB::pick_way(unsigned set_index, unsigned tag, archx::EL el, unsigned offset)
{
    auto& set = array.at(set_index);

    // Choose a way in three passes:
    //
    //   1. If there's a way that already matches our tag/offset, return it
    //   2. If there's an unused way (is_valid == 0), return it
    //   3. Choose a victim appropriately (true LRU or pseudo-LRU)
    //
    for (int w = 0; w < (int)ways; w++) {
        if (set[w].is_valid && set[w].tag == tag && (!el_tagged || set[w].el == el) && set[w].offset == offset) {
            crp->touch(set_index, w, CacheReplacementPolicy::TouchOp::LOOKUP);
            return {w, false};
        }
    }

    for (int w = 0; w < (int)ways; w++) {
        if (!set[w].is_valid) {
            crp->touch(set_index, w, CacheReplacementPolicy::TouchOp::REFILL);
            return {w, true};
        }
    }

    int victim_way = crp->victim(set_index);
    crp->touch(set_index, victim_way, CacheReplacementPolicy::TouchOp::REFILL);

    assert(victim_way >= 0);
    return {victim_way, true};
}

int BTB::pick_matching_way(unsigned set_index, unsigned tag, archx::EL el, unsigned offset)
{
    auto& set = array.at(set_index);

    for (int w = 0; w < (int)ways; w++) {
        if (set[w].is_valid && set[w].tag == tag && (!el_tagged || set[w].el == el) && set[w].offset == offset)
            return w;
    }

    return -1;
}

void BTB::invalidate_all_ways(Fetch fetch)
{
    unsigned set_index;
    std::tie(set_index, std::ignore) = compute_index(fetch->program_counter, fetch->bank);
    auto& set = array.at(set_index);

    for (int w = 0; w < (int)ways; w++) {
        set[w] = BTBEntry();
        set[w].is_valid = false;
    }
}

void BTB::invalidate_paired_ways(Fetch fetch, const BranchContext& context)
{
    auto [set_index, tag] = compute_index(fetch->program_counter, fetch->bank);
    auto& set = array.at(set_index);
    unsigned offset = fetch->exit_pc().extract_range(5, 2);

    for (size_t w = 0; w < ways; w++) {
        if (set[w].is_valid && set[w].tag == tag && (!el_tagged || set[w].el == context.el) && set[w].offset == offset)
        {
            set[w] = BTBEntry();
            set[w].is_valid = false;
            if ((w ^ 0b1) < ways) {
                set[w ^ 0b1] = BTBEntry();
                set[w ^ 0b1].is_valid = false;
            }
            return;
        }
    }
}

void BTB::invalidate_one_way(Fetch fetch, const BranchContext& context)
{
    auto [set_index, tag] = compute_index(fetch->program_counter, fetch->bank);
    auto& set = array.at(set_index);
    unsigned offset = fetch->exit_pc().extract_range(5, 2);

    for (size_t w = 0; w < ways; w++) {
        if (set[w].is_valid && set[w].tag == tag && (!el_tagged || set[w].el == context.el) && set[w].offset == offset)
        {
            set[w] = BTBEntry();
            set[w].is_valid = false;
            return;
        }
    }
}

void BTB::invalidate_line(Fetch fetch, const BranchContext& context)
{
    if (selective_btc_clear_invalidation)
        invalidate_one_way(fetch, context);
    else if (paired_btc_clear_invalidation)
        invalidate_paired_ways(fetch, context);
    else
        invalidate_all_ways(fetch);
}

void to_json(json& j, const BTBEntry& entry)
{
    j = json{
        {"tag", entry.tag},
        {"el", entry.el},
        {"offset", entry.offset},
        {"branch_type", entry.branch_type},
        {"target", entry.target},
        {"target_nextline", entry.target_nextline},
        {"branch_pc", entry.branch_pc},
        {"valid", entry.is_valid},
    };
}

void from_json(const json& j, BTBEntry& entry)
{
    j.at("tag").get_to(entry.tag);
    j.at("el").get_to(entry.el);
    j.at("offset").get_to(entry.offset);
    j.at("branch_type").get_to(entry.branch_type);
    j.at("target").get_to(entry.target);
    j.at("target_nextline").get_to(entry.target_nextline);
    j.at("branch_pc").get_to(entry.branch_pc);
    j.at("valid").get_to(entry.is_valid);
}

void BTB::save_state(json& j)
{
    j["num_sets"] = unsigned(sets);
    j["num_ways"] = unsigned(ways);
    j["tag_bits"] = unsigned(tag_bits);
    j["el_tagged"] = unsigned(el_tagged);
    j["sets"] = json();
    for (unsigned set = 0; set < sets; set++) {
        j["sets"][set] = json();
        for (unsigned way = 0; way < ways; way++) {
            j["sets"][set][way] = array.at(set).at(way);
        }
    }
}

void BTB::restore_state(const json& j)
{
    AMPERE_ASSERT(j["num_sets"].get<unsigned>() == sets, "BTB number of sets in JSON do not match parameters");
    AMPERE_ASSERT(j["num_ways"].get<unsigned>() == ways, "BTB number of ways in JSON do not match parameters");
    AMPERE_ASSERT(j["tag_bits"].get<unsigned>() == tag_bits, "BTB number of tag bits in JSON do not match parameters");
    AMPERE_ASSERT(j["el_tagged"].get<unsigned>() == unsigned(el_tagged),
                  "BTB EL-tagging status in JSON does not match parameters");

    json json_array = j["sets"];
    for (unsigned set = 0; set < sets; set++) {
        for (unsigned way = 0; way < ways; way++) {
            array[set][way] = json_array[set][way].get<BTBEntry>();
        }
    }
}

} // namespace ampere::core
