//
// Copyright (c) 2018-2019 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//

#ifndef PANTHERA_CORE_ADDR_H
#define PANTHERA_CORE_ADDR_H

#include "ampere/archx/addr.h"
#include "ampere/sc/transaction.h"

namespace ampere {
namespace core {

class ProgramCounterObj : public ampere::sc::Transaction
{
public:
    ProgramCounterObj(std::unique_ptr<ampere::archx::ProgramCounter> archx_program_counter)
        : archx_program_counter(std::move(archx_program_counter))
    {
    }

    std::unique_ptr<ampere::archx::ProgramCounter> archx_program_counter;
};

} // namespace core
} // namespace ampere

#endif // PANTHERA_CORE_ADDR_H

/*
 * vim: sw=4:ts=4:et:si:cindent:cino=\:0,(0,g0,t0:
 */
