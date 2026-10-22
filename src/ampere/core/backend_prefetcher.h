#pragma once

//
// Copyright (c) 2021-2022 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//

#include "ampere/archx/addr.h"
#include "ampere/archx/page_size.h"
#include "ampere/archx/el.h"
#include "ampere/sc/transaction.h"
#include "ampere/core/uop.h"
#include "ampere/core/prefetcher.h"

#include <optional>
#include <type_traits>

make_ns_enum(ampere::core, PrefetchType,
    NONE,  // demand
    STA,
    STORE,
    PCP,
    GRETCH,
    L1BOP,
    VA_L2PF
);

namespace ampere::core {

AMPERE_EVENT_DATA_FUNC(PrefetchType x, prefetch_type, static_cast<unsigned>(x));

template<typename T>
class BackendPrefetcher : public Prefetcher<T>, poly<BackendPrefetcher<T>>
{
public:
    using request_t = T;
    using addr_t = typename Prefetcher<T>::addr_t;
    using prefetch_type_t = typename Prefetcher<T>::prefetch_type_t;

    explicit BackendPrefetcher(sc_core::sc_module_name name) : Prefetcher<T>(name) {}

    void drop(T req, PrefetchDropReason reason)
    {
        switch (reason) {
        case PrefetchDropReason::PAGE_MISS_NO_RESOURCES:
            event.dropped_pob_full(req, PrefetcherData<T>::get_addr(req));
        case PrefetchDropReason::PAGE_MISS:
            event.tlb_miss(req, PrefetcherData<T>::get_addr(req));
            break;
        case PrefetchDropReason::CACHE_MISS_NO_RESOURCES:
            event.dropped_ofb_full(req, PrefetcherData<T>::get_addr(req));
            break;
        case PrefetchDropReason::CACHE_HIT:
            event.hit_dl1(req, PrefetcherData<T>::get_addr(req));
            break;
        case PrefetchDropReason::CACHE_MISS_IN_FLIGHT:
        case PrefetchDropReason::CACHE_MISS_PREFETCH_IN_FLIGHT:
            event.hit_ofb(req, PrefetcherData<T>::get_addr(req));
            break;
        default:
            break;
        }

        Prefetcher<T>::drop(req, reason);
    }

    // Train at allocate
    void allocate(uint64_t id, T req) { allocate_impl(id, req); }
    // Train at issue
    void issue(uint64_t id, T req) { issue_impl(id, req); }
    // Train at execute
    void execute(uint64_t index, T req, bool hit, bool prefetch, bool demand, prefetch_type_t prefetch_type)
    {
        if (prefetch && demand && prefetch_type == this->prefetch_type()) {
            if (hit)
                event.demand_hit_cacheline(req, PrefetcherData<T>::get_addr(req));
            else
                event.demand_hit_ofb(req, PrefetcherData<T>::get_addr(req));
        }

        execute_impl(index, req, hit, prefetch, demand, prefetch_type);
    }
    // Train at writeback
    void writeback(uint64_t id, T req) { writeback_impl(id, req); }
    // Train at commit, returns if commit should not be stalled next
    bool commit(uint64_t id, T req) { return commit_impl(id, req); }

    virtual bool can_replay() const { return false; }
    virtual bool can_prefetch_page(T req) const { return true; }

    virtual bool can_allocate_pob(T req, archx::EL current_level) const { return true; }
    bool can_allocate_pob(archx::EL current_level) const { return can_allocate_pob({}, current_level); }

    virtual bool can_issue_prefetch() const { return true; }
    virtual bool can_issue_prefetch(T req) const { return can_issue_prefetch(); }

    virtual bool can_allocate_ofb() const { return true; }
    virtual bool can_allocate_ofb(T req) const { return can_allocate_ofb(); }

protected:
    virtual void allocate_impl(uint64_t id, T req) {}
    virtual void issue_impl(uint64_t id, T req) {}
    virtual void execute_impl(
        uint64_t index, T req, bool hit, bool prefetch, bool demand, prefetch_type_t prefetch_type)
    {
    }
    virtual void writeback_impl(uint64_t id, T req) {}
    virtual bool commit_impl(uint64_t id, T req) { return true; }

    virtual void prefetch_used_impl(PhysAddr addr, PrefetchType type) {}

    struct
    {
        Event hit_ofb{"hit_ofb", "Prefetch request hit an outstanding fill buffer"};
        Event hit_dl1{"hit_dl1", "Prefetch request hit in the DL1"};
        Event demand_hit_cacheline{"demand_hit_cacheline",
                                   "demand request hits a cacheline in DL1 brought in by prefetcher"};
        Event demand_hit_ofb{"demand_hit_ofb", "demand request hits a prefetcher-allocated OFB"};
        Event dropped_ofb_full{"dropped_ofb_full", "PCP dropped since L2 busy or no OFBs available"};
        Event dropped_pob_full{"dropped_pob_full", "Prefetch candidate is dropped because of no space in POB"};
        Event tlb_miss{"tlb_miss", "Prefetch request misses TLB"};
    } event;
};

struct L1PrefetchEntry;

struct BackendMemReq
{
    Txn transaction;
    uint64_t alloc_id{0};
    VirtAddr virt_addr;
    std::optional<PhysAddr> phys_addr;
    unsigned data_size{0};
    ProgramCounter pc;
    ProgramCounter consumer_pc;
    Uop uop = nullptr;
    std::optional<EL> exception_level;
    uint64_t memory_buffer_id{0};
    PrefetchType prefetch_type = PrefetchType::NONE;
    MemOp::PrefetchType l2_prefetch_type{MemOp::NONE};
    unsigned pipe{0};

    BackendMemReq() = default;
    BackendMemReq(const Uop &uop)
        : transaction(uop),
          alloc_id(uop->alloc_id),
          virt_addr(*uop->virt_addr),
          phys_addr(uop->phys_addr),
          data_size(uop->mem_datasize()),
          pc(uop->pc()),
          uop(uop),
          exception_level(uop->ainst()->state().exception_level()),
          memory_buffer_id(uop->is_load() ? uop->lob_id_val : uop->sobid->get_id()),
          pipe(uop->sch.lsu_scheduler_pipe())
    {
    }

    BackendMemReq(const MemReq &req)
        : transaction(req),
          alloc_id(req->id),
          virt_addr(req->virt_addr),
          phys_addr(req->phys_addr),
          data_size(PhysAddr::LINE_SIZE),
          pc(req->pc)
    {
    }

    BackendMemReq(VirtAddr virt_addr, std::optional<PhysAddr> phys_addr) : virt_addr(virt_addr), phys_addr(phys_addr) {}

    BackendMemReq(PrefetchTranslateRequest<MemReq> req)
        : transaction(req.transaction),
          virt_addr(req.virt_addr),
          data_size(PhysAddr::LINE_SIZE),
          prefetch_type(PrefetchType::VA_L2PF),
          l2_prefetch_type(req.prefetch_type)
    {
    }

    BackendMemReq(VirtAddr virt_addr,
                  std::optional<PhysAddr> phys_addr,
                  const Uop &trigger_uop,
                  PrefetchType prefetch_type = PrefetchType::NONE)
        : transaction(trigger_uop),
          alloc_id(trigger_uop ? trigger_uop->alloc_id : 0),
          virt_addr(virt_addr),
          phys_addr(phys_addr),
          data_size(trigger_uop ? trigger_uop->mem_datasize() : 0),
          pc(trigger_uop ? trigger_uop->pc() : 0_pc),
          uop(trigger_uop),
          exception_level(trigger_uop ? std::make_optional(trigger_uop->ainst()->state().exception_level())
                                      : std::nullopt),
          memory_buffer_id(
              trigger_uop ? (trigger_uop->is_load() ? trigger_uop->lob_id_val : trigger_uop->sobid->get_id()) : 0),
          prefetch_type(prefetch_type),
          pipe(trigger_uop ? trigger_uop->sch.lsu_scheduler_pipe() : 0)
    {
    }

    BackendMemReq(VirtAddr virt_addr,
                  std::optional<PhysAddr> phys_addr,
                  const BackendMemReq &req,
                  PrefetchType prefetch_type = PrefetchType::NONE)
        : transaction(req),
          alloc_id(req.alloc_id),
          virt_addr(virt_addr),
          phys_addr(phys_addr),
          data_size(req.data_size),
          pc(req.pc),
          consumer_pc(req.consumer_pc),
          uop(req.uop),
          exception_level(req.exception_level),
          memory_buffer_id(req.memory_buffer_id),
          prefetch_type(prefetch_type),
          pipe(req.pipe)
    {
    }

    BackendMemReq(const L1PrefetchEntry &entry, PrefetchType prefetch_type);

    explicit operator Txn() const { return transaction; }

    L1PrefetchEntry as_entry() const;
};

inline ampere::sc::EventArg<std::uint64_t> to_event_arg(const BackendMemReq &req)
{
    return to_event_arg(req.transaction);
}

template<>
struct PrefetcherData<BackendMemReq>
{
    static VirtAddr get_addr(BackendMemReq req) { return req.virt_addr; }
    static archx::PhysAddr get_phys_addr(BackendMemReq req) { return *req.phys_addr; }
    static PrefetchType get_type(BackendMemReq req) { return req.prefetch_type; }
};

/* Common data structure representing prefetch entry for L1 data prefetchers: PCP and Gretch */
struct L1PrefetchEntry
{
    archx::VirtAddr virt_addr{0}; // address
    std::optional<archx::PhysAddr> phys_addr;
    Uop trigger_uop; // uop which triggered
    sc::Txn parent_txn;
    ProgramCounter consumer_pc{0x0_pc}; // PC of uop which is expected to consume the prefetched data
    PageSize::Size page_size = PageSize::Size::PS_4KB;
    MemOp::PrefetchType l2_prefetch_type{MemOp::NONE};

    // These fields assigned at launch time in lsu.cc
    unsigned int pipe{0}; // LSU pipe/partition

    L1PrefetchEntry() = default;
    L1PrefetchEntry(archx::VirtAddr addr, Uop uop, ProgramCounter consumer_pc = 0_pc)
        : virt_addr(addr), trigger_uop(uop), parent_txn(uop), consumer_pc(consumer_pc)
    {
    }
    L1PrefetchEntry(archx::VirtAddr addr, sc::Txn parent, MemOp::PrefetchType l2_type)
        : virt_addr(addr), trigger_uop(nullptr), parent_txn(parent), consumer_pc(0_pc), l2_prefetch_type(l2_type)
    {
    }
    L1PrefetchEntry(
        archx::VirtAddr addr, Uop uop, sc::Txn parent, ProgramCounter consumer_pc, MemOp::PrefetchType l2_type)
        : virt_addr(addr), trigger_uop(uop), parent_txn(parent), consumer_pc(consumer_pc), l2_prefetch_type(l2_type)
    {
    }

    /* create a mmureq id for prefetch-induced translation request
        by OR-ing upper 32 bits of trigger uop's alloc id with prefetch id */
    uint64_t mmu_req_id(uint64_t &prefetch_id) const
    {
        prefetch_id++;
        uint64_t m_id = (prefetch_id << 32) | parent_txn->txid().value;
        return m_id;
    }

    uint8_t pipe_mask() const { return 1U << pipe; }
};

class L1P_None : public BackendPrefetcher<BackendMemReq>, implementation<L1P_None, BackendPrefetcher<BackendMemReq>>
{
public:
    explicit L1P_None(sc_core::sc_module_name name) : BackendPrefetcher<BackendMemReq>(name) {}

    bool can_replay() const override { return false; }
    bool can_prefetch_page(BackendMemReq req) const override { return false; }
    bool can_allocate_pob(BackendMemReq req, archx::EL current_level) const override { return false; }
    bool can_issue_prefetch() const override { return false; }
};

} // namespace ampere::core

/*
 * vim: sw=4:ts=4:et:si:cindent:cino=\:0,(0,g0,t0:
 */
