//
// Copyright (c) 2018-2019 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//

#pragma once

#include "ampere/sc/connect.h"
#include "ampere/sc/module.h"
#include "ampere/archx/addr.h"

namespace ampere::core {

using namespace ampere::archx;

class PCConsumer : public sc::Putter<ProgramCounter>
{
public:
    bool can_put() const override;
    void put(ProgramCounter pc) override;
    const ProgramCounter& get(uint64_t index = 0) const override;
    uint64_t size() const override;
    std::vector<ProgramCounter> pcs;
};

} // namespace ampere::core

/*
 * vim: sw=4:ts=4:et:si:cindent:cino=\:0,(0,g0,t0:
 */
