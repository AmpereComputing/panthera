//
// Copyright (c) 2018-2019 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//

#pragma once

#include "ampere/sc/connect.h"
#include "ampere/sc/event.h"
#include "ampere/sc/module.h"
#include "ampere/core/instruction.h"

namespace ampere::core {

class InstructionConsumer : public sc::Putter<Instruction>
{
public:
    bool can_put() const override;
    void put(Instruction instruction) override;
    const Instruction& get(uint64_t index = 0) const override;
    uint64_t size() const override;
    void set_max_size(size_t size)
    {
        limit_size = true;
        max_size = size;
    }
    void increase_max_size(size_t size) { max_size += size; }
    void disable_max_size() { limit_size = false; }
    void consume_instructions(size_t n);
    void release(); // release all credits
    std::vector<Instruction> instructions;

private:
    bool limit_size{false};
    size_t max_size{0};
};

} // namespace ampere::core

/*
 * vim: sw=4:ts=4:et:si:cindent:cino=\:0,(0,g0,t0:
 */
