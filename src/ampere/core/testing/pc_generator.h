//
// Copyright (c) 2018-2019 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//

#pragma once

#include "ampere/sc/connect.h"
#include "ampere/sc/event.h"
#include "ampere/sc/module.h"
#include "ampere/archx/addr.h"

namespace ampere::core {

using namespace ampere::archx;

class PCGenerator : public sc::Taker<ProgramCounter>
{
public:
    explicit PCGenerator(bool active, bool increment, bool single);
    bool can_take() const override;
    void clear() override;
    void clear_if(const std::function<bool(const ProgramCounter &)> &pred) override { clear(); }
    void reset(ProgramCounter pc);
    uint64_t num_ready() const override;
    ProgramCounter take() override;
    unsigned width{1};

private:
    bool active;
    const bool increment;
    const bool single;
    ProgramCounter program_counter{0xdeadbeef00000000};
};

} // namespace ampere::core

/*
 * vim: sw=4:ts=4:et:si:cindent:cino=\:0,(0,g0,t0:
 */
