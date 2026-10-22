//
// Copyright (c) 2018-2019 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//

#pragma once

#include "ampere/sc/sc.h"
#include "ampere/sc/transaction.h"
#include "ampere/archx/addr.h"
#include "ampere/archx/page_size.h"

namespace ampere::core {

using namespace ampere::sc;
using namespace ampere::archx;

class MMURespObj;
using MMUResp = ampere::sc::transaction_ptr<MMURespObj>;

class MMURespObj : public ampere::sc::Transaction
{
public:
    MMURespObj(uint64_t id, VirtAddr va, bool instr, bool store, PhysAddr pa, PageSize ps, uint16_t asid, bool fault)
        : id(id),
          virt_addr(va),
          is_instruction(instr),
          is_store(store),
          phys_addr(pa),
          page_size(ps),
          asid(asid),
          faulted(fault)
    {
    }

    uint64_t id;
    VirtAddr virt_addr;
    bool is_instruction;
    bool is_store;
    PhysAddr phys_addr;
    PageSize page_size;
    uint16_t asid;
    bool faulted{false};
};

} // namespace ampere::core

/*
 * vim: sw=4:ts=4:et:si:cindent:cino=\:0,(0,g0,t0:
 */
