//
// Copyright (c) 2018-2020 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//

#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <map>
#include <optional>
#include <utility>

#include "ampere/archx/translation.h"
#include "ampere/core/ixu_defs.h"
#include "ampere/sc/credit.h"
#include "ampere/sc/map.h"
#include "ampere/sc/sc.h"
#include "ampere/core/flush.h"
#include "ampere/core/mmu/req.h"
#include "ampere/core/mmu/op.h"
#include "ampere/core/mmu/pwt.h"
#include "ampere/core/mmu/mru.h"

namespace ampere::core {

using namespace ampere::sc;
using namespace ampere::archx;

struct TranslationState
{
    TranslationState(uint16_t asid, bool using_ttbr0, uint8_t granule_size)
        : asid(asid), using_ttbr0(using_ttbr0), granule_size(granule_size)
    {
    }
    TranslationState(uint16_t asid, const TranslationContext& context)
        : TranslationState(asid, context.using_ttbr0(), context.granule_size)
    {
    }

    uint16_t asid;
    bool using_ttbr0;
    uint8_t granule_size;
};

struct OTBEntry
{
    uint64_t otb_id;
    Credit section;
    MMUOp op{};            /* Operation being processed (even if not yet ready) */
    bool ready{false};     /* Is this ready to be scheduled */
    bool executing{false}; /* Is this currently executing */
    bool faulted{false};
    bool flushing{false};     /* Has a flush occurred since this entry was allocated */
    bool deallocating{false}; /* In the process of being deallocated, should not be duplicate-combined with */
    MMUMRUCopy mru;
    std::optional<PageSize> lookup_size;
    PhysAddr phys_addr;

    uint16_t asid;
    uint8_t granule_size;

    PageSize page_size;
    MMUReq request; /* oldest transaction served by this OTB entry (updated
                       upon duplicates received) */
    PWTEntry pwt;
    Credit fsm_credit;

    OTBEntry(uint64_t id, Credit&& section, MMUReq r, const MMUMRU& mru, const TranslationState& state)
        : otb_id(id),
          section(std::move(section)),
          ready{true},
          mru(mru),
          asid(state.asid),
          granule_size(state.granule_size),
          page_size(granule_size),
          request(r)
    {
    }

    void reset(const MMUMRU& new_mru, const TranslationState& state)
    {
        op = MMUOp::TLBLkp;
        ready = true;
        mru = new_mru;
        this->asid = state.asid;
        this->granule_size = state.granule_size;
    }

    void execute()
    {
        executing = true;
        ready = false;
    }

    bool using_ttbr0() const { return TranslationContext::uses_ttbr0(request->virt_addr); }
    bool non_speculative() const { return request && request->non_speculative; }
    uint8_t page_bits() const { return (faulted || flushing) ? 0 : page_size.page_bits(); }

    bool can_schedule() const { return ready && !executing; }

    bool can_schedule_lookup() const { return (op == MMUOp::TLBLkp || op == MMUOp::Recycle) && can_schedule(); }

    bool can_schedule_walk() const { return op == MMUOp::PageWalk && can_schedule(); }
};

class OTB : public Module
{
    Param<size_t> total_entries{"entries", "number of OTB entries", 8};
    Param<size_t> deallocation_latency{
        "deallocation_latency", "Length of time after response is sent OTB entry will be deallocated", 2};
    Param<bool> combine_duplicates{
        "combine_duplicates",
        "Combine duplicate requests (to same 4k region) to one OTB entry if both loads/stores",
        true};
    Param<bool> _64k_duplicate_check{
        "64k_duplicate_check", "Combine duplicate requests to same 64k region if granule size is 64k", false};
    Param<bool> stall_same_page_duplicates{"stall_same_page_duplicates", "stall_same_page_duplicates", false};
    Param<size_t> flush_allocation_lockout_cycles{
        "flush_allocation_lockout_cycles",
        "Number of cycles the MMU cannot accept new OTB allocations following a flush",
        5};
    Param<bool> reserve_for_oldest_after_full{
        "reserve_for_oldest_after_full",
        "Reserve an OTB entry (next available) for the oldest after reaching full",
        false};
    Param<bool> round_robin_scheduling{
        "round_robin_scheduling", "Check round-robin candidate first for OTB scheduling", false};
    Param<bool> prioritize_scheduling_non_spec{
        "prioritize_scheduling_non_spec", "Prioritize scheduling non-speculative OTB entry", false};
    Param<bool> alternate_first_last_scheduling{
        "alternate_first_last_scheduling", "Alternate between find-first and find-last for OTB scheduling", false};
    Param<bool> sectioned{"sectioned", "Section OTB by request type", false};
    Param<unsigned> _lsu_sections{"lsu_sections", "Number of LSU sections", 1};

    const unsigned shared_section;
    sc_map<CreditPool> sections;

    std::map<uint64_t, OTBEntry> entries;
    uint64_t reservation{0};
    size_t remaining_allocation_lockout_cycles{0};
    bool notify_availablity{false};

    OutIn<uint64_t> deallocation{"deallocation"};

    void deallocate(OTBEntry& otbentry, bool now);
    void deallocate_only(OTBEntry& otbentry);

    static std::vector<std::string> section_names(unsigned lsu_sections);

public:
    struct
    {
        Event allocate{"allocate", "OTB allocate"};
        Event deallocate{"deallocate", "OTB deallocate"};
        Event full{"full", "OTB full"};
        Event almost_full{"almost_full", "OTB almost full (reservation by non-speculative request)"};
        Event duplicate_request{"duplicate_request", "Merged with older duplicate request"};
        Event subsuming_request{"subsuming_request", "Subsuming younger duplicate request"};
        Event non_empty{"non_empty", "OTB is not empty"};
        EventData<unsigned> occupancy{"occupancy", "Number of OTB entries active"};
        EventData<uint64_t> otb_id{"otb_id", "OTB entry's ID"};
        EventData<unsigned> asid{"asid", "ASID"};
        EventData<bool> instr{"instruction", "Is instruction request"};
        EventData<bool> flushed{"flushed", "OTB entry was flushed before it returned a valid response"};
        EventData<unsigned> page_bits{"page_bits", "Number of bits needed to represent page size"};
    } event;

    struct ScheduleInfo
    {
        unsigned round_robin_candidate{0};
        bool use_last = false;
    };

    OutOpt<bool> available{"available"};

    using otb_iterator = decltype(entries)::iterator;
    explicit OTB(const sc_core::sc_module_name& name);

    bool run() override;
    void end_of_elaboration() override;

    bool is_sectioned() const { return sectioned; }
    unsigned lsu_sections() const { return _lsu_sections; }
    unsigned non_shared_sections() const { return _lsu_sections + 1; }
    unsigned total_sections() const { return non_shared_sections() + 1; }

    std::optional<uint64_t> allocate_id() const;
    bool locked_out() const { return remaining_allocation_lockout_cycles; }

    std::pair<std::optional<uint64_t>, bool> try_combine(MMUReq mmureq, const TranslationState& state);
    std::optional<uint64_t> try_allocate(MMUReq mmureq,
                                         MMUMRU& mru,
                                         const TranslationState& state,
                                         bool duplicated = false);

    void deallocate(OTBEntry& otbentry) { deallocate(otbentry, false); }
    void deallocate_immediately(OTBEntry& otbentry) { deallocate(otbentry, true); }

    otb_iterator schedule(std::function<bool(const OTBEntry&)> pred, ScheduleInfo& info);
    void schedule_entries(std::function<bool(OTBEntry&)> func,
                          std::function<bool(const OTBEntry&)> pred,
                          OTB::ScheduleInfo& info);

    OTBEntry* find(uint64_t otb_id)
    {
        if (auto entry = entries.find(otb_id); entry != entries.end())
            return &entry->second;
        return nullptr;
    }
    OTBEntry& at(uint64_t otb_id) { return entries.at(otb_id); }

    auto begin() { return entries.begin(); }
    auto end() { return entries.end(); }
    auto begin() const { return entries.begin(); }
    auto end() const { return entries.end(); }

    bool empty() const { return entries.empty(); }
    bool full() const { return entries.size() == total_entries; }
    auto size() const { return entries.size(); }

    void clear();
    void flush(Flush flush);

    void dump(std::ostream&) const override;
};

} // namespace ampere::core

/*
 * vim: sw=4:ts=4:et:si:cindent:cino=\:0,(0,g0,t0:
 */
