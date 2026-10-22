//
// Copyright (c) 2018-2019 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//

#include "ampere/core/testing/pc_consumer.h"

namespace ampere::core {

bool PCConsumer::can_put() const
{
    return true;
}

void PCConsumer::put(ProgramCounter pc)
{
    pcs.push_back(std::move(pc));
}

const ProgramCounter& PCConsumer::get(uint64_t index) const
{
    return pcs.at(index);
}

uint64_t PCConsumer::size() const
{
    return pcs.size();
}

} // namespace ampere::core

/*
 * vim: sw=4:ts=4:et:si:cindent:cino=\:0,(0,g0,t0:
 */
