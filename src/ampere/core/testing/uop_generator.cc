//
// Copyright (c) 2018-2019 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//

#include "ampere/core/testing/uop_generator.h"
#include "ampere/archx/instruction_simple.h"
#include "ampere/sc/transaction.h"
#include "ampere/core/ixu.h"

namespace ampere::core {

bool UopGenerator::can_take() const
{
    return true;
}

uint64_t UopGenerator::num_ready() const
{
    return 1;
}

Uop UopGenerator::take()
{
    std::shared_ptr<archx::Instruction> archx_instr =
        std::make_shared<archx::InstructionSimple>(program_counter, 0b0'0'0'100010'0'000000000000'00000'00000);
    UopSet archx_uops = furiosa::decode(archx_instr->opcode());
    AMPERE_ASSERT(archx_uops.size() == 1, "unexpected decode result");

    auto archx_uop = std::move(archx_uops[0]);

    archx_uop->src1 = archx::LReg::L1;
    archx_uop->src2 = archx::LReg::L2;
    archx_uop->dst = archx::LReg::L1;

    auto instr = trans_make<Instruction>(std::move(archx_instr), nullptr);
    auto uop = trans_make<Uop>(std::move(archx_uop), std::move(instr));
    program_counter += 4;
    return uop;
}

} // namespace ampere::core

/*
 * vim: sw=4:ts=4:et:si:cindent:cino=\:0,(0,g0,t0:
 */
