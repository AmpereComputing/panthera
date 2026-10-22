//
// Copyright (c) 2018-2024 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//
#include <algorithm>
#include <cmath>
#include <sstream>
#include <fmt/format.h>
#include "ampere/core/ixu.h"
#include "ampere/core/ixu_dbg.h"
#include "ampere/core/ixu_defs.h"
#include "ampere/core/uop.h"
#include "ampere/util/enum.h"
#include "ampere/sc/debug.h"
#include <nlohmann/json.hpp>

using json = nlohmann::json;

#define IXU_DEBUG 0
namespace ampere::core {

[[maybe_unused]] static std::string wr_pregs_str(const Uop& uop)
{ // for debugging
    std::ostringstream ss;
    for (unsigned i = 0; i < uop->ixu_wr_pregs.count; ++i) {
        if (i > 0)
            ss << ", ";
        ss << uop->ixu_wr_pregs.pregs[i] << ':' << (uop->ixu_wr_pregs.ixu[i] ? 'I' : 'F');
    }
    return ss.str();
}

[[maybe_unused]] static std::string sus_status(const Uop& uop)
{
    std::ostringstream ss;
    if (uop->sus.sta_issued) {
        ss << "SUS: A";
        if (uop->sus.std_issued)
            ss << 'D';
        if (uop->sus.sus_pair_data1_issued)
            ss << 'P';
        if (uop->sus.split)
            ss << 'S';
        if (uop->sus.written_back)
            ss << 'W';
    }
    return ss.str();
}

const decltype(pipe_enum()) IXU::pipe_enumeration = pipe_enum();

IXU::IXU(const sc_core::sc_module_name& name) : sc::Module(name)
{
    connect(fsu_zpipe_std_out, fsu_zpipe_std_in).name("fsu_to_lsu_std_exec_pipe").bandwidth(MAX_FSU_PARTITIONS);

    // Using 64 as an essentially unlimited capacity
    connect(fsu_gather_delay).name("fsu_gather_completion_pipe").latency(gather_use_delay).wr_bandwidth(2).capacity(64);

    // used to wakeup every N cycles
    connect(throttle_sample_timer_out, throttle_sample_timer_in).latency(throttle_epoch);

    // Hole in A-pipe scheduling one cycle before the end of the FSU->IXU delay
    connect(ixu_pipe_schedule_f2i).latency(std::max(1U, fsu_f2i_delay - 1)).bandwidth(MAX_FSU_PARTITIONS);

    // Set up lub credit counting for each scheduler (only used for loads)
    lub.init(NUM_SCHED, [this](const char* n, size_t i) {
        return new ampere::sc::CreditPool(ampere::sc::CreditPool::explicit_size{}, n, lub_size);
    });

    connect(load_cancel_dealloc_delay).name("load_cancel_dealloc_delay").latency(1).bandwidth(256);
    connect(fsu_load_cancel_delay).name("fsu_load_cancel_delay").latency(1).bandwidth(256);
    connect(fsu_load_delay).name("fsu_load_delay").latency(fsu_load_delay_param).bandwidth(256);
    cancel_occupies_mem.resize(num_execution_units, 0);

    reset_issue_recs();

    if (fsu_bypass_dump)
        fsu_bypass->dump(std::cout);
    if (fsu_bypass_dump_matrix)
        fsu_bypass->dump_matrix(std::cout);
}

/*************************************************************************
 * init is called by Core::construct() after IDR is constructed,
 * since it determines the size of the scheduler tables.
 *************************************************************************/
void IXU::init(const std::vector<size_t>& entries)
{
    sched_entries = entries;
    schedulers.resize(entries.size());

    int g, e;

    // space for the table handle
    //
    table = new SchedEntry**[schedulers.size()];

    // create the N discrete scheduler tables
    //
    for (auto [sn, scheduler] : std::views::enumerate(schedulers)) {
        auto sch_group_size = scheduler_group_size(*Scheduler::from_sched(sn).sched_type);
        // calculate number of uop groups
        //
        scheduler.sched_groups = sched_entries[sn] / sch_group_size;
        AMPERE_ASSERT(0 == sched_entries[sn] % sch_group_size,
                      "invalid scheduler geometry, scheduler {}: entries {} group {}",
                      sn,
                      sched_entries[sn],
                      sch_group_size);
        AMPERE_ASSERT(sch_group_size < MAX_GROUP_SIZE, "sched group size is too large: {}", sch_group_size);

        scheduler.lub = &lub[sn];

        table[sn] = new SchedEntry*[sched_entries[sn]];
        scheduler.entries.resize(sched_entries[sn]);
        scheduler.groups.resize(scheduler.sched_groups);

        scheduler.stats.sched_occupancy.resize(scheduler.sched_groups + 1, 0);
        scheduler.stats.sched_issue_eligible.resize(scheduler.sched_groups + 1, 0);

        // construct out the entries for each discrete table
        //
        for (size_t i = 0; i < sched_entries[sn]; i++) {
            // coordinates for group alloc
            //
            g = i / sch_group_size; // group assignment
            e = i % sch_group_size; // slot in group

            table[sn][i] = new SchedEntry(g, e);

            scheduler.entries[i] = table[sn][i];
            scheduler.groups[g][e] = table[sn][i];
        }
    }

#if IXU_DEBUG
    printf(" \n");
    printf(" SchA_0  SchB_0  SchM_0  SchA_1  SchB_1  SchM_1 \n");
    printf("----------------------------------------------- \n");
#endif
}

/*************************************************************************
 * Destructor for derived class partitioned scheduler
 *
 *************************************************************************/
IXU::~IXU()
{
    if (table) {
        for (size_t sn = 0; sn < schedulers.size(); sn++) {
            for (size_t i = 0; i < sched_entries[sn]; i++) {
                delete table[sn][i];
            }
            delete[] table[sn];
        }
        delete[] table;
    }
}

/*************************************************************************
 * run method called once per cycle by clocking subsystem.
 *************************************************************************/

bool IXU::run()
{
    now = get_cycle(); // used throughout IXU
    DBG_DPI2F("\nrun");
    DBGIX("\nrun");
    DBGISS("\nRUN");

    if (collect_load_use_stats) {
        clean_load_use_stats();
    }

    for (auto& fsu : fsu_sched)
        fsu.reset();

    load_wakeup_this_cycle = false;
    cancel_this_cycle = false;
    ixu_pipe_schedule_f2i_block = 0;
    issued_slow_int = false;
    if (flush_port->can_read()) {
        flush_pipe();
    }

    // at the end of every throttle epoch, process the data
    if (throttle_enable && 0 == now % throttle_epoch) {
        process_throttle_data(); // sends signal to IDR for possible throttle
    }

    fsu_zpipe_std_ixu_to_lsu_exec(); // delay for FSU STD to LSU reader
    load_cancel_receive();           // receive fake data wake/cancels
    writeback_lsu();                 // writeback results from LSU
    complete_lsu();                  // completion results from LSU

    execute();  // XU04
    schedule(); // XU03
    dispatch(); // DR05/XU01 (XU02 modeled with issue_delay)

    if (detailed_stats_collection) {
        stats_collection();
        return true; // always run
    }

    IXU::load_port_busy.fill(false);
    bool has_zpipe_reservation = false;
    bool has_i2f = false;
    bool has_i2f_shelf = false;
    for (const auto& fsu : fsu_sched) {
        has_zpipe_reservation |= !fsu.zpipe_reservation.empty();
        has_i2f |= static_cast<bool>(fsu.i2f);
        has_i2f_shelf |= static_cast<bool>(fsu.i2f_shelf);
    }

    // wake up if the schedulers have work to do, or if there are
    // uops waiting to be allocated or any of the XUs are busy
    auto might_dealloc =
        std::any_of(schedulers.begin(), schedulers.end(), [](const auto& sch) { return sch.might_dealloc; });
    return might_dealloc || need_to_run() || !load_cancel_reservation.empty() || !scheduler_input_queue.empty()
           || xus_busy() || has_zpipe_reservation || has_i2f || has_i2f_shelf || run_always;
}

void IXU::schedule_cancellation_primary(const Uop& canceled_uop)
{
    // Schedule one primary cancellation per output register of a memop.
    const auto limit = canceled_uop->ixu_wr_pregs.count;
    const auto& pregs = canceled_uop->ixu_wr_pregs;
    for (unsigned idx = 0; idx < limit; ++idx) {
        const auto preg = pregs.pregs[idx];
        const bool is_integer = pregs.ixu[idx];
        const unsigned delay = is_integer ? cancel_to_reap_delay_ixu : cancel_to_reap_delay_fsu; // IXU vs FSU
        const sc::Cycle cancel_cyc = now + delay;
        AMPERE_ASSERT(cancel_cyc >= now, "Cannot schedule cancellations for the past");
        DBGISS("\tScheduling primary cancel for txid: {}, preg: {}, at cycle: {}",
               canceled_uop->get_txid(),
               preg,
               cancel_cyc);
        load_cancel_reservation.emplace(cancel_cyc,
                                        cancel_reservation_t(true /*primary*/, canceled_uop, is_integer, preg));
    }
}

void IXU::schedule_cancellation_secondary(const Uop& canceled_uop)
{
    for (size_t src_idx = 0; src_idx < UOP_MAX_SRCS; ++src_idx) {
        const auto src_preg = canceled_uop->cancel_src_pregs[src_idx];
        if (src_preg) {
            const auto cancel_cyc = canceled_uop->cancel_src_time[src_idx];
            schedule_cancellation_secondary_by_src(cancel_cyc, canceled_uop, src_idx);
        }
    }
}

void IXU::schedule_cancellation_secondary_by_src(sc::Cycle cancel_cyc, const Uop& canceled_uop, size_t src_idx)
{
    AMPERE_ASSERT(cancel_cyc >= now, "Cannot schedule cancellations for the past");
    const auto limit = canceled_uop->ixu_wr_pregs.count;
    if (limit == 0) { // no dests ... still gotta reset stuff
        DBGISS("\tScheduling secondary cancel for txid: {}, NO dest preg,  at cycle: {}",
               canceled_uop->get_txid(),
               cancel_cyc);
        load_cancel_reservation.emplace(
            cancel_cyc,
            cancel_reservation_t(false /*secondary*/, canceled_uop, false /*is_integer*/, 0 /*no preg*/, src_idx));
        return;
    }
    const auto& pregs = canceled_uop->ixu_wr_pregs;
    for (unsigned dst_idx = 0; dst_idx < limit; ++dst_idx) {
        const auto preg = pregs.pregs[dst_idx];
        const bool is_integer = pregs.ixu[dst_idx];
        DBGISS("\tScheduling secondary cancel for txid: {}, preg: {}, at cycle: {}",
               canceled_uop->get_txid(),
               preg,
               cancel_cyc);
        load_cancel_reservation.emplace(
            cancel_cyc, cancel_reservation_t(false /*secondary*/, canceled_uop, is_integer, preg, src_idx));
    }
}

void IXU::clean_load_cancel_reservations()
{
    DBGISS("\tCleaning cancel reservations for  cycle: {}", now);
    load_cancel_reservation.erase(now);
}

bool IXU::will_reap_this_cycle(const Uop& uop) const
{
    const auto txid = uop->get_txid();
    auto [begin, end] = load_cancel_reservation.equal_range(now);
    for (auto itr = begin; itr != end; ++itr) {
        const auto& canceled_item = itr->second;
        if (canceled_item.uop->get_txid() == txid) {
            return true;
        }
    }
    return false;
}

void IXU::load_cancel_reap()
{
    // See what uops are scheduled to REALLY cancel this cycle and clean up the scheduler entry and uops
    auto [begin, end] = load_cancel_reservation.equal_range(now);
    for (auto itr = begin; itr != end; ++itr) {
        auto& canceled_item = itr->second;
        DBGISS("CANCEL PRE-REAP: txid: {}, preg: {}, {}, src_idx: {}",
               canceled_item.uop->get_txid(),
               canceled_item.preg,
               (canceled_item.primary ? "primary" : "secondary"),
               canceled_item.src_idx);
        load_cancel_reap_scheduler_entry(canceled_item);
        load_cancel_reap_uop(canceled_item);
    }
}

void IXU::load_cancel_reap_scheduler_entry(cancel_reservation_t& canceled_item)
{
    const auto& uop = canceled_item.uop;
    auto* sched_entry = table[uop->sch.scheduler()][uop->sch.entry];
    if (uop == sched_entry->uop) {
        // Only count this as a uop cancel if the uop actually took an issue slot
        if (sched_entry->issued_canceled) {
            ev_uop_cancel(uop);
        }
        DBGISS("\tCANCEL REAP SCHED ENTRY: txid: {}", uop->get_txid());
        sched_entry->all_ready = false;
        sched_entry->issued_canceled = false;
        sched_entry->issued_once = false;
    } else {
        // Canceled loads have left the scheduler and live in the LOB now.
        DBGISS("\tScheduler uop mismatch txid: {}, skipping sched-entry cancel-reset", uop->get_txid());
    }
}

void IXU::set_sus_issue_vector(const Uop& uop)
{
    if (uop->is_super_sus_pair()) {
        uop->sus.to_issue_vec = SUPER_SUS_PAIR_ISSUE_ALL; // D2:D1:A
    } else {
        uop->sus.to_issue_vec = SUS_ISSUE_ALL; // D1:A
    }
}

void IXU::rollback_sus_state(const Uop& uop, bool primary, size_t src_idx)
{
    // if you cancel an STA, you must reissue the STD
    uop->sus.std_issued = false;

    if (primary) {                            // only for CAS-like sus that do loads
        uop->sus.to_issue_vec = SUS_ISSUE_D1; // reissue data
        uop->sus.split = true;
        DBGISS("primary rollback sus txid: {} operand 1 clear dat1/dat", uop->get_txid());
        return;
    }
    switch (src_idx) {
    case 0:
        uop->sus.sta_issued = false;
        uop->sus.split = false;
        DBGISS("rollback sus txid: {} operand 0 clear sta/split/dat", uop->get_txid());
        set_sus_issue_vector(uop);
        break;
    case 1:
        if (uop->store_num_address_sources() == 2) {
            uop->sus.sta_issued = false;
            uop->sus.split = false;
            set_sus_issue_vector(uop);
            DBGISS("rollback sus txid: {} operand 1 clear sta/split/dat", uop->get_txid());
        } else {
            uop->sus.to_issue_vec = SUS_ISSUE_D1; // reissue data
            DBGISS("rollback sus txid: {} operand 1 clear dat", uop->get_txid());
        }
        break;
    case 2:
        // we do get super-sus-non-pairs through here (AA->D) (like str_w_regoff)
        // src3 would be the source data operand we reset at top of function
        DBGISS("rollback sus txid: {} operand 2 clear dat", uop->get_txid());
        uop->sus.to_issue_vec = SUS_ISSUE_D1; // reissue data
        break;
    default:
        AMPERE_ERROR("No src3 on stores");
    }
}

void IXU::rollback_super_sus_pair_state(const Uop& uop, bool primary, size_t src_idx)
{
    // if you cancel an STA, you must reissue the STD uop(s)
    uop->sus.std_issued = false;
    uop->sus.sus_pair_data1_issued = false;

    if (primary) { // only for CAS-like sus that do loads
        if (uop->sus.sus_pair_data1_issued) {
            uop->sus.sus_pair_data1_issued = false;
            uop->sus.split = true; // FIXME: check this
            DBGISS("primary rollback super_sus_pair txid: {} operand 1 clear dat1/dat", uop->get_txid());
            uop->sus.to_issue_vec = (SUS_ISSUE_D1 | SUS_ISSUE_D2); // D2, D1, ok A
        }
    } else if (src_idx == 0) {
        uop->sus.sta_issued = false;
        uop->sus.split = false;
        DBGISS("rollback super_sus_pair txid: {} operand 0 clear sta/split/dat", uop->get_txid());
        uop->sus.to_issue_vec = SUPER_SUS_PAIR_ISSUE_ALL; // D2:D1:A
    } else if (src_idx == 1) {
        uop->sus.sus_pair_data1_issued = false;
        DBGISS("rollback super_sus_pair txid: {} operand 1 clear dat1/dat", uop->get_txid());
        uop->sus.to_issue_vec = (SUS_ISSUE_D1 | SUS_ISSUE_D2); // D2, D1, ok A
    } else if (src_idx == 2) {
        uop->sus.to_issue_vec = SUS_ISSUE_D2; // D2, ok D1, ok A
    }
}

void IXU::load_cancel_reap_uop(cancel_reservation_t& canceled_item)
{
    const auto& uop = canceled_item.uop;
    const auto preg = canceled_item.preg;
    DBGISS("\tCANCEL REAP UOP: txid: {}, preg: {}", uop->get_txid(), preg);
    uop->reset_issued(); // FIXME: this may occur on different cycles for primary cancellations spread across two cycles
    if (canceled_item.primary) {
        uop->uncancel();
    } else {
        uop->uncancel(canceled_item.src_idx);
    }

    if (uop->is_super_sus_pair()) {
        rollback_super_sus_pair_state(uop, canceled_item.primary, canceled_item.src_idx);
    } else if (uop->is_single_uop_store()) {
        rollback_sus_state(uop, canceled_item.primary, canceled_item.src_idx);
    }

    if (preg) {
        IssueRec& issue_rec = issue_tracker[preg];
        if (uop->received_load_data) {
            DBGISS("\t\ttxid: {}, already received data so not canceling preg", uop->get_txid());
        } else {
            if (issue_rec.is_primary_cancel()) {
                // Primary cancels do not need to re-issue; Eventually LSU returns data.
                DBGISS("\treset memop primary cancel preg: {}", preg);
                issue_rec.set_waiting_for_data();
            } else {
                DBGISS("\tcleaning preg: {}", preg);
                issue_rec.set_waiting_to_issue();
            }
        }
    }
}

void IXU::load_cancel_receive()
{
    // Receive advance warning from LSU that a load scheduled but will
    // not complete. Really this is a speculative wake which causes
    // two cycles of dependents to get scheduled, then canceled if the
    // load data is not actually available.
    while (load_cancel_in->can_take()) {
        auto canceled_uop = load_cancel_in->take();
        // FIXME: we may want to consider delaying this processing one
        //  cycle. Real machine uses wake info on the next cycle. This
        //  model uses the wake info on the current cycle.
        load_cancel_action(canceled_uop);
    }
    // FSU processes the fake wake/cancel 1cyc delayed
    canceled_issue_rec_primary_delayed();

    // Delay cancel's effect on scheduler deallocation by a cycle to match RTL
    while (load_cancel_dealloc_delay.can_take()) {
        (void)load_cancel_dealloc_delay.take();
        cancel_this_cycle = true;
    }
}

void IXU::load_cancel_action(const Uop& canceled_uop)
{
    ev_load_cancel(canceled_uop);
    load_cancel_dealloc_delay.put(true);
    DBGISS("CANCEL uop txid: {} allocid: {}, disasm: {:15s}",
           canceled_uop->get_txid(),
           canceled_uop->get_alloc_id(),
           canceled_uop->uop_disasm());

    // Danger: This uop might have already been flushed and the
    // load cancel just got delayed in the return port. Need to
    // ignore it if it was flushed and ignore it.
    if (canceled_uop->was_flushed()) {
        DBGISS("Dropping cancel of txid: {} because uop previously flushed", canceled_uop->get_txid());
    } else {
        cancel_issue_rec_primary(canceled_uop);      // poison pdest, enable consumers for a to-be-canceled issue
        schedule_cancellation_primary(canceled_uop); // future reset scheduler entry/uop
    }
}

/*************************************************************************
 * move the group allocation pointer.
 * this is to help search for the next empty group
 *************************************************************************/
int IXU::next_alloc_group(unsigned table, int curr_grp, int curr_idx)
{
    auto sch_group_size = scheduler_group_size(Scheduler::from_sched(table).sched_type.value());

    if (curr_idx + 1 == static_cast<int>(sch_group_size)) {
        // we are at the end of a group. increment by one and wrap.
        int next_grp = (curr_grp + 1) % schedulers[table].sched_groups;
        return next_grp;
    } else {
        // we are not at the end of this group. keep allocating here.
        return curr_grp;
    }
}

/*************************************************************************
 * allocate entry into scheduler.
 * increment the in_use counter.
 *************************************************************************/
void IXU::table_allocate(const Uop& uop)
{
    AMPERE_ASSERT(uop, "Uop should exist");
    AMPERE_ASSERT(uop->sch.entry == -1, "Scheduler entry should NOT be assigned already");
    AMPERE_ASSERT(uop->sch.sched_type, "Scheduler should be assigned already");
    AMPERE_ASSERT(uop->sch.pipe_type != Pipe::INVALID, "Pipe should be assigned already");

    auto sched = uop->sch.scheduler();
    auto sch_group_size = scheduler_group_size(*uop->sch.sched_type);

    // find a free entry in that op-scheduler and populate it
    auto& scheduler = schedulers[sched];
    unsigned int idx = scheduler.group_alloc_ptr * sch_group_size;
    auto* e = table[sched][idx];
    while (e->in_use) {
        ++idx;

        if (idx == sched_entries[sched]) {
            idx = 0; // wrap
        }

        e = table[sched][idx];
    }

    // set group pointer for next alloc
    scheduler.group_alloc_ptr = next_alloc_group(sched, e->grp, e->idx);

    e->in_use = true;
    scheduler.need_to_sort = true;
    sch_insert();
    e->uop = uop;
    e->alloc_time = now;
    uop->sch.entry = idx;
    uop->ixu_wr_pregs = emitted_pregs(uop);

    if (uop->is_single_uop_store()) {
        set_sus_issue_vector(uop);
    }

    DBGISS(
        "INSERT uop txid: {} inum: {}, allocid: {}, sch: {}, will_cancel: {}, disasm: {:15s} \tWRITES: {} {} W-PREGS: "
        "{}",
        uop->get_txid(),
        uop->inum().id(),
        uop->get_alloc_id(),
        uop->sch.name(),
        uop->will_cancel(),
        uop->uop_disasm(),
        (!archx::invalid_reg(uop->reg_dst)) ? archx::regstr(uop->reg_dst) : "",
        (!archx::invalid_reg(uop->reg_dst2)) ? archx::regstr(uop->reg_dst2) : "",
        wr_pregs_str(uop));

#if IXU_DEBUG
    for (int sched = 0; sched < uop->sch.sched; sched++)
        printf("       ");
    printf(" g%de%d \n", e->grp, e->idx);
#endif

    // increment count for that op-scheduler
    scheduler.entries_in_use++;

    e->alloc_id = uop->get_alloc_id(); // not the allocid for gathers, for accurate flushing
    DBG_BYP("allocate allocid: {} txid: {} pc {:016x} uopcode {:s}",
            uop->get_alloc_id(),
            uop->get_txid(),
            uop->pc().value(),
            uop->get_uopcode());

    // sanity check for SUS
    if (uop->is_single_uop_store()) {
        if ((archx::is_integer(e->uop->reg_src[2]) && !uop->is_super_sus()) || archx::INVALID != (e->uop->reg_src[3])) {
            printf("complex SUS detected at alloc_id=%lu\n", e->alloc_id);
            fmt::print("uop PC {} opcode {} srcs {} {} {} {}\n",
                       e->uop->pc(),
                       e->uop->get_uopcode(),
                       e->uop->reg_src[0],
                       e->uop->reg_src[1],
                       e->uop->reg_src[2],
                       e->uop->reg_src[3]);
            AMPERE_ERROR("SUS can only have two sources");
        }
    }

    // Someday BS pipe might be an independent pipe shared by the B schedulers, in which
    // case we mutate the pipe here.
    if (slow_int_writeback_port && uop->is_bs_pipe()) {
        if (shared_slow_int_pipe)
            uop->set_pipe_type(Pipe::BS);
        else
            uop->set_pipe_type(Pipe::S);
    }

    // set up dependencies
    mark_reg_deps(e);

    // set producer bits
    if (uop->is_gather()) {
        // for gathers we have multiple producers and don't know which alloc_id will actually do the
        // final writeback waking up consumers. They all write the same prfid (due to gather_proxy).
        // We used the max alloc_id to wake up the younger dependents.
        DBGISS("to track gather: txid: {}, prfid: {}, alloc_id: {}",
               uop->get_txid(),
               uop->get_prfid_id(),
               uop->get_alloc_id_gather());
        track_preg(uop, uop->get_prfid_id(), uop->get_alloc_id_gather());
        uop->dsts++;
    } else if (uop->prfid) {
        track_preg(uop, uop->prfid->get_id(), uop->get_alloc_id());
        uop->dsts++;
    }
    if (uop->prfid2) {
        track_preg(uop, uop->prfid2->get_id(), uop->get_alloc_id());
        uop->dsts++;
    }
    if (uop->is_flag_producer() || uop->is_temp_flags_producer()) {
        AMPERE_ASSERT(uop->flagsid, "flag producer doesn't have a PRF credit");
        track_preg(uop, uop->flagsid->get_id(), uop->get_alloc_id());
    }
    allocate_issue_rec(uop);
}

void IXU::track_preg(const Uop& uop, std::uint64_t credit_id, std::uint64_t alloc_id)
{
    DBG_BYP("track   src_producer[{}].resident=true, txid: {}, alloc_id: {}", credit_id, uop->get_txid(), alloc_id);
    src_producer[credit_id].resident = true;
    src_producer[credit_id].alloc_id = uop->get_alloc_id();
}

void IXU::untrack_preg(const Uop& uop, std::uint64_t credit_id, std::uint64_t alloc_id)
{
    DBG_BYP("untrack src_producer[{}].resident=true, txid: {}, alloc_id: {}", credit_id, uop->get_txid(), alloc_id);
    AMPERE_ASSERT(src_producer[credit_id].alloc_id == alloc_id,
                  "uop at writeback does not match producer of its PRF entry: {} vs {}",
                  src_producer[credit_id].alloc_id,
                  alloc_id);
    src_producer[credit_id].resident = false;
}

unsigned IXU::div_bypassing_delay(bool fsu_consumer) const
{
    // IXU schedules faster out of the Z pipe than the FSU does
    unsigned bypass_delay = 1;
    if (fsu_consumer) {
        // Prior to Connor, div_wb had an extra cycle of delay when waking fsu consumers
        bypass_delay = fsu_div_bypass_latency;
    }
    return bypass_delay;
}

void IXU::i2f_block_fsu_scheduler(const Uop& producer_uop)
{ // FIXME: should this be const?  we are storing it in i2f
    DBG_DPI2F("i2f ixu-execute done (part 1), allocid: {}", producer_uop->get_alloc_id());
    auto& fsu = fsu_sched[producer_uop->sch.partition_normalized_to(Scheduler::fsu_partitions)];
    fsu.pipe_schedule_i2f = true;
    fsu.i2f = producer_uop;
}

void IXU::f2i_block_ixu_scheduler(const Uop& producer_uop)
{
    ixu_pipe_schedule_f2i.put(producer_uop);
    ev_block_ixu_pipe_next_cycle_for_f2i(producer_uop);
}

void IXU::cancel_issue_rec_primary(const Uop& canceled_uop)
{
    // primary_cancel means we got a (cheating) fake cancellation signal from the
    // LSU when it gave us a wake.

    // We set the FSU and IXU pregs to cancel at different cycles.
    const phys_regs_t& pregs = canceled_uop->ixu_wr_pregs;
    for (unsigned idx = 0; idx < pregs.count; ++idx) {
        const auto preg = pregs.pregs[idx];
        const bool is_integer = pregs.ixu[idx];
        if (is_integer) {
            const sc::Cycle cancel_cyc = now + cancel_to_reap_delay_ixu;
            DBGISS(" cancel issue rec txid: {}, preg: {}, IXU primary", canceled_uop->get_txid(), preg);
            issue_tracker[preg].set_cancel(cancel_cyc, true /*primary cancel*/, now);
        } else {
#define IMPLEMENT_REF_BUG
#if defined(IMPLEMENT_REF_BUG)
            // The bug cancels the issue rec in the current cycle, rather than waiting a cycle. This
            // bug enables more FSU issues. Also required to pass the load cancel fsu unit test.
            const sc::Cycle cancel_cyc = now + cancel_to_reap_delay_fsu;
            DBGISS(" cancel issue rec txid: {}, preg: {}, FSU primary", canceled_uop->get_txid(), preg);
            issue_tracker[preg].set_cancel(cancel_cyc, true /*primary cancel*/, now);
#else
            // We delay processing the cancel/wake for FSU by one cycle.
            // This delays issuing FSU dependent uops.
            DBGISS(
                " cancel issue rec txid: {}, preg: {}, FSU primary DELAYING 1 cycle", canceled_uop->get_txid(), preg);
            fsu_load_cancel_delay.put(preg);
#endif
        }
    }
}

void IXU::canceled_issue_rec_primary_delayed()
{
    while (fsu_load_cancel_delay.can_take()) {
        auto preg = fsu_load_cancel_delay.take();
        const unsigned delay = cancel_to_reap_delay_fsu - 1; // we delayed 1 cycle already in the port
        const sc::Cycle cancel_cyc = now + delay;
        DBGISS(" cancel issue preg: {}, FSU primary", preg);
        issue_tracker[preg].set_cancel(cancel_cyc, true /*primary cancel*/, now);
    }
}

void IXU::cancel_issue_rec_secondary(sc::Cycle cancel_cyc, const Uop& canceled_uop)
{
    // For these children & grandchildren of the primary canceled
    // memop, we wipe all their pregs in the specified cycle.
    const auto& pregs = canceled_uop->ixu_wr_pregs;
    for (unsigned idx = 0; idx < pregs.count; ++idx) {
        const auto preg = pregs.pregs[idx];
        DBGISS(" cancel issue rec, txid: {}, preg: {}, cancel_cyc: {}, secondary",
               canceled_uop->get_txid(),
               preg,
               cancel_cyc);
        issue_tracker[preg].set_cancel(cancel_cyc, false /*secondary cancel*/, 0);
    }
}

void IXU::allocate_issue_rec(const Uop& uop)
{
    const auto& pregs = uop->ixu_wr_pregs;
    for (unsigned idx = 0; idx < pregs.count; ++idx) {
        const auto preg = pregs.pregs[idx];
        DBGISS("Set preg: {} waiting to issue", preg);
        issue_tracker[preg].set_waiting_to_issue();
    }
}

void IXU::mark_pdests_ready(const Uop& uop)
{
    const auto& pregs = uop->ixu_wr_pregs;
    for (unsigned idx = 0; idx < pregs.count; ++idx) {
        const auto preg = pregs.pregs[idx];
        DBGISS("Marking pdest {} ready", preg);
        issue_tracker[preg].set_ready();
    }
}

void IXU::mark_load_pdest_ready(phys_reg_t preg)
{
    DBGISS("Marking load pdest {} ready", preg);
    issue_tracker[preg].set_ready_load(now);
}

void IXU::set_or_schedule_load_ready_ixu(const Uop& uop)
{
    const auto& pregs = uop->ixu_wr_pregs;
    for (unsigned idx = 0; idx < pregs.count; ++idx) {
        const auto preg = pregs.pregs[idx];
        if (!issue_tracker[preg].is_fsu_dst()) {
            mark_load_pdest_ready(preg);
            if (uop->is_single_uop_store() || uop->is_super_sus()) { // cas* dests
                DBGISS("txid: {} marking received_load_data true", uop->get_txid());
                uop->received_load_data = true;
            }
        }
    }
}
void IXU::set_or_schedule_load_ready_fsu(const Uop& uop)
{
    const auto& pregs = uop->ixu_wr_pregs;
    for (unsigned idx = 0; idx < pregs.count; ++idx) {
        const auto preg = pregs.pregs[idx];
        if (issue_tracker[preg].is_fsu_dst()) {
            mark_load_pdest_ready(preg);
        }
    }
}

phys_regs_t IXU::emitted_pregs(const ConstUop& uop)
{
    // collect emitted nonzero pregs and return them in a vector
    phys_regs_t pregs;
    if (uop->is_producer()) {
        auto preg = uop->get_prfid_id(); // factor in gather and their proxy credit
        AMPERE_ASSERT(preg != 0, "PREG MUST BE VALID (dst) preg: {}, disasm: {}\n", preg, uop->uop_disasm());
        pregs.insert(preg, !is_vector(uop->reg_dst));
    }
    if (uop->has_dest2()) {
        auto preg = uop->prfid2->get_id();
        AMPERE_ASSERT(preg != 0, "PREG MUST BE VALID (dst2) preg: {}, disasm: {}\n", preg, uop->uop_disasm());
        pregs.insert(preg, !is_vector(uop->reg_dst2));
    }
    if (uop->is_flag_producer() || uop->is_temp_flags_producer()) {
        auto preg = uop->flagsid->get_id();
        AMPERE_ASSERT(preg != 0, "PREG MUST BE VALID (flg) preg: {}, disasm: {}\n", preg, uop->uop_disasm());
        pregs.insert(preg, true);
    }
    return pregs;
}

/*************************************************************************
 * group block deallocate. check if all uops in group are complete,
 * and only deallocate if they are all done. pipestage XU03
 *************************************************************************/
bool IXU::group_deallocate(unsigned num)
{
    auto& scheduler = schedulers[num];
    if (!scheduler.might_dealloc) {
        return false;
    }
    scheduler.might_dealloc = false;

    if (0 == scheduler.entries_in_use) {
        return false;
    }

    auto sch_group_size = scheduler_group_size(Scheduler::from_sched(num).sched_type.value());
    const sc::Cycle prev_cycle = now - 1;
    // check each group sequentially
    for (unsigned grp = 0; grp < scheduler.sched_groups; grp++) {
        bool g_ready = true;

        // if any one of the entries is not ready, we cannot dealloc
        for (unsigned ent = 0; ent < sch_group_size; ent++) {
            auto* e = scheduler.groups[grp][ent];

            if (!e->can_dealloc) {
                g_ready = false;
                break;
            }

            // If cancel only blocks deallocation of load dependents, then a uop which was made
            // ready by a load exactly 1 cycle ago can't deallocate, because it might get canceled.
            if (cancel_only_blocks_load_dep_dealloc) {
                if (e->load_made_src_ready_cycle == prev_cycle) {
                    DBGISS("NODEALLOC load made src ready prev cycle. sch: {}, grp: {}, idx: {}", num, grp, ent);
                    g_ready = false;
                    // We should check again for deallocation next cycle
                    scheduler.might_dealloc = true;
                    break;
                }
            }
        }

        // if all of them are ready, then let's dealloc now
        if (g_ready) {
            for (unsigned ent = 0; ent < sch_group_size; ent++) {
                const auto& uop = scheduler.groups[grp][ent]->uop;
                table_deallocate(uop);
            }
            scheduler.need_to_sort = true;

            // We deallocated one group, exit out.
            // Since we deallocated one, there might be more waiting to deallocate
            scheduler.might_dealloc = true;
            return true;
        }
    }

    // no groups were available to deallocate
    return false;
}

/*************************************************************************
 * Function for observability. This is basically a watch window for the
 * grouped scheduler entries. Three bits per entry are displayed, which are
 * basically the stages the entry goes through. When all the bits are set
 * to 1 in the entire group, we can deallocate all entries in the group.

          G0        G1        G2        G3        G4
      +---------+---------+---------+---------+---------+
    0 | {1 1 0} | {1 1 1} | {1 1 1} | {0 0 0} | {1 1 1} |
    1 | {1 1 0} | {1 1 0} | {1 1 1} | {0 0 0} | {1 1 1} |
    2 | {1 1 1} | {1 1 1} | {0 0 0} | {0 0 0} | {0 0 0} |
    3 | {0 0 0} | {0 0 0} | {0 0 0} | {0 0 0} | {0 0 0} |
      +---------+---------+---------+---------+---------+

 *************************************************************************/
void IXU::table_print(unsigned num, std::ostream& os) const
{
    auto sch_group_size = scheduler_group_size(Scheduler::from_sched(num).sched_type.value());
    auto& scheduler = schedulers[num];

    os << "\n  ";
    for (unsigned grp = 0; grp < scheduler.sched_groups; grp++) {
        os << "     G" << grp << "   ";
    }
    os << "\n  ";
    for (unsigned grp = 0; grp < scheduler.sched_groups; grp++) {
        os << "+---------";
    }
    os << "+\n";

    for (unsigned ent = 0; ent < sch_group_size; ent++) {
        os << ent << " |";
        for (unsigned grp = 0; grp < scheduler.sched_groups; grp++) {
            auto* e = scheduler.groups[grp][ent];
            os << " {" << e->in_use << ' ' << e->all_ready << ' ' << e->can_dealloc << "} |";
        }
        os << "\n";

        for (unsigned grp = 0; grp < scheduler.sched_groups; grp++) {
            auto* e = scheduler.groups[grp][ent];
            if (!e->in_use) {
                os << "- ";
                continue;
            }
            os << e->uop->pc() << " ";
        }
        os << "\n";
    }
    os << "  ";
    for (unsigned grp = 0; grp < scheduler.sched_groups; grp++) {
        os << "+---------";
    }
    os << "+\n";
}

// deallocate. just reset the entry and decrement the in_use count.
void IXU::table_deallocate(const Uop& u)
{
    AMPERE_ASSERT(u->sch.entry != -1, "Scheduler entry should be assigned already");
    AMPERE_ASSERT(u->sch.sched_type, "Scheduler should be assigned already");

    auto* e = table[u->sch.scheduler()][u->sch.entry];
    AMPERE_ASSERT(e->can_dealloc, "Scheduler entry should be set to deallocate");

    --schedulers[u->sch.scheduler()].entries_in_use;
    ev_sch_dealloc(u);
    DBG_SCH(u, "TBDEALC");
    DBGISS("TBDEALC sch: {}, grp: {}, idx: {}", u->sch.scheduler(), e->grp, e->idx);
    credit_return->put(std::move(u->schid));
    e->reset();
}

/*************************************************************************
 * check number of free entries
 *************************************************************************/
unsigned IXU::table_entries_free(unsigned op_sched)
{
    const auto& scheduler = schedulers[op_sched];
    return static_cast<unsigned int>(sched_entries[op_sched] - scheduler.entries_in_use);
}

unsigned IXU::mem_pipe(PipeType pipe_num, unsigned partition) const
{
    return expand_pipe(pipe_num, partition);
}

bool IXU::xus_busy()
{
    for (auto& x : xu) {
        if (x.busy()) {
            return true;
        }
    }
    return false;
}

/*************************************************************************
 * Run an ALU XU.
 * XU modules are only run explicitly by the IXU
 * not by the simulator scheduler.
 *************************************************************************/
void IXU::alu_execute(PipeType pipe_num, unsigned partition)
{
    auto pipe = expand_pipe(pipe_num, partition);
    auto& alu = xu.at(pipe);
    auto uops = alu.execute();
    for (const auto& uop : uops) { // these uops are done executing
        if (static_cast<unsigned>(pipe_num) >= FSU_PIPE_TYPE_START)
            AMPERE_ASSERT(partition < Scheduler::fsu_partitions,
                          "EXECUTED fsu uop in illegal partition : {} {} {}",
                          uop->get_txid(),
                          uop->get_uopcode(),
                          uop->is_idiv_early_exit());
        uop->execute_done();
        if (fsu_zpipe) {
            DBG_X2Z("{:9} {:15} alu_execute  eeidiv: {}, gpr_x2z: {}, stdv_x2z: {} latency: {} storetype: {}",
                    uop->get_txid(),
                    uop->get_uopcode(),
                    uop->is_idiv_early_exit(),
                    uop->is_fpu_gpr_x_to_z(),
                    uop->is_stdv_x_to_z(),
                    uop->latency(),
                    archx::store_type_str(uop->uop_info()->store_type));

            if (uop->is_x_to_z() && pipe_num == Pipe::X) {
                // this catches the "X->Z" "g*" uops and "stdv combo" uops.
                // They will re-issue from the zpipe_reservation created earlier; Nothing to do
                DBG_X2Z("{:9} {:15} normal x2z finishing x", uop->get_txid(), uop->get_uopcode());
            } else if (uop->is_idiv_early_exit() && pipe_num == Pipe::Z) { // finishing a idiv on Z pipe
                // These start on the X pipe and re-issue on the Z pipe via zpipe_reservation
                DBG_X2Z("{:9} {:15} eeidiv finishing z", uop->get_txid(), uop->get_uopcode());
                writeback_and_commit(uop);
            } else if (uop->is_fsu_store()) { // FSU STORE DATA -> LSU
                // We handled X->Z stuff coming out of X above so this
                // should be purely Z pipe stuff now.
                AMPERE_ASSERT(pipe_num == *fsu_to_lsu_pipe, "Should be pipe Z by now");
                // This could be first execute of a "direct to Z" uop or
                // the 2nd execution (Z pipe execution) of an X->Z stdv.
                DBG_X2Z("XU sending STDV* to LSU. txid: {} {}", uop->get_txid(), uop->get_uopcode());
                zpipe_to_lsu_exec(uop);
            } else {
                if (uop->is_fdiv() || uop->is_idiv() || uop->is_fsqrt()) {
                    if (uop->is_idiv_early_exit() || uop->is_fdiv_early_exit()) {
                        ev_early_exit(uop, uop->get_early_exit_reason());
                    }
                    if (!(uop->is_scalar_idiv() && !uop->is_idiv_early_exit())) {
                        auto& fsu = fsu_sched[uop->sch.partition_normalized_to(Scheduler::fsu_partitions)];
                        fsu.block_pipe_issue_this_cycle_for_div_wb =
                            true; // block x scheduler for div_wb uop "pretend" issue
                        ev_block_fsu_pipe_for_div_wb_issue(uop);
                    }
                }

                DBG_X2Z("{:9} {:15} normal finishing on pipe {}, allocid: {}",
                        uop->get_txid(),
                        uop->get_uopcode(),
                        pipe_name(pipe_port),
                        uop->get_alloc_id());
                writeback_and_commit(uop);
            }
        } else {                                                       // pre-Trinity designs (simple Z pipe delay line)
            if (pipe_num == *fsu_to_lsu_pipe && uop->is_fsu_store()) { // FSU STORES -> LSU
                fsu_zpipe_std_out->put(uop);
                continue;
            }
            writeback_and_commit(uop);
        }
    } /* for */
}

void IXU::writeback_and_commit(const Uop& uop)
{
    // handle stuff coming out of the XUs (not loads)
    DBGISS("writeback and commit:  txid: {}, will_cancel: {}, should_wake_consumers: {}, disasm: {}",
           uop->get_txid(),
           uop->will_cancel(),
           uop->should_wake_consumers(),
           uop->get_uopcode());

    post_exec_hazards(uop);
    if (!uop->will_cancel() && uop->should_wake_consumers()) {
        // GPC takes care of the fixed commit delays for FSU ops.
        complete_uop(uop);
    }
}

void IXU::execute()
{
    // Run execute for all of the ALUs
    for (unsigned partition = 0; partition < Scheduler::ixu_partitions; ++partition) {
        for (unsigned pipe = IXU_PIPE_TYPE_START; pipe < IXU_PIPE_TYPE_END; ++pipe)
            alu_execute(static_cast<PipeType>(pipe), partition);
    }

    for (unsigned partition = 0; partition < Scheduler::fsu_partitions; ++partition) {
        for (unsigned pipe = FSU_PIPE_TYPE_START; pipe < FSU_PIPE_TYPE_END; ++pipe)
            alu_execute(static_cast<PipeType>(pipe), partition);
    }

    alu_execute(Pipe::BS, 0);
}

void IXU::advance_i2f_shelf()
{
    for (auto& fsu : fsu_sched) {
        // Try to send i2f uops into the Y-pipe if the Y-pipe is not
        // processing a the 2nd part of a double pumped uop.

        // this must be called after write-back which sets i2f.
        // this must be called after try_issue() which sets pipe_mid_double_pump_uop.
        if (fsu.pipe_mid_double_pump_uop) {
            AMPERE_ASSERT(fsu.i2f_shelf == nullptr,
                          "there should be nothing on i2f_shelf during 2nd cycle of double-pumped uop");
            if (fsu.i2f) {
                DBG_DPI2F("move i2f to shelf (mid dbl pump on y pipe)");
                ev_i2f_shelf(fsu.i2f);
                fsu.i2f_shelf = fsu.i2f;
                fsu.i2f = nullptr;
            }
            return;
        }

        if (fsu.i2f_shelf) {
            DBG_DPI2F("issue from i2f_shelf, allocid: {}", fsu.i2f_shelf->get_alloc_id());
            issue_i2f_to_fsu_pipe(fsu.i2f_shelf);
            fsu.i2f_shelf = nullptr;

            if (fsu.i2f) { // put i2f on shelf if present, and clear i2f
                DBG_DPI2F("move i2f to shelf (shelf was occupied), allocid: {}", i2f->get_alloc_id());
                ev_i2f_shelf(fsu.i2f);
                fsu.i2f_shelf = fsu.i2f;
                fsu.i2f = nullptr;
            }
        } else if (fsu.i2f) {
            DBG_DPI2F("issue i2f, shelf empty");
            issue_i2f_to_fsu_pipe(fsu.i2f);
            fsu.i2f = nullptr;
        }
    }
}

bool IXU::mem_port_canceled(unsigned port)
{
    return cancel_occupies_mem[port] == now;
}

bool IXU::can_issue_uop(const Uop& uop)
{
    // see if scheduler entry can issue this uop
    const auto pipe_type = uop->sch.pipe_type;
    const auto pipe = uop->sch.pipe();
    bool xydebug = false;
#if defined(DEBUG_IXU)
    xydebug = uop->sch.is_fsu();
#endif

    // new y-pipe double-pumped uops can only issue if "i2f shelf" and no i2f this cycle is empty
    if (pipe_type == i2f_pipe.val() && uop->first_issue_of_double_pumped()) {
        const auto& fsu = fsu_sched[uop->sch.partition_normalized_to(Scheduler::fsu_partitions)];
        if (fsu.i2f_shelf) {
            if (xydebug) {
                DBGIX("  y blocked by shelf  {:x} {:s} {}", uop->pc().value(), uop->get_uopcode(), uop->get_txid());
            }
            DBG_DPI2F("Y-pipe double pump issue blocked; i2f shelf has priority over y-pipe double pump uop");
            return false;
        }
        if (fsu.pipe_schedule_i2f) {
            if (xydebug) {
                DBGIX("  y blocked double pump new  {:x} {:s} {}",
                      uop->pc().value(),
                      uop->get_uopcode(),
                      uop->get_txid());
            }

            DBG_DPI2F("Y-pipe double pump issue blocked; new i2f has priority over y-pipe double pump uop");
            return false;
        }
    }

    if (uop->is_load()) {
        auto mempipe = mem_pipe(pipe_type, uop->sch.partition);
        if (mem_issue_port[mempipe]->can_put() && !mem_port_canceled(mempipe)) {
            return true;
        } else {
            if (xydebug) {
                DBGIX("  load prob  {:x} {:s} {}", uop->pc().value(), uop->get_uopcode(), uop->get_txid());
            }

            return false;
        }
    }

    if (uop->is_ixu_store()) {
        auto mempipe = mem_pipe(pipe_type, uop->sch.partition);
        bool can_issue = mem_issue_port[mempipe]->can_put() && !mem_port_canceled(mempipe);
        if (xydebug && !can_issue) {
            DBGIX("  store prob  {:x} {:s} {}", uop->pc().value(), uop->get_uopcode(), uop->get_txid());
        }
        return can_issue;
    }

    // U/V/W pipes require that the related load pipe is not busy.
    if (pipe_type == Pipe::U || pipe_type == Pipe::V || pipe_type == Pipe::W) {
        if (uvw_load_port_share)
            // Fsu partition 0 uses load_ports of C pipe (0,1) for U0, V0/W0
            // Fsu partition 1 uses load_ports of E pipe (2,3) for U1, V1/W1
            if (IXU::load_port_busy[Scheduler::fsu_load_port_for_uvw(pipe_type, uop->sch.partition)]) {
                ev_uvw_stall(uop);
                return false;
            }
    }

    const bool can_issue = xu[pipe].can_issue(uop);
    if (xydebug && !can_issue) {
        DBGIX("  xu blocked  {:x} {:s} {}", uop->pc().value(), uop->get_uopcode(), uop->get_txid());
    }

    // A slow int uop wants to issue but can't because the BS pipe has already been
    // taken by the other partition this cycle.
    if (shared_slow_int_pipe && uop->is_slow_integer() && can_issue && issued_slow_int) {
        ev_uop_issue(uop, ev_data_ixu_pipe(pipe)); // Emit uop_issue event to match RTL
        ev_bs_pipe_cancel(uop);
        return false;
    }

    if (can_issue && fsu_zpipe) {
        const Cycle now = get_cycle();
        auto& fsu = fsu_sched[uop->sch.partition_normalized_to(Scheduler::fsu_partitions)];
        switch (pipe_type) {
        case Pipe::Z:
            // scheduler issues simple STDV uops to Z pipe if nothing else using Z pipe this cycle
            return !fsu.has_zpipe_reservation(now);
        case Pipe::X: // looking for X->Z uops (including idiv). Try to avoid colliding on Z pipe
            if (uop->is_x_to_z()) {
                // For non-early-exit idiv, we don't model multiple
                // zpipe reservations (one for early exit and one for
                // the actual div latency). We only model one use of
                // the zpipe.

                // To model 2 uses of Z, we could reserve an early
                // exit zpipe use by putting a nullptr in the
                // zpipe_reservation map and then checking-for & skipping
                // nullptr in issue_z().
                const sc::Cycle execution_end = now + uop->latency();
                if (fsu.zpipe_reservation.find(execution_end) != fsu.zpipe_reservation.end()) {
                    // zpipe not-free. could  extending latency by one cycle...
                    // infrequent event so just avoid scheduling it this cycle.
                    // xydebug implied as true here since it is x-to-z
                    DBGIX("  z blocked at end  {:x} {:s} {}, uop latency: {}",
                          uop->pc().value(),
                          uop->get_uopcode(),
                          uop->get_txid(),
                          uop->latency());
                    return false;
                }
            }
            break;
        default:
            break;
        }
    }

    return can_issue;
}

void IXU::event_latencies(const ConstUop& uop)
{
    const auto latency = uop->latency();
    if (uop->is_idiv()) {
        DBG_DIVL("Emitting event w/latency {}, txid: {},  inum {}", latency, uop->get_txid(), uop->inum().id());
        ev_idiv_latency(uop, ev_latency(latency));
    } else if (uop->is_fdiv()) {
        if (uop->is_fdiv64()) {
            ev_fdiv64_latency(uop, ev_latency(latency));
        } else if (uop->is_fdiv32()) {
            ev_fdiv32_latency(uop, ev_latency(latency));
        } else {
            ev_fdiv16_latency(uop, ev_latency(latency));
        }
    } else if (uop->is_fsqrt()) {
        ev_fsqrt_latency(uop, ev_latency(latency));
    }
}

void IXU::sched_can_dealloc(SchedEntry* sch_entry, unsigned sch_idx)
{
    // mark as ready to deallocate
    schedulers[sch_idx].might_dealloc = true;
    if (!sch_entry->can_dealloc) {
        sch_entry->can_dealloc = true;
        sch_remove();
    }
}

void IXU::record_bypass_stats(const ConstUop& consumer_uop)
{
    // Record producer->consumer bypass information for FSU ops
    for (unsigned src_idx = 0; src_idx < UOP_MAX_SRCS; ++src_idx) {
        if (consumer_uop->reg_src[src_idx] == LReg::INVALID) {
            continue;
        }
        const auto phys_reg_src = consumer_uop->reg_psrc[src_idx];
        if (phys_reg_src) {
            const IssueRec& issue_rec = issue_tracker[phys_reg_src];
            bool record{false};
            unsigned bypass_delay{0};
            bool bypassed{false};
            switch (issue_rec.ctype) {
            case CompType::FSU_DIV_SQRT: { // bypassing is relative to the div_wb issue. this is approximate
                const sc::Cycle done_at = (issue_rec.done_at() + // nonzero for DIV/SQRT
                                           div_bypassing_delay(true));
                if (done_at + fsu_max_writeback_latency > now) { // is it bypassed?
                    bypass_delay = now - done_at;
                    bypassed = true;
                }
                record = true;
                break;
            }
            case CompType::FSU:
                if (issue_rec.issue_time + fsu_max_writeback_latency > now) { // is it bypassed?
                    bypass_delay = now - issue_rec.issue_time;
                    bypassed = true;
                }
                record = true;
                break;
            default: // do not record anything
                break;
            }
            if (record) {
                auto prod_cons = std::make_tuple(issue_rec.bypass_stats_info.fsu_producer_uopcode,
                                                 issue_rec.bypass_stats_info.fsu_producer_unit,
                                                 consumer_uop->get_uopcode(),
                                                 consumer_uop->fsu_unit(),
                                                 static_cast<std::uint32_t>(src_idx),
                                                 static_cast<std::uint32_t>(bypassed));

                if (bypass_stats.contains(prod_cons)) { // key exists in table already, augment data
                    bypass_stats[prod_cons].first += 1;
                    bypass_stats[prod_cons].second += bypass_delay;
                } else { // first time we saw this key
                    bypass_stats.emplace(prod_cons, std::make_pair(1, bypass_delay));
                }
            }
        }
    }
}

void IXU::pmu_sve_pred(const ConstUop& uop)
{
    const auto esize = uop->uop_info()->vector_element_size;
    archx::pred_reg_t pval = 0xFFFF;
    if (uop->is_predicated() && (uop->is_predicated_sampled() || uop->is_load() || uop->is_store())) {
        pval = uop->pred(uop->governing_predicate_reg());
        ev_pmu(uop, ev_num(archx::PMU::SVE_PRED_SPEC));
        if (archx::pred_all_zero(pval, esize)) {
            ev_pmu(uop, ev_num(archx::PMU::SVE_PRED_EMPTY_SPEC));
        } else if (archx::pred_all_active(pval, esize)) {
            ev_pmu(uop, ev_num(archx::PMU::SVE_PRED_FULL_SPEC));
        } else {
            ev_pmu(uop, ev_num(archx::PMU::SVE_PRED_PARTIAL_SPEC));
        }
    }
}

void IXU::issue_entry(SchedEntry* e)
{
    const auto& uop = e->uop;

    const auto sched_idx = uop->sch.scheduler();
    schedulers[sched_idx].need_to_sort = true;

    DBGISS("ISSUED uop txid: {}, inum: {}, allocid: {}, sch: {}, will_cancel: {}, disasm: {:15s}, {}",
           uop->get_txid(),
           uop->inum().id(),
           uop->get_alloc_id(),
           uop->sch.name(),
           uop->will_cancel(),
           uop->uop_disasm(),
           sus_status(uop));

    e->issued_canceled = uop->will_cancel();

    issue_uop(uop);

    if (collect_load_use_stats)
        tally_load_use_stats(e);
}

void IXU::issue_uop(const Uop& uop)
{
    auto pipe = uop->sch.pipe();
    auto pipe_type = uop->sch.pipe_type;

    // Emit issue event up here so it counts both real and canceled issues
    ev_uop_issue(uop, ev_data_ixu_pipe(pipe));

    if (uop->is_sve())
        pmu_sve_pred(uop);

    // Remember that the BS pipe has been taken for this cycle
    if (uop->is_slow_integer())
        issued_slow_int = true;

    event_latencies(uop);
    if (enable_bypass_stats_collection && uop->is_fsu())
        record_bypass_stats(uop);

    record_issue(uop);
    uop->set_issued();

    // To avoid having to hunt down and kill uops inside the XUs,
    // don't actually send a canceled uop to the execution units. This
    // also prevents them from duplicate writing back/complete if we
    // happen to reap them (which uncancels the uop) while they were
    // still executing.
    if (uop->will_cancel()) {
        // Since we aren't going to deallocate this scheduler entry, mark it so that it
        // doesn't try to issue again. This flag will be cleared in a few cycles when
        // the uop completes the cancel pipeline.
        schedule_cancellation_secondary(uop);
        const auto cancel_time = uop->will_cancel_at();
        DBGISS("txid: {} Setting secondary cancel at {}", uop->get_txid(), cancel_time);
        AMPERE_ASSERT(cancel_time != 0, "cancel_time cannot be zero");
        cancel_issue_rec_secondary(cancel_time, uop); // poison pdest, make it ready to enable consumers

        if (uop->is_lsu()) {
            bool block_use_mem_pipe = true;
            if (uop->is_single_uop_store()) {
                if (uop->sta_issuing) {
                    block_use_mem_pipe = false;
                    uop->sta_issuing = false;
                }
            }

            if (block_use_mem_pipe) {
                // Mark the memory pipe as occupied so that we don't try to issue another uop to it this same cycle
                const auto mempipe = mem_pipe(pipe_type, uop->sch.partition);
                cancel_occupies_mem[mempipe] = now;
                return;
            }
        } else {
            // Mark the ALU as occupied so that we don't try to issue
            // another uop to it this same cycle.  We do not need to
            // send anything to execute to allow waking the
            // second-generation (grandchildren);  The IssueRec
            // handles that for us.
            xu[pipe].occupy();
            return;
        }
    }

    if (uop->is_lsu()) {
        // Send the uop to the LSU
        auto mempipe = mem_pipe(pipe_type, uop->sch.partition);
        DBGISS("Issuing Normal to LSU: sta-id: {}, txid: {} {:016x} {} on port {}",
               uop->store_associated_sta_id,
               uop->get_txid(),
               uop->pc().value(),
               uop->uop_disasm(),
               mempipe);
        mem_issue_port[mempipe]->put(uop);
    } else {
        // Issue the uop to an XU
        DBGISS("Issuing Normal : txid: {}, pc: {:016x}, uopcode: {}, allocid: {}, issue_count: {}, will_cancel: {}",
               uop->get_txid(),
               uop->pc().value(),
               uop->get_uopcode(),
               uop->get_alloc_id(),
               uop->issue_count,
               uop->will_cancel());

        xu[pipe].issue(uop);
    }

    // collect data for throttling.
    if (throttle_enable) {
        uop->is_fsu() ? ++total_issues_in_epoch_fsu : ++total_issues_in_epoch_ixu;
    }
}

void IXU::fsu_zpipe_std_ixu_to_lsu_exec()
{
    while (fsu_zpipe_std_in->can_take()) {
        auto uop = fsu_zpipe_std_in->take();
        zpipe_to_lsu_exec(uop);
    }
}

void IXU::zpipe_to_lsu_exec(const Uop& uop)
{
    std::optional<uint64_t> flush_id{std::nullopt};
    if (flush_port->can_read()) {
        flush_id = flush_port->read()->alloc_id;
    }

    if (!flush_id || !uop->is_younger(*flush_id)) {
        auto mempipe = mem_pipe(uop->sch.pipe_type, uop->sch.partition);
        DBG_X2Z("Sending Z output to LSU: sta-id: {} txid: {} {:016x} {} on port {}",
                uop->store_associated_sta_id,
                uop->get_txid(),
                uop->pc().value(),
                uop->get_uopcode(),
                port);
        mem_issue_port[mempipe]->put(uop);
        ev_fsu_zpipe_std_exec(uop);
    }
}

/*************************************************************************
 * Method for processing data from throttle algorithm and returning it
 * to IDR for reducing decode width
 *************************************************************************/
void IXU::process_throttle_data()
{
    // Only purpose of this port is to wakeup the IXU module each epoch
    //
    throttle_sample_timer_out->put(true);
    if (throttle_sample_timer_in->can_take()) {
        throttle_sample_timer_in->take();
    }

    // Calculate the ixu and fsu issues per cycle in this epoch. This
    // is an approximation to what the hardware could do.
    //
    float epoch_iipc = total_issues_in_epoch_ixu / (float)throttle_epoch;
    float epoch_fipc = total_issues_in_epoch_fsu / (float)throttle_epoch;
    bool next_ixu_low_issue{false}, next_fsu_low_issue{false};

    // first create the ixu status bit
    //
    if (prev_ixu_low_issue) { // last time we throttled
        next_ixu_low_issue = !(epoch_iipc > throttle_watermark_hi_ixu);
    } else { // we are not throttling
        next_ixu_low_issue = (epoch_iipc < throttle_watermark_lo_ixu);
    }

    // then create the fsu status bit
    //
    if (prev_fsu_low_issue) { // last time we throttled
        next_fsu_low_issue = !(epoch_fipc > throttle_watermark_hi_fsu);
    } else { // we are not throttling
        next_fsu_low_issue = (epoch_fipc < throttle_watermark_lo_fsu);
    }

    // send signals
    idr_throttle_ixu->put(next_ixu_low_issue);
    idr_throttle_fsu->put(next_fsu_low_issue);

    // shift over the history
    prev_ixu_low_issue = next_ixu_low_issue;
    prev_fsu_low_issue = next_fsu_low_issue;

    // reset the counters
    total_issues_in_epoch_ixu = total_issues_in_epoch_fsu = 0;
}

void IXU::finish_lsu_uop_fsu(const Uop& uop)
{
    post_exec_hazards(uop);
    set_or_schedule_load_ready_fsu(uop);
}
void IXU::finish_lsu_uop_ixu(const Uop& uop)
{
    post_exec_hazards(uop);
    set_or_schedule_load_ready_ixu(uop);
}

void IXU::sus_wb(const Uop& uop)
{
    if (uop->is_single_uop_store() && uop->sus.written_back) {
        AMPERE_ERROR("incorrect SUS double writeback from LSU for alloc_id {}", uop->get_alloc_id());
    } else {
        uop->sus.written_back = true;
    }
}
void IXU::writeback_uop_lsu_fsu(const Uop& uop)
{
    DBGISS("WB-LSU FSU uop txid: {} allocid: {},  disasm: {:15s}",
           uop->get_txid(),
           uop->get_alloc_id(),
           uop->uop_disasm());
    sus_wb(uop);

    // check for last gather load to arrive to wake up consumers
    if (uop->is_gather()) {
        DBG_GATHERC("IXU wb - remove use. parent txid: {}, txid: {}, allocid: {}",
                    uop->get_txid_parent(),
                    uop->get_txid(),
                    uop->get_alloc_id());
        GatherProxyCreditPtr gprox = uop->get_gather_proxy();
        if (gprox->done_ixu()) {
            fsu_gather_delay.put(uop); // delay for waking up gather dest consumers
        }
    } else {
        finish_lsu_uop_fsu(uop);
    }
}

void IXU::writeback_uop_lsu_ixu(const Uop& uop)
{
    DBGISS("WB-LSU IXU uop txid: {} allocid: {},  disasm: {:15s}",
           uop->get_txid(),
           uop->get_alloc_id(),
           uop->uop_disasm());
    sus_wb(uop);

    finish_lsu_uop_ixu(uop);
}

void IXU::fsu_writeback_splitter(const Uop& uop)
{
    if (uop->writes_vreg()) {
        if (uop->writes_ixu_reg()) { // some dests are FSU and some IXU (vector load pair). Split their flows
            writeback_uop_lsu_ixu(uop);
            fsu_load_delay.put(uop); // delay the fsu portion a cycle
        } else {                     // just writes FSU regs
            fsu_load_delay.put(uop);
        }
    } else {
        writeback_uop_lsu_ixu(uop);
    }
}

void IXU::writeback_lsu()
{ // called every cycle
    // handle FSU responses that were delayed a cycle relative to IXU due to RC delays.
    while (fsu_load_delay.can_take()) {
        auto uop = fsu_load_delay.take();
        writeback_uop_lsu_fsu(uop);
    }

    // Writeback results from LSU
    for (auto& writeback_port : writeback_xu) {
        while (writeback_port->can_take()) {
            auto uop = writeback_port->take();
            fsu_writeback_splitter(uop);
        }
    }

    while (fsu_gather_delay.can_take()) {
        auto uop = fsu_gather_delay.take();
        finish_lsu_uop_fsu(uop);
    }
}

void IXU::complete_uop(const Uop& uop)
{
    // send uop to GPC for commitment
    DBGISS("ixu completing txid: {}, allocid: {}, {:016x}: {}",
           uop->get_txid(),
           uop->get_alloc_id_general(),
           uop->pc().value(),
           uop->uop_disasm());
    if (uop->is_gather()) {
        DBGISS("ixu completing GATHER txid: {}, allocid: {}, {:016x}: {}",
               uop->get_txid(),
               uop->get_alloc_id_general(),
               uop->pc().value(),
               uop->uop_disasm());
    }

    if (!uop->is_atomic_load() && !uop->is_gather()) {
        if (uop->is_producer())
            issue_tracker[uop->get_prfid_id()].cancelable = false;
        if (uop->has_dest2())
            issue_tracker[uop->prfid2->get_id()].cancelable = false;
    }

    commit_port->put(uop);
    release_producer_credits(uop);
}

void IXU::complete_lsu()
{                                             // called every cycle
    for (auto& complete_port : complete_xu) { // data from LSU
        while (complete_port->can_take()) {
            auto [uop, writeback] = complete_port->take();
            if (writeback) {
                fsu_writeback_splitter(uop);
            }
            // FIXME: LSU actually completes memops & gathers
            complete_uop(uop);
        }
    }
}

void IXU::set_resource_rate(const Uop& uop)
{
    auto resource_rate = uop->archx_uop->info->resource_rate;
    if (idiv_early_exit_rate && uop->is_idiv_early_exit()) {
        resource_rate = idiv_early_exit_rate;
    } else if (fdiv_early_exit_rate && uop->is_fdiv_early_exit()) {
        resource_rate = fdiv_early_exit_rate;
    }
    uop->set_resource_rate(resource_rate);
}

int IXU::compute_extra_div_sqrt_latency(const Uop& uop) const
{ // div & sqrt
    switch (uop->get_uopcode()) {
    case Uopcode::GSDIV_D_NV: // gpr integer div excluding early out
    case Uopcode::GSDIV_S_NV:
    case Uopcode::GUDIV_D_NV:
    case Uopcode::GUDIV_S_NV:
    case Uopcode::SDIV32:
    case Uopcode::SDIV64:
    case Uopcode::UDIV32:
    case Uopcode::UDIV64:
        if (uop->is_idiv_early_exit()) {
            return 0;
        }
        return extra_gpr_div_latency;

    case Uopcode::FDIV_H_NV: // advsimd nonvector (scalar) including early out
    case Uopcode::FDIV_S_NV:
    case Uopcode::FDIV_D_NV:
        return extra_scalar_fdiv_latency;

    case Uopcode::FDIV_H_SV: // advsimd short & long vector
    case Uopcode::FDIV_H_LV:
    case Uopcode::FDIV_S_SV:
    case Uopcode::FDIV_S_LV:
    case Uopcode::FDIV_D_LV:
        return extra_advsimd_fdiv_latency;

    case Uopcode::FDIV_H_PV: // sve fdiv
    case Uopcode::FDIV_S_PV:
    case Uopcode::FDIV_D_PV:
    case Uopcode::FDIVR_H_PV:
    case Uopcode::FDIVR_S_PV:
    case Uopcode::FDIVR_D_PV:

    case Uopcode::SDIV_S_PV: // sve idiv
    case Uopcode::SDIV_D_PV:
    case Uopcode::SDIVR_S_PV:
    case Uopcode::SDIVR_D_PV:
    case Uopcode::UDIV_S_PV:
    case Uopcode::UDIV_D_PV:
    case Uopcode::UDIVR_S_PV:
    case Uopcode::UDIVR_D_PV:
        // assuming for now that vector divides incur same extra
        // bypass overhead the AdvSIMD vector div are seeing.
        return extra_advsimd_fdiv_latency;

    case Uopcode::FSQRT_D_LV:
    case Uopcode::FSQRT_D_NV:
    case Uopcode::FSQRT_D_PV:
    case Uopcode::FSQRT_H_LV:
    case Uopcode::FSQRT_H_NV:
    case Uopcode::FSQRT_H_PV:
    case Uopcode::FSQRT_H_SV:
    case Uopcode::FSQRT_S_LV:
    case Uopcode::FSQRT_S_NV:
    case Uopcode::FSQRT_S_PV:
    case Uopcode::FSQRT_S_SV:
        return extra_fsqrt_latency;

    default:
        return 0;
    }
}

void IXU::compute_base_latency(const Uop& uop)
{
    // set div/sqrt latency, zpipe latency, and i2f extra latency

    auto latency = uop->raw_latency();
    if (uop->fsu_unit_producing() == archx::FsuUnit::DV) {
        if (uop->div_latency) { // idiv/fdiv/fsqrt early exit or idiv variable latency
            latency = uop->div_latency;
        }
    }
    AMPERE_ASSERT(latency > 0, "Latency must be >0");

    if (fsu_zpipe && (uop->is_fsu_store() || uop->get_pipe_type() == *fsu_to_lsu_pipe)) {
        latency =
            xu[expand_pipe(*fsu_to_lsu_pipe, uop->sch.partition_normalized_to(Scheduler::fsu_partitions))].latency(uop);
    }

    if (uop->is_i2f()) {
        uop->fsu_i2f_latency = fsu_i2f_exe_latency;
    }

    const auto extra = compute_extra_div_sqrt_latency(uop); // might be negative
    if (extra < 0 && std::abs(extra) >= latency) {
        AMPERE_ERROR("Latency {} extra offset {} is too large, causing latency underflow", latency, extra);
    }
    latency += extra;
    DBG_DIVL("With extra latency {}, extra {}, txid: {},  inum {}", latency, extra, uop->get_txid(), uop->iid());
    uop->assign_latency(latency);
}

void IXU::watch_loads(const Uop& uop)
{
    // Restrict to vector dests
    if ((uop->is_load() && archx::is_vector(uop->reg_dst)) || uop->is_gather()) {
        const auto alloc_id = uop->get_alloc_id_general();
        if (!data_returned.contains(alloc_id)) { // avoid inserting more than once (can happen with fan out)
            data_returned[alloc_id] = now;
            data_cleanout.emplace_back(alloc_id, now + histogram_load_home_to_schedule_max_bin);
        }
    }
}

void IXU::tally_load_use_stats(SchedEntry* e)
{
    // check all the operands when the instruction schedules because
    // multiple of them could have come from recent loads
    for (unsigned s = 0; s < UOP_MAX_SRCS; s++) {
        if (e->src_ready[s]) {
            const auto alloc_id = e->src_alloc_id[s];
            const auto& it = data_returned.find(alloc_id);
            if (it != data_returned.end()) {
                std::uint64_t load_home_to_schedule = now - it->second;
                if (load_home_to_schedule >= histogram_load_home_to_schedule_max_bin) { // bound check
                    load_home_to_schedule = histogram_load_home_to_schedule_max_bin - 1;
                }
                histogram_load_home_to_schedule[load_home_to_schedule] += 1;
            }
        }
    }
}

void IXU::clean_load_use_stats()
{
    // clean out data_returned & data_cleanout maps after suitable delay
    while (data_cleanout.size()) {
        const auto& rec = data_cleanout.front();
        if (now >= rec.cycle) {
            const auto it = data_returned.find(rec.alloc_id);
            AMPERE_ASSERT(it != data_returned.end(), "Missing data_returned record");
            data_returned.erase(it);
            data_cleanout.pop_front();
        } else {
            break;
        }
    }
}

void IXU::post_exec_hazards(const Uop& uop)
{
    DBGISS("post_exec_hazards txid: {} allocid: {}  pc {:016x} uopcode: {:s} dst: {:s}, will_cancel: {}, W-PREGS: {}",
           uop->get_txid(),
           uop->get_alloc_id(),
           uop->pc().value(),
           uop->get_uopcode(),
           uop->reg_dst,
           uop->will_cancel(),
           wr_pregs_str(uop));

    const bool mem = (uop->is_load() || uop->is_store());
    ev_uop_writeback(uop, ev_data_memop(mem), ev_data_ixu_pipe(uop->sch.pipe())); // only post this once
    if (uop->is_load()) {
        load_wakeup_this_cycle = true;
    }

    // U,V,W pipes use the C pipe write port when it is not busy
    if (uop->is_load_fsu()) {
        auto pipe_type = uop->sch.pipe_type;
        IXU::load_port_busy[Scheduler::fsu_load_port_for_load(pipe_type, uop->sch.partition)] = true;
    }

    // Only do these cross-pipe blocking actions the first time, to make a hole
    // for the uop to execute in the other unit.

    // If an int->fp uop is completing, it takes the Y-pipe's schedule slot this cycle.
    // This happens regardless of whether this writeback will wake up any dependents.
    // Once it executes on Pipe::Y, it is really done.
    if (uop->is_i2f()) {
        if (uop->sch.pipe_type == *i2f_pipe) { // final execution on FSU
            mark_pdests_ready(uop);
        } else { // first execution on IXU
            DBGISS("\tI2F (first part, not on Y)");
            i2f_block_fsu_scheduler(uop);
            return;
        }
    }

    // If an fp->int uop is completing, we should not schedule on one of the A-pipes in the
    // next cycle. This happens regardless of whether this writeback will wake up any dependents.
    if (uop->is_f2i()) {
        f2i_block_ixu_scheduler(uop);
    }
}

void IXU::release_producer_credits(const Uop& producer_uop)
{
    // Once we schedule the writebacks, we do not want the producer to
    // be "resident" in src_producer[], otherwise new dependents may
    // show up and become attached to it but they'll hang forever as
    // they'll never get a writeback.
    const auto alloc_id = producer_uop->get_alloc_id_general(); // alloc_id is used to match producers to consumers

    if (producer_uop->is_gather()) {
        auto gprox = producer_uop->get_gather_proxy();
        gprox->remove_use_ixu();
        if (gprox->done_ixu() && gprox->prfid_still_valid()) {
            DBGISS("to untrack gather txid: {}, prfid: {}, alloc_id: {}",
                   producer_uop->get_txid(),
                   gprox->get_prfid_id(),
                   alloc_id);
            untrack_preg(producer_uop, gprox->get_prfid_id(), alloc_id);
        }
    } else if (producer_uop->prfid) {
        untrack_preg(producer_uop, producer_uop->get_prfid_id(), alloc_id);
    }

    if (producer_uop->prfid2) {
        untrack_preg(producer_uop, producer_uop->prfid2->get_id(), alloc_id);
    }

    if (producer_uop->flagsid) {
        untrack_preg(producer_uop, producer_uop->flagsid->get_id(), alloc_id);
    }
}

/*************************************************************************
 * Spin through all the sources and mark dependencies based on where
 * the dsts are in the machine (reg file, rob, etc)
 *************************************************************************/
void IXU::mark_reg_deps(SchedEntry* e)
{
    // check source dependencies
    e->load_made_src_ready_cycle = 0;
    for (uint s = 0; s < UOP_MAX_SRCS; s++) {
        const auto preg = e->uop->reg_psrc[s];
        const auto lreg = e->uop->reg_src[s];

        // For gathers, there are multiple alloc ids and which one ultimately writes
        // back is random. We use the max alloc id of the 2 or 4 gathers.

        // is the register being produced already scheduled?
        if (archx::invalid_reg(lreg)) {
            DBG_BYP("mark_dep ZR src-ready: allocid: {} txid: {}, src: {}, lreg: {}, psrc: {}  ",
                    e->uop->get_alloc_id(),
                    e->uop->get_txid(),
                    s,
                    regstr(lreg),
                    preg);
            e->src_ready[s] = true;
        } else if (src_producer[preg].resident) {
            // if the reg is resident, mark ourselves on the wakeup table
            auto producer_alloc_id = src_producer[preg].alloc_id;
            AMPERE_ASSERT(e->uop->get_alloc_id() > producer_alloc_id,
                          "The consumer alloc_id must be younger (greater) than that of the producer");
            DBG_BYP(
                "mark_dep producer-resident: allocid: {} txid: {} src: {}, lreg: {}, psrc: {} <- producer allocid: {}",
                e->uop->get_alloc_id(),
                e->uop->get_txid(),
                s,
                regstr(lreg),
                preg,
                producer_alloc_id);
            e->src_alloc_id[s] = producer_alloc_id;
        } else {
            // if the reg isn't resident, set the source as ready immediately
            DBG_BYP("mark_dep src-ready: allocid: {} txid: {}, src: {}, lreg: {}, psrc: {}  ",
                    e->uop->get_alloc_id(),
                    e->uop->get_txid(),
                    s,
                    regstr(lreg),
                    preg);
            e->src_ready[s] = true;
        }
    }
    if (is_ready_check_only(e)) {
        DBG_BYP("mark_dep all-src-ready: allocid: {} txid: {} ", e->uop->get_alloc_id(), e->uop->get_txid());
        ev_uop_ready(e->uop);
    }
}

/*************************************************************************
 * Static function used in quicksorting the entries using heuristic
 * based on in_use, ready, issued, and age.
 *************************************************************************/
static bool sch_entry_sort(SchedEntry* a, SchedEntry* b)
{
    // Return true if you want "a" to be logically before "b" in consideration.
    // "issued once" should be first so it can complete
    if (a->issued_once != b->issued_once) {
        return a->issued_once > b->issued_once;
    }

    // "not in use" is greater, flip the comparison
    if (a->in_use != b->in_use) {
        return a->in_use > b->in_use;
    }

    // can_dealloc is greater
    if (a->can_dealloc != b->can_dealloc) {
        return a->can_dealloc < b->can_dealloc;
    }

    // later uop is greater
    return a->alloc_id < b->alloc_id;
}

/*************************************************************************
 * Schedule loop is run once per cycle
 * Try to schedule ALU ops, then MEM ops, then allocate new entries
 *************************************************************************/
void IXU::schedule()
{
    // On some projects, IXU will not return any credits to IDR in the same cycle as a load cancel
    if (!cancel_this_cycle || cancel_only_blocks_load_dep_dealloc) {
        // XU03: run deallocate check on all subpartitions
        for (unsigned i = 0; i < int_dealloc_per_cycle; i++) {
            for (unsigned partition = 0; partition < Scheduler::ixu_partitions; ++partition) {
                for (unsigned s = 0; s < IXU_INT_SCH_SIZE; ++s)
                    group_deallocate(Scheduler(static_cast<SchedulerType>(IXU_SCH_START + s), partition).scheduler());
            }
        }
        for (unsigned i = 0; i < mem_dealloc_per_cycle; i++) {
            for (unsigned partition = 0; partition < Scheduler::ixu_partitions; ++partition) {
                for (unsigned s = 0; s < MEM_SCH_SIZE; ++s)
                    group_deallocate(Scheduler(static_cast<SchedulerType>(MEM_SCH_START + s), partition).scheduler());
            }
        }
    }
    for (unsigned i = 0; i < fsu_dealloc_per_cycle; i++) {
        for (unsigned partition = 0; partition < Scheduler::fsu_partitions; ++partition) {
            for (unsigned s = 0; s < FSU_SCH_SIZE; ++s)
                group_deallocate(Scheduler(static_cast<SchedulerType>(FSU_SCH_START + s), partition).scheduler());
        }
    }

    // If an FP to int move is arriving next cycle, don't schedule on one
    // of the A pipes this cycle to avoid a writeback conflict
    // Which partition we select ping-pongs for fairness.
    unsigned total_ixu_pipe_schedule_f2i{0};
    while (ixu_pipe_schedule_f2i.can_take()) {
        auto producer_uop = ixu_pipe_schedule_f2i.take();
        mark_pdests_ready(producer_uop);
        ++total_ixu_pipe_schedule_f2i;
    }

    // X-to-Z uops and integer early exit divides use the Z pipe on
    // Trinity onwards. They have precidence over direct-to-Z & STDV
    // uops.
    run_schedule_zpipe();

    // XU02: run scheduler algorithm on all subpartitions
    // B0/B1 share the BS pipe, and X/Y share the Z pipe
    unsigned ixu_partition_offset = now % Scheduler::ixu_partitions;

    // For memory schedulers, always run M0 first. The LSU mostly doesn't care about issue order because
    // it handles everything in age order, but when two loads with the same PC issue at the same time,
    // this ensures the PCP sees (and trains on) the load from partition 0 first.
    for (unsigned partition = 0; partition < Scheduler::ixu_partitions; ++partition)
        for (unsigned s = 0; s < MEM_SCH_SIZE; ++s)
            run_scheduler(Scheduler(static_cast<SchedulerType>(MEM_SCH_START + s), partition));

    for (unsigned partition = 0; partition < Scheduler::ixu_partitions; ++partition) {
        for (unsigned s = 0; s < IXU_INT_SCH_SIZE; ++s) {
            auto rotating_partition = (partition + ixu_partition_offset + 1) % Scheduler::ixu_partitions;
            if (partition >= total_ixu_pipe_schedule_f2i || s != Sch::A)
                run_scheduler(Scheduler(static_cast<SchedulerType>(IXU_SCH_START + s), rotating_partition));
        }
    }

    unsigned fsu_scheduler_offset = now % FSU_SCH_SIZE;
    for (unsigned partition = 0; partition < Scheduler::fsu_partitions; ++partition) {
        for (unsigned i = 0, s = fsu_scheduler_offset; i < FSU_SCH_SIZE; ++i, s = (++s == FSU_SCH_SIZE) ? 0 : s) {
            auto pipe = static_cast<PipeType>(FSU_PIPE_TYPE_START + s);
            if (!fsu_sched[partition].block_pipe_issue_this_cycle_for_div_wb || div_wb_pipe.val() != pipe) {
                auto sch = static_cast<SchedulerType>(FSU_SCH_START + s);
                run_scheduler(Scheduler(sch, partition));
            }
        }
    }

    advance_i2f_shelf();

    std::vector<bool> no_issue_xu_busy(schedulers.size(), false);
    std::vector<bool> no_issue_ilock(schedulers.size(), false);
    unsigned sch_idx = 0;
    for (auto& scheduler : schedulers) {
        no_issue_xu_busy[sch_idx] = scheduler.no_issue_xu_busy;
        no_issue_ilock[sch_idx] = scheduler.no_issue_ilock;
        scheduler.no_issue_xu_busy = false;
        scheduler.no_issue_ilock = false;
        ++sch_idx;
    }

    pmu_stall_xu_busy->put(no_issue_xu_busy);
    pmu_stall_ilock->put(no_issue_ilock);

    clean_zpipe_reservation();
    // after issuing this cycle...
    load_cancel_reap(); // collect stuff that was due to cancel this cycle
    clean_load_cancel_reservations();
}

/*************************************************************************
 * Allocate uops from input queue. Uops will not be able to issue for at least 2
 * cycles after this in most cases, which is modeled with issue_delay. FSU ops
 * may have a longer delay.
 *************************************************************************/
void IXU::dispatch()
{
    while (dispatch_port->can_take()) {
        auto uop = dispatch_port->take();
        divide_properties_init(uop);
        compute_base_latency(uop);
        set_resource_rate(uop);

        table_allocate(uop);

        ev_uop_dispatch(uop,
                        ev_data_alloc_id(uop->get_alloc_id()),
                        ev_data_ixu_partition(uop->sch.partition),
                        ev_data_ixu_sch(uop->sch.scheduler()),
                        ev_data_ixu_pipe(uop->sch.pipe()));
    }
}

/*************************************************************************
 * Scheduling ops out of the specified scheduler (subpartition)
 * pipestage XU02
 *************************************************************************/
void IXU::run_scheduler(Scheduler sch)
{
    DBGISS("run_scheduler {}", sch.name());
    auto sched = sch.scheduler();
    auto& scheduler = schedulers[sched];

    if (scheduler.need_to_sort) {
        // quicksort by age, executing, and in_use bits.
        // entries at the start of the list will be the oldest
        std::sort(scheduler.entries.begin(), scheduler.entries.end(), sch_entry_sort);
        scheduler.need_to_sort = false;
    }

    scheduler.no_issue_xu_busy = true;
    scheduler.no_issue_ilock = true;
    bool issued_or_xu_busy = false;

    int older_uops = 0;
    for (auto* e : scheduler.entries) {
        // list is already sorted, so the first entry which is already issued or invalid indicates we can stop looking
        // Note that we consider issuing uops even if not all their sources are ready, because SUS uops can issue
        // when only the address source is ready.
        if (e->can_dealloc || !e->in_use) {
            break;
        }

        // try_issue() will return true if it issued the uop (in which case there might be another
        // ready uop behind it), or the uop was ready but could not issue because the XU was busy.
        // In either case, we need to run this scheduler again next cycle.

        bool no_lub_stall = true;
        if (e->uop->is_load() && lub_size > 0) {
            int lub_space = scheduler.lub->get_count();
            if (older_uops != 0)
                lub_space -= reserved_lub_credits;
            if (lub_space <= 0) {
                no_lub_stall = false;
                ev_ixu_lub_stall(e->uop);
            }
        }
        issued_or_xu_busy = no_lub_stall && try_issue(e);
        older_uops++;

        if (issued_or_xu_busy)
            AMPERE_ASSERT(sch.partition == sch.normalized_partition(), "ISSUED uop to illegal partition");

        DBGISS("issued_or_xu_busy: {}, sch {}", issued_or_xu_busy, sched);
        // Get information for PMUs
        //   - Did we not issue the uop?
        //        - Was it because of XU busy? - OR-
        //        - Was it because inputs are not ready?
        if (issued_or_xu_busy) {
            // At least one entry was either issued, or not issued due to XU busy - ie, sources were ready
            scheduler.no_issue_ilock = false;
            if (e->can_dealloc) {
                // At least one entry was issued - ie, XU was not busy
                scheduler.no_issue_xu_busy = false;

                const auto& uop = e->uop;
                if (uop->is_load() && lub_size > 0)
                    uop->lubid = scheduler.lub->acquire();
            }
        }

        // The B schedulers have a strange asymmetry in the way they managed the shared BS pipe. If a slow
        // uop will write back next cycle, then scheduling of all 1-cycle uops is blocked (to avoid a writeback
        // conflict), but we can still schedule a younger slow uop. For this loop, that means that if a 1-cycle
        // uop was ready but did not issue, we can keep looking for something younger to issue.
        //
        // But if we pick a slow uop and it can't issue because the BS pipe was taken by the other partition,
        // then the uop is canceled and we just lose the issue slot. This check will stop looking for
        // younger uops to issue in this case.
        auto writeback_conflicts_and_shared_pipe = !slow_int_writeback_port && shared_slow_int_pipe;
        if (writeback_conflicts_and_shared_pipe && issued_or_xu_busy && e->uop->is_slow_integer())
            break;

        // If nothing was issued by the scheduler because all entries had non-ready sources, don't count XU Busy
        // This forces BACKEND_BUSY and BACKEND_ILOCK pmus to count mutexly, with BACKEND_ILOCK taking
        // counting-priority. (This seems more useful vs prioritizing BACKEND_BUSY, because a cycle where we are
        // BACKEND_ILOCK stalled, but not BACKEND_BUSY stalled indicates a dependancy stall, which may be unlocked with
        // more scheduler enteries.)
        if (scheduler.no_issue_ilock)
            scheduler.no_issue_xu_busy = false;
    }
}

void IXU::issue_i2f_to_fsu_pipe(const Uop& uop)
{
    // issue i2f uop into Y-pipe
    DBG_DPI2F("issue_i2f on y-pipe, allocid: {}", uop->get_alloc_id());

    // Set the pipe so that we know not to issue it on pipeY again when
    // it finishes executing.
    uop->set_pipe_type(i2f_pipe.val());

    ev_block_fsu_pipe_for_i2f(uop);
    issue_uop(uop);
}

void IXU::issue_z(const Uop& uop)
{
    if (!uop) {
        xu[expand_pipe(Pipe::Z)].occupy();
        return;
    }

    uop->set_pipe_type(Pipe::Z);
    issue_uop(uop);
}

void IXU::run_schedule_zpipe()
{
    // integer early exit and X-to-Z uops get routed to Z pipe after
    // executing on the X pipe.
    const auto now = get_cycle();
    for (auto& fsu : fsu_sched) {
        auto iter = fsu.zpipe_reservation.find(now);
        if (iter != fsu.zpipe_reservation.end()) {
            issue_z(iter->second);
        }
    }
}

void IXU::clean_zpipe_reservation()
{
    const auto now = get_cycle();
    for (auto& fsu : fsu_sched)
        fsu.zpipe_reservation.erase(now);
}

bool IXU::try_issue_super_sus_pair(SchedEntry* e, const ReadyInfo& ready_info) // returns true if first issue ready
{
    bool address_ready = e->src_ready[0];
    // We can't do anything until the address is ready
    if (!address_ready)
        return false;

    const auto& uop = e->uop;
    bool data1_ready = e->src_ready[1];

    // Super SUS Pairs can issue 3 times (A,D1,D2) or 2 times (A+D1,D2).

    // This function handles the not-last issue of a super-SUS pair:
    // issuing either:
    //  (1) just the address,
    //  (2) the address and data1 together, or
    //  (3) the first data if the address already issued.
    // It returns true if the uop issued or is ready to issue
    // for the not-last time, and false if it is not ready or has
    // already issued address and data1.
    DBGISS("try_issue_super_sus_pair: txid: {}, address_ready: {}, data1_ready: {}",
           uop->get_txid(),
           address_ready,
           data1_ready);

    // The uop has already issued address and data1, and only needs its final issue of data2
    if (uop->sus.sta_issued && uop->sus.sus_pair_data1_issued)
        return false;

    // We have sent the address but not data1, so check whether that is ready
    if (uop->sus.sta_issued && !uop->sus.sus_pair_data1_issued && !data1_ready)
        return false;

    if (!can_issue_uop(uop))
        return true;

    const unsigned src_used_bv = uop->sus.sta_issued ? SUS_OP1 : ((data1_ready ? SUS_OP1 : 0) | SUS_OP0);
    const bool will_cancel = check_if_should_cancel(e, ready_info, src_used_bv);

    if (!uop->sus.sta_issued) {
        // std portion might cancel but sta portion might issue uncanceled
        const auto [sta_cancel, std_cancel] = sus_partial_cancel(uop, ready_info, src_used_bv);
        if (!sta_cancel) {
            // Address is ready and has not been issued, so send it with or without data1
            uop->sta_issuing = true;

            if (!std_cancel)
                uop->sus.sus_pair_data1_issued = data1_ready;

            uop->sus.sta_issued = true;
            uop->sus.split = true;
        }
        uop->sus.to_issue_vec &= ~SUS_ISSUE_A; // turn off address bit
        if (data1_ready) {
            uop->sus.to_issue_vec &= ~SUS_ISSUE_D1; // turn off d1 bit
        }
        DBGISS("\t issuing super sus pair: txid: {}", uop->get_txid());
        ev_sus_sta(uop);
    } else {
        if (!will_cancel)
            uop->sus.sus_pair_data1_issued = true;

        DBGISS("\t issuing super sus pair data1: txid: {}", uop->get_txid());
        uop->sus.to_issue_vec &= ~SUS_ISSUE_D1; // turn off data1 bit
        ev_sus_pair_std1(uop);
    }

    issue_entry(e);
    return true;
}

bool IXU::try_issue_sus(SchedEntry* e, const ReadyInfo& ready_info) // returns true if first issue ready
{
    const auto& uop = e->uop;
    auto address_sources = uop->store_num_address_sources();
    auto data_sources = uop->store_num_data_sources();

    // This function handles the first issue (address only) of a non-pair SUS. It returns true if the uop
    // issued or is ready to issue its address only, false if it is not ready or has already issued its address.

    AMPERE_ASSERT(data_sources <= 1, "Non-pair SUS has two data sources");
    AMPERE_ASSERT(address_sources != 0, "Single-uop store has no address sources");

    bool address_ready = (address_sources == 1) ? e->src_ready[0] : (e->src_ready[0] && e->src_ready[1]);
    bool data_ready = (data_sources == 0) || ((address_sources == 1) ? e->src_ready[1] : e->src_ready[2]);

    DBGISS("try_issue_sus: txid: {}, address_ready: {}, data_ready: {}", uop->get_txid(), address_ready, data_ready);

    // If the address is ready and the data is not, or if this is a super-SUS which must always issue twice, then
    // issue the first half. When the data source is ready, it will issue a second time on the normal path below.
    if (address_ready && (!data_ready || (uop->is_super_sus() && super_sus_always_double_fire))) {
        if (!can_issue_uop(uop)) {
            return true;
        }
        if (!check_if_should_cancel(e, ready_info, 1)) {
            uop->sus.sta_issued = true;
            uop->sus.split = (data_sources > 0); // can only split if have real data sources
        }
        uop->sus.to_issue_vec &= ~SUS_ISSUE_A; // turn off address bit
        ev_sus_sta(uop);
        issue_entry(e);
        return true;
    }

    // The uop has already issued its address, and only needs the final issue of its data
    return false;
}

std::pair<bool, bool> IXU::sus_partial_cancel(const Uop& uop,
                                              const ReadyInfo& ready_info,
                                              unsigned src_used_for_issue_bv)
{
    // if we issue a combined address+data the data portion might be canceled.
    bool retval_sta_cancel = false;
    bool retval_std_cancel = false;
    const unsigned address_sources = uop->store_num_address_sources();

    for (size_t src_idx = 0; src_idx < UOP_MAX_SRCS; ++src_idx) {
        if ((src_used_for_issue_bv & (1 << src_idx)) != 0 && ready_info.cancel_src_pregs[src_idx]) {
            if (src_idx < address_sources) {
                retval_sta_cancel = true;
            } else {
                retval_std_cancel = true;
            }
        }
    }
    return std::make_pair(retval_sta_cancel, retval_std_cancel);
}

bool IXU::check_if_should_cancel(SchedEntry* sched_entry,
                                 const ReadyInfo& ready_info,
                                 unsigned arg_src_used_for_issue_bv)
{
    // returns true if the uop will cancel, false otherwise.
    const auto& uop = sched_entry->uop;
    // src_used_for_issue_bv is a bit vector indexed by src operand index. All bits are 1 for non-sus stores.
    // It is used to allow us to issue SUS uops with accurate cancel behavior. We don't cancel an address-only
    // issue if the data portion were canceled, for example.
    const auto src_used_for_issue_bv = uop->is_single_uop_store() ? arg_src_used_for_issue_bv : ~0U;
    bool retval_will_cancel = false;
    if (ready_info.cancel_time) {
        // set up arrange a cancel for each canceled source. This is mostly for stores
        for (size_t src_idx = 0; src_idx < UOP_MAX_SRCS; ++src_idx) {
            if ((src_used_for_issue_bv & (1 << src_idx)) != 0 && ready_info.cancel_src_pregs[src_idx]) {
                DBGISS("pre_issue_uop UOPcancel Setting txid: {} src_idx: {}, cancel@: {}, preg: {}",
                       uop->get_txid(),
                       src_idx,
                       ready_info.cancel_src_time[src_idx],
                       ready_info.cancel_src_pregs[src_idx]);
                AMPERE_ASSERT(ready_info.cancel_src_time[src_idx] != 0, "cancel src time should not be zero");
                uop->cancel(ready_info.cancel_src_time[src_idx], ready_info.cancel_src_pregs[src_idx], src_idx);
                retval_will_cancel = true;
            }
        }
    }
    DBGISS("pre_issue_uop SLMSRC Setting load_made_src_ready_cycle to {} for sch: {}, scheduler: {}, grp: {}, idx: {}",
           ready_info.load_time,
           uop->sch.name(),
           uop->sch.scheduler(),
           sched_entry->grp,
           sched_entry->idx);
    sched_entry->load_made_src_ready_cycle = ready_info.load_time; // zero or max time load returned
    return retval_will_cancel;
}

bool IXU::try_issue(SchedEntry* e)
{
    // Returns false if (1) issued canceled uop previously, or (2) uop was not ready.
    //
    // Returns true otherwise. That is we issued something
    // or tried to issue something but could not.
    const auto& uop = e->uop;
    bool xydebug = false; // runtime message restriction to X & Y pipes
#if defined(DEBUG_IXU)
    xydebug = uop->sch.is_fsu(); // pipes X, Y or Z.
#endif

    if (xydebug) {
        DBGIX("try issue {:x} {:s} {}", uop->pc().value(), uop->get_uopcode(), uop->get_txid());
    }
    DBGISS("try issue txid: {}", uop->get_txid());

    AMPERE_ASSERT(!e->can_dealloc, "Tried to issue the same uop twice");

    // Mostly do not issue a canceled uop twice. SUS stores have special handling
    if (uop->is_single_uop_store()) {
        // For max-2-issue SUS: if split, we want to allow issue attempts for the data operand.
        // For max-3-issue SUS: We want to allow issue attempts for the remaining 1 or 2 data operands (A+D1, D2) or (A,
        // D1, D2). Once the SUS has issued all its parts, we do not want to allow it to issue canceled any more.
        if (uop->sus_issue_done()) {
            DBGISS("try issue bailing... SUS fully issued already: txid: {}", uop->get_txid());
            return false;
        }
    } else if (e->issued_canceled) {
        DBGISS("try issue bailing... issued canceled already: txid: {}", uop->get_txid());
        return false;
    }

    // This avoids a bookkeeping problem where we reap in the same cycle as we issue.
    // Reaping will clear the canceled aspect of the issue.
    // This would not happen in a more accurate model.
    if (will_reap_this_cycle(uop)) {
        DBGISS("try issue bailing... uop reaping later this cycle: txid: {}", uop->get_txid());
        return false;
    }

    // By default, uops can't issue the first cycle after they allocate into the scheduler.
    // If we support a DR05/FS01 bypass, then they can issue a cycle earlier.
    // FSU uops have some extra delay due to the complexities of figuring out whether and
    // when sources are ready.
    int64_t issue_delay{1};
    if (uop->sch.is_fsu()) {
        issue_delay += fsu_dispatch_latency;
        if (sched_bypass_fsu) {
            issue_delay--;
        }
    } else if (sched_bypass_ixu) {
        issue_delay--;
    }

    // Check whether it's too soon to issue this uop
    if (e->alloc_time + issue_delay >= now) {
        return true;
    }

    // sets src_ready[] for each src; used by sus issuing variations
    const auto ready_info = issue_info(e);

    // Single-uop stores come in many flavors:
    //     Normal SUS: one address source, one data source (could be the first part of a store-pair)
    //     Could be the first part of a store-pair, or a store-pair where the second data is zero (stpz uop)
    //         If both sources are ready, issue a single "fat" SUS below.
    //         If only address is ready, issue address only and then data later
    //     Store-zero (STZ): two address sources, no data source (data is implicit zero)
    //         As soon as both address sources are ready, issue a "fat" SUS below (data is always ready)
    //     Super-SUS, not pair: two address sources, one data source
    //         By default, 3-source SUS always issues twice, even if all sources are ready at the same time,
    //         because we can't read 3 sources from the PRF all at once.
    //     Super-SUS, pair: one address source, two data sources
    //         This can issue twice (addr+data1, then data2) or three times (addr, data1, data2)
    // In no case do we issue data first.

    // Check whether a SUS wants to issue without all its sources ready (address only, or address+data1 for
    // super-SUS pair). The final time a SUS issues (which may also be its first) is handled below.
    if (uop->is_super_sus_pair()) {
        // Super-single-uop store-pair is special because it can issue up to 3 times. Combining that with the
        // other SUS code below made the code a mess, so we'll just handle super-SUS-pair separately here.
        // This will issue the address only or address+data1 if data1 is also ready
        if (try_issue_super_sus_pair(e, ready_info))
            return true;
    } else if (uop->is_single_uop_store() && !uop->sus.sta_issued) {
        // Now we handle the address-only issue of all other SUS types, including super-SUS which is not a pair.
        if (try_issue_sus(e, ready_info))
            return true;
    }

    // any sus uops that land here are either: (1) non-super fat (a+d), or (2) the last data issues of split sus or
    // super sus pairs

    if (!ready_info.ready) {
        if (xydebug) {
            DBGIX("not ready  {:x} {:s} {}", uop->pc().value(), uop->get_uopcode(), uop->get_txid());
        }

        DBGISS("\tcannot issue uop txid: {}", uop->get_txid());
        return false;
    }

    // normal scheduler routine (not yet issued, all ready)
    // check first before writing for schedule
    if (!can_issue_uop(uop)) {
        if (xydebug) {
            DBGIX("\tcannot issue  {:x} {:s} {}", uop->pc().value(), uop->get_uopcode(), uop->get_txid());
        }
        return true;
    }

    if (xydebug) {
        DBGIX("can issue  {:x} {:s} {}", uop->pc().value(), uop->get_uopcode(), uop->get_txid());
    }

    // Figure out which sources are used for SUS so that we can cancel issues appropriately.
    // For super sus pair, only the final data operand is possible here.
    // For other sus, we can be fat a+d or just be data
    unsigned src_used_bv = SUS_ALL;
    if (uop->is_single_uop_store()) {
        if (uop->is_super_sus_pair()) {
            src_used_bv = SUS_OP2; // data2; addr and data1 issues are handled by try_issue_super_sus_pair()
        } else {
            const auto address_sources = uop->store_num_address_sources();
            if (uop->sus.sta_issued) {
                src_used_bv = (address_sources == 1) ? SUS_OP1 : SUS_OP2;             // data on src1 or src2
            } else {                                                                  // sta not issued
                src_used_bv = (address_sources == 1) ? (SUS_OP0 | SUS_OP1) : SUS_ALL; // fat a+d or aa+d
            }
        }
        DBGISS("issuing: txid: {}, sus src_used_used_bv: op0: {}, op1: {}, op2: {}",
               uop->get_txid(),
               (src_used_bv & SUS_OP0),
               (src_used_bv & SUS_OP1),
               (src_used_bv & SUS_OP2));
    }

    (void)check_if_should_cancel(e, ready_info, src_used_bv);

    if (uop->is_single_uop_store()) {
        const auto [sta_cancel, std_cancel] = sus_partial_cancel(uop, ready_info, src_used_bv);
        DBGISS("issuing: txid: {} sta_cancel: {}, std_cancel: {}", uop->get_txid(), sta_cancel, std_cancel);

        if (uop->is_super_sus_pair()) { // only data2 reaches here
            uop->sus.split = true;
            if (!std_cancel) {
                uop->sus.std_issued = true;
            }
        } else if (!sta_cancel) {
            // if sta_cancel, they both cancel
            // std portion might cancel but sta portion might issue uncanceled
            if (!uop->sus.sta_issued) {
                // If SUS has both sides ready, then it can issue as a fat-SUS.
                uop->sus.sta_issued = true;
                uop->sta_issuing = true;
                ev_sus_fat(uop);
            }
            if (std_cancel) {
                // if the data portion canceled, we cheat and do not send the data to the LSU now.
                uop->sus.split = true;
            } else {
                uop->sus.std_issued = true;
                if (uop->sta_issuing) {
                    // may have been split from previous issue attempts
                    uop->sus.split = false;
                }
            }
        }

        // update what issued for SUS uops
        if (uop->is_super_sus_pair()) {
            AMPERE_ASSERT((uop->sus.to_issue_vec & SUS_ISSUE_D1) == 0, "Only final data2 issues expected here");
            uop->sus.to_issue_vec &= ~SUS_ISSUE_D2; // turn off data2
        } else {
            uop->sus.to_issue_vec &= ~SUS_ISSUE_ALL; // turn of address and data (address may already be off if split)
        }
    }

    issue_entry(e);

    if (fsu_zpipe) {
        // reserve zpipe for X2Z uops (including idiv and early exit
        // idiv) for when they are expecting to finish X pipe.
        auto& fsu = fsu_sched[uop->sch.partition_normalized_to(Scheduler::fsu_partitions)];
        if (uop->is_x_to_z()) {
            // if can issue, issue to X, reserve Z pipe later =  now + eediv_latency
            auto start_z = now + uop->latency();
            AMPERE_ASSERT(fsu.zpipe_reservation.find(start_z) == fsu.zpipe_reservation.end(),
                          "Z pipe should be free now (1)");
            DBG_X2Z("{:9} {:15} eeidiv z-reserve for future: {}", uop->get_txid(), uop->get_uopcode(), start_z);

            // If the uop will cancel, put a null pointer into the reservation. This will
            // consume the reserved issue slot but not write back.
            fsu.zpipe_reservation[start_z] = uop->will_cancel() ? nullptr : uop;
        } else if (uop->sch.pipe_type == Pipe::Z) { // "direct to Z" uops
            AMPERE_ASSERT(fsu.zpipe_reservation.find(now) == fsu.zpipe_reservation.end(),
                          "Z pipe should be free now (2)");
            DBG_X2Z("{:9} {:15} z-reserve for now", uop->get_txid(), uop->get_uopcode());
            fsu.zpipe_reservation[now] = uop; // reserve Z pipe for now
        }
    }

    if (uop->is_double_pump()) {
        if (e->issued_once) { // 2nd issue of double pumped uop
            DBG_DPI2F("2nd issue dbl pump. Pipe {}", pipe_name(uop->sch.pipe()));
            ev_double_pump_second_issue(uop, ev_data_ixu_pipe(uop->sch.pipe()));
            if (uop->sch.pipe_type == i2f_pipe.val()) {
                fsu_sched[uop->sch.partition_normalized_to(Scheduler::fsu_partitions)].pipe_mid_double_pump_uop = true;
            }
        } else { // first issue of double pumped uop
            DBG_DPI2F("1st issue dbl pump. Pipe {}", pipe_name(uop->sch.pipe()));
            ev_double_pump_first_issue(uop, ev_data_ixu_pipe(uop->sch.pipe()));
            e->issued_once = true;
            return true;
        }
    }

    if (!uop->will_cancel())
        sched_can_dealloc(e, uop->sch.scheduler());

    return true;
}

void IXU::flush_canceled_uops(std::uint64_t flush_id)
{
    for (auto iter = load_cancel_reservation.begin(); iter != load_cancel_reservation.end();) {
        if (iter->second.uop->is_younger(flush_id)) {
            DBGISS("FLUSHING cancel reservation for txid: {}", iter->second.uop->get_txid());
            iter = load_cancel_reservation.erase(iter);
        } else {
            ++iter;
        }
    }
}

ReadyInfoOne IXU::src_ready_with_bypassing(const Uop& consumer_uop, unsigned src_idx) const
{
    // returns ReadyInfo struct
    // Check one src of this scheduler entry to see if it is ready to use.
    const bool fsu_consumer = consumer_uop->unit() == archx::Uopunit::FpSIMD;
    const auto phys_reg_src = consumer_uop->reg_psrc[src_idx];
    DBGISS("  src_idx: {} lreg: {} preg: {}", src_idx, consumer_uop->reg_src[src_idx], phys_reg_src);

    ReadyInfoOne retval; // defaults to true
    if (phys_reg_src) {
        const IssueRec& issue_rec = issue_tracker[phys_reg_src];
        retval.producer_txid = issue_rec.producer_txid;
        if (issue_rec.is_ready()) { // idle, nothing pending
            // we are ready
            DBGISS("\tchecking preg: {}: ready", phys_reg_src);
            if (issue_rec.ctype == CompType::LOAD) {
                retval.producer_was_load = true;
                retval.load_time = issue_rec.load_ready_time;
            }
        } else if (issue_rec.is_waiting_to_issue()) {
            DBGISS("\tchecking preg: {}: waiting to issue", phys_reg_src);
            retval.ready = false;
        } else if (issue_rec.canceled && issue_rec.cancel_time == now) {
            // we do not want to issue great-grandchildren of canceled load
            retval.ready = false;
        } else { // preg is in progress (issued). check bypassing delays
            // propagate cancellation info to consumer
            DBGISS("\tchecking preg: {} was_canceled = {}, cancel_time: {}",
                   phys_reg_src,
                   issue_rec.canceled,
                   issue_rec.cancel_time);
            retval.src_was_canceled = issue_rec.canceled;
            retval.cancel_time = issue_rec.cancel_time;
            if (issue_rec.canceled) {
                AMPERE_ASSERT(issue_rec.cancel_time != 0, "issue rec cancel time must not be zero");
            }

            switch (issue_rec.ctype) {
            case CompType::LOAD:
                // if we got a real (primary) cancellation for a load dest, the consumer is ready (but will cancel)
                if (now <= issue_rec.cancel_time) { // are we before (or at) the load cancel cycle? is fake issue
                    retval.ready = issue_rec.is_primary_cancel() && issue_rec.canceled;
                    if (retval.ready) {
                        // this load canceled recently but we are have not reaped it yet.... make up a time.
                        // this would prevent deallocation but we won't because it is canceled. basically irrelevant.
                        retval.load_time = now;
                    }
                } else {
                    retval.ready = false; // gotta wait until next wake
                }

                retval.producer_was_load = true;

                DBGISS("\tchecking preg: {}: load-based ready={}, canceled: {}",
                       phys_reg_src,
                       retval.ready,
                       issue_rec.canceled);
                break;

            case CompType::FSU_DIV_SQRT: {
                // div/sqrt producers do not use the bypassing matrix
                const auto done_at = (issue_rec.done_at() + // nonzero for DIV/SQRT
                                      div_bypassing_delay(fsu_consumer));
                if (done_at > now) {
                    retval.ready = false; // not ready yet
                }
            } break;
            case CompType::FSU_FLAGS: {
                const auto done_at = issue_rec.done_at(fsu_flags_latency);
                if (done_at > now) {
                    retval.ready = false; // not ready yet
                }
            } break;
            case CompType::FSU_PREDICATE: { // >= Connor
                const auto done_at = issue_rec.done_at(fsu_predicate_latency);
                if (done_at > now) {
                    retval.ready = false; // not ready yet
                }
            } break;
            case CompType::FSU_F2I:
                // f2i delay handling marks the ixu reg ready. If we get
                // here that f2i flow has not occurred yet, so we are not ready.
                retval.ready = false;
                break;
            case CompType::FSU: {
                const auto done_at = (issue_rec.done_at()
                                      + fsu_bypass->bypass_latency(issue_rec.fsu_producer,
                                                                   fsu_consumer_src::from_uop(consumer_uop, src_idx)));
                if (done_at > now) {
                    retval.ready = false; // not ready yet
                }
            } break;
            case CompType::IXU_I2F:
                // I2F uops rely upon the data being marked ready at
                // end of FSU Y pipe execution before waking
                // FSU consumers.
                retval.ready = false;
                break;
            case CompType::IXU: {
                unsigned extra_delay = 0;
                if (src_idx == 2 && consumer_uop->is_super_sus() && super_sus_src3_delay
                    && issue_rec.producer_is_single_cyc)
                {
                    extra_delay = 1;
                }
                const auto done_at = issue_rec.done_at() + extra_delay;
                if (done_at > now) {
                    retval.ready = false; // not ready yet
                }
            } break;
            } // switch
        }
    }
    DBGISS(
        "\tchecking preg: {} returning: ready: {}, producer_was_load: {}, src_was_canceled: {}, cancel_time: {}, "
        "src_idx: {}",
        phys_reg_src,
        retval.ready,
        retval.producer_was_load,
        retval.src_was_canceled,
        retval.cancel_time,
        src_idx);
    return retval;
}

bool IXU::is_ready_check_only(const SchedEntry* sched_entry) const
{
    if (sched_entry->all_ready) {
        return true;
    }

    const auto& consumer_uop = sched_entry->uop;
    for (unsigned src_idx = 0; src_idx < UOP_MAX_SRCS; ++src_idx) {
        if (consumer_uop->reg_src[src_idx] == LReg::INVALID) {
            continue;
        }
        const auto ready_info = src_ready_with_bypassing(consumer_uop, src_idx);
        if (!ready_info.ready) {
            return false;
        }
    }
    return true;
}

ReadyInfo IXU::issue_info(SchedEntry* sched_entry)
{
    // returns: ready, if some src was a load, if there exists a canceled source, and earliest cancel time.
    ReadyInfo retval; // return value
    const auto& consumer_uop = sched_entry->uop;

    // why is this code here? stale ready indicators. cancel reap does not
    // reach out to all consumer scheduler entries an clear them.
    sched_entry->src_ready.fill(false);
    sched_entry->all_ready = false;

    for (unsigned src_idx = 0; src_idx < UOP_MAX_SRCS; ++src_idx) {
        if (consumer_uop->reg_src[src_idx] == LReg::INVALID) {
            continue;
        }
        const auto ready_info = src_ready_with_bypassing(consumer_uop, src_idx); // per source
        if (ready_info.ready) {
            if (!sched_entry->src_ready[src_idx]) {
                sched_entry->src_ready[src_idx] = true;
                if (sched_entry->last_producer_cycle < now) { // track last ready producer
                    sched_entry->last_producer_txid = ready_info.producer_txid;
                    sched_entry->last_producer_cycle = now;
                }
            }
            if (ready_info.producer_was_load) {
                retval.producer_was_load = true;
                retval.load_time = std::max(retval.load_time, ready_info.load_time); // latest load
                DBGISS("    issue info canceled load time = {}", retval.load_time);
            }
            if (ready_info.src_was_canceled) {
                AMPERE_ASSERT(ready_info.cancel_time != 0, "ready_info.cancel_time must be set nonzero");
                DBGISS("    issue info canceled src: txid: {}, src_idx: {}, lreg: {}, preg: {}, cancel_time: {}",
                       consumer_uop->get_txid(),
                       src_idx,
                       consumer_uop->reg_src[src_idx],
                       consumer_uop->reg_psrc[src_idx],
                       ready_info.cancel_time);
                retval.cancel_src_pregs[src_idx] = consumer_uop->reg_psrc[src_idx];
                retval.cancel_src_time[src_idx] = ready_info.cancel_time;
                if (retval.cancel_time == 0) {
                    // first canceled source
                    retval.cancel_time = ready_info.cancel_time;
                } else {
                    // secondary canceled source
                    retval.cancel_time = std::min(retval.cancel_time, ready_info.cancel_time);
                }
            } else {
                retval.cancel_src_pregs[src_idx] = 0;
                retval.cancel_src_time[src_idx] = 0;
            }
        } else {
            retval.ready = false;
            break;
        }
    }
    if (retval.ready) {
        sched_entry->all_ready = true;
        if (!sched_entry->all_ready_historical) {
            ev_wb_bypass(consumer_uop, ev_data_waking_tx(sched_entry->last_producer_txid));
            sched_entry->all_ready_historical = true;
        }
    } else {
        sched_entry->all_ready_historical = false;
    }
    return retval;
}

static CompType get_comp_type(const ConstUop& producer_uop, archx::LReg dst)
{
    if (producer_uop->is_load() || producer_uop->is_store_with_dest()) {
        return CompType::LOAD;
    }
    if (producer_uop->is_fsu()) {
        if (producer_uop->fsu_unit_producing() == archx::FsuUnit::DV) {
            // this is asimd fdiv, asimd fsqrt, sve div, sve sqrt
            // and also gpr div on trinity/connor.
            return CompType::FSU_DIV_SQRT;
        }
        if (archx::is_predicate(dst)) {
            return CompType::FSU_PREDICATE;
        }
        if (archx::is_flag(dst)) {
            return CompType::FSU_FLAGS;
        }
        if (archx::is_integer(dst)) {
            return CompType::FSU_F2I;
        }
        return CompType::FSU;
    }
    if (producer_uop->is_i2f()) {
        return CompType::IXU_I2F;
    }
    return CompType::IXU;
}

void IXU::record_dest_issue(const ConstUop& producer_uop, archx::LReg dst, std::uint64_t preg)
{
    if (producer_uop->received_load_data) {
        DBGISS("txid: {} Received load data already so not marking dest issued again", producer_uop->get_txid());
        return; // cas* that split receive their load data earlier; avoid setting load dest pending again
    }
    if (producer_uop->is_x_to_z() && producer_uop->sch.pipe_type == Pipe::Z) {
        return; // X-to-Z has already issued and woken up dests (on X)
    }
    const auto ctype = get_comp_type(producer_uop, dst);
    const bool fsu_dst = archx::is_vector(dst);
    unsigned latency = 0;
    unsigned issue_delay = 0; // used for double pump
    switch (ctype) {
    case CompType::IXU_I2F:
        latency = (producer_uop->sch.pipe_type == *i2f_pipe)
                      ? producer_uop->fsu_i2f_latency // 2nd execution on FSU (after i2f / i2f_shelf processing)
                      : producer_uop->latency();      // 1st execution on IXU
        break;
    case CompType::IXU:
        latency = producer_uop->latency();
        break;
    case CompType::FSU_DIV_SQRT: // not a producer in the FSU bypass matrix
        latency = producer_uop->latency();
        break;
    case CompType::LOAD: // uses ready & cancel info. FSU responses are delayed by fsu_load_delay.
        break;
    case CompType::FSU_FLAGS: // flags producer -- fixed latency relative to FSU issue
        // <= Ripley:  wake FS07, data FS09
        // >= Trinity: wake FS05, data FS07
        // FSU issue is FS02
        latency = fsu_flags_latency;
        break;
    case CompType::FSU_PREDICATE: // predicate producer - fixed latency relative to FSU issue
        // >= Connor: FS07 predicate readable by consumer
        // FSU issue is FS02
        latency = fsu_predicate_latency;
        break;
    case CompType::FSU_F2I:
        // latency comes from f2i handling
        break;
    case CompType::FSU: // zero additional latency. bypass matrix handles everything
        if (producer_uop->is_double_pump()) {
            if (producer_uop->first_issue_of_double_pumped()) {
                issue_delay = 1;
            } else {
                return; // avoid double recording issue of double pump uop
            }
        }
        break;
    default:
        break;
    }

    const bool producer_is_single_cyc = producer_uop->is_ixu() && producer_uop->latency() == 1;
    AMPERE_ASSERT(latency < 1000, "Latency is very  large. Assuming this is an error.");
    DBGISS("Setting issue/execute preg: {}, issue_time: {}, latency: {}", preg, now + issue_delay, latency);
    issue_tracker[preg] = IssueRec(producer_uop->get_txid(),
                                   now + issue_delay,
                                   ctype,
                                   latency,
                                   fsu_producer_unit::from_uop(producer_uop),
                                   producer_is_single_cyc,
                                   fsu_dst);

    if (enable_bypass_stats_collection) {
        if (producer_uop->is_fsu()) {
            issue_tracker[preg].set_fsu_producer_info(producer_uop->get_uopcode(), producer_uop->fsu_unit());
        } else { // clear the information
            issue_tracker[preg].set_fsu_producer_info();
        }
    }
}

void IXU::record_issue(const ConstUop& producer_uop)
{
    // Record information about the outputs of this uop so we can
    // wake up dependent consumers later on.
    if (producer_uop->is_producer()) {
        record_dest_issue(producer_uop, producer_uop->reg_dst, producer_uop->get_prfid_id());
    }
    if (producer_uop->has_dest2()) {
        record_dest_issue(producer_uop, producer_uop->reg_dst2, producer_uop->prfid2->get_id());
    }
    // FLAGS and FLGT0 (temp flags)
    if (producer_uop->is_flag_producer()) {
        record_dest_issue(producer_uop, archx::LReg::FLAGS, producer_uop->flagsid->get_id());
    } else if (producer_uop->is_temp_flags_producer()) {
        record_dest_issue(producer_uop, archx::LReg::FLGT0, producer_uop->flagsid->get_id());
    }
}

void IXU::flush_zpipe(std::uint64_t flush_id)
{
    for (auto& fsu : fsu_sched) {
        for (auto iter = fsu.zpipe_reservation.begin(); iter != fsu.zpipe_reservation.end();) {
            if (iter->second && iter->second->is_younger(flush_id)) {
                iter = fsu.zpipe_reservation.erase(iter);
            } else {
                ++iter;
            }
        }
    }
}

// Method for handling mispredicts, etc
void IXU::flush_pipe()
{
    auto flush = flush_port->read();
    auto flush_alloc_id = flush->alloc_id;
    DBGISS("flushing alloc_id: {}", flush_alloc_id);

    flush_zpipe(flush_alloc_id);
    flush_canceled_uops(flush_alloc_id);

    for (auto& fsu : fsu_sched) {
        if (fsu.i2f && fsu.i2f->get_alloc_id() > flush_alloc_id) {
            DBG_DPI2F("flushing i2f");
            fsu.i2f = nullptr;
        }
        if (fsu.i2f_shelf && fsu.i2f_shelf->get_alloc_id() > flush_alloc_id) {
            DBG_DPI2F("flushing i2f_shelf");
            fsu.i2f_shelf = nullptr;
        }
    }

    // go through each scheduler table
    for (auto [sn, entries] : std::views::enumerate(sched_entries)) {
        for (size_t i = 0; i < entries; i++) {
            // do an age comparison of the alloc_id and invalidate the younger ones
            auto* e = table[sn][i];
            if (flush_alloc_id < e->alloc_id) {
                DBGISS("Flushing sched_entry for txid: {} alloc_id: {}, flush_id: {}",
                       e->uop->get_txid(),
                       e->uop->get_alloc_id(),
                       flush_alloc_id);
                e->uop->set_flushed();
                // The pdests are gone at this point so we cannot "reach" the IssueRec. Assuming anything reading those
                // pdests will get a new writer after the flush.
                DBGISS("CANDEALLOC FSH sch: {}, grp: {}, idx: {}", sn, e->grp, e->idx);
                sched_can_dealloc(e, sn); // Mark as ready to deallocate
                ev_ixu_flush_entry(e->uop);
            }
        }
    }

    // Gather producers use the max alloc_id for the group of 2 or 4.
    //
    // The gather-write-back alloc_id is >= all the individual gather
    // ids. That's what the consumers use to pick up the final
    // register write.  The individual gathers use their own alloc_ids
    // for their operand reads.
    //
    // There are intermediate alloc_ids for gmov* operands in the
    // gather seqeuence. Those might need to be flushed too if they
    // are younger (larger alloc_id) than the flush'ed uop. So it
    // would be wrong to take the gather write-alloc_id during flushes
    // of gathers. Example gather flow:
    //
    //   add
    //   gmovpair
    //   gather 1of4
    //   gather 2of4   <-- example this uop is flushed; we want to flush all younger (below/lower) uops
    //   gmovpair      <-- if we used the gather's max alloc_id we'd miss flushing this uop.
    //   gather 3of4
    //   gather 4of4   <-- max alloc_id used for waking up consumers
    //

    // for all the registers, check src_producer ages and invalidate
    // younger ones (larger-valued alloc_id)
    for (size_t preg = 0; preg < MAX_PRF_SIZE; ++preg) {
        if (src_producer[preg].alloc_id > flush_alloc_id) {
            src_producer[preg].resident = false;
        }
    }

    // clear out the input queue
    while (!scheduler_input_queue.empty()) {
        auto u = scheduler_input_queue.front();
        ev_ixu_flush_entry(u);
        DBG_SCH(u, "INPUTQ");
        release_credit(u->schid);
        scheduler_input_queue.pop();
    }

    // clear out the dispatch_port
    while (dispatch_port->can_take()) {
        auto u = dispatch_port->take();
        ev_ixu_flush_entry(u);
        if (u->schid) {
            DBG_SCH(u, "DISPTCH");
            release_credit(u->schid);
        }
    }
    dispatch_port->clear();

    // clear the XUs
    for (size_t sn = 0; sn < num_execution_units; sn++)
        xu[sn].flush(flush);
}

/*************************************************************************
 * Method which collects statistics on a per-cycle basis. This is useful
 * for measuring occupancy for power studies.
 *************************************************************************/
void IXU::stats_collection()
{
    stat_sched_ld_writeback += load_wakeup_this_cycle;

    // loop through all the schedulers
    for (auto [num, scheduler] : std::views::enumerate(schedulers)) {
        if (0 == scheduler.entries_in_use) {
            ++scheduler.stats.sched_occupancy[0];
            ++scheduler.stats.sched_issue_eligible[0];
            continue;
        }

        auto sch_group_size = scheduler_group_size(Scheduler::from_sched(num).sched_type.value());
        unsigned groups_occupied{0}, groups_eligible{0};

        // loop through groups, then entries in groups
        for (unsigned grp = 0; grp < scheduler.sched_groups; grp++) {
            bool entries_occupied{false}, entries_eligible{false};
            for (unsigned ent = 0; ent < sch_group_size; ent++) {
                auto* e = scheduler.groups[grp][ent];
                // first check occupancy
                if (e->in_use) {
                    entries_occupied = true;
                }

                // next check issue eligible
                if (e->in_use && !e->can_dealloc && is_ready_check_only(e)) {
                    entries_eligible = true;
                }
            }
            groups_occupied += entries_occupied;
            groups_eligible += entries_eligible;
        }

        // recorded once per cycle
        ++scheduler.stats.sched_occupancy[groups_occupied];
        ++scheduler.stats.sched_issue_eligible[groups_eligible];
    }

    // stats for analyzing throttling mechanisms,
    // recording when various schedulers are empty
    uint64_t partition_non_empty{0};
    for (auto sched : util::each_enum<SchedulerType>()) {
        auto partitions = scheduler_partitions(sched);
        for (unsigned i = 0; i < partitions; ++i) {
            bool empty = !schedulers[Scheduler(sched, i).scheduler()].entries_in_use;
            partition_non_empty |= (!empty) << i;
            if (empty) {
                ev_sch_empty_cycles(ev_sched(sched));
                break;
            }
        }
    }

    if (auto empty_partitions = ~partition_non_empty; empty_partitions)
        ev_partition_empty_cycles(ev_partition(std::countr_zero(empty_partitions)));
}

void IXU::dump_load_use_histo() const
{
    if (collect_load_use_stats) {
        json jlog;
        for (size_t i = 0; i < histogram_load_home_to_schedule_max_bin; i++) {
            jlog[i] = {i, histogram_load_home_to_schedule[i]};
        }
        fmt::print("load-to-schedule histogram (start)\n");
        fmt::print("{}\n", jlog.dump(4));
        fmt::print("load-to-schedule histogram (end)\n");
    }
}

static double round4(double x)
{
    double y = std::round(1000.0 * x) / 1000.0;
    return y;
}

void IXU::dump_bypass_stats(std::ostream& os) const
{
    if (enable_bypass_stats_collection) {
        json jlog;
        unsigned i = 0;
        for (const auto& [key, value] : bypass_stats) {
            const auto prod_uop = archx::uopcode_to_string(std::get<0>(key));
            const auto prod_unit = util::enum_to_string(std::get<1>(key));
            const auto cons_uop = archx::uopcode_to_string(std::get<2>(key));
            const auto cons_unit = util::enum_to_string(std::get<3>(key));
            const std::uint32_t cons_src_idx = std::get<4>(key);
            const std::uint32_t bypassed = std::get<5>(key);
            const unsigned count = value.first;
            const double avg_delay = round4(static_cast<double>(value.second) / static_cast<double>(count));
            jlog[i++] = {prod_uop, prod_unit, cons_uop, cons_unit, cons_src_idx, bypassed, count, avg_delay};
        }
        std::string table_name = "fsu bypass stats";
        fmt::print(os, "{} (start)\n", table_name);
        fmt::print(os, "{}\n", jlog.dump());
        fmt::print(os, "{} (end)\n", table_name);
    }
}

void IXU::dump_schedulers(std::ostream& os) const
{
    os << "= IXU ======================================================\n";
    os << "\nbits are {in_use, all_ready, can_dealloc}\n";
    for (unsigned i = 0; i < schedulers.size(); ++i) {
        auto s = Scheduler::from_sched(i);
        os << "\nSCH " << s.name() << ":";
        table_print(i, os);
    }
    os << "============================================================\n";
}

// Method which can run at simulation exit.
void IXU::dump(std::ostream& os) const
{
    dump_load_use_histo();
    dump_schedulers(os);

    dump_bypass_stats(os);

    if (!detailed_stats_collection) {
        return;
    }

    os << "\nDetailed stats for design and power, in .csv form\nIXU_STATS, " << now << ", ";

    for (const auto& scheduler : schedulers) {
        os << ", ";
        for (unsigned grp = 0; grp <= scheduler.sched_groups; grp++)
            os << scheduler.stats.sched_occupancy[grp] << ", ";
    }

    for (const auto& scheduler : schedulers) {
        os << ", ";
        for (unsigned grp = 0; grp <= scheduler.sched_groups; grp++)
            os << scheduler.stats.sched_issue_eligible[grp] << ", ";
    }

    os << ", " << stat_sched_ld_writeback << "\n";
}

/*************************************************************************
 * Reset for SchedEntry
 *
 *************************************************************************/
void SchedEntry::reset()
{
    if (uop) {
        uop.reset();
    }

    src_ready.fill(false);
    load_made_src_ready_cycle = 0;
    src_alloc_id.fill(0);
    alloc_id = 0;
    alloc_time = 0;
    issued_once = false;
    issued_canceled = false;
    last_producer_txid = 0;
    last_producer_cycle = 0;
    in_use = all_ready = all_ready_historical = can_dealloc = false;
}

// called after all objects exist and before simulation starts
void IXU::end_of_elaboration()
{
    throttle_enable = idr_throttle_ixu.bind_count() > 0 && idr_throttle_fsu.bind_count() > 0;

    // The latency and repeat rate of early-exit divides is different on different projects.
    // Override the uop_info table here so that all the users of the Uop object see the same values.
    AMPERE_ASSERT(idiv_early_exit_latency >= 0, "Must be nonnegative");
    AMPERE_ASSERT(idiv_early_exit_rate >= 0, "Must be nonnegative");

    // If FP divider early exit is not enabled (Furiosa), leave all the latencies and repeat rates
    // at their defaults, which equal the latency/rate of the normal non-early case. Otherwise,
    // override them the same way we do for integer divide.
    if (scalar_fdiv_fsqrt_early_exit) {
        AMPERE_ASSERT(fdiv_early_exit_latency >= 0, "Must be nonnegative");
        AMPERE_ASSERT(fdiv_early_exit_rate >= 0, "Must be nonnegative");
    }
}

} // namespace ampere::core

/*
 * vim: sw=4:ts=4:et:si:cindent:cino=\:0,(0,g0,t0:
 */
