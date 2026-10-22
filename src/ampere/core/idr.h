//
// Copyright (c) 2018-2025 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//

#pragma once

#include <array>
#include <vector>
#include <utility>
#include <algorithm>
#include <unordered_map>
#include "ampere/archx/decoder.h"
#include "ampere/archx/driver.h"
#include "ampere/archx/pac_mode.h"
#include "ampere/archx/uop_info.h"
#include "ampere/core/flush.h"
#include "ampere/core/idr/throttling.h"
#include "ampere/core/instruction.h"
#include "ampere/core/ixu_defs.h"
#include "ampere/core/uop.h"
#include "ampere/sc/credit.h"
#include "ampere/sc/map.h"
#include "ampere/sc/pipeline.h"
#include "ampere/sc/sc.h"
#include "ampere/archx/trinity_decoder_mode.h"
#include "ampere/core/idr/assignment.h"
#include "ampere/core/idr/swizzle.h"
#include "ampere/core/idr/idr_align.enum.h"
#include "ampere/core/idr/idr_counter_balancing.h"
#include "ampere/core/idr/idr_defs.h"
#include "ampere/core/idr/idr_load_balancing.h"
#include "ampere/util/enum.h"
#include "ampere/sc/submodule.h"

make_ns_enum(ampere::core, IDRStoreType,
    STA,
    SUS
);

make_ns_enum(ampere::core, BufferResource,
    INT_RF,
    FP_RF,
    FLAG_RF,
    PRED_RF,
    GATHER_TBL,
    ROB,
    LOB,
    SOB,
    BOB
);

namespace ampere::core {

using namespace ampere::sc;

class IDR : public Module
{
public:
    archx::DriverPort driver_port;
    archx::PACMode pac_mode; // checks commited arch state in driver, used to set decoder mode
    Param<unsigned> fsu_partitions{"fsu_partitions", "Active number of FSU partitions <= MAX_FSU_PARTITIONS", 1};
    // Params for wider fsu By (not z) operations
    Param<bool> u_pipe_enable{"u_pipe_enable", "Fsu By not Z uops allowed on U pipe", false};
    Param<bool> v_pipe_enable{"v_pipe_enable", "Fsu By not Z uops with 1 src allowed on V pipe", false};
    Param<bool> w_pipe_enable{"w_pipe_enable", "Fsu By not Z uops allowed on W pipe", false};
    Param<bool> x_pipe_by_enable{"x_pipe_by_enable", "Fsu By not Z uops allowed on X pipe", true};
    Param<bool> y_pipe_by_enable{"y_pipe_by_enable", "Fsu By not Z uops allowed on Y pipe", true};
    Param<bool> loads_on_e_pipe{"loads_on_e_pipe", "Loads can execute on E pipe as well as C", false};

private:
    Param<bool> pac_ia{"pac_ia", "Support Pointer Authentication pac_ia, All ELx", false};
    Param<bool> pac_ib{"pac_ib", "Support Pointer Authentication pac_ib, All ELx", false};
    Param<bool> pac_da{"pac_da", "Support Pointer Authentication pac_da, All ELx", false};
    Param<bool> pac_db{"pac_db", "Support Pointer Authentication pac_db, All ELx", false};

    Param<size_t> uop_decode_width{"uop_decode_width", "max uops decoded per cycle", 4};
    Param<int> scheduler_write_ports{
        "scheduler_write_ports", "Max number of uops that can be written to a scheduler in one cycle", 2};
    Param<bool> allow_split_one_triple{
        "allow_split_one_triple", "Allow IDR to grab 1 uop from triples (3 uop flows)", false};

    Param<unsigned> inst_read_width{"inst_read_width", "max number on instructions the idr can read per cycle", 5};
    Param<bool> fusion_enabled{"fusion_enabled", "Support fusion", true};
    Param<bool> fusion_ignore_unknown{"fusion_ignore_unknown", "Ignore any unknown fusions, don't assert", false};
    Param<bool> fusion_other{"fusion_other", "Allow fusion for unsupported fusions in idr", false};
    Param<bool> fusion_skip_checks{"fusion_skip_checks", "Skip additional furiosa fusion checks", false};
    Param<bool> fusion_mov_mov{"fusion_mov_mov", "Fuse mov mov instructions", true};
    Param<bool> fusion_mov_mov_trinity{"fusion_mov_mov_trinity", "Fuse additional mov mov instructions", false};
    Param<bool> fusion_bc{"fusion_bc", "Fuse flag producer branch consumer instructions", true};
    Param<bool> fusion_aes{"fusion_aes", "Fuse AES instructions", true};
    Param<bool> fusion_aes_delay_ripley{
        "fusion_aes_delay_ripley",
        "If last decoder contains first half of a common AES fusion pattern, delay it for a cycle",
        false};
    Param<bool> fusion_adr{"fusion_adr", "Fuse ADR instructions", true};
    Param<bool> fusion_direct_branch{
        "fusion_direct_branch", "Fuse direct branches to almost any prior instruction", false};
    Param<bool> fusion_nop{"fusion_nop", "Fuse nops to almost any prior instruction", false};
    Param<bool> fusion_cbz{"fusion_cbz", "Fuse some ALU ops with CBZ/CBNZ", true};
    Param<bool> fusion_addp1{"fusion_addp1", "Fuse ADD+ADD+1 and SUB-SUB-1", true};
    Param<bool> fusion_addsub_imm24{"fusion_addsub_imm24", "Fuse ADD/SUB of a 24-bit immediate", true};
    Param<bool> fusion_addssubs_cset{"fusion_addssubs_cset", "Fuse ADDS/SUBS and CSET", true};
    Param<bool> fusion_delay_enable{
        "fusion_delay_enable",
        "Enable delayed decode of an instruction in the last decode lane which is likely to fuse",
        true};
    Param<bool> fusion_movprfx_enable{"fusion_movprfx_enable", "Enable SVE movprfx fusion", true};
    Param<bool> fusion_div_msub_enable{"fusion_div_msub_enable", "Enable DIV+MSUB = MOD fusion", false};
    Param<bool> improved_flow_tda_connor{
        "improved_flow_tda_connor",
        "Account frontend stall cycles as STALL_FRONTEND_FLUSH during static window following flush",
        false};
    Param<bool> improved_flow_tda_alita{
        "improved_flow_tda_alita",
        "Account BTC, BTB clear cycles towards STALL_FRONTEND_FLOW in preference of any potential MEMBOUND cycles that "
        "haven't yet missed the tlb/cache (IL1 fetch latency)",
        false};
    Param<int> btc_clear_flush_width{"btc_clear_flush_width", "Number of pipe stages cleared by a BTC clear", 4};
    Param<int> btb_clear_flush_width{"btb_clear_flush_width", "Number of pipe stages cleared by a BTB clear", 5};

    Param<bool> jit_scheduler{"jit_scheduler",
                              "Always assign uop to a scheduler that has free resources.  This is meant to be used "
                              "with ixu jit schduler option.",
                              false};
    Param<bool> smart_least_loaded{"smart_least_loaded",
                                   "Take into account schedulers already assigned this cycle when picking the "
                                   "least-loaded scheduler. Intended for use when pipe_balancing is disabled.",
                                   false};
    Param<bool> pipe_balancing{"pipe_balancing", "Attempt to balance pipelines by looking at scheduler credits", false};
    Param<bool> simple_default_pipes{
        "simple_default_pipes",
        "Use simpler and more consistent pipe assignment when the choice of pipe doesn't matter",
        false};
    Param<bool> execute_idr_uops{"execute_idr_uops", "Send IDR-executed uops to IXU", true};
    Param<bool> swob_drain_timeout_enable{"swob_drain_timeout_enable", "Enable timeout drain of the SWOB buffer", true};
    Param<int> swob_drain_timeout{"swob_drain_timeout", "Max number cycles a write to SPR uop can be in buffer", 256};
    Param<unsigned> swob_size{"swob_size", "Number of entries in SWOB buffer", 16};
    Param<bool> swob_raw_cam_check_enable{
        "swob_raw_cam_check_enable",
        "Enable checking MRS at dispatch against SWOB buffer entries to conditionally trigger RAW flushes",
        false};
    Param<bool> improved_spr_behavior{
        "improved_spr_behavior",
        "Improved SPR behavior. Avoid unnecessary forced post-stall, pre-stall, CSF and other serializations",
        false};
    Param<bool> detailed_stats_collection{"detailed_stats_collection", "Occupancy data for design and power", false};
    Param<bool> decode_stats_collection{
        "decode_stats_collection",
        "Instruction decode, uop count, and uopcode frequency stats -- use with dump_at_end",
        false};
    Param<unsigned> recent_crypto_window{
        "recent_crypto_window",
        "Use crypto-friendly versions of instructions if we have seen crypto within this many decode groups",
        32};
    Param<bool> fast_crypto_bypass{"fast_crypto_bypass", "Use crypto-friendly versions of uops when possible", false};

    Param<bool> enable_move_elim_int_reg{
        "enable_move_elim_int_reg", "Execute integer register-register move uops directly in rename", false};
    Param<bool> enable_move_elim_int_zero{
        "enable_move_elim_int_zero", "Execute integer move of zero directly in rename", false};
    Param<bool> enable_move_elim_vec_reg{
        "enable_move_elim_vec_reg", "Execute vector register-register move uops directly in rename", false};
    Param<uint64_t> move_elim_max_counter{
        "move_elim_max_counter",
        "Move elimination: the maximum number of logical registers that can be pointing to the same physical register",
        2};
    Param<bool> move_elim_same_group{
        "move_elim_same_group", "Rename resolves inter-group dependencies to detect moves", false};
    Param<unsigned> align_branch_limit{"align_branch_limit", "Maximum number of branches allowed in a uop group", 2};
    Param<unsigned> align_SIR_limit{
        "align_SIR_limit", "Maximum number of different cache lines allowed in a uop group", 2};
    Param<unsigned> align_fusion_limit_2uop{
        "align_fusion_limit_2uop", "Cannot decode a 2-uop instruction after this many fusions", 2};
    Param<unsigned> align_fusion_limit_long{
        "align_fusion_limit_long", "Cannot decode a >=3-uop instruction after this many fusions", 2};
    Param<bool> fdiv_ping_pong{
        "fdiv_ping_pong",
        "Override all other pipe assignment for FP divider uops, and just alternate between pipes",
        false};
    Param<bool> split_double_uop{
        "split_double_uop", "Allow alignment to split double uop instructions across groups", false};
    Param<bool> tmp_flag_enable{
        "tmp_flag_enable", "FLGT0 flag temp enable. For SVE and FEAT_CSSC. Trinity onwards", false};
    Param<uint64_t> fsu_schedule_per_cycle{
        "fsu_schedule_per_cycle", "The number of uops the FSU X/Y schedulers can schedule per cycle", 2};
    Param<uint64_t> random_jitter{
        "random_jitter", "If non-zero, add tiny random stalls and use this vale as the random seed", 0};
    Param<bool> enable_NEP{"enable_NEP", "FEAT_AFP NEP. false=off, true=dest as extra src for unary ops", false};
    Param<bool> bob_stall_is_backend_stall{
        "bob_stall_is_backend_stall",
        "Controls if IDR_STALL_BOB_ID counts as Frontend Stall (Furiosa/Ripley RTL) or Backend Stall (Trinity onwards)",
        false};
    Param<bool> enable_per_cycle_pmu{"enable_per_cycle_pmu", "Output the PERCYC events (slows down panthera)", false};
    Param<uint64_t> stall_frontend_flush_latency{
        "stall_frontend_flush_latency",
        "The number of cycles following a flush for frontend stalls get counted as STALL_FRONTEND_FLUSH",
        6};
    Param<bool> dr03_iq_credit_return{
        "dr03_iq_credit_return", "Return IQ credits to ICF in DR03 instead of DR04", false};
    Param<bool> sched_credit_stall_bug{
        "sched_credit_stall_bug", "When backend stalled, wait for 2x the scheduler credits needed", true};

    Param<bool> cleaving_enable_external{
        "cleaving_enable_external", "Enable cleaving of two single-uop instructions", false};
    Param<bool> cleaving_enable_internal{
        "cleaving_enable_internal", "Enable cleaving of uops within an instruction, when XLAT says they cleave", true};
    Param<int> cleaving_fsu_limit{
        "cleaving_fsu_limit", "Maximum number of FSU uops which can be part of a cleaved pair", 0};
    Param<bool> cleaving_allow_super_sus{
        "cleaving_allow_super_sus", "Allow super-SUS to cleave with add from same instruction", false};
    Param<bool> cleaving_allow_fused{
        "cleaving_allow_fused", "Allow one half of a cleaved pair to be a fused uop, aka clusion", false};
    Param<bool> cleaving_allow_move_first{
        "cleaving_allow_move_first", "Allow an eliminatable move to be the first half of a cleaved pair", false};
    Param<bool> cleaving_allow_move_second{
        "cleaving_allow_move_second", "Allow an eliminatable move to be the second half of a cleaved pair", false};
    Param<int> cleaving_max_srcs{
        "cleaving_max_srcs",
        "Maximum total number of valid source registers allowed between both uops of a cleaved pair",
        3};
    Param<bool> cleaving_stats_collection{"cleaving_stats_collection",
                                          "Enable collection of detailed stats on cleaved uops; use with dump_at_end",
                                          false};

    Param<bool> cleaving_allow_branch_first{
        "cleaving_allow_branch_first", "Allow branches to be the first half of a cleaved pair", false};
    Param<bool> cleaving_allow_fault_second{
        "cleaving_allow_fault_second", "Allow faultable uops to be the second half of a cleaved pair", false};
    Param<bool> cleaving_allow_two_vecdst{
        "cleaving_allow_two_vecdst", "Allow a cleaved pair to write two vector dests", false};
    Param<bool> cleaving_allow_two_flgdst{
        "cleaving_allow_two_flgdst", "Allow a cleaved pair to write two flag dests", false};
    Param<bool> cleaving_allow_same_sched{
        "cleaving_allow_same_sched", "Allow cleaving of two uops which require the same scheduler", false};
    Param<bool> cleaving_no_src_restrictions{
        "cleaving_no_src_restrictions", "Disable various cleaving rules about which sources can go where", false};

    // params for wider machine
    Param<bool> six_wide_dispatch{"six_wide_dispatch", "remove multi-uop decoder restriction", false};
    Param<bool> cross_pipe{"cross_pipe", "scheduling for 6 wide machine", false};
    Param<bool> a_uops_on_k_pipe{
        "a_uops_on_k_pipe", "Uops which can go to an A scheduler can also go to K scheduler", false};
    Param<bool> b_uops_on_k_pipe{
        "b_uops_on_k_pipe", "Uops which can go to a B scheduler can also go to K scheduler", false};
    Param<bool> ab_uops_on_k_pipe{
        "ab_uops_on_k_pipe", "Uops which can go to either A or B scheduler can also go to K scheduler", false};
    Param<bool> a_uops_on_j_pipe{
        "a_uops_on_j_pipe", "Uops which can go to an A scheduler can also go to J scheduler", false};
    Param<bool> b_uops_on_j_pipe{
        "b_uops_on_j_pipe", "Uops which can go to a B scheduler can also go to J scheduler", false};
    Param<bool> ab_uops_on_j_pipe{
        "ab_uops_on_j_pipe", "Uops which can go to either A or B scheduler can also go to J scheduler", false};
    Param<bool> load_pair_c_only{"load_pair_c_only", "Loads-pairs can execute on C pipe only", false};
    Param<bool> stores_on_f_pipe{"stores_on_f_pipe", "Stores can execute on F pipe as well as D", false};
    Param<bool> fsu_sch_bw_limit{"fsu_sch_bw_limit", "Stall next if we send too many uops to an FSU scheduler", true};

    Param<bool> profile_wrong_path{"profile_wrong_path", "Profile wrong path instructions", false};

    void tally_wrong_path_instr(const Instruction& inst)
    {
        auto adis_encoding = inst->archx_instruction->adis_info();
        wrong_path_histo[adis_encoding]++;
    }
    void dump_wrong_path_info(std::ostream& os) const
    {
        os << "BEGIN WRONG PATH HISTOGRAM\n";
        std::vector<std::pair<adis::encoding_enum_t, std::uint64_t>> elems(wrong_path_histo.begin(),
                                                                           wrong_path_histo.end());
        std::sort(
            elems.begin(), elems.end(), [](const auto& left, const auto& right) { return left.second < right.second; });
        for (auto& it : elems) {
            os << adis::encoding_name(it.first) << "," << it.second << "\n";
        }
        os << "END WRONG PATH HISTOGRAM\n";
    }
    std::unordered_map<adis::encoding_enum_t, std::uint64_t> wrong_path_histo;

    void stats_collection();

public:
    explicit IDR(const sc_core::sc_module_name& name);
    ~IDR() override;

    void start_of_simulation() override;
    bool run() override;

    In<Instruction> in{"in"};
    Out<Uop> out{"out"};
    Out<Uop> gpc_dispatch_port{"gpc_dispatch"};
    Out<Uop> flush_req_to_gpc{"flush_req_to_gpc"};
    Out<Uop> lsu_mem_rob_preview_port{"lsu_mem_rob_preview"};
    Out<Uop> bob_alloc{"bob_alloc"};
    In<bool> rob_empty_in{"rob_empty_in"};
    In<bool> lob_balance_in{"lob_balance_in"};

    In<Credit> lsu_credit_return{"lsu_credit_return"};
    In<Credit> ixu_credit_return{"ixu_credit_return"};
    In<Uop> rob_uop_return{"rob_uop_return"};
    In<Credit> bob_credit_return{"bob_credit_return"};

    In<std::vector<bool>> pmu_sched_stall_xu_busy{"pmu_sched_stall_xu_busy"};
    In<std::vector<bool>> pmu_sched_stall_ilock{"pmu_sched_stall_ilock"};

    Out<Credit> feq_credit_return{"feq_credit_return"};
    Out<Credit> instruction_credit_return{"instruction_credit_return"};
    Out<uint> frontend_stall{"frontend_stall_pipe"}; // frontend stall occurred this cycle

    Read<Flush> flush{"flush"};

    Read<bool> pmu_oldest_load_tlb_miss{"pmu_oldest_load_tlb_miss"};
    Read<bool> pmu_oldest_load_cache_miss{"pmu_oldest_load_cache_miss"};
    Read<bool> pmu_oldest_load_ofb_full{"pmu_oldest_load_ofb_full"};
    Read<bool> pmu_oldest_load_mem_miss{"pmu_oldest_load_mem_miss"};
    Read<bool> pmu_oldest_ldst_valid{"pmu_oldest_ldst_valid"};
    Read<bool> pmu_oldest_ldst_atomic{"pmu_oldest_ldst_atomic"};
    Read<bool> pmu_oldest_load_st_wait{"pmu_oldest_load_st_wait"};

    Read<bool> pmu_frontend_cache_miss{"pmu_frontend_cache_miss"};
    Read<bool> pmu_frontend_tlb_miss{"pmu_frontend_tlb_miss"};
    Read<bool> pmu_frontend_l2_miss{"pmu_frontend_l2_miss"};
    Read<bool> pmu_frontend_feq_empty{"pmu_frontend_feq_empty"};
    Read<bool> pmu_frontend_mem_stall{"pmu_frontend_mem_stall"};
    Read<bool> pmu_btc_clear{"pmu_btc_clear"};
    Read<bool> pmu_btb_clear{"pmu_btb_clear"};

    inline size_t lob_size() const { return lob.size; }
    inline size_t sob_size() const { return sob.size; }
    inline size_t rob_size() const { return rob.size; }
    std::vector<size_t> sch_size() const;

private:
    archx::DecoderMode get_decoder_mode() const
    {
        /* The decoder mode enumerations are generated by archx
           however there is the possiblity that different decoders can
           have new mode bits, different mode bits or rearranged mode
           bits. That means if we ever had multiple decoders with mode
           bits, we will need to support per-decoder mode bits. As we
           probe each registered decoder to see if it recognizes an
           opcode, we'll need to juggle/subset the logical mode bits
           for that decoder so that it gets only what it expects. */
        archx::DecoderMode mode = archx::trinity::DecoderModeDefault;
        if (pac_ia || pac_mode.pac_ia())
            mode |= archx::trinity::DecoderMode_Pac_Ia_Enabled_ON;
        if (pac_ib || pac_mode.pac_ib())
            mode |= archx::trinity::DecoderMode_Pac_Ib_Enabled_ON;
        if (pac_da || pac_mode.pac_da())
            mode |= archx::trinity::DecoderMode_Pac_Da_Enabled_ON;
        if (pac_db || pac_mode.pac_db())
            mode |= archx::trinity::DecoderMode_Pac_Db_Enabled_ON;

        return mode;
    }

    inline static const size_t RESOURCE_COUNT =
        NUM_SCHED + util::enum_size<SchedulerType>() + util::enum_size<BufferResource>();

    static const decltype(scheduler_enum()) scheduler_enumeration;
    static const util::FrozenEnumeration<unsigned> resource_enumeration;

    auto buffer_resource_id(BufferResource resource) const { return sch.size() + std::to_underlying(resource); }
    auto sch_bandwidth_resource_id(SchedulerType sched) const
    {
        return sch.size() + util::enum_size<BufferResource>() + std::to_underlying(sched);
    }

    static constexpr unsigned NUM_RF =
        std::to_underlying(BufferResource::PRED_RF) - std::to_underlying(BufferResource::INT_RF) + 1;
    const archx::PMU stall_backend_rename_pmus[NUM_RF] = {archx::PMU::STALL_BACKEND_RENAME_INT,
                                                          archx::PMU::STALL_BACKEND_RENAME_FP,
                                                          archx::PMU::STALL_BACKEND_RENAME_FLAG,
                                                          archx::PMU::STALL_BACKEND_RENAME_PRED};

    std::string get_resource_name(unsigned i) const
    {
        if (i < sch.size())
            return "Sch" + Scheduler::from_sched(i).extended_name();
        if (i < (sch.size() + util::enum_size<BufferResource>()))
            return std::string(
                util::enum_to_string(static_cast<BufferResource>(i - util::enum_size<BufferResource>())));
        return std::string(
            util::enum_to_string(static_cast<SchedulerType>(i - sch.size() + util::enum_size<BufferResource>())));
    }

    using ResourceVector = std::bitset<RESOURCE_COUNT>;

    class UopGroup : public DecodedUopGroup
    {
    public:
        explicit UopGroup(size_t decode_lanes) : DecodedUopGroup(decode_lanes) {}

        void assign_banks();
        void assign_pre_swizzle_banks();
        unsigned count_instructions() const;

        std::array<uint64_t, RESOURCE_COUNT> resource_count{};
        bool resource_count_complete{false};

    private:
        std::pair<int, int> lane_to_bank(unsigned lane) const;
    };

    // throttle bits
    size_t current_decode_width{0};

public:
    Submodule<IDRWidthThrottle, IDRWidthThrottleNone> width_throttle{
        "width_throttle",
        as_const_ref(ThrottlingProperties{.width = static_cast<unsigned>(*uop_decode_width), .sch = sch})};

private:
    Submodule<IDRSchedulerThrottle, IDRSchedulerThrottleNone> scheduler_throttle{
        "scheduler_throttle",
        as_const_ref(ThrottlingProperties{.width = static_cast<unsigned>(*uop_decode_width), .sch = sch})};

    // for detailed_stats_collection
    std::vector<uint64_t> resource_total, resource_maxed;

    // helper for align()
    void set_memop(const std::shared_ptr<ampere::archx::Instruction>& archx_instruction, Uop& uop, unsigned memop_idx);
    bool fusion_delay(unsigned instr_remaining);
    bool can_start_2uop_decode(unsigned decode_lane, unsigned fusions);
    bool can_start_3uop_decode(unsigned decode_lane, unsigned fusions);
    bool can_start_quad_decode(unsigned& decode_lane, unsigned fusions);
    bool get_more_uops(const Uop& prev_uop, unsigned instr_remaining, unsigned& decode_lane, unsigned& fusions);
    void collect_cleaving_stats(const Uop& uop1, const Uop& uop2);
    uint64_t cleaving_scheduler_vector(const Uop& uop) const;
    bool can_cleave(const Uop& ua, const Uop& ub) const;
    static bool is_aarch64_fp_or_sve(const Uop& uop);

    bool align(Pipeline<UopGroup>::Data uop_group);
    bool decode(Pipeline<UopGroup>::Data uop_group);
    bool rename(Pipeline<UopGroup>::Data uop_group);

    // support for tracking IQ-to-uop-group formation limits
    unsigned branch_count;
    unsigned SIR_count;
    ProgramCounter previous_pc{0x0_pc};
    static bool is_new_SIR(ProgramCounter pc, ProgramCounter prev_pc);
    bool align_limits_test(bool is_branch, const std::vector<Instruction>& instrs, bool emit);
    void align_limits_update(const Uop& uop);
    void init_align_limits();

    Pipeline<UopGroup> idr_pipe{"pipeline",
                                {
                                    AMPERE_PIPE_STAGE_M(IDR, align),
                                    AMPERE_PIPE_STAGE_M(IDR, decode),
                                    AMPERE_PIPE_STAGE_M(IDR, rename),
                                }};

    Out<Uop> idr_uop_delay_out{"idr_uop_delay_out"};
    In<Uop> idr_uop_delay_in{"idr_uop_delay_in"};

    Event ev_feq_dealloc{"feq_dealloc", "Dealloc Front End Queue entry"};
    Event ev_align{"align", "Align instructions for decode"};
    Event ev_uop_decode{"uop_decode", "Uop decode"};
    Event ev_uop_rename{"uop_rename", "Uop rename"};
    Event ev_uop_dispatch{"uop_dispatch", "Uop dispatches from IDR to IXU/FSU"};
    Event ev_frontend_stall{"frontend_stall", "Uop failed to dispatch due to no uops from front end"};
    Event ev_backend_stall{"backend_stall",
                           "Uop failed to dispatch due to backend unable to accept uops. Excludes pre-drain and "
                           "post-drain dispatch stalls (IDR_STALL_DRAIN)"};
    Event ev_fe_bw{"fe_bw", "Frontend BW defined as instructions/non-backend-stalled cycle"};
    Event ev_idr_uop{"idr_uop", "IDR encountered IDR-executed uop and marked it as complete"};
    Event ev_uop_store_type{"uop_store_type", "Uop store type"};
    Event ev_width_switch{"width_switch", "Switch in decode width"};
    Event ev_pmu{"pmu", "A PMU event has occurred"};
    Event ev_pmu_backend_busy{"pmu_backend_busy", "A backend busy PMU event has occurred"};
    Event ev_pmu_backend_ilock{"pmu_backend_ilock", "A backend ilock PMU event has occurred"};
    Event ev_wrong_path{"wrong_path", "A wrong path instruction decoded"};
    EventData<bool> ev_emulated{"emulated", "A wrong path instruction was emulated"};
    EventData<bool> ev_emulated_load{"emulated_load", "A wrong path load instruction was emulated"};
    EventData<bool> ev_emulated_store{"emulated_store", "A wrong path store instruction was emulated"};
    Event ev_elim_int_reg{"elim_int_reg", "Integer register-register move was eliminated"};
    Event ev_elim_int_zero{"elim_int_zero", "Integer register-zero move was eliminated"};
    Event ev_elim_vec_reg{"elim_vec_reg", "Vector register-register move was eliminated"};
    Event ev_elim_ctr_overflow{"elim_ctr_overflow", "Move could have been eliminated with a larger counter"};
    Event ev_swob_flush{"swob_flush", "SWOB drained by flush"};
    Event ev_crypto_bypass{"crypto_bypass", "Instruction redirected to FSU CU or BY due to recent crypto"};
    Event ev_align_limited{"align_limited", "Reason IDR align was limited"};
    Event ev_recovery_stall{"recovery_stall", "Uops were delayed at dispatch because pdest recovery was not done"};
    Event ev_resource_stall{"resource_stall", "Stalled due to resource"};
    Event ev_scheduler_stall{"scheduler_stall", "Stalled due to schedulers"};
    Event ev_scheduler_bandwidth_stall{"scheduler_bandwidth_stall", "Stalled due to scheduler bandwidth"};
    void emit_align_reason(IDRAlignReason r);

    Param<bool> resource_stats_detailed{"resource_stats_detailed", "Support detailed resource stats", false};

    EventData<std::string> uopcode_disasm{"uopcode_disasm", "Disassembly string for Uop"};
    EventData<std::string> uop_pipes{"uop_pipes", "Pipes a UOP is allowed to use"};
    EventData<uint64_t> alloc_id{"alloc_id", "Allocation (sequence) id"};
    EventData<decltype(resource_enumeration)> stall_cause_data{
        "backend_stall_cause", "Cause of the backend stall", resource_enumeration};
    EventData<int> ixu_partition_data{"ixu_partition", "Execution partition number"};
    EventData<FusionType> inst_fusion_data{"uop_fusion", "Fusion type"};
    EventData<unsigned> inst_count{"inst_count", "Number of instructions"};
    EventData<size_t> iq_occupancy{"iq_occupancy", "Number of instructions in the IQ"};
    EventData<size_t> iq_rightpath_occupancy{"iq_rightpath_occupancy",
                                             "Number of instructions in the IQ on the right path"};
    EventData<bool> uop_group_ld_balance{"ld_balance", "Load balance performed"};
    EventData<std::string> uop_group_swizzle_type{"swizzle_performed", "Swizzle Type performed"};
    EventData<IDRStoreType> store_type_data{"store_type", "Store type"};
    EventData<int> cleaving_data{"cleaved", "Uop is the second half of a cleaved pair"};
    EventData<std::string> lanes_start_data{"start_lanes", "Uops in lanes before balancing/swizzling"};
    EventData<std::string> lanes_middle_data{"after_load_balance", "Uops in lanes after balancing"};
    EventData<std::string> lanes_end_data{"end_lanes", "Uops in lanes after balancing/swizzling"};
    EventData<PMU> ev_num{"event", "PMU event number"};
    EventData<decltype(scheduler_enum())> ev_sch_num{"sch_num", "Scheduler number", scheduler_enumeration};
    EventData<SchedulerType> ev_sch_type_num{"sch_type_num", "Scheduler type"};
    EventData<BufferResource> ev_buffer_num{"buffer_num", "Buffer number"};
    EventData<size_t> ev_decode_width{"decode_width", "requested decode width"};

    LocalRandom random;
    std::deque<Instruction> instruction_fifo;
    std::deque<Uop> pending_uops;
    std::deque<Uop> swob;
    Cycle swob_oldest;

    CountedCreditPool rf_int{"rf_int"};
    CountedCreditPool rf_flag{"rf_flag"};
    CountedCreditPool rf_fp{"rf_fp"};
    CountedCreditPool rf_pred{"rf_pred"};
    CountedCreditPool gather_tbl{"gather_tbl"};
    CreditPool rob{"rob"};
    CreditPool lob{"lob"};
    CreditPool sob{"sob"};
    CreditPool bob{"bob"};

    const std::array<In<Credit>*, 3> credit_returns = {
        &ixu_credit_return,
        &lsu_credit_return,
        &bob_credit_return,
    };
    // Note: order must match order in BufferResource
    const std::array<CreditPool*, util::enum_size<BufferResource>()> buffer_resource_credit_pools = {
        &rf_int, &rf_fp, &rf_flag, &rf_pred, &gather_tbl, &rob, &lob, &sob, &bob};

    uint64_t lob_id_counter{0};       // counter to assign lob id values to uops to enable indexing into Gretch's DRT
    uint64_t lob_alloc_id_counter{0}; // monotonic lob allocation id

    sc_map<CreditPool> sch{"sch", scheduler_enumeration};

    uint64_t alloc_id_counter{0};
    uint64_t next_rob_entry{0};
    bool do_flush{false};
    Cycle flush_cycle{-1};
    Cycle btc_clear_cycle{0};
    Cycle btb_clear_cycle{0};
    Flush flush_data{nullptr};
    void handle_flush_requests();

    bool waiting_for_rob_empty{false};
    bool waiting_for_sob_empty{false};
    bool waiting_for_flush{false};
    bool rob_empty{true};
    int64_t barrier_reaches_rob{0};
    Cycle stall_recovery_end{0};
    Cycle stall_frontend_flush_end{0};
    int recent_crypto_counter{0};
    uint64_t jitter_counter{0};
    bool idr_no_restart{false};
    bool previously_backend_stalled{false};

    // IXU & FSU scheduler signals for PMUs
    std::vector<bool> pmu_stall_busy;
    std::vector<bool> pmu_stall_ilock;

    uint64_t spec_rf_table[util::enum_max_numeric_size<archx::LReg>()]{0};
    Credit committed_rf_table[util::enum_max_numeric_size<archx::LReg>()];
    std::deque<Uop> dispatched_uops;
    std::array<SchedulerType, MAX_FSU_PARTITIONS> next_div_scheduler;
    std::vector<int> sch_assigned_this_cycle;
    // decode_source mapped to opcode, uop count, decode count
    std::map<std::string, std::tuple<uint32_t, uint32_t, uint64_t>> decode_stats;
    std::map<archx::Uopcode, uint64_t> uopcode_stats;
    std::map<std::string, uint64_t> cleaving_stats;
    std::stringstream verbose_ss;

    bool check_for_resources{true};
    ResourceVector resource_vector;

    bool stall_idr_fsu{false};
    bool fsu_schedulers_free{true};
    // indicate which of Pipes X or Y is the stall source. Only valid when stall_idr_fsu == true.
    PipeType stall_idr_fsu_pipe{Pipe::X}; // initialize to something safe; var is never read uninitialized

    uint64_t sta_uop_id{0};

    IDRLoadBalancing load_balancing;
    Swizzle swizzle;
    IDRCounterBalancing counter_balancing;

    Submodule<IDRSchedulerAssignment, BinaryIDRSchedulerAssignment> int_scheduler_assignment{
        "int_scheduler_assignment",
        as_const_ref(AssignmentProperties{.width = static_cast<unsigned>(*uop_decode_width),
                                          .sch = sch,
                                          .uses_pre_swizzle_partition = false,
                                          .partitions = IXU_PARTITIONS})};
    Submodule<IDRSchedulerAssignment, BinaryIDRSchedulerAssignment> fp_scheduler_assignment{
        "fp_scheduler_assignment",
        as_const_ref(AssignmentProperties{.width = static_cast<unsigned>(*uop_decode_width),
                                          .sch = sch,
                                          .uses_pre_swizzle_partition = true,
                                          .partitions = fsu_partitions})};
    std::array<decltype(int_scheduler_assignment.get()), 2> assignments = {
        int_scheduler_assignment.get(),
        fp_scheduler_assignment.get(),
    };

    void modify_fsu_unit(const Uop& uop);
    void check_move_elimination(UopGroup& uop_row);
    void count_resources(UopGroup& uop_row);
    ResourceVector check_resources(UopGroup& uop_row);
    void record_stalls(UopGroup& uop_row, ResourceVector& resource_vector, std::size_t util, unsigned resource);
    unsigned stall_cause(const ResourceVector& resource_vector);
    void assign_resources(const Uop& uop);
    void iq_credit_return(UopGroup& uop_row);
    void dispatch(UopGroup& uop_row);
    void dispatch_pmu(UopGroup& uop_row); // pmu events for dispatch()
    bool schedule_jit(const UopGroup& uop_row);
    void assign_alloc_ids(UopGroup& uop_row);
    void reclaim_credits();
    std::string lanes_str(const UopGroup& uops) const;
    std::optional<Scheduler> pipe_balance(const Uop& uop);
    void cleaving_assign_pipes(const Uop& ua, const Uop& ub);
    void assign_pipes(const UopGroup& uop_group);
    void assign_sch(const Uop& uop, Scheduler sch);
    void assign_sch(const Uop& uop, SchedulerType sched_type, std::optional<unsigned> partition = {});
    void assign_schedulers(const std::vector<Uop>& uops, const std::vector<std::optional<Scheduler>>& schedulers);
    void assign_pipe(const Uop& uop);
    Scheduler least_loaded_sch(const std::vector<Scheduler>& eligible_sch, unsigned partition, int lane);

    bool can_fit_fused_uop();
    static bool check_div_msub_fuse(const Instruction& inst1, const Instruction& inst2);
    std::deque<Uop> create_uops(const std::vector<Instruction>& instrs, UopSet& archx_uops, FusionType fusion_type);
    std::deque<Uop> decode_instructions(unsigned max_instructions);

    static std::string disassemble_uop(const Uop& uop);
    std::string allowed_pipes(const Uop& uop) const;
    bool uop_goes_to_exec(const Uop& uop) const;
    void throttle_check();

    void check_self_sync(UopGroup& uop_row) const;
    bool update_swob(UopGroup& uop_group);
    void pmu_backend_events(Txn txn, const ResourceVector& resource);
    void pmu_events();

    void dump(std::ostream&) const override;
    void table_print(std::ostream&) const;
    void print_rf_entry(std::ostream&, int r, const char* prefix) const;
};

} // namespace ampere::core

/*
 * vim: sw=4:ts=4:et:si:cindent:cino=\:0,(0,g0,t0:
 */
