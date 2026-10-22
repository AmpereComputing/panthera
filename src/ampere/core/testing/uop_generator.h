//
// Copyright (c) 2018-2019 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//

#pragma once

#include "ampere/sc/connect.h"
#include "ampere/sc/event.h"
#include "ampere/sc/module.h"
#include "ampere/archx/addr.h"
#include "ampere/core/uop.h"

namespace ampere::core {

using namespace ampere::archx;

class UopGenerator : public sc::Taker<Uop>
{
public:
    bool can_take() const override;
    Uop take() override;
    void clear() override {}
    void clear_if(const std::function<bool(const Uop&)>& pred) override {}
    const Uop& peek(uint64_t index = 0) const override { throw std::runtime_error{"no peeking"}; }
    uint64_t num_ready() const override;
    unsigned width{1};

private:
    archx::ProgramCounter program_counter{0xdeadbeef00000000};
};

} // namespace ampere::core

/*
 * vim: sw=4:ts=4:et:si:cindent:cino=\:0,(0,g0,t0:
 */
