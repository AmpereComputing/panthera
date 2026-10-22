//
// Copyright (c) 2018-2023 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//

#pragma once

#include <cstdint>
#include "ampere/archx/addr.h"
#include "ampere/core/idr/idr_defs.h"
#include "ampere/core/testing/uops.h"

namespace ampere::core::testing {

inline static DecodedUopGroup make_uop_group(unsigned amount, unsigned width)
{
    DecodedUopGroup group(width);
    for (unsigned i = 0; i < amount; ++i) {
        auto &uop = group.uops.emplace_back(core::create_nop_uop());
        uop->alloc_id = i + 1;
        ++group.lanes_used;
    }

    return group;
}

inline static DecodedUopGroup make_uop_group(uint32_t opcode, unsigned amount, unsigned width)
{
    auto pc = 0x0_pc;
    DecodedUopGroup group(width);
    for (unsigned i = 0; i < amount; ++i) {
        auto &uop = group.uops.emplace_back(core::create_core_uop(opcode, pc));
        uop->alloc_id = i + 1;
        ++group.lanes_used;
        pc += 4;
    }

    return group;
}

inline static DecodedUopGroup make_uop_group_from_opcodes(const std::vector<uint32_t> &opcodes, unsigned width)
{
    uint64_t alloc_id = 0;
    auto pc = 0x0_pc;
    DecodedUopGroup group(width);
    for (auto opcode : opcodes) {
        auto &uop = group.uops.emplace_back(core::create_core_uop(opcode, pc));
        uop->alloc_id = ++alloc_id;
        ++group.lanes_used;
        pc += 4;
    }

    return group;
}

} // namespace ampere::core::testing
