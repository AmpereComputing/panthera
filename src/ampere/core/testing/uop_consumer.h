//
// Copyright (c) 2018-2019 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//

#pragma once

#include "ampere/sc/connect.h"
#include "ampere/sc/event.h"
#include "ampere/sc/module.h"
#include "ampere/core/uop.h"

namespace ampere::core {

class UopConsumer : public sc::Putter<Uop>
{
public:
    bool can_put() const override;
    void put(Uop uop) override;
    const Uop& get(uint64_t index = 0) const override;
    uint64_t size() const override;
    void release();
    std::vector<Uop> uops;
};

} // namespace ampere::core

/*
 * vim: sw=4:ts=4:et:si:cindent:cino=\:0,(0,g0,t0:
 */
