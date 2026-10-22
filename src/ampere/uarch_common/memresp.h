//
// Copyright (c) 2018-2020 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//

#ifndef AMPERE_UARCH_COMMON_MEMRESP_H
#define AMPERE_UARCH_COMMON_MEMRESP_H

#include "ampere/sc/sc.h"
#include "ampere/sc/transaction.h"
#include "ampere/archx/addr.h"
#include "ampere/uarch_common/memop.h"

namespace ampere::uarch_common {

using namespace ampere::sc;
using namespace ampere::archx;

class MemRespObj;
using MemResp = ampere::sc::transaction_ptr<MemRespObj>;

class MemRespObj : public ampere::sc::Transaction
{
public:
    explicit MemRespObj(uint64_t id,
                        PhysAddr phys_addr,
                        bool value,
                        MemOp::Type type = MemOp::Type::NO_TYPE,
                        MemOp::AtomicType atomic_type = MemOp::ATOMIC_NONE,
                        bool unique = false)
        : id(id), phys_addr(phys_addr), value(value), type(type), atomic_type(atomic_type), unique(unique)
    {
    }

    uint64_t id;
    PhysAddr phys_addr;
    bool value;
    MemOp::Type type; // type of transaction that this is a response to
    MemOp::AtomicType atomic_type;
    bool unique = false;

    bool atomic() const { return atomic_type != MemOp::ATOMIC_NONE; }
};

} // namespace ampere::uarch_common

#endif // AMPERE_UARCH_COMMON_MEMRESP_H

/*
 * vim: sw=4:ts=4:et:si:cindent:cino=\:0,(0,g0,t0:
 */
