//
// Copyright (c) 2018-2023 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//

#include <algorithm>
#include <vector>
#include "ampere/archx/inst_info.h"
#include "ampere/archx/register.h"
#include "ampere/archx/uop_info.h"
#include "fmt/core.h"
#include "ampere/core/uop.h"

namespace ampere::core {

// #define DEBUG_PRED
#if defined(DEBUG_PRED)
#define DBG_PRED(format_str, ...) fmt::print(stderr, format_str "\n", ##__VA_ARGS__)
#else
#define DBG_PRED(format_str, ...)
#endif

bool UopObj::calc_is_looping() const
{
    for (const auto& inst : instructions) {
        if (inst->looping) {
            return true;
        }
    }
    return false;
}

bool UopObj::is_store_address() const
{
    return (archx_uop->info->store_type == archx::StoreType::SUS || archx_uop->info->store_type == archx::StoreType::STA
            || archx_uop->info->store_type == archx::StoreType::STZ
            || archx_uop->info->store_type == archx::StoreType::SPECIAL
            || archx_uop->info->store_type == archx::StoreType::PREFETCH);
}

bool UopObj::is_store_data() const
{
    return (is_ixu_store_data() || is_fsu_store());
}

bool UopObj::is_ixu_store_data() const
{
    return (archx_uop->info->store_type == archx::StoreType::STD || is_ixu_store_high());
}

bool UopObj::is_ixu_store_high() const
{
    return (archx_uop->info->store_type == archx::StoreType::STDHI);
}

bool UopObj::is_fsu_store_high() const
{
    return (archx_uop->info->store_type == archx::StoreType::STDVHI);
}

bool UopObj::is_fsu_store_low() const
{
    return (archx_uop->info->store_type == archx::StoreType::STDV);
}

bool UopObj::is_store_data_high() const
{
    return (is_ixu_store_high() || is_fsu_store_high());
}

unsigned UopObj::store_num_address_sources() const
{
    switch (archx_uop->info->store_type) {
    case archx::StoreType::SPECIAL:
        return 1;
    case archx::StoreType::STA:
        return 2;
    case archx::StoreType::STZ:
        if (is_pair())
            return 1; // STZ pair is one address, one data, one implicit zero
        else
            return 2; // STZ non-pair is two address, one implicit zero
    case archx::StoreType::SUS:
        if (is_super_sus() && !is_pair())
            return 2; // super-SUS (non-pair) has two address sources
        else
            return 1; // normal SUS (pair or not)
    default:
        AMPERE_ERROR("store_num_address_sources() called for uop with unexpected store type");
        return 0;
    }
}

unsigned UopObj::store_num_data_sources() const
{
    switch (archx_uop->info->store_type) {
    case archx::StoreType::SPECIAL:
    case archx::StoreType::STA:
        return 0;
    case archx::StoreType::STZ:
        if (is_pair())
            return 1;
        else
            return 0;
    case archx::StoreType::SUS:
        if (is_super_sus() && is_pair())
            return 2; // super-SUS pair has two data sources
        else
            return 1;
    default:
        AMPERE_ERROR("store_num_data_sources() called for uop with unexpected store type");
        return 0;
    }
}

bool UopObj::is_stz() const
{
    return (archx_uop->info->store_type == archx::StoreType::STZ);
}

bool UopObj::is_single_uop_store() const
{
    return (archx_uop->info->store_type == archx::StoreType::SUS || archx_uop->info->store_type == archx::StoreType::STZ
            || archx_uop->info->store_type == archx::StoreType::SPECIAL);
}

bool UopObj::is_super_sus() const // 3 sources (2 address + 1 data) OR (1 address + 2 data)
{
    return (archx_uop->info->store_type == archx::StoreType::SUS && reg_src[2] != archx::LReg::INVALID);
}

bool UopObj::is_super_sus_pair() const
{ // 3 sources (1 address + 2 data)
    return is_super_sus() && is_pair();
}

bool UopObj::is_dataless_store() const
{
    return (archx_uop->info->store_type == archx::StoreType::STZ && !is_single_uop_store_pair());
}

bool UopObj::is_single_uop_store_pair() const
{
    return ((archx_uop->info->store_type == archx::StoreType::STZ || is_super_sus()) && is_pair());
}

bool UopObj::is_store_special() const
{
    return (archx_uop->info->store_type == archx::StoreType::SPECIAL);
}

bool UopObj::is_sw_prefetch() const
{
    return archx_uop->info->prefetch != archx::SWPrefetchType::NONE;
}

bool UopObj::is_sw_prefetch_as_sta() const
{
    return is_sw_prefetch() && archx_uop->info->store_type == archx::StoreType::PREFETCH;
}

bool UopObj::is_store_prefetch() const
{
    return archx_uop->info->prefetch == archx::SWPrefetchType::STORE;
}

bool UopObj::is_nontemporal() const
{
    return archx_uop->info->nontemporal;
}

bool UopObj::is_barrier_dmb_st() const
{
    return barrier_type == archx::BarrierType::DMB_ST;
}

bool UopObj::is_barrier_dmb_all() const
{
    return barrier_type == archx::BarrierType::DMB_ALL;
}

bool UopObj::is_barrier_dsb() const
{
    return barrier_type == archx::BarrierType::DSB;
}

bool UopObj::is_store_barrier() const
{
    return (is_barrier_dmb_st() || is_barrier_dmb_all() || is_barrier_dsb());
}

bool UopObj::is_atomic_load() const
{
    return (is_atomic() && !invalid_reg(reg_dst));
}

bool UopObj::is_atomic_store() const
{
    return (is_atomic() && invalid_reg(reg_dst));
}

bool UopObj::is_exclusive_load() const
{
    return (is_exclusive() && !invalid_reg(reg_dst));
}

bool UopObj::is_executed_in_idr() const
{
    return unit() == archx::Uopunit::IDR;
}

// These stall before dispatch from IDR until ROB is empty
bool UopObj::is_pre_dispatch_drain() const
{
    // In RTL, pre_drain is enforced by the 'f_IsMrsPreDrain' function in IdrPkg.sv
    if (is_mfspr()
        && (archx_uop->imm == 0xda21 || // FPCR
            archx_uop->imm == 0xc640 || // ICC_IAR0_EL1
            archx_uop->imm == 0xc660 || // ICC_IAR1_EL1
            archx_uop->imm == 0xc7ef || // ICV_IAR0_EL1
            archx_uop->imm == 0xcfe0 || // ICV_IAR1_EL1
            archx_uop->imm == 0xdf05 || // CNTPCTSS_EL0
            archx_uop->imm == 0xdf06 || // CNTVCTSS_EL0
            archx_uop->imm == 0x9820 || // DBGDTR_EL0
            archx_uop->imm == 0x9828 || // DBGDTRRX_EL0
            archx_uop->imm == 0xc2b1 || // TFSRE0_EL1
            archx_uop->imm == 0xc2b0 || // TFSR_EL1
            archx_uop->imm == 0xe2b0 || // TFSR_EL2
            archx_uop->imm == 0xf2b0    // TFSR_EL3
            ))
        return true;

    return (archx_uop->pre_drain);
}

// These dispatch from IDR, then stall until ROB is empty
bool UopObj::is_post_dispatch_drain() const
{
    // Reads of FPSR wait until ROB is empty since it can be updated by commit of FP uops
    if (is_mfspr()
        && (archx_uop->imm == 0xda21 || // FPCR
            archx_uop->imm == 0xc640 || // ICC_IAR0_EL1
            archx_uop->imm == 0xc660 || // ICC_IAR1_EL1
            archx_uop->imm == 0xc7ef || // ICV_IAR0_EL1
            archx_uop->imm == 0xcfe0 || // ICV_IAR1_EL1
            archx_uop->imm == 0xc2b1 || // TFSRE0_EL1
            archx_uop->imm == 0xc2b0 || // TFSR_EL1
            archx_uop->imm == 0xe2b0 || // TFSR_EL2
            archx_uop->imm == 0xf2b0    // TFSR_EL3
            ))
        return true;

    return (archx_uop->info->drain_type == archx::DrainType::POST_DRAIN);
}

bool UopObj::is_self_sync_msr() const
{
    return (self_sync);
}

// These dispatch from IDR and then just wait for a flush
bool UopObj::is_post_dispatch_stall() const
{
    for (const auto& inst : instructions)
        if (inst->archx_instruction->is_exception())
            return true;

    if (is_self_sync_msr())
        return true;

    return (archx_uop->info->drain_type == archx::DrainType::POST_STALL);
}

bool UopObj::is_isolated() const
{
    if (archx_uop->isolate)
        return true;

    if (is_pre_dispatch_drain() || is_post_dispatch_drain() || is_post_dispatch_stall())
        return true;

    return false;
}

bool UopObj::is_mtspr() const
{
    return archx_uop->info->is_mtspr;
}

bool UopObj::is_mfspr() const
{
    return archx_uop->info->is_mfspr;
}

bool UopObj::requires_memory_tag_check() const
{
    switch (ainst()->inst_info().mem_tag_check) {
    case archx::MemoryTaggingCheck::UNKNOWN:
    case archx::MemoryTaggingCheck::CHECK_IF_NOT_SP:
        return archx_uop->src1 != archx::LReg::SP;
    case archx::MemoryTaggingCheck::ALWAYS_CHECKED:
        break;
    }
    return true;
}

// On the integer side, mov is assembled as orr xn, xzr -- unless the register being moved is sp, in which
// case it's an add of sp and immediate 0. So detect an OR or ADD with one valid source and no immediate.
// We only count 64-bit uops, since a 32-bit move zeros the upper bits (and so isn't really a pure move).
bool UopObj::is_int_move_reg() const
{
    // A move which been fused with a B or a NOP can still be eliminated, because those are also IDR-executed
    // uops. Any other fusion means the uop has to go to IXU.
    switch (fusion_type) {
    case FusionType::INVALID:
    case FusionType::NOP:
    case FusionType::B_UNCOND:
        break;
    default:
        return false;
    }

    switch (ainst()->move_type()) {
    case MoveType::ORR64:
        return reg_src[0] == LReg::ZR && reg_src[1] != LReg::ZR && archx_uop->imm == 0 && archx_uop->imm_shift == 0;
    case MoveType::ADD64:
        return archx_uop->imm == 0 && archx_uop->imm_shift == 0;
    default:
        return false;
    }
}

// There are two commonly used ways to zero an integer register: move an immediate of 0, or move from the zero register.
//
// gcc10 seems to like to zero a register using mov Xn, #0 instead of mov Xn, xzr. So detect
// a move of immediate zero into a register as if it were a register move.
// Surprisingly, movz of #0 with a non-zero shift count also exists.
//
// mov xn, xzr is assembled as orr xn, xzr. So detect an OR with two invalid sources and no immediate.
// Here we count both 32-bit and 64-bit uops since both will zero the entire destination register.
bool UopObj::is_int_move_zero() const
{
    if (is_fused())
        return false;

    switch (ainst()->move_type()) {
    case MoveType::ORR32:
    case MoveType::ORR64:
        return reg_src[0] == LReg::ZR && reg_src[1] == LReg::ZR;
    case MoveType::MOVZ:
        return archx_uop->imm == 0;
    default:
        return false;
    }
}

// On the vector side, move is orr xn, xn. So detect an OR with both sources the same.
// If we ever actually implement this, we might want to do it with a special uop like we do for integer.
bool UopObj::is_vec_move_reg() const
{
    if ((archx_uop->opcode == archx::Uopcode::ORR_B_LV || archx_uop->opcode == archx::Uopcode::ORR_B_SV)
        && archx_uop->src1 == archx_uop->src2)
        return true;
    if (archx_uop->opcode == archx::Uopcode::MOVPRFX_D_UV) // SVE unpredicated movprfx
        return true;

    return false;
}

// This function is not currently used since getting elimination of vector move-zero working in IDR was too
// tricky and we don't currently plan to implement it.
bool UopObj::is_vec_move_zero() const
{
    // 0f000407  movi  v7.2s, #0                -- cmode 0, op 0, abcdefgh 0, uop movi_s_sv
    // 2f00e407  movi  d7, #0000000000000000    -- cmode e, op 1, abcdefgh 0, uop movi_d_sv
    // 4f000407  movi  v7.4s, #0                -- cmode 0, op 0, abcdefgh 0, uop movi_s_lv
    // 6f00e407  movi  v7.2d, #0000000000000000 -- cmode e, op 1, abcdefgh 0, uop movi_d_lv

    if (!(archx_uop->opcode == archx::Uopcode::MOVI_S_SV || archx_uop->opcode == archx::Uopcode::MOVI_D_SV
          || archx_uop->opcode == archx::Uopcode::MOVI_S_LV || archx_uop->opcode == archx::Uopcode::MOVI_D_LV))
        return false;

    uint32_t opcode = instructions.front()->archx_instruction->opcode();

    uint32_t abc = (opcode >> 16) & 0x7;
    uint32_t defgh = (opcode >> 5) & 0x1f;
    if (abc != 0 || defgh != 0)
        return false;

    uint32_t cmode = (opcode >> 12) & 0xf;
    uint32_t op = (opcode >> 29) & 0x1;
    return (cmode == 0 && op == 0) || (cmode == 0xe && op == 1);
}

bool UopObj::is_vector_idiv() const
{
    switch (archx_uop->opcode) {
        // sve vector idiv
    case Uopcode::SDIV_D_PV:
    case Uopcode::SDIV_S_PV:
    case Uopcode::SDIVR_D_PV:
    case Uopcode::SDIVR_S_PV:
    case Uopcode::UDIV_D_PV:
    case Uopcode::UDIV_S_PV:
    case Uopcode::UDIVR_D_PV:
    case Uopcode::UDIVR_S_PV:
        return true;
    default:
        return false;
    }
}

bool UopObj::is_vector_idiv32() const
{
    switch (archx_uop->opcode) {
        // sve vector idiv
    case Uopcode::SDIV_S_PV:
    case Uopcode::SDIVR_S_PV:
    case Uopcode::UDIV_S_PV:
    case Uopcode::UDIVR_S_PV:
        return true;
    default:
        return false;
    }
}

bool UopObj::is_vector_udiv() const
{
    switch (archx_uop->opcode) {
        // sve vector idiv
    case Uopcode::UDIV_D_PV:
    case Uopcode::UDIV_S_PV:
    case Uopcode::UDIVR_D_PV:
    case Uopcode::UDIVR_S_PV:
        return true;
    default:
        return false;
    }
}

bool UopObj::is_scalar_idiv() const
{
    switch (archx_uop->opcode) {
    case Uopcode::GSDIV_D_NV:
    case Uopcode::GSDIV_S_NV:
    case Uopcode::GUDIV_D_NV:
    case Uopcode::GUDIV_S_NV:
    case Uopcode::UDIV32:
    case Uopcode::UDIV64:
    case Uopcode::SDIV32:
    case Uopcode::SDIV64:
        return true;
    default:
        return false;
    }
}

bool UopObj::is_scalar_udiv() const
{
    switch (archx_uop->opcode) {
    case Uopcode::GUDIV_D_NV:
    case Uopcode::GUDIV_S_NV:
    case Uopcode::UDIV32:
    case Uopcode::UDIV64:
        return true;
    default:
        return false;
    }
}

bool UopObj::is_scalar_idiv32() const
{
    switch (archx_uop->opcode) {
    case Uopcode::GSDIV_S_NV:
    case Uopcode::GUDIV_S_NV:
    case Uopcode::UDIV32:
    case Uopcode::SDIV32:
        return true;
    default:
        return false;
    }
}

bool UopObj::is_scalar_sdiv32() const
{
    switch (archx_uop->opcode) {
    case Uopcode::GSDIV_S_NV:
    case Uopcode::SDIV32:
        return true;
    default:
        return false;
    }
}

bool UopObj::is_scalar_udiv32() const
{
    switch (archx_uop->opcode) {
    case Uopcode::GUDIV_S_NV:
    case Uopcode::UDIV32:
        return true;
    default:
        return false;
    }
}

bool UopObj::is_udiv() const
{ // unsigned integer divide
    switch (archx_uop->opcode) {
    case Uopcode::GUDIV_D_NV:
    case Uopcode::GUDIV_S_NV:
    case Uopcode::UDIV32:
    case Uopcode::UDIV64:
    case Uopcode::UDIV_S_PV:
    case Uopcode::UDIV_D_PV:
    case Uopcode::UDIVR_S_PV:
    case Uopcode::UDIVR_D_PV:
        return true;
    default:
        return false;
    }
}

bool UopObj::is_reverse_divide() const
{
    switch (archx_uop->opcode) {
    case Uopcode::FDIVR_H_PV:
    case Uopcode::FDIVR_S_PV:
    case Uopcode::FDIVR_D_PV:
    case Uopcode::UDIVR_S_PV:
    case Uopcode::UDIVR_D_PV:
    case Uopcode::SDIVR_S_PV:
    case Uopcode::SDIVR_D_PV:
        return true;
    default:
        return false;
    }
}

bool UopObj::is_fdiv16() const
{
    switch (archx_uop->opcode) {
    case Uopcode::FDIV_H_LV:
    case Uopcode::FDIV_H_NV:
    case Uopcode::FDIV_H_PV:
    case Uopcode::FDIV_H_SV:
    case Uopcode::FDIVR_H_PV:
        return true;
    default:
        return false;
    }
}
bool UopObj::is_fdiv32() const
{
    switch (archx_uop->opcode) {
    case Uopcode::FDIV_S_LV:
    case Uopcode::FDIV_S_NV:
    case Uopcode::FDIV_S_PV:
    case Uopcode::FDIV_S_SV:
    case Uopcode::FDIVR_S_PV:
        return true;
    default:
        return false;
    }
}

bool UopObj::is_fdiv64() const
{
    switch (archx_uop->opcode) {
    case Uopcode::FDIV_D_LV:
    case Uopcode::FDIV_D_NV:
    case Uopcode::FDIV_D_PV:
    case Uopcode::FDIVR_D_PV:
        return true;
    default:
        return false;
    }
}

archx::ResourceType UopObj::get_resource_type() const
{
    return archx_uop->info->resource_type;
}

unsigned UopObj::get_resource_count() const
{
    return archx_uop->info->resource_count;
}

unsigned UopObj::get_resource_count(unsigned max_count) const
{
    return std::min(archx_uop->info->resource_count, max_count);
}

bool UopObj::is_line_split() const
{
    if (!(is_load() || is_store_address()))
        return false;
    if (!virt_addr)
        return false;
    if (is_predicated_off())
        return false;
    const auto bytes = mem_datasize();
    if (bytes) {
        auto end_addr = *virt_addr + bytes - 1;
        if (virt_addr->line_number() != end_addr.line_number())
            return true;
    }
    return false;
}

bool UopObj::is_4k_split() const
{
    if (!(is_load() || is_store_address()))
        return false;
    if (!virt_addr)
        return false;
    if (is_predicated_off())
        return false;

    const auto bytes = mem_datasize();
    if (bytes) {
        auto end_addr = *virt_addr + bytes - 1;
        if (virt_addr->page_number_4k() != end_addr.page_number_4k())
            return true;
    }
    return false;
}

bool UopObj::is_branch() const
{
    if (fused_with_unconditional_branch)
        return true;
    if (archx_uop->end_inst)
        return branch_instruction() != nullptr;

    return false;
}

bool UopObj::is_mispredicted_branch() const
{
    if (archx_uop->end_inst) {
        for (const auto& inst : instructions) {
            if (inst->archx_instruction->is_branch() && !inst->archx_instruction->is_eret()
                && (inst->is_mispredicted_branch_type() || inst->is_mispredicted_direction()
                    || inst->is_mispredicted_target()))
            {
                return true;
            }
        }
    }

    return false;
}

bool UopObj::is_bs_pipe() const
{
    return (archx::Uoppipe::BS & archx_uop->info->allowed_pipes);
}

bool UopObj::is_dc_zva() const
{
    return (archx_uop->opcode == archx::Uopcode::SYS_DC_ZVA || archx_uop->opcode == archx::Uopcode::SYS_DC_GZVA);
}

bool UopObj::is_dc_va() const
{
    return (archx_uop->opcode == archx::Uopcode::SYS_DC_VA || archx_uop->opcode == archx::Uopcode::SYS_DC_IVA);
}

std::optional<archx::FlushType> UopObj::requires_flush() const
{
    if (aborted)
        return archx::FlushType::ABORTED;

    if (is_self_sync_msr())
        return archx::FlushType::CSF_WTH_CMT;

    if (archx_uop->opcode == archx::Uopcode::WFE || archx_uop->opcode == archx::Uopcode::WFI)
        return archx::FlushType::CSF_WTH_CMT;

    if (archx_uop->end_inst) {
        for (auto inst : instructions) {
            auto flush = inst->requires_flush();
            if (flush) {
                return flush;
            }
        }
    }

    return std::nullopt;
}

bool UopObj::could_mispredict() const
{
    for (const auto& inst : instructions)
        if (archx::could_mispredict(inst->archx_instruction->branch_type()))
            return true;

    return false;
}

bool UopObj::could_fault() const
{
    if (is_load() || is_store_address())
        return true;

    if (requires_flush())
        return true;

    if (archx_uop->info->is_pac_auth)
        return true;

    return false;
}

void UopObj::fifo_clear()
{
    // careful: this fn is called by sc/fifo.h in a non-obvious way.
    if (robid)
        release_credit(robid);
    if (is_gather()) {
        GatherProxyCreditPtr gprox = get_gather_proxy();
        if (gprox) {
            DBG_GATHER("uop.cc: fifo_clear gather uop parent txid {}", gprox->get_txid());
            gprox->flush();
        }
    } else if (prfid)
        release_credit(prfid);
    if (prfid2)
        release_credit(prfid2);
    if (flagsid)
        release_credit(flagsid);
    if (schid)
        release_credit(schid);
    if (lobid)
        release_credit(lobid);
    if (sobid)
        release_credit(sobid);
    if (bobid)
        release_credit(bobid);
    if (lubid)
        release_credit(lubid);
}
sc::Cycle UopObj::will_cancel_at() const
{
    if (canceled) {
        // return the minimum time of the nonzero elements.
        sc::Cycle retval = 0;
        bool value_set = false;
        for (size_t src_idx = 0; src_idx < UOP_MAX_SRCS; ++src_idx) {
            const sc::Cycle ctime = cancel_src_time[src_idx];
            if (ctime) {
                if (!value_set || (ctime < retval)) {
                    retval = ctime;
                    value_set = true;
                }
            }
        }
        AMPERE_ASSERT(value_set && retval != 0, "Must have a nonzero cancel time");
        return retval;
    }
    return 0;
}

} // namespace ampere::core

/*
 * vim: sw=4:ts=4:et:si:cindent:cino=\:0,(0,g0,t0:
 */
