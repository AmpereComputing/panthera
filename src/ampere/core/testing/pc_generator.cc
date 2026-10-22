//
// Copyright (c) 2018-2019 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//

#include "ampere/core/testing/pc_generator.h"

namespace ampere::core {

PCGenerator::PCGenerator(const bool active, const bool increment, const bool single)
    : active(active), increment(increment), single(single)
{
}

bool PCGenerator::can_take() const
{
    return active;
}

uint64_t PCGenerator::num_ready() const
{
    return can_take() ? 1 : 0;
}

void PCGenerator::reset(ProgramCounter pc)
{
    program_counter = pc;
    active = true;
}

ProgramCounter PCGenerator::take()
{
    if (increment) {
        program_counter += 0x4;
    }
    if (single) {
        active = false;
    }
    return program_counter;
}

void PCGenerator::clear()
{
    throw std::runtime_error("Can't clear a PCGenerator");
}

} // namespace ampere::core

/*
 * vim: sw=4:ts=4:et:si:cindent:cino=\:0,(0,g0,t0:
 */
