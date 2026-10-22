//
// Copyright (c) 2022 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//

#pragma once

#include "ampere/sc/sc.h"
#include "ampere/core/l2/crp/l2_replacement_policy.h"
#include "ampere/core/l2/crp/rrip.h"

namespace ampere::core {

using namespace ampere::sc;

class L2DRRIP : public implements<L2DRRIP, L2RRIP>
{
public:
    explicit L2DRRIP(const sc_core::sc_module_name& name);

    Param<uint64_t> brrip_long_frequency{
        "brrip_long_frequency",
        "Every N fills, fill with age (rrip_max - insert_delta). Value of 1 means SRRIP (never insert at rrip_max)",
        32};
    Param<uint64_t> set_dueling_mask{"set_dueling_mask", "Mask of set bits used for policy selection", 0x3f};
    Param<uint64_t> dedicated_srrip_match{
        "dedicated_srrip_match",
        "The always-SRRIP evaluation sets are (set & set_dueling_mask) == dedicated_srrip_match",
        0xc};
    Param<uint64_t> dedicated_brrip_match{
        "dedicated_brrip_match",
        "The always-BRRIP evaluation sets are (set & set_dueling_mask) == dedicated_brrip_match",
        0x30};
    Param<unsigned> psel_counter_bits{"psel_counter_bits", "Width of policy selection counter", 8};
    Param<int> psel_srrip_delta{
        "psel_srrip_delta", "Amount to increment psel counter on fill to dedicated SRRIP set", 1};
    Param<int> psel_brrip_delta{
        "psel_brrip_delta", "Amount to decrement psel counter on fill to dedicated BRRIP set", 1};

    void touch(uint64_t bank,
               uint64_t set,
               uint64_t way,
               TouchOp op,
               bool demand,
               const Set& set_contents,
               MemReq req = nullptr) override;

private:
    enum class Policy {
        SRRIP,
        BRRIP
    };

    bool dedicated_srrip(uint64_t set);
    bool dedicated_brrip(uint64_t set);

    uint64_t brrip_counter{0};
    Policy current_policy{Policy::SRRIP};
    SaturatingCounter psel_counter{(1u << psel_counter_bits) - 1};

    Event ev_switch_to_srrip{"switch_to_srrip", "Majority policy switched to SRRIP"};
    Event ev_switch_to_brrip{"switch_to_brrip", "Majority policy switched to BRRIP"};
    Event ev_srrip_fill{"srrip_fill", "Filled a line with SRRIP insertion"};
    Event ev_brrip_fill{"brrip_fill", "Filled a line with BRRIP insertion"};
};

} // namespace ampere::core
