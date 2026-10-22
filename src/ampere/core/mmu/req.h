//
// Copyright (c) 2018-2019 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//

#pragma once

#include "ampere/core/ixu_defs.h"
#include "ampere/sc/sc.h"
#include "ampere/sc/transaction.h"
#include "ampere/archx/addr.h"

namespace ampere::core {

using namespace ampere::sc;
using namespace ampere::archx;

class MMUReqObj;
using MMUReq = ampere::sc::transaction_ptr<MMUReqObj>;
using ConstMMUReq = ampere::sc::const_ptr<MMUReq>;

class MMUReqObj : public ampere::sc::Transaction
{
public:
    MMUReqObj(uint64_t id, VirtAddr va, bool is_store, bool non_speculative = false)
        : id(id), virt_addr(va), is_store(is_store), non_speculative(non_speculative)
    {
    }
    MMUReqObj(uint64_t id, ProgramCounter pc) : id(id), virt_addr(pc2va(pc)), is_instruction(true) {}

    uint64_t id;
    VirtAddr virt_addr;
    bool is_instruction{false};
    bool is_store{false};
    bool non_speculative{false};
    unsigned channel{0};
    unsigned section{0};
};

} // namespace ampere::core

/*
 * vim: sw=4:ts=4:et:si:cindent:cino=\:0,(0,g0,t0:
 */
