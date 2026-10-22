//
// Copyright (c) 2018-2020 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//

#pragma once

#include <cstdint>
#include "ampere/archx/page_size.h"
#include "ampere/sc/sc.h"
#include "ampere/sc/transaction.h"
#include "ampere/archx/addr.h"
#include "ampere/uarch_common/memop.h"

namespace ampere::uarch_common {

using namespace ampere::sc;
using namespace ampere::archx;

class MemReqObj;
using MemReq = ampere::sc::transaction_ptr<MemReqObj>;
using ConstMemReq = ampere::sc::const_ptr<MemReq>;

namespace Request {
struct L1Contents
{
    std::list<PhysAddr> set_addrs;
    bool no_clear_l1_bits = false;
    void add(PhysAddr addr) { set_addrs.push_back(addr); }
};
using Fill = std::pair<MemReq, L1Contents>;
} // namespace Request

class MemReqObj : public ampere::sc::Transaction
{
public:
    explicit MemReqObj(int64_t id,
                       PhysAddr phys_addr,
                       MemOp::Requester origin,
                       MemOp::Type type = MemOp::Type::READ,
                       unsigned size = 8,
                       bool nontemporal = false,
                       MemOp::PrefetchType prefetch_type = MemOp::PrefetchType::NONE,
                       MemOp::BarrierType barrier_type = MemOp::BarrierType::BAR_NONE,
                       MemOp::AtomicType atomic_type = MemOp::ATOMIC_NONE,
                       MemOp::SnoopType snoop_type = MemOp::SNOOP_NONE,
                       PageSize::Size page_size = PageSize::Size::INVALID,
                       uint64_t bytemask = 0,
                       VirtAddr virt_addr = 0x0_va,
                       ProgramCounter pc = 0x0_pc)
        : id(id),
          phys_addr(phys_addr),
          origin(origin),
          type(type),
          size(size),
          nontemporal(nontemporal),
          prefetch_type(prefetch_type),
          barrier_type(barrier_type),
          atomic_type(atomic_type),
          snoop_type(snoop_type),
          page_size(page_size),
          bytemask(bytemask),
          virt_addr(virt_addr),
          pc(pc)
    {
        if (bytemask == 0)
            this->bytemask = util::mask(size);
    }

    inline static MemReqObj code(int64_t id, PhysAddr phys_addr, PageSize::Size page_size = PageSize::Size::INVALID)
    {
        return MemReqObj(id,
                         phys_addr,
                         MemOp::ICF,
                         MemOp::READ,
                         8,
                         false,
                         MemOp::PrefetchType::NONE,
                         MemOp::BAR_NONE,
                         MemOp::ATOMIC_NONE,
                         MemOp::SNOOP_NONE,
                         page_size);
    }

    inline static MemReqObj store(int64_t id,
                                  ProgramCounter pc,
                                  PhysAddr phys_addr,
                                  VirtAddr virt_addr,
                                  unsigned size,
                                  uint64_t bytemask,
                                  PageSize::Size page_size,
                                  MemOp::BarrierType barrier_type = MemOp::BAR_NONE,
                                  MemOp::AtomicType atomic_type = MemOp::ATOMIC_NONE,
                                  bool nontemporal = false)
    {
        return MemReqObj(id,
                         phys_addr,
                         MemOp::LSU,
                         MemOp::WRITE,
                         size,
                         nontemporal,
                         MemOp::PrefetchType::NONE,
                         barrier_type,
                         atomic_type,
                         MemOp::SNOOP_NONE,
                         page_size,
                         bytemask,
                         virt_addr,
                         pc);
    }

    inline static MemReqObj load(int64_t id,
                                 ProgramCounter pc,
                                 PhysAddr phys_addr,
                                 VirtAddr virt_addr,
                                 unsigned size,
                                 uint64_t bytemask,
                                 PageSize::Size page_size,
                                 bool nontemporal = false,
                                 MemOp::Requester origin = MemOp::LSU,
                                 MemOp::PrefetchType prefetch_type = MemOp::PrefetchType::NONE)
    {
        return MemReqObj(id,
                         phys_addr,
                         origin,
                         MemOp::READ,
                         size,
                         nontemporal,
                         prefetch_type,
                         MemOp::BAR_NONE,
                         MemOp::ATOMIC_NONE,
                         MemOp::SNOOP_NONE,
                         page_size,
                         bytemask,
                         virt_addr,
                         pc);
    }

    inline static MemReqObj load_prefetch(int64_t id,
                                          ProgramCounter pc,
                                          PhysAddr phys_addr,
                                          VirtAddr virt_addr,
                                          unsigned size,
                                          uint64_t bytemask,
                                          PageSize::Size page_size,
                                          bool nontemporal = false,
                                          MemOp::Requester origin = MemOp::LSU,
                                          MemOp::PrefetchType prefetch_type = MemOp::PrefetchType::NONE)
    {
        auto req = MemReqObj(id,
                             phys_addr,
                             origin,
                             MemOp::READ,
                             size,
                             nontemporal,
                             prefetch_type,
                             MemOp::BAR_NONE,
                             MemOp::ATOMIC_NONE,
                             MemOp::SNOOP_NONE,
                             page_size,
                             bytemask,
                             virt_addr,
                             pc);
        req.from_prefetch = true;
        return req;
    }

    inline static MemReqObj eviction(int64_t id,
                                     PhysAddr phys_addr,
                                     MemOp::Requester origin,
                                     MemOp::Type type = MemOp::Type::WRITE)
    {
        return MemReqObj(id,
                         phys_addr,
                         origin,
                         type,
                         PhysAddr::LINE_SIZE,
                         false,
                         MemOp::PrefetchType::NONE,
                         MemOp::BarrierType::BAR_NONE,
                         MemOp::AtomicType::ATOMIC_NONE,
                         MemOp::SnoopType::SNOOP_NONE,
                         PageSize::Size::INVALID,
                         util::mask(PhysAddr::LINE_BITWIDTH));
    }

    inline static MemReqObj snoop(int64_t id, PhysAddr phys_addr, MemOp::Requester origin, MemOp::SnoopType snoop_type)
    {
        return MemReqObj(id,
                         phys_addr,
                         origin,
                         MemOp::Type::SNOOP,
                         8,
                         false,
                         MemOp::PrefetchType::NONE,
                         MemOp::BAR_NONE,
                         MemOp::ATOMIC_NONE,
                         snoop_type);
    }

    inline static MemReqObj prefetch(int64_t id,
                                     PhysAddr phys_addr,
                                     VirtAddr virt_addr,
                                     MemOp::Requester origin,
                                     MemOp::PrefetchType prefetch_type,
                                     PageSize::Size page_size = PageSize::Size::INVALID)
    {
        auto req = MemReqObj(id,
                             phys_addr,
                             origin,
                             MemOp::READ,
                             8,
                             false,
                             prefetch_type,
                             MemOp::BAR_NONE,
                             MemOp::ATOMIC_NONE,
                             MemOp::SNOOP_NONE,
                             page_size,
                             0,
                             virt_addr);
        req.from_prefetch = true;
        return req;
    }

    inline static MemReqObj prefetch(int64_t id,
                                     PhysAddr phys_addr,
                                     MemOp::Requester origin,
                                     MemOp::PrefetchType prefetch_type,
                                     PageSize::Size page_size = PageSize::Size::INVALID)
    {
        return prefetch(id, phys_addr, 0x0_va, origin, prefetch_type, page_size);
    }

    int64_t id;
    PhysAddr phys_addr;
    MemOp::Requester origin;
    MemOp::Type type;
    unsigned size;
    bool nontemporal{false};
    bool from_prefetch{false};

    MemOp::PrefetchType prefetch_type{MemOp::PrefetchType::NONE};
    MemOp::BarrierType barrier_type{MemOp::BarrierType::BAR_NONE};
    MemOp::AtomicType atomic_type{MemOp::AtomicType::ATOMIC_NONE};
    MemOp::SnoopType snoop_type{MemOp::SnoopType::SNOOP_NONE};
    PageSize::Size page_size{PageSize::Size::INVALID};

    uint64_t bytemask;
    VirtAddr virt_addr;   // Only required to be set on read requests generated by LSU
    ProgramCounter pc{0}; // for use by L2 replacement predictors

    bool atomic() const { return atomic_type != MemOp::ATOMIC_NONE; }
    bool atomic_load() const { return atomic_type == MemOp::ATOMIC_LOAD || atomic_type == MemOp::ATOMIC_LOAD_WIDE; }
    bool ordered() const { return barrier_type != MemOp::BarrierType::BAR_NONE || atomic(); }
    bool exclusive() const
    {
        return barrier_type == MemOp::BAR_STORE_EXCL || barrier_type == MemOp::BAR_STORE_EXCL_RELEASE;
    }

    bool is_prefetch() const
    {
        return from_prefetch || origin == MemOp::PREFETCH || prefetch_type != MemOp::PrefetchType::NONE;
    }
    bool is_write() const { return type == MemOp::WRITE || type == MemOp::READUNIQUE || type == MemOp::MAKEUNIQUE; }

    unsigned store_types() const
    {
        return MemOp::StoreType::STORE_ANY | barrier_to_store_type(barrier_type)
               | (atomic() ? MemOp::StoreType::STORE_ATOMIC : 0);
    }
};

} // namespace ampere::uarch_common

/*
 * vim: sw=4:ts=4:et:si:cindent:cino=\:0,(0,g0,t0:
 */
