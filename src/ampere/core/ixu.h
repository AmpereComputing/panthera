//
// Copyright (c) 2018-2024 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//

#pragma once

#include <list>
#include <limits>
#include <iostream>
#include <map>
#include <deque>
#include <array>
#include <unordered_map>

#include "ampere/sc/sc.h"
#include "ampere/sc/map.h"
#include "ampere/archx/register.h"
#include "ampere/core/instruction.h"
#include "ampere/core/uop.h"
#include "ampere/core/xu.h"
#include "ampere/core/ixu_defs.h"
#include "ampere/core/flush.h"
#include "ampere/core/early_exit.enum.h"
#include "ampere/core/ixu.enum.h"
#include "ampere/core/fsu/bypass.h"
#include "ampere/sc/value.h"
#include "pmu.h"

#define NUM_INT_SCHED 6

namespace ampere::core {

constexpr size_t MAX_32B_ELEMENTS_PER_VECTOR = archx::MAXVL / sizeof(std::int32_t);

// for src_used_bv
constexpr unsigned SUS_ALL = 7;
constexpr unsigned SUS_OP0 = 1;
constexpr unsigned SUS_OP1 = 2;
constexpr unsigned SUS_OP2 = 4;

struct generic_vector_t
{
    // cannot use archx::vector_t because this is 2x that width
    std::uint64_t u64[MAX_32B_ELEMENTS_PER_VECTOR]{};
    std::int64_t i64[MAX_32B_ELEMENTS_PER_VECTOR]{};
};

struct generic_vector_pair_t
{
    generic_vector_t numerator;
    generic_vector_t denominator;
};

struct SchedEntry
{
    //////////
    /// NOTE: all fields in this struct must be initialized in the reset() function in ixu.cc
    //////////

    // which group are we in?
    const int grp;

    // which entry in the group is this one?
    const int idx;

    // one uop per entry
    Uop uop{nullptr};

    // entry is being used
    bool in_use{false};

    // one bit for each possible source,
    std::array<bool, UOP_MAX_SRCS> src_ready;

    // Cycle when load makes a source ready, or 0 if source is not from a load
    sc::Cycle load_made_src_ready_cycle{0};

    // alloc id for which uop is writing that source
    std::array<uint64_t, UOP_MAX_SRCS> src_alloc_id;

    // memoization of all sources being ready
    bool all_ready{false};
    bool all_ready_historical{false}; // for wb_bypass event

    // uop is complete and can deallocate
    bool can_dealloc{false};

    // need to hold a timestamp.
    sc::Cycle alloc_time;

    // copy of the alloc_id held in the uop.
    uint64_t alloc_id{0};

    // for double-pump fsu uops so that we know to hold on to
    // scheduler entry and issue 2nd time.
    bool issued_once{false};

    // uop fake-issued due to load cancel
    bool issued_canceled{false};

    std::uint64_t last_producer_txid{0}; // for wb_bypass event reporting
    sc::Cycle last_producer_cycle{0};    // for wb_bypass event reporting
    SchedEntry(const int g, const int i) : grp(g), idx(i) { reset(); }

    void reset();
};

static const int MAX_GROUP_SIZE = 10;

enum class CompType {
    IXU,
    IXU_I2F,
    FSU,
    FSU_DIV_SQRT,
    FSU_PREDICATE,
    FSU_FLAGS,
    FSU_F2I,
    LOAD
};

struct IssueRec
{
    // record the time of issue and producer information

    /* The life cycle of an IssueRec:
    - Idle/Ready: starting state, no writer
    - Unissued: waiting to issue
    - Issued/Real: normal issue
    - Issued/Canceled: issued (but will cancel)
    - Idle/Ready: when we send the preg back to GPC/IDR on commit, we make it ready again
    */
    std::uint64_t producer_txid{0};
    sc::Cycle issue_time{0};
    sc::Cycle cancel_time{0};     // time the load will actually cancel and we'll stop issuing canceled dependents
    sc::Cycle load_ready_time{0}; // for managing scheduler entry deallocation
    CompType ctype{CompType::IXU};
    unsigned latency{0};
    fsu_producer_unit fsu_producer{};
    bool fsu_dst{false};
    bool producer_is_single_cyc{false}; // for super sus src3 delay
    bool canceled{false};
    bool ready{false};
    bool primary_cancel{false}; // actually signaled cancel from LSU, vs being in shadow of canceled uop.
    bool cancelable{true};

    struct
    {
        archx::Uopcode fsu_producer_uopcode{archx::Uopcode::INVALID};
        archx::FsuUnit fsu_producer_unit{archx::FsuUnit::NONE};
    } bypass_stats_info;

    void set_fsu_producer_info(archx::Uopcode uopcode = archx::Uopcode::INVALID,
                               archx::FsuUnit unit = archx::FsuUnit::NONE)
    {
        bypass_stats_info.fsu_producer_uopcode = uopcode;
        bypass_stats_info.fsu_producer_unit = unit;
    }

    IssueRec() = default;
    IssueRec(std::uint64_t producer_txid,
             sc::Cycle time,
             CompType ctype,
             unsigned latency,
             fsu_producer_unit fsu_producer,
             bool producer_is_single_cyc,
             bool fsu_dst)
        : producer_txid(producer_txid),
          issue_time(time),
          ctype(ctype),
          latency(latency),
          fsu_producer(fsu_producer),
          fsu_dst(fsu_dst),
          producer_is_single_cyc(producer_is_single_cyc)
    {
    }

    bool is_fsu_dst() const { return fsu_dst; }
    bool is_ready() const { return ready; } // no writer pending
    bool is_issued() const { return issue_time != 0; }
    bool is_waiting_to_issue() const { return !ready && issue_time == 0; }
    void set_issued(sc::Cycle now) { issue_time = now; }
    void set_waiting_for_data()
    {
        // preserve issue time
        primary_cancel = false;
        canceled = false;
        cancel_time = 0;
        ready = false; // should already be false
    }
    void set_waiting_to_issue()
    {
        primary_cancel = false;
        canceled = false;
        cancel_time = 0;
        issue_time = 0;
        ready = false;
    }
    void set_ready()
    {
        issue_time = 0;
        ready = true;
    }
    void set_ready_load(sc::Cycle now)
    {
        load_ready_time = now;
        set_ready();
    }
    void set_cancel(sc::Cycle arg_cancel_time, bool arg_primary_cancel, sc::Cycle arg_load_ready_time)
    {
        AMPERE_ASSERT(cancelable, "Cannot cancel, producer already committed");
        primary_cancel = arg_primary_cancel;
        canceled = true;
        cancel_time = arg_cancel_time;
        load_ready_time = arg_load_ready_time;
    }
    bool is_canceled() const { return canceled; }
    bool is_primary_cancel() const { return primary_cancel; }
    auto done_at(unsigned latency = 0) const { return issue_time + (latency ? latency : this->latency); }
};

struct cancel_reservation_t
{
    bool primary{false};
    Uop uop{nullptr};
    bool ixu{false};    // controls delay in reaping the cancellation
    phys_reg_t preg{0}; // zero denotes invalid register (used mostly for std/stdv uops)
    size_t src_idx{0};
    // for primary cancels, we do not need the _src_idx parameter, so use 0.
    cancel_reservation_t(bool primary, const Uop& uop, bool ixu, phys_reg_t preg, size_t src_idx = 0)
        : primary(primary), uop(uop), ixu(ixu), preg(preg), src_idx(src_idx)
    {
    }
};

struct ReadyInfoOne
{
    bool ready{true};
    bool producer_was_load{false};
    bool src_was_canceled{false};
    sc::Cycle cancel_time{0};
    sc::Cycle load_time{0};
    std::uint64_t producer_txid{0};
};

struct ReadyInfo : ReadyInfoOne
{
    std::array<phys_reg_t, UOP_MAX_SRCS> cancel_src_pregs{};
    std::array<sc::Cycle, UOP_MAX_SRCS> cancel_src_time{};
};

/*************************************************************************
 * This models a partitioned scheduler in six parts, with dependencies
 * tracked via lreg.
 *************************************************************************/
class IXU : public Module
{
public:
    Param<size_t> num_execution_units{"num_execution_units", "number of XUs", 12};
    Param<bool> uvw_load_port_share{
        "uvw_load_port_share", "FSU By pipes U/V/W share a write port with Load from LSU", true};

private:
    Param<size_t> sch_group_size_int{"sch_group_size_int", "number of integer uops in a group", 2};
    Param<size_t> sch_group_size_mem{"sch_group_size_mem", "number of memory uops in a group", 2};
    Param<size_t> sch_group_size_fp{"sch_group_size_fp", "number of floating point uops in a group", 2};
    Param<size_t> fsu_dealloc_per_cycle{
        "fsu_dealloc_per_cycle", "number of scheduler groups FSU can dealloc in one cycle", 1};
    Param<size_t> int_dealloc_per_cycle{
        "int_dealloc_per_cycle", "number of scheduler groups INT-ALU can dealloc in one cycle", 1};
    Param<size_t> mem_dealloc_per_cycle{
        "mem_dealloc_per_cycle", "number of scheduler groups IXU-MEM can dealloc in one cycle", 1};
    Param<unsigned> fsu_dispatch_latency{"fsu_dispatch_latency", "number of extra cycles for allocating into FSU", 1};
    Param<unsigned> fsu_flag_wb_delay{"fsu_flag_wb_delay", "Extra latency for FSU flag writeback", 2};
    Param<unsigned> fsu_f2i_delay{"fsu_f2i_delay", "Extra latency for FP to int moves", 2};
    Param<unsigned> fsu_i2f_exe_latency{"fsu_i2f_exe_latency", "FSU execution time for I2F uops", 2};
    Param<bool> super_sus_src3_delay{
        "super_sus_src3_delay",
        "3-source SUS uops take an extra cycle to wake up when src3 is produced by an AB uop",
        true};
    Param<bool> super_sus_always_double_fire{
        "super_sus_always_double_fire", "3-source SUS uops issue twice even if all sources are ready", true};
    Param<bool> sched_bypass_ixu{"sched_bypass_ixu", "Allow IXU uops to issue one cycle earlier", false};
    Param<bool> sched_bypass_fsu{"sched_bypass_fsu", "Allow FSU uops to issue one cycle earlier", false};
    Param<bool> detailed_stats_collection{"detailed_stats_collection", "Occupancy data for design and power", false};
    Param<bool> enable_bypass_stats_collection{
        "enable_bypass_stats_collection", "Stats on FSU bypass producers and consumers (use with dump_at_end)", false};
    Param<bool> run_always{"run_always", "Run the module every cycle", false};

    Param<unsigned> idiv_early_exit_latency{"idiv_early_exit_latency", "Latency of an early-exit integer divide", 15};
    Param<unsigned> idiv_vector_early_exit_latency{
        "idiv_vector_early_exit_latency", "Latency of a vector early-exit integer divide", 7};
    Param<unsigned> idiv_early_exit_rate{
        "idiv_early_exit_rate", "Cycles from the end of an early-exit divide when another divide can issue", 1};
    Param<unsigned> fdiv_early_exit_latency{"fdiv_early_exit_latency", "Latency of an early-exit FP divider uop", 7};
    Param<unsigned> fdiv_early_exit_rate{
        "fdiv_early_exit_rate",
        "Cycles from the end of an early-exit FP divider uop when another divider uop can issue",
        0};

    Param<OneOf<unsigned, 4, 16, 64>> divide_radix{
        "divide_radix", "Radix of the divider  (4 (2b/iteration), 16 (4b/iter), 64 (6b/iter))", 16};
    Param<bool> divide_fixed_latency{"divide_fixed_latency", "Divide is fixed latency", false};

    Param<bool> fdiv_normal_numbers_optimization{
        "fdiv_normal_numbers_optimization",
        "Shave one cycle off the front end of fdiv if all the inputs are not denormals",
        false};

    Param<bool> idiv_use_value_derived_latency{
        "idiv_use_value_derived_latency", "Shorten IDIV latency using knowledge of values being divided", false};
    Param<bool> fdiv_use_value_derived_latency{
        "fdiv_use_value_derived_latency", "Shorten FDIV{32,64} latency using knowledge of values being divided", false};
    Param<bool> scalar_fdiv_fsqrt_early_exit{
        "enable_fdiv_early_exit", "Scalar FP divide and sqrt uops have lower latency in early-exit cases", false};
    Param<bool> vector_idiv_early_exit{"vector_idiv_early_exit", "Vector integer divides support early exit", false};
    Param<bool> vector_fdiv_early_exit{"vector_fdiv_early_exit", "Vector FP divides support early exit", false};
    Param<bool> vector_sqrt_early_exit{"vector_sqrt_early_exit", "Vector sqrt support early exit", false};
    Param<bool> fsqrt_early_exit_odd_powers_of_two{
        "fsqrt_early_exit_odd_powers_of_two", "Fsqrt early exit on odd powers of 2", false}; // true for connor onwards
    Param<bool> idiv_early_out_plus_minus_one{
        "idiv_early_out_plus_minus_one",
        "Subtle integer early out tests: numerator==denominator and number of bits are equal",
        false};

    Param<FSUBypassDefinition> fsu_bypass{"fsu_bypass", "FSU bypass configuration", {}};
    Param<bool> fsu_bypass_dump{"fsu_bypass_dump", "Dump FSU bypass configuration", false};
    Param<bool> fsu_bypass_dump_matrix{"fsu_bypass_dump_matrix", "Dump FSU bypass matrix configuration", false};

    // 7 <= ripley, 5 >= trinity ... value in backend.cfg for furiosa and trinity
    Param<unsigned> fsu_flags_latency{"fsu_flags_latency", "FSU flags latency relative to FSU issue.", 7};
    Param<unsigned> fsu_predicate_latency{"fsu_predicate_latency", "FSU predicate latency relative to FSU issue.", 5};
    Param<unsigned> fsu_div_bypass_latency{"fsu_div_bypass_latency", "Bypass latency of Dv producer", 3};

    Param<bool> fsu_zpipe{"fsu_zpipe", "Z pipe enabled", false};

    Param<bool> enable_sched_wakeup_delay{
        "enable_sched_wakeup_delay",
        "Add 1 cycle to wakeup of dependents of uops being written into the scheduler",
        false};
    Param<bool> cancel_only_blocks_load_dep_dealloc{
        "cancel_only_blocks_load_dep_dealloc",
        "Load cancel only prevents deallocation of sched entries which were woken by a load one cycle ago, instead of "
        "all IXU deallocation",
        false};
    Param<unsigned> cancel_to_reap_delay_ixu{
        "cancel_to_reap_delay_ixu",
        "Time between receiving cancel message from LSU and the IXU processing the cancel",
        2};
    Param<unsigned> cancel_to_reap_delay_fsu{
        "cancel_to_reap_delay_fsu",
        "Time between receiving cancel message from LSU and the FSU processing the cancel",
        3};
    Param<bool> slow_int_writeback_port{
        "slow_int_writeback_port", "Slow integer pipe can independently schedule and write back", false};
    Param<bool> shared_slow_int_pipe{"shared_slow_int_pipe", "Slow integer pipe is shared between partitions", true};
    Param<unsigned> throttle_epoch{"throttle_epoch", "length of data collection epoch for throttling, in cycles", 64};
    Param<float> throttle_watermark_lo_ixu{
        "throttle_watermark_lo_ixu", "Issue per cycle IXU average that triggers entering IDR throttle mode", 0.4};
    Param<float> throttle_watermark_hi_ixu{
        "throttle_watermark_hi_ixu", "Issue per cycle IXU average that triggers exiting IDR throttle mode", 0.4};
    Param<float> throttle_watermark_lo_fsu{
        "throttle_watermark_lo_fsu", "Issue per cycle FSU average that triggers entering IDR throttle mode", 0.2};
    Param<float> throttle_watermark_hi_fsu{
        "throttle_watermark_hi_fsu", "Issue per cycle FSU average that triggers exiting IDR throttle mode", 0.2};
    Param<int> extra_scalar_fdiv_latency{"extra_scalar_fdiv_latency", "Extra latency for scalar fdiv", 0};
    Param<int> extra_advsimd_fdiv_latency{"extra_advsimd_fdiv_latency", "Extra latency for advsimd fdiv", 0};
    Param<int> extra_gpr_div_latency{"extra_gpr_div_latency", "Extra latency for GPR div", 0};
    Param<int> extra_srt_div_latency{"extra_srt_div_latency", "Extra latency for variable latency SRT div", 2};
    Param<int> extra_fsqrt_latency{"extra_fsqrt_latency", "Extra latency for fsqrt", 0};
    Param<unsigned> fsu_load_delay_param{
        "fsu_load_delay_param", "Extra latency for load writing FP/vector register", 1};
    Param<unsigned> fsu_max_writeback_latency{
        "fsu_max_writeback_latency", "Cycles, after issue, before FSU uops write to VRF", 10};
    Param<size_t> reserved_lub_credits{"reserved_lub_credits", "LUB credits reserved for only the oldest load", 0};
    Param<size_t> lub_size{"lub_size", "LUB size per load pipe ", 0};

    Param<WithinRange<PipeType, FSU_ALU_PIPE_TYPE_FIRST, FSU_ALU_PIPE_TYPE_LAST>> i2f_pipe{
        "i2f_pipe", "I2F (int-to-fp) pipe", "Y"};
    Param<WithinRange<PipeType, IXU_ALU_PIPE_TYPE_FIRST, IXU_ALU_PIPE_TYPE_LAST>> f2i_pipe{
        "f2i_pipe", "F2I (fp-to-int) pipe", "A"};
    Param<WithinRange<PipeType, FSU_ALU_PIPE_TYPE_FIRST, FSU_ALU_PIPE_TYPE_LAST>> div_wb_pipe{
        "div_wb_pipe", "Div WB pipe", "X"};
    Param<WithinRange<PipeType, FSU_PIPE_TYPE_START, static_cast<PipeType>(FSU_PIPE_TYPE_END - 1)>> fsu_to_lsu_pipe{
        "fsu_to_lsu_pipe", "FSU-to-LSU pipe (e.g. STDV)", "Z"};

    Param<sc::Cycle> debug_start{"debug_start", "Hook for starting printf debugging", 0};
    Param<sc::Cycle> debug_end{
        "debug_end", "Hook for stopping printf debugging", std::numeric_limits<std::int64_t>::max()};

    static const decltype(pipe_enum()) pipe_enumeration;

public:
    In<Uop> dispatch_port{"dispatch"};
    Out<Credit> credit_return{"credit_return"};
    Read<Flush> flush_port{"flush"};
    Out<Uop> commit_port{"commit"};
    OutOpt<bool> idr_throttle_ixu{"idr_throttle_ixu"};
    OutOpt<bool> idr_throttle_fsu{"idr_throttle_fsu"};

    Out<std::vector<bool>> pmu_stall_xu_busy{"pmu_stall_xu_busy"};
    Out<std::vector<bool>> pmu_stall_ilock{"pmu_stall_ilock"};

    sc_core::sc_vector<Out<Uop>> mem_issue_port{"mem_issue", num_execution_units};
    sc_core::sc_vector<In<Uop>> writeback_xu{"writeback_xu", num_execution_units};
    sc_core::sc_vector<In<std::pair<Uop, bool>>> complete_xu{"complete_xu", num_execution_units};
    In<Uop> load_cancel_in{"load_cancel_in"};
    std::vector<sc::Cycle> cancel_occupies_mem;

    // NOTE: we only use a subset here for excluding the E pipe (since the port will not be connected)
    sc_map<XU> xu{"xu", pipe_enumeration.begin(), std::next(pipe_enumeration.begin(), num_execution_units)};

private:
    Event ev_uop_dispatch{"uop_dispatch", "Uop dispatched to scheduler"};
    Event ev_uop_ready{"uop_ready", "Uop is ready to schedule"};
    Event ev_uop_issue{"uop_issue", "Uop issued from scheduler"};
    Event ev_ixu_lub_stall{"ixu_lub_stall", "Load uop not issued from scheduler because no room in lub"};
    Event ev_uop_writeback{"uop_data_available", "Uop writes into sch, data available for bypass"};
    Event ev_uop_complete{"uop_complete", "Uop complete"};
    Event ev_wb_bypass{"wb_bypass", "uop picked up data from writeback"};
    Event ev_sch_dealloc{"sch_dealloc", "entry removed from scheduler"};
    Event ev_ixu_flush_entry{"sch_flush_entry", "entry marked for dealloc by GPC flush"};
    Event ev_fsu_zpipe_std_exec{"fsu_zpipe_std_exec", "FSU passed Z-pipe STD uop to LSU with a delay"};
    Event ev_sus_sta{"sus_sta_issue", "SUS fires STA half"};
    Event ev_sus_fat{"sus_fat_issue", "SUS fires both halves"};
    Event ev_sus_pair_std1{"sus_pair_std1", "Single-uop store pair issued data source 1"};
    Event ev_early_exit{"early_exit", "IDIV/FDIV/FSQRT early exit"};
    Event ev_uvw_stall{"uvw_stall", "UVW uop not issued due to load port busy"};
    Event ev_bs_pipe_cancel{"bs_pipe_cancel",
                            "BS-pipe uop could not issue because the other partition won the BS pipe this cycle"};
    Event ev_pmu{"pmu", "A PMU event has occurred"};

    EventData<bool> ev_data_memop{"memop", "if this was a memory op"};
    EventData<decltype(pipe_enumeration)> ev_data_ixu_pipe{"ixu_pipe", "Execution pipe number", pipe_enumeration};
    EventData<unsigned> ev_data_ixu_sch{"ixu_sch", "IXU scheduler"};
    EventData<unsigned> ev_data_ixu_partition{"ixu_partition", "Execution partition number"};
    EventData<uint64_t> ev_data_alloc_id{"alloc_id", "Allocation (sequence) id"};
    EventData<std::uint64_t> ev_data_waking_tx{"waking_tx", "Uop (txid) which woke this one"};

    Event ev_idiv_latency{"idiv_latency", "integer divide latency"};
    Event ev_fdiv16_latency{"fdiv16_latency", "fp16 divide latency"};
    Event ev_fdiv32_latency{"fdiv32_latency", "fp32 divide latency"};
    Event ev_fdiv64_latency{"fdiv64_latency", "fp64 divide latency"};
    Event ev_fsqrt_latency{"fsqrt_latency", "fsqrt latency"};
    EventData<unsigned> ev_latency{"latency", "operation latency"};
    EventData<PMU> ev_num{"event", "PMU event number"};

public:
    std::queue<Uop> scheduler_input_queue;

    void end_of_elaboration() override;

    // number of entries in each scheduler table
    std::vector<size_t> sched_entries;
    std::array<bool, 2 * MAX_FSU_PARTITIONS> load_port_busy{false};

    void divide_properties_init(const Uop& uop);
    generic_vector_pair_t idiv_value_fixer(const Uop& uop) const;

    std::uint64_t idiv_variable_latency_config(const Uop& uop, const generic_vector_pair_t& nd) const;
    std::uint64_t fdiv_variable_latency_config(const Uop& uop) const;
    unsigned sdiv_iterations(std::int64_t numerator, std::int64_t denominator, unsigned esize) const;
    unsigned udiv_iterations(std::uint64_t numerator, std::uint64_t denominator, unsigned esize) const;
    std::uint64_t vector_idiv_iterations_config(const Uop& uop, const generic_vector_pair_t& nd) const;
    std::uint64_t scalar_idiv_iterations_config(const Uop& uop, const generic_vector_pair_t& nd) const;
    unsigned idiv_latency(unsigned esize, unsigned srt_iterations) const;
    // dispatch based on divide_radix param
    unsigned f64_div_iterations(std::uint64_t numerator, std::uint64_t denominator) const;
    unsigned f32_div_iterations(std::uint32_t numerator, std::uint32_t denominator) const;
    unsigned u64_div_iterations(std::uint64_t numerator, std::uint64_t denominator) const;
    unsigned u32_div_iterations(std::uint32_t numerator, std::uint32_t denominator) const;
    unsigned s64_div_iterations(std::int64_t numerator, std::int64_t denominator) const;
    unsigned s32_div_iterations(std::int32_t numerator, std::int32_t denominator) const;

    EarlyExitReason fp_early_exit_config(const Uop& uop) const;
    EarlyExitReason int_early_exit_config(const Uop& uop, const generic_vector_pair_t& nd) const;
    EarlyExitReason idiv_vector_early_exit_config(const Uop& uop, const generic_vector_pair_t& nd, int elements) const;
    static EarlyExitReason fdiv_early_exit_config(const Uop& uop); // vector and scalar
    EarlyExitReason fsqrt_early_exit_config(const Uop& uop) const; // vector and scalar
    EarlyExitReason test_fsqrt_early_exit(std::uint64_t radicand, int size) const;
    EarlyExitReason test_idiv_early_exit_u64(std::uint64_t numerator, std::uint64_t denominator) const;
    EarlyExitReason test_idiv_early_exit_i64(std::int64_t numerator, std::int64_t denominator) const;

private:
    // a list of sched_entries
    // in std::pair, first is op-sched, second is sch.entry
    using consumer_t = std::list<std::pair<int, int>>;

    // vector of consumer lists
    using consumers_vec_t = std::unordered_map<uint64_t, consumer_t>;

    struct IXUScheduler
    {
        unsigned entries_in_use{0};
        std::vector<SchedEntry*> entries; // for sorting elements in the SchedEntry table
        std::vector<std::array<SchedEntry*, MAX_GROUP_SIZE>> groups;
        unsigned sched_groups{0};
        unsigned group_alloc_ptr{0};
        ampere::sc::CreditPool* lub;

        bool might_dealloc{false};

        // used to avoid re-sorting each scheduler. schedulers.size(u dirty (true)
        // on: (1) insertions, (2) issues, (3) dealloc sched entries
        bool need_to_sort{false};

        bool no_issue_xu_busy{false};
        bool no_issue_ilock{false};

        struct
        {
            std::vector<unsigned> sched_occupancy;
            std::vector<unsigned> sched_issue_eligible;
        } stats;
    };
    std::vector<IXUScheduler> schedulers;
    sc_core::sc_vector<CreditPool> lub{"lub"};

public:
    explicit IXU(const sc_core::sc_module_name& name);
    ~IXU() override;
    // second part of constructor
    using Module::init;
    void init(const std::vector<size_t>& entries);
    bool run() override;

    Param<unsigned> gather_use_delay{"gather_use_delay", "delay before using gather results", 3};

    inline unsigned scheduler_group_size(SchedulerType op) const
    {
        if (is_int_sch(op))
            return sch_group_size_int;
        else if (is_mem_sch(op))
            return sch_group_size_mem;
        return sch_group_size_fp;
    }

private:
    Event ev_block_fsu_pipe_for_div_wb_issue{"block_fsu_pipe_for_div_wb_issue",
                                             "Block X scheduler issue for div_wb uop modeling"};
    Event ev_block_fsu_pipe_for_i2f{"block_fsu_pipe_for_i2f", "Y pipe blocked due to i2f execution"};
    Event ev_i2f_shelf{"i2f_shelf", "I2F uop placed on shelf due to double-pumped Y-pipe uop"};
    Event ev_double_pump_first_issue{"double_pump_first_issue", "double-pumped uop issued for first execution."};
    Event ev_double_pump_second_issue{"double_pump_second_issue", "double-pumped uop issued for second execution."};
    Event ev_block_ixu_pipe_next_cycle_for_f2i{"block_ixu_pipe_next_cycle_for_f2i",
                                               "One A pipe blocked in the next cycle due to f2i writeback"};
    Event ev_fsu_bypass_delay{"fsu_bypass_delay", "FSU variable bypass delay"};
    Event ev_fsu_pred_ixu_bypass_delay{"fsu_pred_ixu_bypass_delay", "FSU-to-IXU predicate bypass delay"};
    Event ev_sched_wakeup_delay{"sched_wakeup_delay", "Wakeup of this uop's dependents was delayed 1 cycle"};
    Event ev_load_cancel{"load_cancel", "Speculative load wake from LSU was canceled because load did not complete"};
    Event ev_uop_cancel{"uop_cancel", "Dependent uop was canceled by a load"};
    EventData<int> fsu_delay_len{"fsu_delay_len", "FSU bypass delay cycles"};

    Event ev_sch_empty_cycles{"sch_empty_cycles", "Cycles when IXU Scheduler is empty (any partition)"};
    EventData<SchedulerType> ev_sched{"sched", "Scheduler"};
    Event ev_partition_empty_cycles{"partition_empty_cycles", "Cycles when at least one partition is empty"};
    EventData<unsigned> ev_partition{"partition", "Partition"};

    void event_latencies(const ConstUop& uop);

    void execute();
    void load_cancel_receive();
    void load_cancel_action(const Uop& canceled_uop);
    void canceled_issue_rec_primary_delayed();
    void load_cancel_reap();
    void load_cancel_reap_scheduler_entry(cancel_reservation_t& canceled_item);
    void load_cancel_reap_uop(cancel_reservation_t& canceled_item);
    static void rollback_sus_state(const Uop& uop, bool primary, size_t src_idx);
    static void rollback_super_sus_pair_state(const Uop& uop, bool primary, size_t src_idx);
    bool will_reap_this_cycle(const Uop& uop) const;
    void fsu_bypass_wb();
    void fsu_zpipe_std_ixu_to_lsu_exec();
    void process_throttle_data();

    // these methods are called once per cycle by run()
    void writeback_lsu();
    void complete_lsu();
    void complete_uop(const Uop& uop);
    void schedule();
    void dispatch();
    void flush_pipe();
    void run_scheduler(Scheduler sch);

    void finish_lsu_uop_fsu(const Uop& uop);
    void finish_lsu_uop_ixu(const Uop& uop);
    void fsu_writeback_splitter(const Uop& uop);
    void writeback_uop_lsu_fsu(const Uop& uop); // really only LSU data returns
    void writeback_uop_lsu_ixu(const Uop& uop); // really only LSU data returns
    static void sus_wb(const Uop& uop);

    SchedEntry*** table = nullptr;
    bool throttle_enable{false};

    sc::Cycle now{0};

    // for group alloc/dealloc
    int next_alloc_group(unsigned table, int curr_grp, int curr_idx);
    void sched_can_dealloc(SchedEntry* sch_entry, unsigned sch_idx); // mark as ready to deallocate

    unsigned stat_sched_ld_writeback{0};
    bool load_wakeup_this_cycle{false};
    bool cancel_this_cycle{false};
    std::map<std::tuple<archx::Uopcode, archx::FsuUnit, archx::Uopcode, archx::FsuUnit, std::uint32_t, std::uint32_t>,
             std::pair<uint64_t, uint64_t>>
        bypass_stats;
    void record_bypass_stats(const ConstUop& uop);
    void dump_bypass_stats(std::ostream& os) const;

    void stats_collection();

    struct AllocidTime
    {
        std::uint64_t alloc_id;
        sc::Cycle cycle;
        AllocidTime(std::uint64_t alloc_id, sc::Cycle cycle) : alloc_id(alloc_id), cycle(cycle) {}
    };
    Param<bool> collect_load_use_stats{"collect_load_use_stats", "Collect load-to-use stats", false};
    std::map<std::uint64_t, std::uint64_t> data_returned; // allocid -> time
    std::deque<AllocidTime> data_cleanout;
    ;
    static constexpr size_t histogram_load_home_to_schedule_max_bin = 100;
    std::array<std::uint64_t, histogram_load_home_to_schedule_max_bin> histogram_load_home_to_schedule;
    void watch_loads(const Uop& uop);
    void tally_load_use_stats(SchedEntry* e);
    void dump_load_use_histo() const;
    void clean_load_use_stats();

    static void set_sus_issue_vector(const Uop& uop);
    void table_allocate(const Uop& uop);
    void table_deallocate(const Uop& uop);
    void track_preg(const Uop& uop, std::uint64_t credit_id, std::uint64_t alloc_id);
    void untrack_preg(const Uop& uop, std::uint64_t credit_id, std::uint64_t alloc_id);
    bool group_deallocate(unsigned num);
    void alu_execute(PipeType pipe_num, unsigned partition);
    bool issue_eligible(SchedEntry* e);
    bool can_issue_uop(const Uop& uop);
    void issue_entry(SchedEntry* e);
    void issue_uop(const Uop& uop);
    void pmu_sve_pred(const ConstUop& uop);
    bool try_issue_super_sus_pair(SchedEntry* e, const ReadyInfo& ready_info);
    bool try_issue_sus(SchedEntry* e, const ReadyInfo& ready_info);
    bool try_issue(SchedEntry* e);
    static bool check_if_should_cancel(SchedEntry* sched_entry,
                                       const ReadyInfo& ready_info,
                                       unsigned arg_src_used_for_issue_bv = 0);
    static std::pair<bool, bool> sus_partial_cancel(const Uop& uop,
                                                    const ReadyInfo& ready_info,
                                                    unsigned src_used_for_issue_bv);

    bool xus_busy();
    unsigned table_entries_free(unsigned op_sched);
    unsigned mem_pipe(PipeType pipe_num, unsigned partition) const;
    unsigned uvw_mem_pipe(PipeType pipe_num, unsigned partition) const;
    bool mem_port_canceled(unsigned port);

    struct FSUScheduling
    {
        bool pipe_schedule_i2f{false};
        bool pipe_mid_double_pump_uop{false};

        // when fsu is issuing a 2nd uop of a double-pumped operation on y
        // pipe, we cannot steal y-pipe for i2f; we can delay one cycle. When
        // we delay an i2f by one cycle we place it on the "i2f_shelf". If
        // the i2f_shelf is full, we cannot do a new i2f operation; that new i2f
        // must go on to i2f_shelf for next cycle.
        Uop i2f{nullptr};
        Uop i2f_shelf{nullptr};

        bool block_pipe_issue_this_cycle_for_div_wb{false}; // for simulating internal uop div_wb issue
        std::map<sc::Cycle, Uop> zpipe_reservation;         // cycles that the Z pipe is reserved. Holds uop to issue

        void reset()
        {
            block_pipe_issue_this_cycle_for_div_wb = false;
            pipe_mid_double_pump_uop = false;
            pipe_schedule_i2f = false;
        }

        bool has_zpipe_reservation(Cycle now) const { return zpipe_reservation.find(now) != zpipe_reservation.end(); }
    };
    FSUScheduling fsu_sched[MAX_FSU_PARTITIONS];

    void advance_i2f_shelf();
    void issue_i2f_to_fsu_pipe(const Uop& uop);

    bool issued_slow_int{false};

    // Count of insertions, minus non-canceled issues and second issues of double-issue uops.
    // Don't count first issue of double-issue uops.
    // independent of specific scheduler. used for IXU::run() run-next-cycle model optimization
    std::uint32_t sch_occupancy{0};
    bool need_to_run() const { return sch_occupancy > 0; }
    void sch_remove()
    {
        AMPERE_ASSERT(sch_occupancy > 0, "Scheduler occupancy should be nonzero if issuing");
        --sch_occupancy;
    }
    void sch_insert() { ++sch_occupancy; }

    // member variables for throttling algorithm
    unsigned total_issues_in_epoch_ixu{0};
    unsigned total_issues_in_epoch_fsu{0};
    bool prev_ixu_low_issue{false}, prev_fsu_low_issue{false};

    // internal port for waking up IXU every N cycles to report stats
    In<bool> throttle_sample_timer_in;
    Out<bool> throttle_sample_timer_out;

    void compute_base_latency(const Uop& uop);
    int compute_extra_div_sqrt_latency(const Uop& uop) const;
    void set_resource_rate(const Uop& uop);

    // FSU bypass delays
    OutIn<Uop> ixu_pipe_schedule_f2i{"ixu_pipe_schedule_f2i"};
    unsigned ixu_pipe_schedule_f2i_block{0}; // used for f2i delay == 1

    // internal port for FSU Z-pipe store data
    In<Uop> fsu_zpipe_std_in{"fsu_zpipe_std_in"};
    Out<Uop> fsu_zpipe_std_out{"fsu_zpipe_std_out"};

    // internal port for FSU gather completion
    OutIn<Uop> fsu_gather_delay{"fsu_gather_delay"};

    OutIn<bool> load_cancel_dealloc_delay{"load_cancel_dealloc_delay"};
    OutIn<phys_reg_t> fsu_load_cancel_delay{"fsu_load_cancel_delay"};
    OutIn<Uop> fsu_load_delay{"fsu_load_delay"};

    std::multimap<sc::Cycle, cancel_reservation_t> load_cancel_reservation;
    void flush_canceled_uops(std::uint64_t flush_id);
    void schedule_cancellation_primary(const Uop& canceled_uop);
    void schedule_cancellation_secondary(const Uop& canceled_uop);
    void schedule_cancellation_secondary_by_src(sc::Cycle cyc, const Uop& canceled_uop, size_t src_idx);
    void clean_load_cancel_reservations();

    // table for who is producing which LReg
    //
    struct source_producer_s
    {
        uint64_t alloc_id{0};
        bool resident{false}; // true==writer has not written-back yet
    };

    // array indexed by LReg. Used to tell consumers what producer
    // alloc_id is writing their src register.
    std::array<source_producer_s, MAX_PRF_SIZE> src_producer{{{0}}};

    void mark_reg_deps(SchedEntry* e);
    void post_exec_hazards(const Uop& finishing_uop);
    void release_producer_credits(const Uop& producer_uop);
    int compute_fsu_bypass_delay(unsigned producer_index,
                                 bool predicate_producer,
                                 const ConstUop& consumer,
                                 unsigned source,
                                 bool dest_no_unr_byp = false) const;

    void record_dest_issue(const ConstUop& producer_uop, archx::LReg dst, std::uint64_t preg);
    void record_issue(const ConstUop& producer_uop);

    void cancel_issue_rec_primary(const Uop& canceled_uop);
    void cancel_issue_rec_secondary(sc::Cycle cancel_time, const Uop& canceled_uop);
    void allocate_issue_rec(const Uop& uop);
    void mark_pdests_ready(const Uop& uop);

    void mark_load_pdest_ready(phys_reg_t preg);
    void set_or_schedule_load_ready_fsu(const Uop& uop);
    void set_or_schedule_load_ready_ixu(const Uop& uop);

    static phys_regs_t emitted_pregs(const ConstUop& uop);
    unsigned div_bypassing_delay(bool fsu_consumer) const;
    void i2f_block_fsu_scheduler(const Uop& producer_uop);
    void f2i_block_ixu_scheduler(const Uop& producer_uop);

    ReadyInfoOne src_ready_with_bypassing(const Uop& consumer_uop, unsigned src_idx) const;
    bool is_ready_check_only(const SchedEntry* sched_entry) const;
    ReadyInfo issue_info(SchedEntry* sched_entry);

    void reset_issue_recs()
    {
        for (auto& irec : issue_tracker) {
            irec.set_ready();
        }
    }
    std::array<IssueRec, MAX_PRF_SIZE> issue_tracker; // indexed by preg

    void run_schedule_zpipe();
    void writeback_and_commit(const Uop& uop);

    void zpipe_to_lsu_exec(const Uop& uop);
    void issue_z(const Uop& uop);
    void clean_zpipe_reservation();
    void flush_zpipe(std::uint64_t flush_id);

    // for debugging
    void table_print(unsigned num, std::ostream&) const;
    void table_print(unsigned num) const { table_print(num, std::cout); };

    void dump(std::ostream&) const override;
    void dump_schedulers(std::ostream&) const;
};
} // namespace ampere::core

/*
 * vim: sw=4:ts=4:et:si:cindent:cino=\:0,(0,g0,t0:
 */
