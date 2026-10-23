//
// Copyright (c) 2018-2022 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//

#pragma once

#include <sysc/communication/sc_interface.h>
#include "ampere/archx/el.h"
#include "ampere/sc/port.h"
#include "ampere/sc/putter.h"
#include "ampere/sc/sc.h"
#include "ampere/core/uop.h"
#include "ampere/archx/addr.h"
#include "ampere/sc/transaction.h"
#include "ampere/uarch_common/cache.h"
#include "ampere/core/prefetcher.h"
#include "ampere/core/backend_prefetcher.h"
#include "ampere/uarch_common/memreq.h"

#include <boost/circular_buffer.hpp>
#include <optional>

namespace ampere::core {

using namespace ampere::sc;
using namespace archx;
using namespace uarch_common;

/*************************************************************************
 * L1 Prefetcher: Program Counter Prefetcher (PCP) is based on detecting
 * address patterns within subsequent iterations of the same instruction
 * loops. We create a table to hold each PC's state machine for triggering
 * prefetches, and update that on every load.
 *************************************************************************/

enum class StrideTrainingMethod {
    HI_TO_LO,
    LO_TO_HI,
    STABLE_HI
};

// Training feedback to other units/prefetchers
template<typename T>
class StrideTraining : public sc_core::sc_interface
{
public:
    using Method = StrideTrainingMethod;
    virtual void train(T req, Method method) = 0;
    virtual std::optional<T> gather() = 0;
    virtual bool should_inc_multiplier(PrefetchType prefetch_type) const { return false; }
};

// #define PCP_MAX_INFLIGHT 32

// Program Counter Prefetcher
class PCP : public BackendPrefetcher<BackendMemReq>
{
    enum {
        PCP_INIT,      // Initial state (entry never trained)
        PCP_SEARCH,    // Inactive, training on stride
        PCP_ACTIVE_HI, // Active, with high(er) confidence
        PCP_ACTIVE_LO  // Active (potentially), with Low(er) confidence
    };

    // one entry per program counter hash
    struct Entry
    {
        explicit Entry(int state) : state{state} {}
        explicit Entry(int state, ProgramCounter pc) : pc{pc}, last_pc{pc}, last_last_pc{pc}, state{state} {}
        // Last address seen, in Furiosa it is 12 bits virtual [11:0]
        // Need all bits for stride calculation. Would require 13 bit adder.
        archx::VirtAddr last_vaddr{0};

        // Last prefetched address
        archx::VirtAddr last_prefetched_vaddr{0};

        // stride, should be one more bit than addr, for sign. Hence 13 bits.
        int stride{0};

        ProgramCounter pc;

        // These are for event stream only
        // They keep track of the full previous pc used to index into this
        // entry. We use them to measure aliasing when multiple PCs index into
        // the same loacation in the table.
        ProgramCounter last_pc;
        ProgramCounter last_last_pc;

        // state in state machine (two bits)
        int state;

        // id of stride hit, first and second. (for debug)
        uint64_t stride_alloc_id[2]{0};

        // The current stride multiplier, used with adaptive stride algorithm
        unsigned stride_mult{2};

        int offset() const { return static_cast<int>(stride_mult) * stride; }

        void push_stride_alloc_id(uint64_t alloc_id)
        {
            stride_alloc_id[1] = stride_alloc_id[0];
            stride_alloc_id[0] = alloc_id;
        }

        /*used only for event stream*/
        void update_last_pc(ProgramCounter pc)
        {
            last_last_pc = last_pc;
            last_pc = pc;
        }

        int64_t allocation_distance(uint64_t alloc_id) const
        {
            return static_cast<int64_t>(alloc_id) - static_cast<int64_t>(stride_alloc_id[0]);
        }

        int64_t allocation_distance() const
        {
            return static_cast<int64_t>(stride_alloc_id[0]) - static_cast<int64_t>(stride_alloc_id[1]);
        }

        // Check if younger than last update (i.e. in order)
        bool younger_than_last(uint64_t alloc_id) const { return allocation_distance(alloc_id) >= 0; }
    };
    class Table : public ManagedCache<ProgramCounter, PCP::Entry>
    {
        Param<size_t> tag_bits_param{"tag_bits", "tag bits used", 0};

        unsigned ms_tag_bit;

    public:
        unsigned max_act_hi{0};
        unsigned max_act_lo{0};
        unsigned act_hi_cnt{0};
        unsigned act_lo_cnt{0};

        explicit Table(const sc_core::sc_module_name& name)
            : ManagedCache<ProgramCounter, PCP::Entry>(name, PCP::Entry(PCP_SEARCH))
        {
            if (tag_bits_param) {
                ms_tag_bit = tag_bits_param + set_bits + line_bits - 1;
            } else {
                ms_tag_bit = 8;
                if (num_ways > 1) {
                    std::cerr << "WARNING: Table is associative, but tag_bits_param was not specified. In this case, "
                                 "the tags will always match, and only one way of each set will be used."
                              << std::endl;
                }
            }
        }

        Index compute_set(ProgramCounter address) const override
        {
            if (set_bits == 0)
                return 0;
            auto index = address.extract_range(line_bits + set_bits - 1, line_bits);
            if (index >= num_sets)
                AMPERE_ERROR("set index is larger than the number of sets");
            return index;
        }

        uint64_t compute_tag(ProgramCounter address) const override
        {
            if (num_ways == 1)
                return 0;
            return address.extract_range(ms_tag_bit, line_bits + set_bits);
        }

        Result lookup_or_insert(const BackendMemReq& request);

        using ManagedCache<ProgramCounter, PCP::Entry>::get_sets;
    };

public:
    optional_sc_port<StrideTraining<BackendMemReq>> training;

private:
    Param<bool> upper_va_bits_bug{
        "upper_va_bits_bug", "Upper VA bits are always zero-extended, dropping TTBR1 prefetches", false};
    Param<unsigned> addr_mask{"addr_mask",
                              "which bits to use for address stride calculation",
                              0xfff}; // study shows that 0x1ffe or 0x3ffc is slightly better than 0xfff.
                                      // {0xfff,0x1ffe,0x3ffc} == {4095,8190,16380}
    Param<bool> enable_adaptive_stride_mult{
        "enable_adaptive_stride_mult", "Enable adaptive stride multiplier algorithm", true};
    Param<bool> enable_slow_stride_increase{"enable_slow_stride_increase",
                                            "Increase multiplier after seeing OFB hits >= the current multiplier using "
                                            "a shared counter (instead of every hit)",
                                            false};
    Param<bool> multiplier_decrease_on_stride_mismatch{
        "multiplier_decrease_on_stride_mismatch",
        "Divide stride multiplier by two whenever the stride doesn't match",
        false};
    Param<unsigned> stride_mult_max{
        "stride_mult_max", "Maximum stride multiplier that can be used with adaptive stride multiplier algorithm", 4};
    Param<unsigned> max_slow_increase_ctr{
        "max_slow_increase_ctr",
        "If enable_slow_stride_increase==true, maximum number of OFBs needed to increase multiplier",
        stride_mult_max.val()};
    Param<bool> fill_hole_on_mult_inc{
        "fill_hole_on_mult_inc", "Trigger a prefetch when we increase the distance", false};
    Param<bool> page_cross_4k{"page_cross_4k", "allow prefetching across 4k boundary", true};
    Param<bool> remove_dup{
        "remove_dup", "remove duplicate requests from going on-deck. 523.xalanc improves by 1.07x", false};
    Param<bool> prefetch_in_active_lo{
        "prefetch_in_active_lo", "Trigger a prefetch if entry is in ACTIVE_LO as well as ACTIVE_HI", false};
    Param<bool> train_into_active_lo{"train_into_active_lo", "Train into ACTIVE_LO (instead of directly to HI)", false};
    Param<bool> update_stride_on_active_lo{
        "update_stride_on_active_lo", "Update the stored stride on transition to ACTIVE_LO", true};
    Param<bool> truncate_offset_bug{
        "truncate_offset_bug", "Truncate offset to 14 bits after applying multiplier", false};
    Param<bool> recycle_tlb_miss{"recycle_tlb_miss", "Replay a prefetch candidate which misses TLB", false};
    Param<bool> drop_zero_stride_prefetches{
        "drop_zero_stride_prefetches", "Drop prefetches if training detects a stride of zero", true};
    Param<bool> out_of_order_stride_match{
        "out_of_order_stride_match", "PCP considers 2X and -X as stride matches when entry is in ACT_HI", false};
    Param<bool> page_fetch{"page_fetch", "fetch TLB on page miss", false};
    Param<bool> stride_mult_once_per_ofb{
        "stride_mult_once_per_ofb", "Only call the PCP to increase the stride multiplier once per OFB entry", false};
    Param<bool> inc_on_ofb_hit{"inc_on_ofb_hit", "foo", false};

public:
    explicit PCP(const sc_core::sc_module_name& name) : BackendPrefetcher<BackendMemReq>(name) {}

    using BackendPrefetcher<BackendMemReq>::train;
    void train(BackendMemReq request) { train(0, request, false, false, true, {}); }

protected:
    // train the table after the v->p address translation
    void train_impl(BackendMemReq request, bool hit, bool prefetch, bool demand) override;
    void execute_impl(
        uint64_t index, BackendMemReq req, bool hit, bool prefetch, bool demand, PrefetchType prefetch_type) override;
    void drop_impl(BackendMemReq req, PrefetchDropReason reason) override;
    void launch_impl(BackendMemReq req) override;
    void fill_impl(uint64_t index, BackendMemReq req, bool fb_pref, bool was_alloc_by_pf) override;
    PrefetchType prefetch_type() const override { return PrefetchType::PCP; }

    void stride_mult_inc(BackendMemReq request, ProgramCounter pc, bool aggressive);

public:
    // Call to attempt to increment stride multiplier.
    void stride_mult_inc(BackendMemReq request, bool aggressive) { stride_mult_inc(request, request.pc, aggressive); }
    bool should_inc_stride_mult(bool prefetch, bool done_already = false) const;

    bool can_prefetch_page(BackendMemReq req) const override { return page_fetch; }
    using BackendPrefetcher::can_allocate_pob;
    bool can_allocate_pob(BackendMemReq req, archx::EL current_level) const override;
    bool can_replay() const override { return recycle_tlb_miss; }

    void launch_related(BackendMemReq req);

    void table_print(std::ostream& os) const;
    void dump(std::ostream&) const override;

    Event ev_train{"train", "Trained PCP"};
    Event ev_trigger{"trigger", "PCP triggered and is sitting on deck"};
    Event ev_page_cross{"page_cross", "PCP not issued since it crossed a page boundary"};
    Event ev_stride_mult_inc{"stride_mult_inc", "PCP increased the stride multiplier"};
    Event ev_stride_mult_dec{"stride_mult_dec", "PCP decreased the stride multiplier"};
    Event ev_stride_mult_rst{"stride_mult_rst", "PCP reset the stride multiplier"};
    EventData<unsigned> ev_stride_multiplier{"stride_multiplier", "value of stride multiplier"};
    EventData<unsigned> ev_pcp_index{"pcp_index", "PCP table index"};
    EventData<unsigned> ev_pcp_way{"pcp_way", "PCP table way"};
    EventData<uint64_t> ev_pcp_pc{"pcp_pc", "PC of uop triggered this prefetch"};
    EventData<int> ev_stride{"stride", "value of stride"};
    EventData<int> ev_allocation_distance{"allocation_distance", "distance in allocation from updates"};
    Event ev_active_hi_entries{"active_hi_entries",
                               "number of active_hi entries at the end of simulation in the stride_table"};
    Event ev_active_lo_entries{"active_lo_entries",
                               "number of active_lo entries at the end of simulation in the stride_table"};
    Event ev_training_dropped{"training_dropped", "PCP training dropped because of same idx access"};
    Event ev_aliased_evicted{"aliased_evicted", "useful PCP entry is evicted due to aliasing"};
    Event ev_aliased_trigger{"aliased_trigger",
                             "if two different loads aliased, but the stride matched and a trigger occured"};

    // PMU
    Event ev_pmu{"pmu", "A PMU event has occurred"};
    EventData<PMU> ev_pmu_num{"event", "PMU event number"};

private:
    Table table{"table"};

    bool should_trigger(const Entry& entry) const;
    void trigger_pf(const BackendMemReq& request);

    unsigned last_train_idx{0};
    unsigned last_train_way{0};
    int64_t last_train_cycle{0};

    // Number of times 'stride_mult_inc' has been called since being reset (on
    // account of exceeding the current multiplier of the associated PCP entry)
    unsigned stride_mult_inc_called{0};

    bool signal_training{false};

    struct InFlight
    {
        VirtAddr virt_addr;
        ProgramCounter pc;
        bool stride_mult_increase{false};
        InFlight(VirtAddr virt_addr, ProgramCounter pc) : virt_addr(virt_addr.line_address()), pc(pc) {}
    };
    std::unordered_map<PhysAddr, InFlight> in_flight;

    InFlight& add_inflight(const BackendMemReq& request)
    {
#ifdef PCP_MAX_INFLIGHT
        if (in_flight.size() > PCP_MAX_INFLIGHT) {
            for (auto& [addr, e] : in_flight)
                std::cerr << "phys_addr: " << addr << ", virt_addr: " << e.virt_addr << std::endl;
            AMPERE_ASSERT(false, "More in-flight than there should be");
        }
#endif
        return in_flight.try_emplace(request.phys_addr->line_address(), request.virt_addr.line_address(), request.pc)
            .first->second;
    }

    InFlight* get_inflight(const BackendMemReq& request)
    {
        if (auto e = in_flight.find(request.phys_addr->line_address()); e != in_flight.end())
            return &e->second;
        return nullptr;
    }

    void remove_inflight(const BackendMemReq& request)
    {
        if (request.phys_addr) {
            in_flight.erase(request.phys_addr->line_address());
        } else {
            for (auto it = in_flight.begin(); it != in_flight.end();) {
                if (it->second.virt_addr.same_cache_line(request.virt_addr))
                    it = in_flight.erase(it);
                else
                    ++it;
            }
        }
    }

protected:
    void end_of_elaboration() override;
    void end_of_simulation() override;
};

class L1P_PCP : public PCP, implementation<L1P_PCP, BackendPrefetcher<BackendMemReq>>
{
    OutIn<BackendMemReq> pipe{"pipe"};

    Param<bool> ignore_other_loads_in_instruction{
        "ignore_other_loads_in_instruction", "Allow PCP ignore loads other than the first in an instruction", false};

public:
    explicit L1P_PCP(const sc_core::sc_module_name& name);

    bool run() override;

protected:
    void issue_impl(uint64_t id, BackendMemReq req) override;
    void flush_impl(Flush flush_info) override;
    bool commit_impl(uint64_t id, BackendMemReq req) override;
};

} // namespace ampere::core

/*
 * vim: sw=4:ts=4:et:si:cindent:cino=\:0,(0,g0,t0:
 */
