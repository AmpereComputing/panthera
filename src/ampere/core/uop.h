//
// Copyright (c) 2018-2023 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//

#pragma once

#include <fmt/format.h>
#include <fmt/ostream.h>
#include <fmt/ranges.h>

#include <climits>
#include <cstdint>
#include <stdexcept>
#include <utility>
#include <vector>
#include <optional>
#include <array>
#include <memory>

#include "ampere/archx/addr.h"
#include "ampere/archx/register.h"
#include "ampere/archx/uop.h"
#include "ampere/archx/flush_type.h"
#include "ampere/archx/bitfield.h"
#include "ampere/archx/predication.h"
#include "ampere/archx/uop_info.h"
#include "ampere/archx/vector.h"
#include "ampere/sc/transaction.h"
#include "ampere/sc/cycle.h"
#include "ampere/core/instruction.h"
#include "ampere/sc/credit.h"
#include "ampere/core/gather.h"
#include "ampere/core/ixu_defs.h"
#include "ampere/core/early_exit.enum.h"

const size_t UOP_MAX_SRCS = 4;
const size_t UOP_MAX_DSTS = 3;

namespace ampere::core {

using sc::Credit;

using phys_reg_t = std::uint16_t;

struct phys_regs_t
{
    std::uint16_t count{0};
    std::array<phys_reg_t, UOP_MAX_DSTS> pregs{};
    std::array<bool, UOP_MAX_DSTS> ixu{};

    void insert(phys_reg_t preg, bool is_ixu)
    {
        pregs[count] = preg;
        ixu[count] = is_ixu;
        ++count;
    }
};

struct pipe_enables_t
{
    bool a_uops_on_j_pipe{false};
    bool b_uops_on_j_pipe{false};
    bool ab_uops_on_j_pipe{false};
    bool a_uops_on_k_pipe{false};
    bool b_uops_on_k_pipe{false};
    bool ab_uops_on_k_pipe{false};
    bool loads_on_e_pipe{false};
    bool stores_on_f_pipe{false};
    bool load_pair_c_only{false};
    bool u_pipe_enable{false};
    bool v_pipe_enable{false};
    bool w_pipe_enable{false};
    bool x_pipe_by_enable{true};
    bool y_pipe_by_enable{true};
};

constexpr unsigned SUS_ISSUE_A = 1;
constexpr unsigned SUS_ISSUE_D1 = 2;
constexpr unsigned SUS_ISSUE_D2 = 4;
constexpr unsigned SUPER_SUS_PAIR_ISSUE_ALL = 7;
constexpr unsigned SUS_ISSUE_ALL = 3;

class UopObj : public ampere::sc::Transaction
{
    void common_init(pipe_enables_t pipe_enables)
    {
        // populate some fields from the archx_uop.
        reg_src[0] = archx_uop->src1;
        reg_src[1] = archx_uop->src2;
        reg_src[2] = archx_uop->src3;

        // SVE for FSU governing predicate is in pred. IXU and LSU use src3.
        // Access the governing predicate using governing_predicate_reg()
        reg_src[3] = archx_uop->pred;

        reg_dst = archx_uop->dst;
        reg_dst2 = archx_uop->dst2;
#if 0 // DEBUGGING
        if (is_sve() && is_predicated()) {
            const auto preg = governing_predicate_reg();
            const auto pval = pred(preg);
            fmt::print("SVE A64: {} ==> UOP: {} --- PREG: {} PVAL: {:04x}\n",
                       a64_disasm(), uop_disasm(), preg, pval);
        }
#endif

        auto pipes = archx_uop->info->allowed_pipes;

        bool is_a = bool(pipes & archx::Uoppipe::A);
        bool is_b = bool(pipes & archx::Uoppipe::B);
        bool is_ab = is_a && is_b;
        if ((pipe_enables.a_uops_on_k_pipe && is_a) || (pipe_enables.b_uops_on_k_pipe && is_b)
            || (pipe_enables.ab_uops_on_k_pipe && is_ab))
            pipes |= static_cast<int>(archx::Uoppipe::K);

        if ((pipe_enables.a_uops_on_j_pipe && is_a) || (pipe_enables.b_uops_on_j_pipe && is_b)
            || (pipe_enables.ab_uops_on_j_pipe && is_ab))
            pipes |= static_cast<int>(archx::Uoppipe::J);

        bool is_c = bool(pipes & archx::Uoppipe::C);
        bool is_d = bool(pipes & archx::Uoppipe::D);
        if (pipe_enables.loads_on_e_pipe && is_c)
            if (!archx_uop->info->pair || !pipe_enables.load_pair_c_only)
                pipes |= static_cast<int>(archx::Uoppipe::E);
        if (pipe_enables.stores_on_f_pipe && is_d)
            pipes |= static_cast<int>(archx::Uoppipe::F);

        if (archx_uop->info->is_fsu_uvw) {
            if (pipe_enables.u_pipe_enable)
                pipes |= static_cast<int>(archx::Uoppipe::U);
            else
                pipes &= ~static_cast<int>(archx::Uoppipe::U);
            if (pipe_enables.w_pipe_enable)
                pipes |= static_cast<int>(archx::Uoppipe::W);
            else
                pipes &= ~static_cast<int>(archx::Uoppipe::W);
            if (!pipe_enables.x_pipe_by_enable)
                pipes &= ~static_cast<int>(archx::Uoppipe::X);
            if (!pipe_enables.y_pipe_by_enable)
                pipes &= ~static_cast<int>(archx::Uoppipe::Y);
        }
        if (archx_uop->info->is_fsu_v) {
            if (pipe_enables.v_pipe_enable)
                pipes |= static_cast<int>(archx::Uoppipe::V);
            else
                pipes &= ~static_cast<int>(archx::Uoppipe::V);
        }

        resolved_allowed_pipes = static_cast<archx::Uoppipe>(pipes);
    }

public:
    bool received_load_data{false};

    UopObj(std::unique_ptr<archx::Uop> uop, const Instruction& i, pipe_enables_t pipe_enables = {})
        : archx_uop(std::move(uop)),
          instructions{i},
          barrier_type{archx_uop->info->barrier_type},
          looping(calc_is_looping())
    {
        common_init(pipe_enables);
    }

    UopObj(std::unique_ptr<archx::Uop> uop, const std::vector<Instruction>& i, pipe_enables_t pipe_enables = {})
        : archx_uop(std::move(uop)),
          instructions{i},
          barrier_type{archx_uop->info->barrier_type},
          looping(calc_is_looping())
    {
        common_init(pipe_enables);
    }

    archx::LReg governing_predicate_reg() const
    {
        archx::LReg preg = archx::LReg::INVALID;
        if (is_sve() && is_predicated()) {
            if (archx_uop->is_fsu()) {
                preg = archx_uop->pred; // FSU
            } else {
                preg = archx_uop->src3; // IXU/LSU
            }
        }
        return preg;
    }

    std::unique_ptr<archx::Uop> archx_uop;

    long get_imm() const
    {
        long imm = archx_uop->imm;
        if (archx_uop->imm_signext) {
            unsigned long const sign_mask = 1UL << (archx_uop->imm_width - 1);
            imm = imm & ((1UL << archx_uop->imm_width) - 1);
            imm = (imm ^ sign_mask) - sign_mask;
        }

        AMPERE_ASSERT(!(archx_uop->imm_shift && archx_uop->info->offset_lsl),
                      "Uop {} has both imm_shift and offset_lsl",
                      archx_uop->opcode);
        imm <<= archx_uop->imm_shift;
        imm <<= archx_uop->info->offset_lsl;

        return imm;
    }

    int get_imm_bits() const
    {
        long imm = get_imm();
        auto size_of_imm = 0;
        if (imm < 0) {
            imm = imm * (-1);
            // if imm is power of two and negative (-128 can be represented in 7bits not 8bits), decrement the size
            if (imm && !(imm & (imm - 1)))
                size_of_imm = -1;
        }

        constexpr auto size_of_long = sizeof(long) * CHAR_BIT;
        auto num_of_leading_zeros_in_imm = __builtin_clzl(imm);
        size_of_imm += (size_of_long - num_of_leading_zeros_in_imm);
        return size_of_imm;
    }
    bool calc_is_looping() const;

    bool is_producer() const { return !archx::invalid_reg(reg_dst); }

    bool has_dest2() const { return !archx::invalid_reg(reg_dst2); }

    unsigned int_dests() const { return (archx::is_integer(reg_dst) ? 1 : 0) + (archx::is_integer(reg_dst2) ? 1 : 0); }
    unsigned vec_dests() const { return (archx::is_vector(reg_dst) ? 1 : 0) + (archx::is_vector(reg_dst2) ? 1 : 0); }

    bool is_flag_producer() const { return archx_uop->produce_flags; };
    bool is_temp_flags_producer() const { return archx_uop->produce_tmpflags; };

    bool is_stack_source() const
    {
        for (uint64_t i = 0; i < UOP_MAX_SRCS; i++)
            if (reg_src[i] == archx::LReg::SP)
                return true;
        return false;
    }

    bool is_ixu() const { return archx_uop->is_ixu(); }

    bool is_lsu() const { return archx_uop->is_lsu(); }

    bool is_fsu() const { return archx_uop->is_fsu(); }

    bool writes_vreg() const { return archx::is_vector(reg_dst) || archx::is_vector(reg_dst2); }
    bool writes_ixu_reg() const
    {
        return archx::is_integer(reg_dst) || archx::is_flag(reg_dst) || archx::is_predicate(reg_dst)
               || archx::is_integer(reg_dst2) || archx::is_flag(reg_dst2) || archx::is_predicate(reg_dst2);
    }

    archx::Uopunit unit() const { return archx_uop->unit(); }

    bool is_load() const { return unit() == archx::Uopunit::Load; }
    bool is_store_with_dest() const { return archx_uop->is_store_with_dest(); }
    bool is_load_fsu() const { return is_load() && (is_vector(reg_dst) || is_vector(reg_dst2)); }

    bool is_ixu_store() const { return unit() == archx::Uopunit::Store; }

    bool is_fsu_store() const { return (is_fsu_store_low() || is_fsu_store_high()); }

    bool is_store() const { return is_ixu_store() || is_fsu_store(); }

    archx::Uoppipe allowed_pipes() const { return resolved_allowed_pipes; }

    bool is_pipe_ab() const { return allowed_pipes() & (archx::Uoppipe::A | archx::Uoppipe::B | archx::Uoppipe::BS); }

    bool is_pipe_cd() const
    {
        return allowed_pipes() & (archx::Uoppipe::C | archx::Uoppipe::D | archx::Uoppipe::E | archx::Uoppipe::F);
    }

    bool is_pipe_xyz() const { return allowed_pipes() & (archx::Uoppipe::X | archx::Uoppipe::Y | archx::Uoppipe::Z); }

    bool is_pipe_uvw() const { return allowed_pipes() & (archx::Uoppipe::U | archx::Uoppipe::V | archx::Uoppipe::W); }

    bool is_pipe_fsu() const { return is_pipe_xyz() || is_pipe_uvw(); }

    bool is_Aonly() const { return allowed_pipes() == archx::Uoppipe::A; }

    bool is_Bonly() const { return (allowed_pipes() == archx::Uoppipe::B) || (allowed_pipes() == archx::Uoppipe::BS); }

    bool is_Xonly() const { return (allowed_pipes() == archx::Uoppipe::X); }

    bool is_Yonly() const { return (allowed_pipes() == archx::Uoppipe::Y); }

    bool is_Zonly() const { return (allowed_pipes() == archx::Uoppipe::Z); }

    // IDR modifies fsu unit for recent_crypto mode for some uops
    std::optional<archx::FsuUnit> refined_fsu_unit{std::nullopt};
    void update_fsu_unit(archx::FsuUnit new_unit) { refined_fsu_unit = new_unit; }
    archx::FsuUnit fsu_unit() const
    {
        if (refined_fsu_unit.has_value())
            return refined_fsu_unit.value();
        return archx_uop->info->fsu_unit;
    }
    archx::FsuUnit fsu_unit_producing() const
    {
        // For the recent-crypto optimization, we use the same fsu unit for producing/consuming
        if (refined_fsu_unit.has_value())
            return refined_fsu_unit.value();
        return archx_uop->info->fsu_unit_producing;
    }
    bool is_crypto() const { return fsu_unit() == FsuUnit::CU; }

    bool is_store_address() const;
    bool is_store_data() const;
    bool is_ixu_store_data() const;
    bool is_ixu_store_high() const;
    bool is_fsu_store_high() const;
    bool is_fsu_store_low() const;
    bool is_store_data_high() const;
    bool is_single_uop_store() const;
    bool is_stz() const;
    bool is_super_sus() const;      // 3 sources (any kind, 2A+1D or 1A+2D)
    bool is_super_sus_pair() const; // 3 sources with 2 data
    bool is_dataless_store() const;
    bool is_single_uop_store_pair() const; // could be STZ pair (1A+1D+implicit zero data)  *OR* super sus pair
    bool is_store_special() const;
    bool is_sw_prefetch() const;
    bool is_sw_prefetch_as_sta() const;
    bool is_store_prefetch() const;
    auto sw_prefetch_level() const { return archx_uop->info->prefetch_level; }
    bool is_nontemporal() const;
    bool is_barrier_dmb_st() const;
    bool is_barrier_dmb_all() const;
    bool is_barrier_dsb() const;
    bool is_store_barrier() const;
    bool is_atomic_load() const;
    bool is_atomic_store() const;
    bool is_exclusive_load() const;
    bool is_pre_dispatch_drain() const;
    bool is_post_dispatch_drain() const;
    bool is_post_dispatch_stall() const;
    bool is_self_sync_msr() const;
    bool is_isolated() const;
    bool is_mtspr() const;
    bool is_mfspr() const;

    bool is_predicated() const { return archx_uop->is_predicated(); }

    bool is_predicated_sampled() const { return archx_uop->is_predicated_sampled(); }

    bool requires_memory_tag_check() const;

private:
    bool idiv_early_exit{false};
    bool fdiv_early_exit{false};
    EarlyExitReason early_exit_reason{EarlyExitReason::Invalid};

public:
    std::uint64_t div_latency{0};

    EarlyExitReason get_early_exit_reason() const { return early_exit_reason; }
    void set_early_exit_reason(EarlyExitReason reason) { early_exit_reason = reason; }
    bool is_idiv_early_exit() const { return idiv_early_exit; }
    bool is_fdiv_early_exit() const { return fdiv_early_exit; }
    void set_idiv_early_exit(bool ee) { idiv_early_exit = ee; }
    void set_fdiv_early_exit(bool ee) { fdiv_early_exit = ee; }

    bool instr_has_exception() const { return ainst()->has_exception(); }

    const std::shared_ptr<archx::Instruction>& ainst() const { return instructions.front()->archx_instruction; }
    const archx::UopInfo* uop_info() const { return archx_uop->info; }
    std::array<uint64_t, 2> vfp(archx::LReg r) const { return ainst()->state().read_vfp(r); }
    std::uint64_t gpr(archx::LReg r) const { return ainst()->state().read(r); }
    std::int64_t gpri(archx::LReg r) const { return ainst()->state().read(r); }
    archx::pred_reg_t pred(archx::LReg r) const { return ainst()->state().read_pred(r); }

    bool is_sve() const { return ainst()->is_sve(); }

private:
    bool generated_L1_prefetch{false};

public:
    void set_generated_L1_prefetch() { generated_L1_prefetch = true; }
    bool get_generated_L1_prefetch() const { return generated_L1_prefetch; }

private:
    mutable std::optional<bool> predicated_off{std::nullopt};

public:
    bool is_predicated_off() const
    {
        if (!predicated_off.has_value()) {
            predicated_off = archx::is_predicated_off(instructions.front()->archx_instruction, archx_uop);
        }
        return predicated_off.value();
    }
    unsigned store_num_address_sources() const;
    unsigned store_num_data_sources() const;
    bool is_int_move_reg() const;
    bool is_int_move_zero() const;
    bool is_vec_move_reg() const;
    bool is_vec_move_zero() const;

    bool is_idiv() const { return uop_info()->is_idiv; }
    bool is_fdiv() const { return uop_info()->is_fdiv; }
    bool is_fsqrt() const { return uop_info()->is_fsqrt; }

    bool is_udiv() const;
    bool is_reverse_divide() const; // some SVE divs swap operand semantics
    bool is_scalar_idiv() const;
    bool is_scalar_udiv() const;
    bool is_scalar_idiv32() const;
    bool is_scalar_udiv32() const;
    bool is_scalar_sdiv32() const;
    bool is_vector_idiv() const;   // sve
    bool is_vector_idiv32() const; // sve
    bool is_vector_udiv() const;   // sve
    bool is_fdiv16() const;
    bool is_fdiv32() const;
    bool is_fdiv64() const;

    archx::ResourceType get_resource_type() const;
    std::optional<unsigned> resource_rate;
    void set_resource_rate(unsigned r)
    {
        AMPERE_ASSERT(!resource_rate.has_value(), "Not allowed to update resource rate times");
        resource_rate = r;
    }
    auto get_resource_rate() const { return resource_rate.value(); }
    unsigned get_resource_count() const;
    unsigned get_resource_count(unsigned max_count) const;

    bool is_pair() const { return archx_uop->info->pair; };
    bool is_atomic() const { return archx_uop->info->atomic; };
    bool is_exclusive() const { return archx_uop->info->exclusive; };
    bool is_acquire() const { return archx_uop->info->acquire; };
    bool is_release() const { return archx_uop->info->release; };
    bool is_ordered() const
    {
        return is_atomic() || is_exclusive() || is_acquire() || is_release() || is_store_barrier();
    }
    bool is_i2f() const { return archx_uop->info->is_i2f; };
    bool is_f2i() const { return archx_uop->info->is_f2i; }; // gpr dest only, not flags
    bool nep_dest_as_src() const { return archx_uop->info->nep_dest_as_src; };
    bool is_dc_zva() const;
    bool is_dc_va() const;

    bool is_double_pump() const { return archx_uop->info->is_double_pump; };
    unsigned issue_count{0};        // for double pumped ops
    unsigned execute_done_count{0}; // for double pumped ops, we need to avoid writing back if not the last issue
    void set_issued() { issue_count++; }
    void reset_issued()
    { // For when a double-pumped uop is canceled by a load
        issue_count = 0;
    }
    bool first_issue_of_double_pumped() const { return issue_count == 0; }
    bool secondary_issue_of_double_pumped() const { return issue_count > 0; }
    void execute_done() { execute_done_count++; }
    phys_regs_t ixu_wr_pregs; // written physical regs
    std::array<phys_reg_t, UOP_MAX_SRCS> cancel_src_pregs{};
    std::array<sc::Cycle, UOP_MAX_SRCS> cancel_src_time{};
    bool sta_issuing{false};

    bool should_wake_consumers() const
    {
        // Double pumped FSU uops only wake up consumers on their 2nd execution.
        // Everything else wakes up consumers on first (and only) execution.
        if (is_double_pump() && execute_done_count < 2) {
            return false;
        }
        if (is_i2f() && sch.pipe_type != Pipe::Y) {
            return false;
        }
        return true;
    }
    bool needs_writeback() const
    {
        return !archx::invalid_reg(reg_dst) || !archx::invalid_reg(reg_dst2) || is_flag_producer()
               || is_temp_flags_producer();
    }

    bool is_fpu_gpr_x_to_z() const { return uop_info()->is_fpu_gpr_x_to_z; }
    bool is_stdv_x_to_z() const { return uop_info()->is_stdv_x_to_z; }
    bool is_gather2() const { return uop_info()->is_gather2 && !wrong_path(); }
    bool is_gather4() const { return uop_info()->is_gather4 && !wrong_path(); }
    bool is_x_to_z() const { return is_stdv_x_to_z() || is_fpu_gpr_x_to_z(); }

    bool is_gather() const { return archx_uop->is_gather() && !wrong_path(); }
    bool is_scatter() const { return archx_uop->is_scatter() && !wrong_path(); }

    unsigned vector_element_size() const { return archx_uop->vector_element_size(); }

    unsigned op_count() const { return archx_uop->uop_op_count(); }

    MultipleMemopType multiple_memop() const
    { // for {ld,st}{2,3,4} and gather/scatter
        return archx_uop->multiple_memop();
    }

    unsigned sequence_number() const
    {                                        // for {ld,st}{2,3,4} and gather/scatter
        return archx_uop->sequence_number(); // return value is 0-based
    }

    bool is_executed_in_idr() const;

    bool is_older(std::uint64_t id) const { return (alloc_id < id); }
    bool is_younger(std::uint64_t id) const { return (alloc_id > id); }
    bool is_older(const ampere::sc::transaction_ptr<const UopObj>& uop) const { return is_older(uop->get_alloc_id()); }
    bool is_younger(const ampere::sc::transaction_ptr<const UopObj>& uop) const
    {
        return is_younger(uop->get_alloc_id());
    }

    bool is_single_uop_instr() const { return archx_uop->start_inst && archx_uop->end_inst; }
    bool same_instruction(ampere::sc::transaction_ptr<UopObj> uop) const { return pc() == uop->pc(); }
    bool canceled{false};
    void cancel(sc::Cycle arg_cancel_time, phys_reg_t src_preg, size_t src_idx)
    {
        cancel_src_pregs[src_idx] = src_preg;
        cancel_src_time[src_idx] = arg_cancel_time;
        canceled = true;
    }
    sc::Cycle will_cancel_at() const;

    void uncancel() { canceled = false; }
    void uncancel(size_t src_idx)
    {
        cancel_src_pregs[src_idx] = 0;
        cancel_src_time[src_idx] = 0;
        canceled = false;
    }
    bool will_cancel() const { return canceled; }

    bool could_mispredict() const;
    bool could_fault() const;

    bool fused_with_unconditional_branch{false};

    bool flushed{false};
    void set_flushed() { flushed = true; }
    bool was_flushed() const { return flushed; }
    Instruction flush_instruction() const { return instructions.back(); }
    Instruction branch_instruction() const
    {
        for (const auto& inst : instructions)
            if (inst->archx_instruction->is_branch())
                return inst;

        return nullptr;
    }

    bool is_branch() const;
    bool is_mispredicted_branch() const;
    bool is_looping() const { return looping; };

    auto raw_latency() const
    {
        // latency guaranteed to be > 0 by archx at build time
        return uop_info()->latency;
    }
    std::optional<unsigned> latency_computed;
    unsigned fsu_i2f_latency{0};

    bool has_latency() const { return latency_computed.has_value(); }
    void assign_latency(unsigned latency)
    {
        AMPERE_ASSERT(!latency_computed.has_value(), "Not allowed to update latency multiple times");
        AMPERE_ASSERT(latency < 1000, "Latency is very large. Assuming this is an error.");
        latency_computed = latency;
    }
    bool fsu_issue_of_i2f() const
    {
        // the 2nd issue of an i2f uop
        return is_i2f() && issue_count == 2;
    }
    unsigned latency() const
    {
        AMPERE_ASSERT(latency_computed.has_value(), "No value set for uop latency");

        if (fsu_issue_of_i2f()) {
            AMPERE_ASSERT(fsu_i2f_latency > 0, "FSU I2F latency must be > 0");
            return fsu_i2f_latency; // set in IXU::compute_base_latency()
        }
        return latency_computed.value();
    }

    bool is_bs_pipe() const;
    bool is_line_split() const;
    bool is_4k_split() const;
    bool is_slow_integer() const { return is_ixu() && latency() > 1; }

    std::optional<archx::FlushType> requires_flush() const;
    ProgramCounter pc() const { return instructions.front()->archx_instruction->program_counter(); }
    auto exception_level() const { return instructions.front()->exception_level(); }
    auto branch_pc() const
    {
        if (auto inst = branch_instruction(); inst)
            return inst->archx_instruction->program_counter();
        return instructions.back()->archx_instruction->program_counter();
    }

    // put the GatherProxyCredit in the common shared instruction that spawned the gather uops
    auto get_gather_proxy() { return instructions.back()->gather_proxy; }
    const auto& get_gather_proxy() const { return instructions.back()->gather_proxy; }

    void set_alloc_id(std::uint64_t _alloc_id) { alloc_id = _alloc_id; }
    std::uint64_t get_alloc_id() const { return alloc_id; }
    auto get_alloc_id_gather() const
    {
        if (is_gather())
            return get_gather_proxy()->get_alloc_id();

        throw std::logic_error("Called get_alloc_id_gather() for non-gather uop.");
    }
    auto get_alloc_id_general() const
    {
        if (is_gather())
            return get_alloc_id_gather();
        return get_alloc_id();
    }
    auto get_uopcode() const { return archx_uop->opcode; }
    auto a64_disasm() const { return ainst()->disasm(); }
    auto uop_disasm() const { return archx_uop->disasm(); }

    auto inum() const { return instructions.back()->inum(); }

    void get_fusion_opcodes(std::uint32_t* opcodes) const
    {
        unsigned ctr = 0;
        for (const auto& inst : instructions) {
            AMPERE_ASSERT(ctr < 2, "Can only fuse 2 instructions");
            opcodes[ctr++] = inst->archx_instruction->opcode();
        }
    }

    bool is_fused() const { return instructions.size() >= 2; }

private:
    // mutable because modified in const context
    mutable std::optional<bool> on_wrong_path;

public:
    bool wrong_path() const { return on_wrong_path.value_or(instructions.back()->wrong_path()); }

    // unique
    auto get_txid() const { return txid().value; }

    // sorta-unique value, common to all uops for this instr
    auto get_txid_parent() const { return instructions.back()->txid().value; }

    // Prefetch uops claim to have a size of 4 bytes, but hardware ignores it. Override to 1 byte to prevent splits.
    std::optional<unsigned> dynamic_mem_datasize;
    void set_dynamic_mem_datasize(unsigned bytes) { dynamic_mem_datasize = bytes; }
    unsigned mem_datasize() const
    {
        if (dynamic_mem_datasize.has_value())
            return dynamic_mem_datasize.value();
        if (is_sw_prefetch())
            return 1;
        return uop_info()->mem_datasize;
    }

    // resources
    uint64_t alloc_id{0};
    uint64_t rob_entry{0};

    Credit prfid;
    Credit prfid2;
    Credit flagsid;
    Credit robid;
    Credit schid;
    Credit lobid;
    Credit lubid;
    Credit sobid;
    Credit bobid;
    uint64_t lob_id_val{0};       // assign a lob id value in IDR to enable indexing into Gretch's DRT
    uint64_t lob_alloc_id_val{0}; // monotonic id value indicating order in program load stream (doesn't wrap)

    auto get_prfid_id() const
    {
        if (is_gather())
            return get_gather_proxy()->get_prfid_id();
        return prfid->get_id();
    }

    auto get_prfid2_id() const { return prfid2->get_id(); }

    auto get_flagsid_id() const { return flagsid->get_id(); }

    unsigned dsts{0};

    struct DecodeInfo
    {
        unsigned lane{0}; // which uop within the partition
        unsigned pre_swizzle_lane{0};
        unsigned partition{0};
        unsigned pre_swizzle_partition{0};
    } decode;

    struct : Scheduler
    {
        int entry{-1};                     // entry inside of the op-scheduler
        PipeType pipe_type{Pipe::INVALID}; // which of N pipes for issue

        auto& operator=(const Scheduler& s)
        {
            this->sched_type = s.sched_type;
            this->partition = s.partition;
            this->check_normalized();
            return *this;
        }

        unsigned pipe() const { return expand_pipe(pipe_type, partition); }
        unsigned lsu_scheduler_pipe() const
        {
            return (pipe_type == Pipe::E || pipe_type == Pipe::F) ? partition + 2 : partition;
        }
    } sch;

    void set_pipe_type(PipeType pipe) { sch.pipe_type = pipe; }
    auto get_pipe_type() const { return sch.pipe_type; }

    // single-uop-store will double fire from the scheduler
    struct
    {
        bool sta_issued{false};
        bool std_issued{false};
        bool written_back{false};
        bool split{false};
        bool sus_pair_data1_issued{false};

        // The following portion is NOT used by LSU.
        // One bit per piece of info that needs to issue.
        // SUS:             2 bits: A, D
        // STZ:             1 bit:  A
        // SuperSUSnotpair: 2 bits: A, D
        // SuperSUSpair:    3 bits: A, D1, D2
        unsigned to_issue_vec{0};
    } sus;

    bool sus_issue_done() const { return sus.to_issue_vec == 0; }

    // time when uop began execution
    sc::Cycle issue_cycle{0};

    bool complete{false};
    bool aborted{false};
    bool first_mem_uop{false};
    bool faulted{false};
    bool raw_scrubbed{false};
    bool is_eliminated_move{false};
    unsigned swob_occupancy{0};
    bool self_sync{false};
    bool cleaved_first_half{false};
    bool cleaved_second_half{false};
    FusionType fusion_type{FusionType::INVALID};
    unsigned uop_number{0};       // uop index within instruction
    unsigned instruction_uops{0}; // total number of uops in this uop's instruction

    // reg sources and destination
    archx::LReg reg_src[UOP_MAX_SRCS]{}; // init in constructor
    archx::LReg reg_dst{archx::LReg::INVALID};
    archx::LReg reg_dst2{archx::LReg::INVALID}; // second dest. for load pair
    uint64_t reg_psrc[UOP_MAX_SRCS]{0};

    std::vector<Instruction> instructions{nullptr};
    size_t instrs_since_last_branch{0}; // only populated if is_branch() == true

    std::optional<archx::VirtAddr> virt_addr;
    std::optional<archx::PhysAddr> phys_addr;
    uint64_t store_associated_sta_id{0};

    archx::BarrierType barrier_type;
    bool looping{false};
    archx::Uoppipe resolved_allowed_pipes{archx::Uoppipe::NONE};

    void fifo_clear();
};

using Uop = ampere::sc::transaction_ptr<UopObj>;
using ConstUop = ampere::sc::const_ptr<Uop>;

} // namespace ampere::core

/*
 * vim: sw=4:ts=4:et:si:cindent:cino=\:0,(0,g0,t0:
 */
