//
// Copyright (c) 2019-2024 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//
#pragma once
#include <cstdint>
#include <vector>
#include <optional>

#include "ampere/archx/addr.h"
#include "ampere/archx/branch_target.h"
#include "ampere/archx/branch_type.h"
#include "ampere/archx/barrier_type.h"
#include "ampere/archx/decoder.h"
#include "ampere/archx/divide_type.h"
#include "ampere/archx/prefetch_type.h"
#include "ampere/archx/exception_type.h"
#include "ampere/archx/control.h"
#include "ampere/archx/inst_info.h"
#include "ampere/archx/address_translation_type.h"
#include "ampere/archx/cache_maint_type.h"
#include "ampere/archx/uop.h"
#include "ampere/archx/arch_state.h"
#include "ampere/archx/sysreg.h"
#include "ampere/archx/decoder_mode.h"
#include "ampere/adis/src/adis.h"
#include "ampere/archx/tlbi.h"
#include "ampere/archx/inum.h"

namespace ampere::archx {

class Instruction
{
public:
    virtual ~Instruction() = default;
    virtual ProgramCounter program_counter() const = 0;
    virtual PhysAddr program_counter_pa() const = 0;
    virtual ProgramCounter next_program_counter() const = 0;
    virtual uint32_t opcode() const = 0;
    virtual bool has_exception() const = 0;

private:
    // mutable allows us to modify it in a const method
    mutable std::optional<adis::encoding_enum_t> adis_enc_info{std::nullopt};

public:
    adis::encoding_enum_t adis_info() const
    {
        if (!adis_enc_info) {
            adis_enc_info = adis::decode(opcode());
        }
        return adis_enc_info.value();
    }

    virtual std::string disasm() const = 0;
    DecoderMode decode_mode() const { return dec_mode; }

    virtual VirtAddr virtual_address(unsigned n = 0) const = 0;
    virtual uint32_t num_addresses() const = 0; // count of virtual/physical addresses
    virtual PhysAddr physical_address(unsigned n = 0) const = 0;

    virtual bool memop_predicated_off(unsigned n = 0) const = 0;
    virtual std::uint32_t memop_bytes(unsigned n = 0) const = 0; // number of memory bytes referenced

    // id of the instruction that preceeded this instruction in program order
    virtual inum_t prev_inum() const = 0;
    virtual inum_t inum() const = 0;
    virtual bool wrong_path() const = 0;
    // artificial encodings are wrong path instructions for which we
    // do not have the required instruction text (opcode) bits. So we
    // insert a default instruction pattern.
    virtual bool artificial_encoding() const = 0;
    virtual bool emulated() const = 0;
    virtual ArchState state() const = 0;

    virtual UopSet decode(DecoderMode decoder_mode = DecoderModeDefault);
    virtual const char* decode_source(DecoderMode decoder_mode = DecoderModeDefault);

    virtual void execute() = 0;
    virtual std::optional<SimulationControl> control() = 0;

    const InstInfo& inst_info() const;

    // pass in 2 32b opcodes since we don't represent fused
    // instructions as actual instructions.
    static const InstInfoFusion& inst_info_fusion(std::uint32_t* opcodes);

    BranchType branch_type() const { return inst_info().branch_type; }
    bool is_branch() const { return archx::is_branch(branch_type()); }
    bool is_taken_branch() const
    {
        auto br_type = branch_type();

        // Make our best guess about whether this instruction is a taken
        // branch. Typically if the next instruction is not PC+4, it is a
        // taken branch, but this may not be the case if the instruction
        // following the branch took an exception before committing. We can
        // differentiate this case by calculating the expected branch target
        // for direct branches, but have to continue guessing for indirect
        // branches (for now) because we lack the register values to calculate
        // their targets. Note that this cannot return a sensible answer for
        // branches on the wrong path, since next_program_counter() is
        // necessarily fabricated (we don't have enough information to
        // determine whether this wrong-path branch would have been taken).
        auto probably_taken = next_program_counter() != program_counter() + 4;
        if (probably_taken && is_direct(br_type))
            probably_taken = branch_target_if_taken(program_counter(), opcode()) == next_program_counter();

        return archx::is_branch(br_type)
               && (is_unconditional(br_type) || is_unspecified_branch(br_type)
                   || (is_conditional(br_type) && probably_taken));
    }
    ProgramCounter branch_target() const
    {
        if (is_direct(branch_type()))
            return branch_target_if_taken(program_counter(), opcode());
        // Ideally we would always calculate the target, but we must fall back
        // on next_program_counter() because we do not have register values to
        // calculate indirect branch targets
        return next_program_counter();
    }
    Barrier_Type barrier_type() const { return inst_info().barrier_type; }
    bool is_barrier() const { return archx::is_barrier(barrier_type()); }
    bool is_clrex() const { return adis::clrex(adis_info()); }
    bool is_fusion_delay_hint() const { return inst_info().is_fusion_delay_hint; }
    bool is_movprfx_unpredicated() const { return adis_info() == adis::ENC_movprfx_z_z_; }
    bool is_movprfx_predicated() const { return adis_info() == adis::ENC_movprfx_z_p_z_; }
    bool is_idiv() const
    {
        const auto ai = adis_info();
        return (ai == adis::ENC_SDIV_64_dp_2src || ai == adis::ENC_SDIV_32_dp_2src || ai == adis::ENC_UDIV_64_dp_2src
                || ai == adis::ENC_UDIV_32_dp_2src);
    }
    bool is_msub() const
    {
        const auto ai = adis_info();
        return (ai == adis::ENC_MSUB_32A_dp_3src || ai == adis::ENC_MSUB_64A_dp_3src);
    }
    unsigned vector_sources() const { return adis::vector_srcs(adis_info()); }

    bool is_tlbi() const { return adis::tlbi(adis_info()); }
    bool is_tlbi_by_va_all_level() const
    {
        if (is_tlbi()) {
            return tlbi_by_va_all_level(sys_qualifier());
        }
        return false;
    }
    bool is_tlbi_by_va_last_level() const
    {
        if (is_tlbi()) {
            return tlbi_by_va_last_level(sys_qualifier());
        }
        return false;
    }
    bool is_tlbi_by_va() const { return is_tlbi_by_va_all_level() || is_tlbi_by_va_last_level(); }
    uint16_t tlbi_by_va_asid() const;
    uint8_t tlbi_by_va_ttl() const;
    VirtAddr tlbi_by_va_addr() const;

    AddressTranslationType address_translation_type() const;
    bool is_address_translation() const { return address_translation_type() != AddressTranslationType::NONE; }
    adis::sys_alias_t cache_maint_type() const;
    adis::sys_alias_t sys_qualifier() const
    {
        // see build/src/ampere/adis/adis-sys-alias-qualifiers.h
        return adis::sysql(adis_info());
    }
    bool is_cache_maint() const { return is_ic() || is_dc(); }

    ExceptionType exception_type() const;
    bool is_eret() const { return exception_type() == ExceptionType::ERET; }
    bool is_exception() const { return exception_type() != ExceptionType::NONE && !is_eret(); }
    DivideType divide_type() const;
    bool is_divide() const { return divide_type() != DivideType::NONE; }
    int divide_srcs() const;
    LReg divide_src(int idx) const;
    LReg tlbi_src() const;
    adis::encoding_enum_t prefetch_type() const; // returns adis::ENC_INVALID if not a prefetch
    bool is_msr_reg() const { return adis::msr_reg(adis_info()); }
    bool is_msr_imm() const { return adis::msr_imm(adis_info()); }
    bool is_msr() const { return is_msr_reg() || is_msr_imm(); }
    bool is_mrs() const { return adis::mrs(adis_info()); }
    bool is_load() const { return inst_info().is_load; }
    bool is_store() const { return inst_info().is_store; }
    bool is_prefetch() const { return inst_info().is_prefetch; }
    bool is_gpr_prefetch() const
    {
        switch (adis_info()) {
        case adis::ENC_PRFM_P_ldst_pos:
        case adis::ENC_PRFM_P_loadlit:
        case adis::ENC_PRFM_P_ldst_regoff:
        case adis::ENC_PRFUM_P_ldst_unscaled:
            return true;
        default:
            return false;
        }
    }
    MoveType move_type() const { return inst_info().move_type; }

    bool is_dc_zva() const { return adis::dc_zva(adis_info()); }
    bool is_dc_gzva() const { return adis::dc_gzva(adis_info()); }
    bool is_aesd() const { return adis::aesd(adis_info()); }
    bool is_aese() const { return adis::aese(adis_info()); }
    bool is_nop() const { return adis::nop(adis_info()); }

    bool is_sve() const { return adis::sve_family(adis_info()); }
    bool is_sve_simd() const { return adis::sve_simd(adis_info()); }
    bool is_ase() const { return adis::advsimd(adis_info()); }
    bool is_ase_simd() const { return adis::advsimd_simd(adis_info()); }
    bool is_ic() const { return adis::ic(adis_info()); }
    bool is_dc() const { return adis::dc(adis_info()); }
    SysReg msr_sysreg() const;

    static std::pair<archx::FusionType, archx::UopSet> decode_fusion(std::uint32_t* opcodes,
                                                                     bool skip_checks,
                                                                     DecoderMode decoder_mode = DecoderModeDefault);

    static void set_decoders(const std::vector<std::string>& decoder_names);
    static UopSet decode(std::uint32_t opcode, DecoderMode decoder_mode = DecoderModeDefault);
    static const char* decode_source(std::uint32_t opcode, DecoderMode decoder_mode = DecoderModeDefault);

protected:
    DecoderMode dec_mode{DecoderModeDefault};
    mutable const InstInfo* m_inst_info{nullptr};
    static std::vector<DecoderFunctions> decoders;
};

} // namespace ampere::archx

/*
 * vim: sw=4:ts=4:et:si:cindent:cino=\:0,(0,g0,t0:
 */
