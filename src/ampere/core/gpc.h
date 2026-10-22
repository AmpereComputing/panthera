//
// Copyright (c) 2018-2024 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//

#pragma once
#include <functional>
#include <ctime>
#include <chrono>

#include "ampere/archx/flush_type.h"
#include "ampere/sc/sc.h"
#include "ampere/archx/driver.h"
#include "ampere/core/uop.h"
#include "ampere/core/flush.h"

namespace ampere::core {

using namespace ampere::sc;
using namespace ampere::archx;

class GPC : public Module
{
public:
    Param<size_t> uop_retire_width{"uop_retire_width", "how many uops can retire in one cycle", 4};
    Param<size_t> num_execution_units{"num_execution_units", "number of XUs that GPC can receive mispredicts from", 8};
    Param<size_t> commit_bypass{"commit_bypass", "how many uops to attempt for commit bypass in this group", 0};
    Param<Cycle> commit_freeze_cycles{
        "commit_freeze_cycles", "how many cycles commit is frozen after each flush (other than NO_RESTART)", 6};
    Param<Cycle> no_restart_commit_freeze{
        "no_restart_commit_freeze", "How many cycles commit is frozen after NO_RESTART flush", 6};
    Param<uint32_t> commit_branches{
        "commit_branches", "how many branch uops can commit in this group", std::numeric_limits<std::uint32_t>::max()};
    Param<uint32_t> commit_intdsts_per_cycle{"commit_intdsts_per_cycle",
                                             "how many integer destinations can be written in each cycle?",
                                             std::numeric_limits<std::uint32_t>::max()};
    Param<uint32_t> commit_vecdsts_per_cycle{"commit_vecdsts_per_cycle",
                                             "how many vector destinations can be written in each cycle?",
                                             std::numeric_limits<std::uint32_t>::max()};
    Param<Cycle> brain_dead_limit{
        "brain_dead_limit", "indicate brain dead after this number of cycles without a committed Uop", 100000};
    Param<bool> stash_only{"stash_only", "Allow core to service L3 cache requests, while doing nothing else", false};
    Param<unsigned> heartbeat{"heartbeat", "print out cycle and IPC stat every N instructions", 1000000};
    Param<bool> heartbeat_print_total{"heartbeat_print_total",
                                      "print out cycle and IPC stat every N instructions regardless of exception level",
                                      true};
    Param<bool> heartbeat_per_el{
        "heartbeat_per_el", "print out cycle and IPC stat every N instructions in every exception level", false};
    Param<std::uint64_t> max_instructions{"max_instructions",
                                          "maximum number of (committed) instructions to simulate *after warmup*",
                                          std::numeric_limits<std::uint64_t>::max()};
    Param<std::optional<std::uint64_t>> warmup_instructions{
        "warmup_instructions",
        "number of (committed) instructions to simulate *during warmup*, if not provided will warmup until "
        "START_MEASUREMENT is seen",
        {}};
    Param<std::uint64_t> max_warmup_instructions{"max_warmup_instructions",
                                                 "maximum number of (committed) instructions to simulate *during "
                                                 "warmup* (will exit warmup and measurement if past)",
                                                 std::numeric_limits<std::uint64_t>::max()};
    Param<bool> disable_translation_checker{
        "disable_translation_checker", "Silently ignore invalid MMU translations", false};
    Param<CycleDelta> fsu_commit_latency{
        "fsu_commit_latency", "minimum cycles between issue and commit for FSU X and Y uops", 8};
    Param<CycleDelta> fsu_commit_latency_z{
        "fsu_commit_latency_z", "minimum cycles between issue and commit for FSU Z uops", 5}; // Trinity and later
    Param<bool> enable_fsu_commit_delay{"enable_fsu_commit_delay", "enable delay from FSU writeback to commit", false};
    Param<bool> enable_lsu_commit_delay{"enable_lsu_commit_delay", "enable delay from LSU writeback to commit", false};
    Param<bool> enable_ixu_commit_delay{"enable_ixu_commit_delay", "enable delay from IXU writeback to commit", false};
    Param<Cycle> swob_drain_delay{
        "swob_drain_delay",
        "additional cycles beyond commit_freeze_cycles to wait after no-restart flush before commit resumes",
        9};
    Param<Cycle> nonspec_flush_delay{
        "nonspec_flush_delay", "cycles to stall before committing a uop which causes a non-speculative flush", 3};
    Param<bool> allow_partial_cleaving{
        "allow_partial_cleaving", "Allow ROB to see the first half of a cleaved pair without the second half", false};
    Param<uint64_t> phyaddroffset{"phyaddroffset", "Offset physical address after translation", 0};

    explicit GPC(const sc_core::sc_module_name& name);
    bool run() override;
    void credit_flush(Uop u);

    DriverPort driver_port;
    In<Uop> dispatch_port{"dispatch"};
    In<Uop> flush_req_from_idr{"flush_req_from_idr"};
    In<Uop> commit_port{"commit"};
    In<Uop> commit_delay_in_port{"commit_delay_in"};
    Out<Uop> commit_delay_out_port{"commit_delay_out"};
    Out<uint> loads_committed_port{"loads_retired"};
    Out<uint> stores_committed_port{"stores_retired"};
    Out<uint> branches_committed_port{"branches_retired"};
    Out<uint> instructions_committed_port{"intructions_retired"};
    Out<uint64_t> lsu_next_to_commit_port{"barrier_next_to_commit"};
    Out<Flush> flush_port{"flush_port"};
    Read<Flush> flush_read_port{"flush_read_port"};
    Out<Uop> idr_uop_return{"idr_uop_return"};
    sc_core::sc_vector<In<Uop>> mispredict_port{"mispredict", num_execution_units};
    sc_core::sc_vector<In<Uop>> mispredict_indirect_port{"mispredict_indirect", num_execution_units};
    In<Uop> raw_scrub_port{"raw_scrub"};
    Out<bool> rob_empty_out{"rob_empty_out"};

    Event ev_brain_dead{"brain_dead", "Simulation brain dead"};
    Event ev_commit_bypass{"commit_bypass_event", "bypass on this line"};

protected:
    void print_simulator_metrics(
        const char* phase, uint64_t cycles, uint64_t instructions, float khz, float kips, float vmsize);
    void end_of_simulation() override;
    void dump(std::ostream&) const override;

private:
    Event ev_start_measurement{"start_measurement", "Start measuring now"};
    Event ev_stop_measurement{"stop_measurement", "Stop measuring now"};
    Event ev_stop_max_instruction{"stop_max_instruction", "Maximum instruction count reached"};
    Event ev_sim_speed{"sim_speed", "Simulator speed"};
    Event ev_flush{"flush", "GPC starts to send flush"};
    Event ev_flush_broadcast{"flush_broadcast", "Flush signal received by all units"};
    Event ev_uop_commit{"uop_commit", "A Uop is committed"};
    Event ev_idiv_commit{"idiv_commit", "integer divide committed"};
    Event ev_fdiv_commit{"fdiv_commit", "floating point divide committed"};
    Event ev_fsqrt_commit{"fsqrt_commit", "floating point square-root committed"};
    Event ev_cleaved_uop_commit{"cleaved_uop_commit", "A Uop is committed"};
    Event ev_instruction_commit{"instruction_commit", "An Instruction is committed"};
    Event ev_looped_instruction_commit{"looped_instruction_commit",
                                       "An Instruction is committed that was fetched from loop detector"};
    Event ev_pmu{"pmu", "A PMU event has occurred"};
    Event ev_instruction_checker_error{"instruction_checker_error", "An instruction checker error occurred"};
    Event ev_cycle{"cycle", "Core executed 1 cycle"};
    Event ev_emulation{"emulation", "Emulation validation data"};
    EventData<std::uint64_t> emulation_divergences{"emulation_divergences", "Emulation divergences"};
    EventData<std::string> error_msg{"error_msg", "Error message"};
    EventData<std::uint64_t> cycle_event_data{"cycle_count", "Core clock cycle when event occurred"};
    EventData<std::uint64_t> commit_delay_data{"commit_pushout", "Cycles from last commit"};
    EventData<std::uint64_t> commit_pc_data{"commit_pc", "Program Counter of committed instruction"};
    EventData<std::uint64_t> flush_restart_pc_data{"flush_restart_pc", "Program counter to restart at after flush"};
    EventData<archx::inum_t> ev_inum{"inum", "Instruction ID"};
    EventData<bool> ev_emulated{"emulated", "Emulated correct path instruction"};

    EventData<std::uint64_t> ev_cpu_msec_total{"cpu_msec_total", "Total CPU time (milliseconds)"};
    EventData<std::uint64_t> ev_cpu_msec_warmup{"cpu_msec_warmup", "Warmup CPU time (milliseconds)"};
    EventData<std::uint64_t> ev_cpu_msec_measurement{"cpu_msec_measurement", "Measurement CPU time (milliseconds)"};
    EventData<std::uint64_t> ev_wall_msec_total{"wall_msec_total", "Total wall-clock  time (milliseconds)"};
    EventData<std::uint64_t> ev_wall_msec_warmup{"wall_msec_warmup", "Warmup wall-clock time (milliseconds)"};
    EventData<std::uint64_t> ev_wall_msec_measurement{"wall_msec_measurement",
                                                      "Measurement wall-clock time (milliseconds)"};

    EventData<bool> ev_predicated_off{"predicated_off", "Predicated off"};
    EventData<bool> ev_cleaved{"cleaved", "Second half of a cleaved uop"};

    EventData<PMU> ev_num{"event", "PMU event number"};
    EventData<FlushType> flush_type{"flush_type", "Reason for the flush"};
    EventData<unsigned> speed_hz_data{"speed_hz", "sim speed in Hz"};
    EventData<unsigned> speed_ips_data{"speed_ips", "sim speed in IPS"};
    EventData<unsigned> speed_hz_data_warmup{"speed_hz_warmup", "sim speed in Hz during warmup"};
    EventData<unsigned> speed_ips_data_warmup{"speed_ips_warmup", "sim speed in IPS during warmup"};
    Event ev_hint_0x54_commit{"hint_0x54_commit", "When a HINT #0x54 instruction commits."};

    EventData<uint64_t> ev_robid{"robid", "The uop's ROB entry"};
    EventData<bool> ev_warmup{"warmup", "warmup instruction or not"};
    EventData<unsigned> ev_el{"exception_level", "exception level for each committed instruction"};

    void dispatch();
    bool allowed_to_commit() const { return get_cycle() >= commit_freeze_ends; }
    bool ready_to_commit(const Uop& uop) const;
    void commit(uint width, bool bypass = false);
    void report();
    void commit_uop(const Uop& uop);
    void commit_instruction(const Instruction& instruction);
    Uop process_mispredicts();
    Uop process_raw_scrubs();
    std::pair<Uop, FlushType> select_older_flusher(const Uop& mispredict, const Uop& raw_scrub);
    void generate_flush(const Uop& uop, FlushType type, bool pre_commit, bool post_commit);
    void flush(std::uint64_t alloc_id);
    void flush_uop(const Uop& uop);
    void check_for_braindeath();

    void start_measurement(const Instruction& at_instruction = nullptr);
    void stop_measurement(Event* stop_event = nullptr);
    void simulation_control(const Instruction& instruction, bool exclude_start = false);
    void uop_commit_checker(const Uop& uop);
    void instruction_commit_checker(const Instruction& instruction);
    void architectural_commit_pmus(const Instruction& instruction);
    void architectural_commit_branch_pmus(const Instruction& instruction);
    void print_heartbeat(const std::string& phase, std::uint64_t instrs);
    unsigned long get_vmsize();

    std::deque<Uop> rob;

    std::optional<uint64_t> flush_id{};
    Instruction last_committed_instr;
    uint64_t instrs_since_last_branch{0};
    Cycle warmup_cycles{0};
    Cycle last_commit_cycle{0};
    Cycle last_instr_commit_cycle{0};
    Cycle commit_freeze_ends{0};
    Cycle last_heartbeat_cycle{0};
    Uop last_committed_uop{nullptr};
    uint64_t last_committed_robid{0};
    uint64_t uops_committed{0};
    uint uops_retired_this_cycle{0};
    uint64_t last_lsu_uop_id{0};
    bool rob_was_empty{false};
    Uop idr_flush_uop{nullptr};
    bool idr_requested_flush{false};
    int swob_drain_timer{0};
    bool stopped{false};

    bool warmup_state{true};
    archx::EL curr_el{archx::EL::EL0};
    std::optional<Cycle> start_measurement_cycle;
    std::optional<std::uint64_t> warmup_instrs_retired;
    std::optional<std::uint64_t> warmup_instrs_retired_el[archx::EL::EL3 + 1];
    std::optional<std::uint64_t> warmup_uops_retired_el[archx::EL::EL3 + 1];
    std::optional<std::uint64_t> instrs_retired;
    std::optional<std::uint64_t> instrs_retired_el[archx::EL::EL3 + 1];
    std::optional<std::uint64_t> last_heartbeat_instrs_retired_el[archx::EL::EL3 + 1];
    std::optional<std::uint64_t> uops_retired_el[archx::EL::EL3 + 1];
    std::optional<std::uint64_t> last_heartbeat_uops_retired_el[archx::EL::EL3 + 1];
    std::optional<Cycle> cycles_el[archx::EL::EL3 + 1];
    std::optional<Cycle> last_heartbeat_cycles_el[archx::EL::EL3 + 1];
    std::optional<Cycle> warmup_cycles_el[archx::EL::EL3 + 1]{0};
    // cpu time does not count slurm suspend time.
    std::clock_t cpu_time_start[2];
    std::chrono::steady_clock::time_point wall_time_start[2];
    float vmsize_warmup{0};
};

} // namespace ampere::core

/*
 * vim: sw=4:ts=4:et:si:cindent:cino=\:0,(0,g0,t0:
 */
