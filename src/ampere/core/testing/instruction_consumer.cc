//
// Copyright (c) 2018-2019 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//

#include <algorithm>

#include "ampere/core/testing/instruction_consumer.h"

namespace ampere::core {

bool InstructionConsumer::can_put() const
{
    return !limit_size || instructions.size() < max_size;
}

void InstructionConsumer::put(Instruction instruction)
{
    instructions.emplace_back(std::move(instruction));
}

const Instruction& InstructionConsumer::get(uint64_t index) const
{
    return instructions.at(index);
}

uint64_t InstructionConsumer::size() const
{
    return instructions.size();
}

// Remove instructions from the consumer, allowing more to be consumed if a
// `max_size` is in place, and also freeing associated credits
void InstructionConsumer::consume_instructions(size_t n = 1)
{
    auto end = instructions.end();
    if (n < instructions.size())
        end = instructions.begin() + n;

    for (auto it = instructions.begin(); it != end; it++) {
        auto& inst = *it;
        release_credit(inst->fifoid);
        if (inst->is_fetch_exit() && inst->fetch->feq_credit)
            release_credit(inst->fetch->feq_credit);
    }
    instructions.erase(instructions.begin(), end);
}

void InstructionConsumer::release()
{
    consume_instructions(instructions.size());
}

} // namespace ampere::core

/*
 * vim: sw=4:ts=4:et:si:cindent:cino=\:0,(0,g0,t0:
 */
