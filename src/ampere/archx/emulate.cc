//
// Copyright (c) 2024 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//

#include <limits>
#include <optional>
#include <cstdint>
#include <array>
#include <map>
#include <fmt/format.h>
#include "ampere/adis/adis-encoding-fields.h"
#include "ampere/archx/translation.h"
#include "ampere/archx/register.h"
#include "ampere/archx/emulate.h"
#include "ampere/archx/branch_target.h"

// #define DEBUG_EMULATE
#if defined(DEBUG_EMULATE)
#define DBGEMU(format_str, ...) fmt::print(stderr, "EMU: " format_str "\n", ##__VA_ARGS__)
#else
#define DBGEMU(format_str, ...) \
    do {                        \
    } while (0)
#endif

namespace ampere::archx::emulate {
static std::uint32_t portable_clz32(std::uint32_t x)
{
    // unlike aarch64, x86-64 returns 31 for the builtin with arg 0
    if (x) {
        return __builtin_clz(x);
    }
    return 32;
}
static std::uint64_t portable_clz64(std::uint64_t x)
{
    // unlike aarch64, x86-64 returns 63 for the builtin with arg 0
    if (x) {
        return __builtin_clzll(x);
    }
    return 64;
}
static std::string regstr(archx::LReg r)
{
    std::ostringstream o;
    o << r;
    return o.str();
}

union fp64_t
{
    struct
    {
        std::uint64_t fraction : 52;
        std::uint64_t exponent : 11;
        std::uint64_t sign : 1;
    } s;
    std::uint64_t u;
    double f;
    bool nan() const { return s.exponent == 0x3FF && s.fraction != 0; }
    bool zero() const { return s.exponent == 0 && s.fraction == 0; }
    bool pos_infinity() const { return s.exponent == 0x3FF && s.fraction == 0 && s.sign == 0; }
    bool neg_infinity() const { return s.exponent == 0x3FF && s.fraction == 0 && s.sign == 1; }
};

union fp32_t
{
    struct
    {
        std::uint32_t fraction : 23;
        std::uint32_t exponent : 8;
        std::uint32_t sign : 1;
    } s;
    std::uint32_t u;
    float f;
    bool nan() const { return s.exponent == 0xFF && s.fraction != 0; }
    bool zero() const { return s.exponent == 0 && s.fraction == 0; }
    bool pos_infinity() const { return s.exponent == 0xFF && s.fraction == 0 && s.sign == 0; }
    bool neg_infinity() const { return s.exponent == 0xFF && s.fraction == 0 && s.sign == 1; }
};

union fp16_t
{
    struct
    {
        std::uint16_t fraction : 10;
        std::uint16_t exponent : 5;
        std::uint16_t sign : 1;
    } s;
    std::uint16_t u;
#if defined(__aarch64__) // PROBLEM FOR X86-64
    __fp16 f;
#endif
    bool nan() const { return s.exponent == 0x1F && s.fraction != 0; }
    bool zero() const { return s.exponent == 0 && s.fraction == 0; }
    bool pos_infinity() const { return s.exponent == 0x1F && s.fraction == 0 && s.sign == 0; }
    bool neg_infinity() const { return s.exponent == 0x1F && s.fraction == 0 && s.sign == 1; }
};

union pstate_nzcv_t
{
    std::uint32_t i;
    struct
    {
        std::uint32_t dummy : 28;
        std::uint32_t v : 1;
        std::uint32_t c : 1;
        std::uint32_t z : 1;
        std::uint32_t n : 1;
    } s;
};

static std::uint64_t pstate_nzcv(std::uint32_t n, std::uint32_t z, std::uint32_t c, std::uint32_t v)
{
    return (n << 31) | (z << 30) | (c << 29) | (v << 28); // set pstate.nzcv flags in correct bits
}

// FIXME: copied from driver_is.cc
static Translation translate(const ArchState& arch_state, VirtAddr va, bool is_instr = false)
{
    Translation tr(va,
                   Bitfield{arch_state.read(TCR_EL1)},
                   Bitfield{arch_state.read(TTBR0_EL1)},
                   Bitfield{arch_state.read(TTBR1_EL1)},
                   is_instr);
    while (!tr.done()) {
        PhysAddr pte_addr = tr.next_pte();
        uint64_t pte_contents = arch_state.read(pte_addr);
        tr.pte_fetched(pte_contents);
    }
    return tr;
}

static std::uint64_t mask_64(std::uint64_t x)
{
    if (x == 64)
        return std::numeric_limits<std::uint64_t>::max();
    return (1ULL << x) - 1;
}
static std::uint32_t mask_32(std::uint32_t x)
{
    if (x == 32)
        return std::numeric_limits<std::uint32_t>::max();
    const std::uint32_t one = 1;
    return (one << x) - 1;
}

static std::uint64_t ror_64(std::uint64_t x, std::uint64_t r, std::uint64_t width)
{
    if (width == 64 && r == 0) // caught by -fsanitize=undefined
        return x;
    return mask_64(width) & ((x >> r) | (x << (width - r)));
}
static std::uint64_t ror_32(std::uint32_t x, std::uint32_t r, std::uint32_t width)
{
    if (width == 32 && r == 0) // caught by -fsanitize=undefined
        return x;
    return mask_32(width) & ((x >> r) | (x << (width - r)));
}

static std::int64_t sign_extend(std::int64_t x, std::int64_t bit)
{
    // if bit == n, we sign extend using bit n-1 of x.
    std::int64_t m = 1ULL << (bit - 1);
    return (x ^ m) - m;
}

class Aarch64Emulate
{
public:
    Aarch64Emulate() { initialize_emu_fnptrs(); }

private:
    static std::optional<std::uint64_t> ld_regoff_common(
        const ArchState& arch_state, unsigned rn, unsigned rm, unsigned option, unsigned shift, unsigned size)
    {
        // size is log2 # of bytes
        const auto regvaln = arch_state.read(archx::cvt_gpr(rn, archx::LReg::SP));
        auto regvalm = rm == 31 ? 0 : arch_state.read(archx::cvt_gpr(rm));
        switch (option) {
        case 0b010: // UXTW
            regvalm = regvalm & 0xFFFF'FFFFULL;
            break;
        case 0b011: // LSL (handled below)
            break;
        case 0b110: // SXTW
            regvalm = sign_extend(regvalm, 32);
            break;
        case 0b111: // SXTX (nothing to do)
            break;
        default:
            return std::nullopt; // illegal, do not emulate
        }
        if (shift) {
            regvalm = regvalm << size;
        }
        const std::uint64_t addr = regvaln + regvalm;
        return addr;
    }

    static std::optional<std::uint64_t> ld_regoff(const ArchState& arch_state,
                                                  const std::uint32_t itext,
                                                  const std::uint32_t size)
    {
        const auto rn = adis::fldfrom_LDR_32_ldst_regoff(itext, adis::fields_t::Rn); // could be SP
        const auto rm = adis::fldfrom_LDR_32_ldst_regoff(itext, adis::fields_t::Rm);
        const auto option = adis::fldfrom_LDR_32_ldst_regoff(itext, adis::fields_t::option);
        const auto shift = adis::fldfrom_LDR_32_ldst_regoff(itext, adis::fields_t::S);
        return ld_regoff_common(arch_state, rn, rm, option, shift, size);
    }

    static bool condition_holds(unsigned cond, unsigned pstate_nzcv)
    {
        pstate_nzcv_t u{pstate_nzcv};

        bool result;
        switch ((cond >> 1) & 0x7) {
        case 0b000:
            result = (u.s.z == 1);
            break; // EQ or NE
        case 0b001:
            result = (u.s.c == 1);
            break; // CS or CC
        case 0b010:
            result = (u.s.n == 1);
            break; // MI or PL
        case 0b011:
            result = (u.s.v == 1);
            break; // VS or VC
        case 0b100:
            result = (u.s.c == 1) && (u.s.z == 0);
            break; // HI or LS
        case 0b101:
            result = (u.s.n == u.s.v);
            break; // GE or LT
        case 0b110:
            result = (u.s.n == u.s.v) && (u.s.z == 0);
            break; // GT or LE
        case 0b111:
            result = true;
            break; // AL
        default:
            break;
        }

        if ((cond & 1) == 1 && cond != 0b1111) {
            result = !result;
        }
        return result;
    }

    static bool emulate_bcond(archx::InstructionIS* instr)
    {
        // compute next_pc. determine if taken or not
        auto cond = adis::fldfrom_B_only_condbranch(instr->opcode(), adis::fields_t::cond);
        // imm19 not used since relying on branch_target_if_taken()
        // auto imm19 = adis::fldfrom_B_only_condbranch(itext, adis::fields_t::imm19);
        ProgramCounter target = branch_target_if_taken(instr->program_counter(), instr->opcode());
        bool taken = condition_holds(cond, instr->pstate_nzcv());
        if (taken) {
            instr->set_next_pc(target);
            instr->set_emulated_br_taken();
        } else {
            instr->set_next_pc(instr->program_counter() + 4);
        }
        return true;
    }

    static bool emulate_tbz(archx::InstructionIS* instr) { return emulate_test_bit(instr, 0); }
    static bool emulate_tbnz(archx::InstructionIS* instr) { return emulate_test_bit(instr, 1); }

    static bool emulate_test_bit(archx::InstructionIS* instr, std::uint64_t zero_or_one)
    {
        // Using TBZ format field extractor functions (fldfrm); It is identical to the TBNZ format
        const auto src_reg = adis::fldfrom_TBZ_only_testbranch(instr->opcode(), adis::fields_t::Rt);
        bool taken = false;
        if (src_reg == 31) {
            // tbz with wzr/xzr is unconditionally taken
            // tbnz with wzr/xzr is unconditionally not-taken
            if (zero_or_one == 0) {
                taken = true;
            }
        } else {
            const auto b5 = adis::fldfrom_TBZ_only_testbranch(instr->opcode(), adis::fields_t::b5);
            const auto b40 = adis::fldfrom_TBZ_only_testbranch(instr->opcode(), adis::fields_t::b40);
            const std::uint64_t bit_pos = (b5 << 5) | b40; // handles 32b vs 64b: b5==0 for 32b
            const auto src_lreg = archx::cvt_gpr(src_reg);
            const ArchState& input_state = instr->state_ref(); // input
            const auto input_val = input_state.read(src_lreg);
            if (((input_val >> bit_pos) & 1) == zero_or_one) {
                taken = true;
            }
        }
        if (taken) {
            ProgramCounter target = branch_target_if_taken(instr->program_counter(), instr->opcode()); // uses imm14
            instr->set_next_pc(target);
            instr->set_emulated_br_taken();
        } else {
            instr->set_next_pc(instr->program_counter() + 4);
        }
        return true;
    }

    static bool emulate_cbz64(archx::InstructionIS* instr)
    {
        const auto src_reg = adis::fldfrom_CBZ_64_compbranch(instr->opcode(), adis::fields_t::Rt);
        const ArchState& input_state = instr->state_ref(); // input
        const auto src_lreg = archx::cvt_gpr(src_reg);
        const auto input_val = src_reg == 31 ? 0 : input_state.read(src_lreg);
        if (input_val == 0) {
            ProgramCounter target = branch_target_if_taken(instr->program_counter(), instr->opcode()); // uses imm19
            instr->set_next_pc(target);
            instr->set_emulated_br_taken();
        } else {
            instr->set_next_pc(instr->program_counter() + 4);
        }
        return true;
    }
    static bool emulate_cbnz64(archx::InstructionIS* instr)
    {
        const auto src_reg = adis::fldfrom_CBZ_64_compbranch(instr->opcode(), adis::fields_t::Rt);
        const ArchState& input_state = instr->state_ref(); // input
        const auto src_lreg = archx::cvt_gpr(src_reg);
        const auto input_val = src_reg == 31 ? 0 : input_state.read(src_lreg);
        if (input_val != 0) {
            ProgramCounter target = branch_target_if_taken(instr->program_counter(), instr->opcode()); // uses imm19
            instr->set_next_pc(target);
            instr->set_emulated_br_taken();
        } else {
            instr->set_next_pc(instr->program_counter() + 4);
        }
        return true;
    }

    static bool emulate_cbz32(archx::InstructionIS* instr)
    {
        const auto src_reg = adis::fldfrom_CBZ_64_compbranch(instr->opcode(), adis::fields_t::Rt);
        const ArchState& input_state = instr->state_ref(); // input
        const auto src_lreg = archx::cvt_gpr(src_reg);
        const auto input_val = src_reg == 31 ? 0 : input_state.read(src_lreg);
        if (static_cast<std::uint32_t>(input_val) == 0) {
            ProgramCounter target = branch_target_if_taken(instr->program_counter(), instr->opcode()); // uses imm19
            instr->set_next_pc(target);
            instr->set_emulated_br_taken();
        } else {
            instr->set_next_pc(instr->program_counter() + 4);
        }
        return true;
    }
    static bool emulate_cbnz32(archx::InstructionIS* instr)
    {
        const auto src_reg = adis::fldfrom_CBZ_64_compbranch(instr->opcode(), adis::fields_t::Rt);
        const ArchState& input_state = instr->state_ref(); // input
        const auto src_lreg = archx::cvt_gpr(src_reg);
        const auto input_val = src_reg == 31 ? 0 : input_state.read(src_lreg);
        if (static_cast<std::uint32_t>(input_val) != 0) {
            ProgramCounter target = branch_target_if_taken(instr->program_counter(), instr->opcode()); // uses imm19
            instr->set_next_pc(target);
            instr->set_emulated_br_taken();
        } else {
            instr->set_next_pc(instr->program_counter() + 4);
        }
        return true;
    }

    static bool emulate_b_uncond(archx::InstructionIS* instr)
    {
        // imm26 not used because relying on branch_target_if_taken()
        ProgramCounter target = branch_target_if_taken(instr->program_counter(), instr->opcode());
        instr->set_next_pc(target);
        instr->set_emulated_br_taken();
        return true;
    }
    static bool emulate_indirect_branch(archx::InstructionIS* instr, unsigned rtgt)
    {
        std::uint64_t target = 0;
        if (rtgt != 31) {
            const ArchState& input_state = instr->state_ref(); // input
            const auto lreg_tgt = archx::cvt_gpr(rtgt);
            target = input_state.read(lreg_tgt); // panthera handles target misalignment
        }
        ProgramCounter target_pc(target);
        instr->set_next_pc(target_pc);
        instr->set_emulated_br_taken();
        return true;
    }

    static bool emulate_ret(archx::InstructionIS* instr)
    {
        const auto rn = adis::fldfrom_RET_64R_branch_reg(instr->opcode(), adis::fields_t::Rn);
        return emulate_indirect_branch(instr, rn);
    }

    static bool emulate_br(archx::InstructionIS* instr)
    {
        const auto rn = adis::fldfrom_BR_64_branch_reg(instr->opcode(), adis::fields_t::Rn);
        return emulate_indirect_branch(instr, rn);
    }

    static bool emulate_blr(archx::InstructionIS* instr)
    {
        ProgramCounter return_target(instr->program_counter() + 4);
        instr->write_gpr64(archx::LReg::L30, return_target.value());

        const auto rn = adis::fldfrom_BLR_64_branch_reg(instr->opcode(), adis::fields_t::Rn);
        return emulate_indirect_branch(instr, rn);
    }

    static bool emulate_bl(archx::InstructionIS* instr)
    {
        const std::int64_t imm26 = adis::fldfrom_BL_only_branch_imm(instr->opcode(), adis::fields_t::imm26);
        const ProgramCounter return_target(instr->program_counter() + 4);
        instr->write_gpr64(archx::LReg::L30, return_target.value());
        // target not handled by branch_target_if_taken()
        const std::int64_t offset = sign_extend(imm26, 26) << 2;
        const ProgramCounter pc = instr->program_counter();
        ProgramCounter target;
        if (offset < 0) {
            target = pc - (-offset);
        } else {
            target = pc + offset;
        }

        instr->set_next_pc(target);
        instr->set_emulated_br_taken();
        return true;
    }

    static bool emulate_sturq(archx::InstructionIS* instr)
    {
        const auto itext = instr->opcode();
        const std::uint64_t imm = adis::fldfrom_STURB_32_ldst_unscaled(itext, adis::fields_t::imm9);
        const auto rn = adis::fldfrom_STURB_32_ldst_unscaled(itext, adis::fields_t::Rn); // can be SP
        const auto rt = adis::fldfrom_STURB_32_ldst_unscaled(itext, adis::fields_t::Rt);
        const ArchState& input_state = instr->state_ref(); // input
        const auto regvaln = input_state.read(archx::cvt_gpr(rn, archx::LReg::SP));
        const auto regvalt = input_state.read_vfp(archx::cvt_vec(rt)); // value to store
        const VirtAddr va(regvaln + sign_extend(imm, 9));
        instr->add_emulated_virt_addr(va);
        mem_write_128bit(instr, va, regvalt);
        return true;
    }
    static bool emulate_sturb_gpr(archx::InstructionIS* instr)
    {
        const auto itext = instr->opcode();
        const std::uint64_t imm = adis::fldfrom_STURB_32_ldst_unscaled(itext, adis::fields_t::imm9);
        const auto rn = adis::fldfrom_STURB_32_ldst_unscaled(itext, adis::fields_t::Rn); // can be SP
        const auto rt = adis::fldfrom_STURB_32_ldst_unscaled(itext, adis::fields_t::Rt);
        const ArchState& input_state = instr->state_ref(); // input
        const auto regvaln = input_state.read(archx::cvt_gpr(rn, archx::LReg::SP));
        const std::uint8_t regvalt = rt == 31 ? 0 : input_state.read(archx::cvt_gpr(rt)); // value to store
        const VirtAddr va(regvaln + sign_extend(imm, 9));
        instr->add_emulated_virt_addr(va);
        mem_write_8bit(instr, va, regvalt);
        return true;
    }

    static bool emulate_stur_32(archx::InstructionIS* instr)
    {
        const auto itext = instr->opcode();
        const std::uint64_t imm = adis::fldfrom_STUR_32_ldst_unscaled(itext, adis::fields_t::imm9);
        const auto rn = adis::fldfrom_STUR_32_ldst_unscaled(itext, adis::fields_t::Rn); // can be SP
        const auto rt = adis::fldfrom_STUR_32_ldst_unscaled(itext, adis::fields_t::Rt);
        const ArchState& input_state = instr->state_ref(); // input
        const auto regvaln = input_state.read(archx::cvt_gpr(rn, archx::LReg::SP));
        const std::uint32_t regvalt = rt == 31 ? 0 : input_state.read(archx::cvt_gpr(rt)); // value to store
        const VirtAddr va(regvaln + sign_extend(imm, 9));
        instr->add_emulated_virt_addr(va);
        mem_write_32bit(instr, va, regvalt);
        return true;
    }
    static bool emulate_stur_64(archx::InstructionIS* instr)
    {
        const auto itext = instr->opcode();
        const std::uint64_t imm = adis::fldfrom_STUR_64_ldst_unscaled(itext, adis::fields_t::imm9);
        const auto rn = adis::fldfrom_STUR_64_ldst_unscaled(itext, adis::fields_t::Rn); // can be SP
        const auto rt = adis::fldfrom_STUR_64_ldst_unscaled(itext, adis::fields_t::Rt);
        const ArchState& input_state = instr->state_ref(); // input
        const auto regvaln = input_state.read(archx::cvt_gpr(rn, archx::LReg::SP));
        const std::uint64_t regvalt = rt == 31 ? 0 : input_state.read(archx::cvt_gpr(rt)); // value to store
        const VirtAddr va(regvaln + sign_extend(imm, 9));
        instr->add_emulated_virt_addr(va);
        mem_write_64bit(instr, va, regvalt);
        return true;
    }

    static bool emulate_str_64_ldst_pre(archx::InstructionIS* instr)
    {
        const auto itext = instr->opcode();
        const auto imm = adis::fldfrom_STR_64_ldst_immpre(itext, adis::fields_t::imm9);
        const auto rn = adis::fldfrom_STR_64_ldst_immpre(itext, adis::fields_t::Rn); // can be SP
        const auto rt = adis::fldfrom_STR_64_ldst_immpre(itext, adis::fields_t::Rt);
        const ArchState& input_state = instr->state_ref(); // input
        const auto base_reg = archx::cvt_gpr(rn, archx::LReg::SP);
        const auto regvaln = input_state.read(base_reg);
        const std::uint64_t regvalt = rt == 31 ? 0 : input_state.read(archx::cvt_gpr(rt)); // value to store

        const VirtAddr va(regvaln + sign_extend(imm, 9));
        instr->add_emulated_virt_addr(va);
        mem_write_64bit(instr, va, regvalt);
        instr->write_gpr64(base_reg, va.value());
        return true;
    }

    // strh pre
    static bool emulate_strh_pre(archx::InstructionIS* instr)
    {
        const auto itext = instr->opcode();
        const auto imm = adis::fldfrom_STRH_32_ldst_immpost(itext, adis::fields_t::imm9);
        const auto rn = adis::fldfrom_STRH_32_ldst_immpost(itext, adis::fields_t::Rn); // can be SP
        const auto rt = adis::fldfrom_STRH_32_ldst_immpost(itext, adis::fields_t::Rt);
        const ArchState& input_state = instr->state_ref(); // input
        const auto base_reg = archx::cvt_gpr(rn, archx::LReg::SP);
        const auto regvaln = input_state.read(base_reg);
        const std::uint32_t regvalt = rt == 31 ? 0 : input_state.read(archx::cvt_gpr(rt)); // value to store
        const VirtAddr va(regvaln + sign_extend(imm, 9));
        instr->add_emulated_virt_addr(va);
        mem_write_16bit(instr, va, regvalt);
        instr->write_gpr64(base_reg, va.value());
        return true;
    }
    // strh post
    static bool emulate_strh_post(archx::InstructionIS* instr)
    {
        const auto itext = instr->opcode();
        const auto imm = adis::fldfrom_STRH_32_ldst_immpost(itext, adis::fields_t::imm9);
        const auto rn = adis::fldfrom_STRH_32_ldst_immpost(itext, adis::fields_t::Rn); // can be SP
        const auto rt = adis::fldfrom_STRH_32_ldst_immpost(itext, adis::fields_t::Rt);
        const ArchState& input_state = instr->state_ref(); // input
        const auto base_reg = archx::cvt_gpr(rn, archx::LReg::SP);
        const auto regvaln = input_state.read(base_reg);
        const std::uint32_t regvalt = rt == 31 ? 0 : input_state.read(archx::cvt_gpr(rt)); // value to store

        const VirtAddr pre_va(regvaln);
        instr->add_emulated_virt_addr(pre_va);
        mem_write_16bit(instr, pre_va, regvalt);
        const VirtAddr post_va(regvaln + sign_extend(imm, 9));
        instr->write_gpr64(base_reg, post_va.value());
        return true;
    }
    // strh pos
    static bool emulate_strh_pos(archx::InstructionIS* instr)
    {
        const auto itext = instr->opcode();
        const auto imm12 = adis::fldfrom_STRH_32_ldst_pos(itext, adis::fields_t::imm12);
        const auto rn = adis::fldfrom_STRH_32_ldst_pos(itext, adis::fields_t::Rn); // can be SP
        const auto rt = adis::fldfrom_STRH_32_ldst_pos(itext, adis::fields_t::Rt);
        const ArchState& input_state = instr->state_ref(); // input
        const auto regvaln = input_state.read(archx::cvt_gpr(rn, archx::LReg::SP));
        const std::uint32_t regvalt = rt == 31 ? 0 : input_state.read(archx::cvt_gpr(rt)); // value to store

        const VirtAddr va(regvaln + (imm12 * 2));
        instr->add_emulated_virt_addr(va);
        mem_write_16bit(instr, va, regvalt);
        return true;
    }

    static bool emulate_str_64_ldst_post(archx::InstructionIS* instr)
    {
        const auto itext = instr->opcode();
        const auto imm = adis::fldfrom_STR_64_ldst_immpost(itext, adis::fields_t::imm9);
        const auto rn = adis::fldfrom_STR_64_ldst_immpost(itext, adis::fields_t::Rn); // can be SP
        const auto rt = adis::fldfrom_STR_64_ldst_immpost(itext, adis::fields_t::Rt);
        const ArchState& input_state = instr->state_ref(); // input
        const auto base_reg = archx::cvt_gpr(rn, archx::LReg::SP);
        const auto regvaln = input_state.read(base_reg);
        const std::uint64_t regvalt = rt == 31 ? 0 : input_state.read(archx::cvt_gpr(rt)); // value to store

        const VirtAddr pre_va(regvaln);
        instr->add_emulated_virt_addr(pre_va);
        mem_write_64bit(instr, pre_va, regvalt);
        const VirtAddr post_va(regvaln + sign_extend(imm, 9));
        instr->write_gpr64(base_reg, post_va.value());
        return true;
    }

    static bool emulate_str_64_ldst_pos(archx::InstructionIS* instr)
    {
        const auto itext = instr->opcode();
        const auto imm12 = adis::fldfrom_STR_64_ldst_pos(itext, adis::fields_t::imm12);
        const auto rn = adis::fldfrom_STR_64_ldst_pos(itext, adis::fields_t::Rn); // can be SP
        const auto rt = adis::fldfrom_STR_64_ldst_pos(itext, adis::fields_t::Rt);
        const ArchState& input_state = instr->state_ref(); // input
        const auto regvaln = input_state.read(archx::cvt_gpr(rn, archx::LReg::SP));
        const std::uint64_t regvalt = rt == 31 ? 0 : input_state.read(archx::cvt_gpr(rt)); // value to store

        const VirtAddr va(regvaln + (imm12 << 3)); // *8
        instr->add_emulated_virt_addr(va);
        mem_write_64bit(instr, va, regvalt);
        return true;
    }

    static bool emulate_str_32_ldst_immpost(archx::InstructionIS* instr)
    {
        const auto itext = instr->opcode();
        const auto imm = adis::fldfrom_STR_32_ldst_immpost(itext, adis::fields_t::imm9);
        const auto rn = adis::fldfrom_STR_32_ldst_immpost(itext, adis::fields_t::Rn); // can be SP
        const auto rt = adis::fldfrom_STR_32_ldst_immpost(itext, adis::fields_t::Rt);
        const ArchState& input_state = instr->state_ref(); // input
        const auto base_reg = archx::cvt_gpr(rn, archx::LReg::SP);
        const auto regvaln = input_state.read(base_reg);
        const std::uint32_t regvalt = rt == 31 ? 0 : input_state.read(archx::cvt_gpr(rt)); // value to store

        const VirtAddr base_va(regvaln);
        instr->add_emulated_virt_addr(base_va);
        mem_write_32bit(instr, base_va, regvalt);
        const VirtAddr post_va(regvaln + sign_extend(imm, 9) * 4);
        instr->write_gpr64(base_reg, post_va.value());
        return true;
    }
    static bool emulate_str_32_ldst_immpre(archx::InstructionIS* instr)
    {
        const auto itext = instr->opcode();
        const auto imm = adis::fldfrom_STR_32_ldst_immpre(itext, adis::fields_t::imm9);
        const auto rn = adis::fldfrom_STR_32_ldst_immpre(itext, adis::fields_t::Rn); // can be SP
        const auto rt = adis::fldfrom_STR_32_ldst_immpre(itext, adis::fields_t::Rt);
        const ArchState& input_state = instr->state_ref(); // input
        const auto base_reg = archx::cvt_gpr(rn, archx::LReg::SP);
        const auto regvaln = input_state.read(base_reg);
        const std::uint32_t regvalt = rt == 31 ? 0 : input_state.read(archx::cvt_gpr(rt)); // value to store

        const VirtAddr va(regvaln + sign_extend(imm, 9) * 4);
        instr->add_emulated_virt_addr(va);
        mem_write_32bit(instr, va, regvalt);
        instr->write_gpr64(base_reg, va.value());
        return true;
    }
    static bool emulate_str_32_ldst_pos(archx::InstructionIS* instr)
    {
        const auto itext = instr->opcode();
        const auto imm12 = adis::fldfrom_STR_32_ldst_pos(itext, adis::fields_t::imm12);
        const auto rn = adis::fldfrom_STR_32_ldst_pos(itext, adis::fields_t::Rn); // can be SP
        const auto rt = adis::fldfrom_STR_32_ldst_pos(itext, adis::fields_t::Rt);
        const ArchState& input_state = instr->state_ref(); // input
        const auto regvaln = input_state.read(archx::cvt_gpr(rn, archx::LReg::SP));
        const std::uint32_t regvalt = rt == 31 ? 0 : input_state.read(archx::cvt_gpr(rt)); // value to store

        // compute dest virt address
        const VirtAddr va(regvaln + (imm12 << 2)); // *4
        instr->add_emulated_virt_addr(va);
        mem_write_32bit(instr, va, regvalt);
        return true;
    }

    static void mem_write_8bit(archx::InstructionIS* instr, const VirtAddr& va, const std::uint8_t data)
    {
        const ArchState& input_state = instr->state_ref(); // input
        if (auto translation = translate(input_state, va); !translation.faulted()) {
            auto pa = translation.result_address();
            // update memory
            instr->arch_state_output = std::move(instr->arch_state_output).write(pa, data);
        }
    }
    static void mem_write_16bit(archx::InstructionIS* instr, const VirtAddr& va, const std::uint16_t data)
    {
        const ArchState& input_state = instr->state_ref(); // input
        const VirtAddr mva = va & ~1ULL;                   // FIXME: handle unaligned writes
        if (auto translation = translate(input_state, mva); !translation.faulted()) {
            auto pa = translation.result_address();
            // update memory
            instr->arch_state_output = std::move(instr->arch_state_output).write(pa, data);
        }
    }
    static void mem_write_32bit(archx::InstructionIS* instr, const VirtAddr& va, const std::uint32_t data)
    {
        const ArchState& input_state = instr->state_ref(); // input
        const VirtAddr mva = va & ~3ULL;                   // FIXME: handle unaligned writes
        if (auto translation = translate(input_state, mva); !translation.faulted()) {
            auto pa = translation.result_address();
            // update memory
            instr->arch_state_output = std::move(instr->arch_state_output).write(pa, data);
        }
    }
    static void mem_write_64bit(archx::InstructionIS* instr, const VirtAddr& va, const std::uint64_t data)
    {
        const ArchState& input_state = instr->state_ref(); // input
        const VirtAddr mva = va & ~7ULL;                   // FIXME: handle unaligned writes
        if (auto translation = translate(input_state, mva); !translation.faulted()) {
            auto pa = translation.result_address();
            // update memory
            instr->arch_state_output = std::move(instr->arch_state_output).write(pa, data);
        }
    }
    static void mem_write_128bit(archx::InstructionIS* instr,
                                 const VirtAddr& va,
                                 const std::array<std::uint64_t, 2> data)
    {
        const VirtAddr va1 = va & ~7ULL;       // FIXME: handle unaligned writes
        const VirtAddr va2 = (va + 8) & ~7ULL; // FIXME: handle unaligned writes
        mem_write_64bit(instr, va1, data[0]);
        mem_write_64bit(instr, va2, data[1]);
    }

    static std::optional<std::uint64_t> mem_read(archx::InstructionIS* instr, const VirtAddr& va, const unsigned bytes)
    {
        DBGEMU("mem_read va = {:016x}", va.value());
        const ArchState& input_state = instr->state_ref(); // input
        if (auto translation = translate(input_state, va); !translation.faulted()) {
            auto pa = translation.result_address();
            std::uint64_t mem_val = input_state.read(pa, bytes);
            DBGEMU("\tva: {:016x} pa: {:016x} memval: {:016x}", va.value(), pa.value(), mem_val);
            return mem_val;
        }
        return std::nullopt;
    }

    static std::optional<__uint128_t> mem_read_128(archx::InstructionIS* instr, const VirtAddr& va)
    {
        const auto mval1 = mem_read(instr, va, 8);
        const auto mval2 = mem_read(instr, va + 8, 8);
        __uint128_t result = 0;
        if (mval2.has_value()) {
            result = mval2.value();
            result = result << 64;
        }
        if (mval1.has_value()) {
            result |= mval1.value();
        }
        if (mval1.has_value() || mval2.has_value()) {
            return result;
        }
        return std::nullopt;
    }

    enum addressing_mode_t {
        DEFAULT,
        PRE_INCREMENT,
        POST_INCREMENT
    };

    static bool emulate_ldp_d(archx::InstructionIS* instr, addressing_mode_t amode = addressing_mode_t::DEFAULT)
    {
        const auto itext = instr->opcode();
        const auto rt1 = adis::fldfrom_LDP_D_ldstpair_off(itext, adis::fields_t::Rt);
        const auto rt2 = adis::fldfrom_LDP_D_ldstpair_off(itext, adis::fields_t::Rt2);
        const auto rn = adis::fldfrom_LDP_D_ldstpair_off(itext, adis::fields_t::Rn); // can be SP
        const auto imm7 = adis::fldfrom_LDP_D_ldstpair_off(itext, adis::fields_t::imm7);
        const ArchState& input_state = instr->state_ref(); // input
        const auto base_reg = archx::cvt_gpr(rn, archx::LReg::SP);
        const auto regvaln = input_state.read(base_reg);
        const auto offset = sign_extend(imm7, 7) * 8;
        VirtAddr va;
        if (amode != addressing_mode_t::POST_INCREMENT) {
            va = VirtAddr(regvaln + offset);
        } else {
            va = VirtAddr(regvaln);
        }
        instr->add_emulated_virt_addr(va);
        instr->add_emulated_virt_addr(va + 8); // FIXME do we have 2 addresses for LDP?
        const auto mval1 = mem_read(instr, va, 8);
        // FIXME: ignoring case where rt1==rt2
        if (mval1.has_value()) {
            instr->write_vfp(archx::cvt_vec(rt1), mval1.value());
        }
        const auto mval2 = mem_read(instr, va + 8, 8);
        if (mval2.has_value()) {
            instr->write_vfp(archx::cvt_vec(rt2), mval2.value());
        }

        switch (amode) {
        case addressing_mode_t::PRE_INCREMENT:
            instr->write_gpr64(base_reg, va.value());
            break;
        case addressing_mode_t::POST_INCREMENT:
            instr->write_gpr64(base_reg, va.value() + offset);
            break;
        case addressing_mode_t::DEFAULT:
            break;
        }
        return true;
    }
    static bool emulate_ldp_q(archx::InstructionIS* instr)
    {
        return emulate_ldp_q_common(instr, addressing_mode_t::DEFAULT);
    }
    static bool emulate_ldp_q_pre(archx::InstructionIS* instr)
    {
        return emulate_ldp_q_common(instr, addressing_mode_t::PRE_INCREMENT);
    }
    static bool emulate_ldp_q_post(archx::InstructionIS* instr)
    {
        return emulate_ldp_q_common(instr, addressing_mode_t::POST_INCREMENT);
    }
    static bool emulate_ldp_q_common(archx::InstructionIS* instr, addressing_mode_t amode = addressing_mode_t::DEFAULT)
    {
        const auto itext = instr->opcode();
        const auto rt1 = adis::fldfrom_LDP_Q_ldstpair_off(itext, adis::fields_t::Rt);
        const auto rt2 = adis::fldfrom_LDP_Q_ldstpair_off(itext, adis::fields_t::Rt2);
        const auto rn = adis::fldfrom_LDP_Q_ldstpair_off(itext, adis::fields_t::Rn); // can be SP
        const auto imm7 = adis::fldfrom_LDP_Q_ldstpair_off(itext, adis::fields_t::imm7);
        const ArchState& input_state = instr->state_ref(); // input
        const auto base_reg = archx::cvt_gpr(rn, archx::LReg::SP);
        const auto regvaln = input_state.read(base_reg);
        const auto offset = sign_extend(imm7, 7) * 16;
        VirtAddr va;
        if (amode != addressing_mode_t::POST_INCREMENT) {
            va = VirtAddr(regvaln + offset);
        } else {
            va = VirtAddr(regvaln);
        }
        instr->add_emulated_virt_addr(va);
        instr->add_emulated_virt_addr(va + 16); // IDR does not use 2nd address for LDP; can omit this one
        // FIXME: ignoring case where rt1==rt2

        const auto mval1 = mem_read_128(instr, va);
        if (mval1.has_value()) {
            instr->write_vfp(archx::cvt_vec(rt1), mval1.value());
        }
        const auto mval2 = mem_read_128(instr, va + 16);
        if (mval2.has_value()) {
            instr->write_vfp(archx::cvt_vec(rt2), mval2.value());
        }

        switch (amode) {
        case addressing_mode_t::PRE_INCREMENT:
            instr->write_gpr64(base_reg, va.value());
            break;
        case addressing_mode_t::POST_INCREMENT:
            instr->write_gpr64(base_reg, va.value() + offset);
            break;
        case addressing_mode_t::DEFAULT:
            break;
        }
        return true;
    }

    static bool emulate_ldpsw(archx::InstructionIS* instr)
    {
        return emulate_ldpsw_common(instr, addressing_mode_t::DEFAULT);
    }
    static bool emulate_ldpsw_pre(archx::InstructionIS* instr)
    {
        return emulate_ldpsw_common(instr, addressing_mode_t::PRE_INCREMENT);
    }
    static bool emulate_ldpsw_post(archx::InstructionIS* instr)
    {
        return emulate_ldpsw_common(instr, addressing_mode_t::POST_INCREMENT);
    }
    static bool emulate_ldpsw_common(archx::InstructionIS* instr, addressing_mode_t amode = addressing_mode_t::DEFAULT)
    {
        const auto itext = instr->opcode();
        const auto rt1 = adis::fldfrom_LDPSW_64_ldstpair_off(itext, adis::fields_t::Rt);
        const auto rt2 = adis::fldfrom_LDPSW_64_ldstpair_off(itext, adis::fields_t::Rt2);
        const auto rn = adis::fldfrom_LDPSW_64_ldstpair_off(itext, adis::fields_t::Rn); // can be SP
        const auto imm7 = adis::fldfrom_LDPSW_64_ldstpair_off(itext, adis::fields_t::imm7);
        const ArchState& input_state = instr->state_ref(); // input
        const auto base_reg = archx::cvt_gpr(rn, archx::LReg::SP);
        const auto regvaln = input_state.read(base_reg);
        const auto offset = sign_extend(imm7, 7) * 4;
        VirtAddr va;
        if (amode != addressing_mode_t::POST_INCREMENT) {
            va = VirtAddr(regvaln + offset);
        } else {
            va = VirtAddr(regvaln);
        }
        instr->add_emulated_virt_addr(va);
        instr->add_emulated_virt_addr(va + 4); // IDR does not use 2nd address for LDP; can omit this one
        // FIXME: ignoring case where rt1==rt2
        if (rt1 != 31) {
            const auto mval1 = mem_read(instr, va, 4);
            if (mval1.has_value()) {
                instr->write_gpr64(archx::cvt_gpr(rt1), sign_extend(mval1.value(), 32));
            }
        }
        if (rt2 != 31) {
            const auto mval2 = mem_read(instr, va + 4, 4);
            if (mval2.has_value()) {
                instr->write_gpr64(archx::cvt_gpr(rt2), sign_extend(mval2.value(), 32));
            }
        }

        switch (amode) {
        case addressing_mode_t::PRE_INCREMENT:
            instr->write_gpr64(base_reg, va.value());
            break;
        case addressing_mode_t::POST_INCREMENT:
            instr->write_gpr64(base_reg, va.value() + offset);
            break;
        case addressing_mode_t::DEFAULT:
            break;
        }
        return true;
    }

    static bool emulate_ldp_s(archx::InstructionIS* instr)
    {
        return emulate_ldp_s_common(instr, addressing_mode_t::DEFAULT);
    }
    static bool emulate_ldp_s_pre(archx::InstructionIS* instr)
    {
        return emulate_ldp_s_common(instr, addressing_mode_t::PRE_INCREMENT);
    }
    static bool emulate_ldp_s_post(archx::InstructionIS* instr)
    {
        return emulate_ldp_s_common(instr, addressing_mode_t::POST_INCREMENT);
    }
    static bool emulate_ldp_s_common(archx::InstructionIS* instr, addressing_mode_t amode = addressing_mode_t::DEFAULT)
    {
        const auto itext = instr->opcode();
        const auto rt1 = adis::fldfrom_LDP_S_ldstpair_off(itext, adis::fields_t::Rt);
        const auto rt2 = adis::fldfrom_LDP_S_ldstpair_off(itext, adis::fields_t::Rt2);
        const auto rn = adis::fldfrom_LDP_S_ldstpair_off(itext, adis::fields_t::Rn); // can be SP
        const auto imm7 = adis::fldfrom_LDP_S_ldstpair_off(itext, adis::fields_t::imm7);
        const ArchState& input_state = instr->state_ref(); // input
        const auto base_reg = archx::cvt_gpr(rn, archx::LReg::SP);
        const auto regvaln = input_state.read(base_reg);
        const auto offset = sign_extend(imm7, 7) * 4;
        VirtAddr va;
        if (amode != addressing_mode_t::POST_INCREMENT) {
            va = VirtAddr(regvaln + offset);
        } else {
            va = VirtAddr(regvaln);
        }
        instr->add_emulated_virt_addr(va);
        instr->add_emulated_virt_addr(va + 4); // IDR does not use 2nd address for LDP; can omit this one
        const auto mval1 = mem_read(instr, va, 4);
        // FIXME: ignoring case where rt1==rt2
        if (mval1.has_value()) {
            instr->write_vfp(archx::cvt_vec(rt1), mval1.value());
        }
        const auto mval2 = mem_read(instr, va + 4, 4);
        if (mval2.has_value()) {
            instr->write_vfp(archx::cvt_vec(rt2), mval2.value());
        }

        switch (amode) {
        case addressing_mode_t::PRE_INCREMENT:
            instr->write_gpr64(base_reg, va.value());
            break;
        case addressing_mode_t::POST_INCREMENT:
            instr->write_gpr64(base_reg, va.value() + offset);
            break;
        case addressing_mode_t::DEFAULT:
            break;
        }
        return true;
    }

    static bool emulate_stp_q_off(archx::InstructionIS* instr)
    {
        return emulate_stp_q(instr, addressing_mode_t::DEFAULT);
    }
    static bool emulate_stp_q_pre(archx::InstructionIS* instr)
    {
        return emulate_stp_q(instr, addressing_mode_t::PRE_INCREMENT);
    }
    static bool emulate_stp_q_post(archx::InstructionIS* instr)
    {
        return emulate_stp_q(instr, addressing_mode_t::POST_INCREMENT);
    }
    static bool emulate_stp_q(archx::InstructionIS* instr, addressing_mode_t amode = addressing_mode_t::DEFAULT)
    {
        const auto itext = instr->opcode();
        const auto rt1 = adis::fldfrom_STP_Q_ldstpair_off(itext, adis::fields_t::Rt);
        const auto rt2 = adis::fldfrom_STP_Q_ldstpair_off(itext, adis::fields_t::Rt2);
        const auto rn = adis::fldfrom_STP_Q_ldstpair_off(itext, adis::fields_t::Rn); // can be SP
        const auto imm7 = adis::fldfrom_STP_Q_ldstpair_off(itext, adis::fields_t::imm7);
        const ArchState& input_state = instr->state_ref(); // input
        const auto base_reg = archx::cvt_gpr(rn, archx::LReg::SP);
        const auto regvaln = input_state.read(base_reg);
        const auto offset = sign_extend(imm7, 7) * 16;
        VirtAddr va;
        if (amode != addressing_mode_t::POST_INCREMENT) {
            va = VirtAddr(regvaln + offset);
        } else {
            va = VirtAddr(regvaln);
        }
        instr->add_emulated_virt_addr(va);
        instr->add_emulated_virt_addr(va + 16); // FIXME do we have 2 addresses for LDP?
        const auto src_data1 = input_state.read_vfp(archx::cvt_vec(rt1));
        const auto src_data2 = input_state.read_vfp(archx::cvt_vec(rt2));
        mem_write_64bit(instr, va, src_data1[0]);
        mem_write_64bit(instr, va + 8, src_data1[1]);
        mem_write_64bit(instr, va + 16, src_data2[0]);
        mem_write_64bit(instr, va + 24, src_data2[1]);
        switch (amode) {
        case addressing_mode_t::PRE_INCREMENT:
            instr->write_gpr64(base_reg, va.value());
            break;
        case addressing_mode_t::POST_INCREMENT:
            instr->write_gpr64(base_reg, va.value() + offset);
            break;
        case addressing_mode_t::DEFAULT:
            break;
        }
        return true;
    }

    static bool emulate_stp_s_off(archx::InstructionIS* instr)
    {
        return emulate_stp_s(instr, addressing_mode_t::DEFAULT);
    }
    static bool emulate_stp_s_pre(archx::InstructionIS* instr)
    {
        return emulate_stp_s(instr, addressing_mode_t::PRE_INCREMENT);
    }
    static bool emulate_stp_s_post(archx::InstructionIS* instr)
    {
        return emulate_stp_s(instr, addressing_mode_t::POST_INCREMENT);
    }
    static bool emulate_stp_s(archx::InstructionIS* instr, addressing_mode_t amode = addressing_mode_t::DEFAULT)
    {
        const auto itext = instr->opcode();
        const auto rt1 = adis::fldfrom_STP_S_ldstpair_off(itext, adis::fields_t::Rt);
        const auto rt2 = adis::fldfrom_STP_S_ldstpair_off(itext, adis::fields_t::Rt2);
        const auto rn = adis::fldfrom_STP_S_ldstpair_off(itext, adis::fields_t::Rn); // can be SP
        const auto imm7 = adis::fldfrom_STP_S_ldstpair_off(itext, adis::fields_t::imm7);
        const ArchState& input_state = instr->state_ref(); // input
        const auto base_reg = archx::cvt_gpr(rn, archx::LReg::SP);
        const auto regvaln = input_state.read(base_reg);
        const auto offset = sign_extend(imm7, 7) * 4;
        VirtAddr va;
        if (amode != addressing_mode_t::POST_INCREMENT) {
            va = VirtAddr(regvaln + offset);
        } else {
            va = VirtAddr(regvaln);
        }
        instr->add_emulated_virt_addr(va);
        instr->add_emulated_virt_addr(va + 8); // FIXME do we have 2 addresses for LDP?
        const auto src_data1 = input_state.read_vfp(archx::cvt_vec(rt1));
        const auto src_data2 = input_state.read_vfp(archx::cvt_vec(rt2));
        mem_write_64bit(instr, va, src_data1[0]);
        mem_write_64bit(instr, va + 8, src_data2[0]);

        switch (amode) {
        case addressing_mode_t::PRE_INCREMENT:
            instr->write_gpr64(base_reg, va.value());
            break;
        case addressing_mode_t::POST_INCREMENT:
            instr->write_gpr64(base_reg, va.value() + offset);
            break;
        case addressing_mode_t::DEFAULT:
            break;
        }
        return true;
    }

    static bool emulate_stp_d_off(archx::InstructionIS* instr)
    {
        return emulate_stp_d(instr, addressing_mode_t::DEFAULT);
    }
    static bool emulate_stp_d_pre(archx::InstructionIS* instr)
    {
        return emulate_stp_d(instr, addressing_mode_t::PRE_INCREMENT);
    }
    static bool emulate_stp_d_post(archx::InstructionIS* instr)
    {
        return emulate_stp_d(instr, addressing_mode_t::POST_INCREMENT);
    }
    static bool emulate_stp_d(archx::InstructionIS* instr, addressing_mode_t amode = addressing_mode_t::DEFAULT)
    {
        const auto itext = instr->opcode();
        const auto rt1 = adis::fldfrom_STP_D_ldstpair_off(itext, adis::fields_t::Rt);
        const auto rt2 = adis::fldfrom_STP_D_ldstpair_off(itext, adis::fields_t::Rt2);
        const auto rn = adis::fldfrom_STP_D_ldstpair_off(itext, adis::fields_t::Rn); // can be SP
        const auto imm7 = adis::fldfrom_STP_D_ldstpair_off(itext, adis::fields_t::imm7);
        const ArchState& input_state = instr->state_ref(); // input
        const auto base_reg = archx::cvt_gpr(rn, archx::LReg::SP);
        const auto regvaln = input_state.read(base_reg);
        const auto offset = sign_extend(imm7, 7) * 8;
        VirtAddr va;
        if (amode != addressing_mode_t::POST_INCREMENT) {
            va = VirtAddr(regvaln + offset);
        } else {
            va = VirtAddr(regvaln);
        }
        instr->add_emulated_virt_addr(va);
        instr->add_emulated_virt_addr(va + 8); // FIXME do we have 2 addresses for LDP?
        const auto src_data1 = input_state.read_vfp(archx::cvt_vec(rt1));
        const auto src_data2 = input_state.read_vfp(archx::cvt_vec(rt2));
        mem_write_64bit(instr, va, src_data1[0]);
        mem_write_64bit(instr, va + 8, src_data2[0]);

        switch (amode) {
        case addressing_mode_t::PRE_INCREMENT:
            instr->write_gpr64(base_reg, va.value());
            break;
        case addressing_mode_t::POST_INCREMENT:
            instr->write_gpr64(base_reg, va.value() + offset);
            break;
        case addressing_mode_t::DEFAULT:
            break;
        }
        return true;
    }

    static bool emulate_ldp_32(archx::InstructionIS* instr, addressing_mode_t amode = addressing_mode_t::DEFAULT)
    {
        const auto itext = instr->opcode();
        const auto rt1 = adis::fldfrom_LDP_32_ldstpair_off(itext, adis::fields_t::Rt);
        const auto rt2 = adis::fldfrom_LDP_32_ldstpair_off(itext, adis::fields_t::Rt2);
        const auto rn = adis::fldfrom_LDP_32_ldstpair_off(itext, adis::fields_t::Rn); // can be SP
        const auto imm7 = adis::fldfrom_LDP_32_ldstpair_off(itext, adis::fields_t::imm7);
        const ArchState& input_state = instr->state_ref(); // input
        const auto base_reg = archx::cvt_gpr(rn, archx::LReg::SP);
        const auto regvaln = input_state.read(base_reg);
        const auto offset = sign_extend(imm7, 7) * 4;
        VirtAddr va;
        if (amode != addressing_mode_t::POST_INCREMENT) {
            va = VirtAddr(regvaln + offset);
        } else {
            va = VirtAddr(regvaln);
        }
        instr->add_emulated_virt_addr(va);
        instr->add_emulated_virt_addr(va + 4); // FIXME do we have 2 addresses for LDP?
        const auto mval1 = mem_read(instr, va, 4);
        // FIXME: ignoring case where rt1==rt2
        if (mval1.has_value() && rt1 != 31) {
            instr->write_gpr32(archx::cvt_gpr(rt1), static_cast<std::uint32_t>(mval1.value()));
        }
        const auto mval2 = mem_read(instr, va + 4, 4);
        if (mval2.has_value() && rt2 != 31) {
            instr->write_gpr32(archx::cvt_gpr(rt2), static_cast<std::uint32_t>(mval2.value()));
        }

        switch (amode) {
        case addressing_mode_t::PRE_INCREMENT:
            instr->write_gpr64(base_reg, va.value());
            break;
        case addressing_mode_t::POST_INCREMENT:
            instr->write_gpr64(base_reg, va.value() + offset);
            break;
        case addressing_mode_t::DEFAULT:
            break;
        }
        return true;
    }

    static bool emulate_ldp_64(archx::InstructionIS* instr, addressing_mode_t amode = addressing_mode_t::DEFAULT)
    {
        const auto itext = instr->opcode();
        const auto rt1 = adis::fldfrom_LDP_64_ldstpair_off(itext, adis::fields_t::Rt);
        const auto rt2 = adis::fldfrom_LDP_64_ldstpair_off(itext, adis::fields_t::Rt2);
        const auto rn = adis::fldfrom_LDP_64_ldstpair_off(itext, adis::fields_t::Rn); // can be SP
        const auto imm7 = adis::fldfrom_LDP_64_ldstpair_off(itext, adis::fields_t::imm7);
        const ArchState& input_state = instr->state_ref(); // input
        const auto base_reg = archx::cvt_gpr(rn, archx::LReg::SP);
        const auto regvaln = input_state.read(base_reg);
        const auto offset = sign_extend(imm7, 7) * 8;
        VirtAddr va;
        if (amode != addressing_mode_t::POST_INCREMENT) {
            va = VirtAddr(regvaln + offset);
        } else {
            va = VirtAddr(regvaln);
        }
        instr->add_emulated_virt_addr(va);
        instr->add_emulated_virt_addr(va + 8);
        const auto mval1 = mem_read(instr, va, 8);
        // FIXME: ignoring case where rt1==rt2
        if (mval1.has_value() && rt1 != 31) {
            instr->write_gpr64(archx::cvt_gpr(rt1), mval1.value());
        }
        const auto mval2 = mem_read(instr, va + 8, 8);
        if (mval2.has_value() && rt2 != 31) {
            instr->write_gpr64(archx::cvt_gpr(rt2), mval2.value());
        }

        switch (amode) {
        case addressing_mode_t::PRE_INCREMENT:
            instr->write_gpr64(base_reg, va.value());
            break;
        case addressing_mode_t::POST_INCREMENT:
            instr->write_gpr64(base_reg, va.value() + offset);
            break;
        case addressing_mode_t::DEFAULT:
            break;
        }
        return true;
    }

    static bool emulate_stp_32(archx::InstructionIS* instr, addressing_mode_t amode = addressing_mode_t::DEFAULT)
    {
        const auto itext = instr->opcode();
        const auto rt1 = adis::fldfrom_STP_32_ldstpair_off(itext, adis::fields_t::Rt);
        const auto rt2 = adis::fldfrom_STP_32_ldstpair_off(itext, adis::fields_t::Rt2);
        const auto rn = adis::fldfrom_STP_32_ldstpair_off(itext, adis::fields_t::Rn); // can be SP
        const auto imm7 = adis::fldfrom_STP_32_ldstpair_off(itext, adis::fields_t::imm7);
        const ArchState& input_state = instr->state_ref(); // input
        const auto base_reg = archx::cvt_gpr(rn, archx::LReg::SP);
        const auto regvaln = input_state.read(base_reg);
        const auto offset = sign_extend(imm7, 7) * 4;
        VirtAddr va;
        if (amode != addressing_mode_t::POST_INCREMENT) {
            va = VirtAddr(regvaln + offset);
        } else {
            va = VirtAddr(regvaln);
        }
        instr->add_emulated_virt_addr(va);
        instr->add_emulated_virt_addr(va + 4); // FIXME do we have 2 addresses for STP?
        const auto data1 = rt1 == 31 ? 0 : input_state.read(archx::cvt_gpr(rt1));
        const auto data2 = rt2 == 31 ? 0 : input_state.read(archx::cvt_gpr(rt2));
        mem_write_64bit(instr, va, data1);
        mem_write_64bit(instr, va + 4, data2);

        switch (amode) {
        case addressing_mode_t::PRE_INCREMENT:
            instr->write_gpr64(base_reg, va.value());
            break;
        case addressing_mode_t::POST_INCREMENT:
            instr->write_gpr64(base_reg, va.value() + offset);
            break;
        case addressing_mode_t::DEFAULT:
            break;
        }
        return true;
    }

    static bool emulate_stp_64(archx::InstructionIS* instr, addressing_mode_t amode = addressing_mode_t::DEFAULT)
    {
        const auto itext = instr->opcode();
        const auto rt1 = adis::fldfrom_STP_64_ldstpair_off(itext, adis::fields_t::Rt);
        const auto rt2 = adis::fldfrom_STP_64_ldstpair_off(itext, adis::fields_t::Rt2);
        const auto rn = adis::fldfrom_STP_64_ldstpair_off(itext, adis::fields_t::Rn); // can be SP
        const auto imm7 = adis::fldfrom_STP_64_ldstpair_off(itext, adis::fields_t::imm7);
        const ArchState& input_state = instr->state_ref(); // input
        const auto base_reg = archx::cvt_gpr(rn, archx::LReg::SP);
        const auto regvaln = input_state.read(base_reg);
        const auto offset = sign_extend(imm7, 7) * 8;
        VirtAddr va;
        if (amode != addressing_mode_t::POST_INCREMENT) {
            va = VirtAddr(regvaln + offset);
        } else {
            va = VirtAddr(regvaln);
        }
        // fmt::print(stderr,"emulate_stp_64: base_val={:016x} imm7={:02x} offset={:016x} va={:016x}\n",
        //            regvaln, imm7, offset, va.value());

        instr->add_emulated_virt_addr(va);
        instr->add_emulated_virt_addr(va + 8); // FIXME do we have 2 addresses for STP?
        const auto data1 = rt1 == 31 ? 0 : input_state.read(archx::cvt_gpr(rt1));
        const auto data2 = rt2 == 31 ? 0 : input_state.read(archx::cvt_gpr(rt2));
        mem_write_64bit(instr, va, data1);
        mem_write_64bit(instr, va + 8, data2);

        switch (amode) {
        case addressing_mode_t::PRE_INCREMENT:
            instr->write_gpr64(base_reg, va.value());
            break;
        case addressing_mode_t::POST_INCREMENT:
            instr->write_gpr64(base_reg, va.value() + offset);
            break;
        case addressing_mode_t::DEFAULT:
            break;
        }
        return true;
    }

    static bool emulate_csneg32(archx::InstructionIS* instr)
    { // CSNEG_32_condsel
        const auto rd = adis::fldfrom_CSNEG_32_condsel(instr->opcode(), adis::fields_t::Rd);
        if (rd != 31) {
            const auto cond = adis::fldfrom_CSNEG_32_condsel(instr->opcode(), adis::fields_t::cond);
            const auto rm = adis::fldfrom_CSNEG_32_condsel(instr->opcode(), adis::fields_t::Rm);
            const auto rn = adis::fldfrom_CSNEG_32_condsel(instr->opcode(), adis::fields_t::Rn);
            std::uint32_t result;
            const ArchState& input_state = instr->state_ref(); // input
            if (condition_holds(cond, instr->pstate_nzcv())) {
                result = (rn == 31) ? 0 : input_state.read(archx::cvt_gpr(rn));
            } else {
                std::uint32_t tmp = input_state.read(archx::cvt_gpr(rm));
                result = (rm == 31) ? 0 : ((~tmp) + 1);
            }
            instr->write_gpr32(archx::cvt_gpr(rd), result);
        }
        return true;
    }

    static bool emulate_csneg64(archx::InstructionIS* instr)
    { // CSNEG_64_condsel
        const auto rd = adis::fldfrom_CSNEG_64_condsel(instr->opcode(), adis::fields_t::Rd);
        if (rd != 31) {
            const auto cond = adis::fldfrom_CSNEG_64_condsel(instr->opcode(), adis::fields_t::cond);
            const auto rm = adis::fldfrom_CSNEG_64_condsel(instr->opcode(), adis::fields_t::Rm);
            const auto rn = adis::fldfrom_CSNEG_64_condsel(instr->opcode(), adis::fields_t::Rn);
            std::uint64_t result;
            const ArchState& input_state = instr->state_ref(); // input
            if (condition_holds(cond, instr->pstate_nzcv())) {
                result = (rn == 31) ? 0 : input_state.read(archx::cvt_gpr(rn));
            } else {
                std::uint64_t tmp = input_state.read(archx::cvt_gpr(rm));
                result = (rm == 31) ? 0 : ((~tmp) + 1);
            }
            instr->write_gpr64(archx::cvt_gpr(rd), result);
        }
        return true;
    }

    static bool emulate_movz(archx::InstructionIS* instr, bool size_32bit)
    {
        const auto itext = instr->opcode();
        const auto rd = adis::fldfrom_MOVZ_32_movewide(itext, adis::fields_t::Rd);
        const std::uint64_t imm16 = adis::fldfrom_MOVZ_32_movewide(itext, adis::fields_t::imm16);
        const auto hw = adis::fldfrom_MOVZ_32_movewide(itext, adis::fields_t::hw);
        if (size_32bit && hw > 1) {
            return false; // undefined
        }
        if (rd != 31) {
            const std::uint64_t shift = hw << 4;
            std::uint64_t result = imm16 << shift;
            instr->write_gpr64(archx::cvt_gpr(rd), result);
        }
        return true;
    }

    static bool emulate_movk(archx::InstructionIS* instr, bool size_32bit)
    {
        const auto itext = instr->opcode();
        const auto rd = adis::fldfrom_MOVK_32_movewide(itext, adis::fields_t::Rd);
        const std::uint64_t imm16 = adis::fldfrom_MOVK_32_movewide(itext, adis::fields_t::imm16);
        const auto hw = adis::fldfrom_MOVK_32_movewide(itext, adis::fields_t::hw);
        if (size_32bit && hw > 1) {
            return false; // undefined
        }
        const ArchState& input_state = instr->state_ref();
        if (rd != 31) {
            const std::uint64_t orig_value = input_state.read(archx::cvt_gpr(rd));
            const std::uint64_t shift = hw << 4;
            const std::uint64_t orval = imm16 << shift;
            const std::uint64_t mask = ~(0xFFFFULL << shift);
            const std::uint64_t intermed = orig_value & mask;
            std::uint64_t result = intermed | orval;
            if (size_32bit) {
                result = static_cast<std::uint32_t>(result);
            }
            instr->write_gpr64(archx::cvt_gpr(rd), result);
        }
        return true;
    }

    static bool emulate_movn(archx::InstructionIS* instr, bool size_32bit)
    {
        const auto itext = instr->opcode();
        const auto rd = adis::fldfrom_MOVN_32_movewide(itext, adis::fields_t::Rd);
        const std::uint64_t imm16 = adis::fldfrom_MOVN_32_movewide(itext, adis::fields_t::imm16);
        const auto hw = adis::fldfrom_MOVN_32_movewide(itext, adis::fields_t::hw);
        if (size_32bit && hw > 1) {
            return false; // undefined
        }
        if (rd != 31) {
            const std::uint64_t shift = hw << 4;
            std::uint64_t result = imm16 << shift;
            if (size_32bit) {
                result = static_cast<std::uint32_t>(~result);
            } else {
                result = ~result;
            }
            instr->write_gpr64(archx::cvt_gpr(rd), result);
        }
        return true;
    }
    static bool emulate_subs_32_ext(archx::InstructionIS* instr)
    {
        return emulate_subs_32_ext_common(instr, true /* set flags */);
    }
    static bool emulate_sub_32_ext(archx::InstructionIS* instr)
    {
        return emulate_subs_32_ext_common(instr, false /* no set flags */);
    }
    static bool emulate_subs_64_ext(archx::InstructionIS* instr)
    {
        return emulate_subs_64_ext_common(instr, true /* set flags */);
    }
    static bool emulate_sub_64_ext(archx::InstructionIS* instr)
    {
        return emulate_subs_64_ext_common(instr, false /* no set flags */);
    }
    static bool emulate_subs_32_ext_common(archx::InstructionIS* instr, bool set_flags)
    {
        const auto itext = instr->opcode();
        const auto shift = adis::fldfrom_SUBS_32S_addsub_ext(itext, adis::fields_t::imm3);
        if (shift >= 5) {
            return false; // undefined
        }
        const auto rn = adis::fldfrom_SUBS_32S_addsub_ext(itext, adis::fields_t::Rn);
        const auto rm = adis::fldfrom_SUBS_32S_addsub_ext(itext, adis::fields_t::Rm);
        const ArchState& input_state = instr->state_ref();
        const auto nval = input_state.read(archx::cvt_gpr(rn, archx::LReg::SP));
        const std::uint32_t mval = rm == 31 ? 0 : input_state.read(archx::cvt_gpr(rm));
        const auto option = adis::fldfrom_SUBS_32S_addsub_ext(itext, adis::fields_t::option);
        std::uint32_t mval_mod = 0;
        switch (option) { // modify mval
        case 0b000:       // UXTB
            mval_mod = mval & 0xFFULL;
            break;
        case 0b001: // UXTH
            mval_mod = mval & 0xFFFFULL;
            break;
        case 0b010: // LSL/UXTW
            mval_mod = mval;
            break;
        case 0b011: // UXTX
            mval_mod = mval;
            break;
        case 0b100: // SXTB
            mval_mod = sign_extend(mval & 0xFFULL, 8);
            break;
        case 0b101: // SXTH
            mval_mod = sign_extend(mval & 0xFFFFULL, 16);
            break;
        case 0b110: // SXTW
            mval_mod = mval;
            break;
        case 0b111: // SXTX
            mval_mod = mval;
            break;
        default: // not possible
            break;
        }

        mval_mod = mval_mod << shift;
        const std::uint32_t operand2 = ~mval_mod;
        const uint64_t uresult = uresult64(nval, operand2, 1);
        const std::uint32_t result = static_cast<std::uint32_t>(uresult);

        // if set_flags == true && rd == 31, we do not write dest
        // if set_flags == false && rd == 31, we do not write SP
        const auto rd = adis::fldfrom_SUBS_32S_addsub_ext(itext, adis::fields_t::Rd);
        if (rd != 31 || !set_flags) {
            const auto lreg_dst = archx::cvt_gpr(rd, archx::LReg::SP); // we drop rd==31 writes if setting flags
            instr->write_gpr64(lreg_dst, result);
        }

        if (set_flags) {
            const std::uint64_t output_flags = flags32(nval, operand2, 1);
            instr->write_flags(output_flags);
        }
        return true;
    }
    static bool emulate_subs_64_ext_common(archx::InstructionIS* instr, bool set_flags)
    {
        const auto itext = instr->opcode();
        const auto shift = adis::fldfrom_SUBS_64S_addsub_ext(itext, adis::fields_t::imm3);
        if (shift >= 5) {
            return false; // undefined
        }
        const auto rn = adis::fldfrom_SUBS_64S_addsub_ext(itext, adis::fields_t::Rn);
        const auto rm = adis::fldfrom_SUBS_64S_addsub_ext(itext, adis::fields_t::Rm);
        const ArchState& input_state = instr->state_ref();
        const auto nval = input_state.read(archx::cvt_gpr(rn, archx::LReg::SP));
        const auto mval = rm == 31 ? 0 : input_state.read(archx::cvt_gpr(rm));
        const auto option = adis::fldfrom_SUBS_64S_addsub_ext(itext, adis::fields_t::option);
        std::uint64_t mval_mod = 0;
        switch (option) { // modify mval
        case 0b000:       // UXTB
            mval_mod = mval & 0xFFULL;
            break;
        case 0b001: // UXTH
            mval_mod = mval & 0xFFFFULL;
            break;
        case 0b010: // UXTW
            mval_mod = mval & 0xFFFF'FFFFULL;
            break;
        case 0b011: // LSL/UXTX
            mval_mod = mval;
            break;
        case 0b100: // SXTB
            mval_mod = sign_extend(mval & 0xFFULL, 8);
            break;
        case 0b101: // SXTH
            mval_mod = sign_extend(mval & 0xFFFFULL, 16);
            break;
        case 0b110: // SXTW
            mval_mod = sign_extend(mval & 0xFFFF'FFFFULL, 32);
            break;
        case 0b111: // SXTX
            mval_mod = mval;
            break;
        default: // not possible
            break;
        }

        mval_mod = mval_mod << shift;
        const std::uint64_t operand2 = ~mval_mod;
        const __uint128_t uresult = uresult128(nval, operand2, 1);
        const std::uint64_t result = static_cast<std::uint64_t>(uresult);

        // if set_flags == true && rd == 31, we do not write dest
        // if set_flags == false && rd == 31, we do not write SP
        const auto rd = adis::fldfrom_SUBS_64S_addsub_ext(itext, adis::fields_t::Rd);
        if (rd != 31 || !set_flags) {
            const auto lreg_dst = archx::cvt_gpr(rd, archx::LReg::SP); // we drop rd==31 writes if setting flags
            instr->write_gpr64(lreg_dst, result);
        }

        if (set_flags) {
            const std::uint64_t output_flags = flags64(nval, operand2, 1);
            instr->write_flags(output_flags);
        }
        return true;
    }

    static bool emulate_sub_64_addsub_shift(archx::InstructionIS* instr, bool set_pstate_flags)
    {
        const auto itext = instr->opcode();
        auto shift_type = adis::fldfrom_SUBS_64_addsub_shift(itext, adis::fields_t::shift); // shift/rotate type
        if (shift_type == 3) {
            return false; // undefined
        }
        auto rn = adis::fldfrom_SUBS_64_addsub_shift(itext, adis::fields_t::Rn);
        auto rm = adis::fldfrom_SUBS_64_addsub_shift(itext, adis::fields_t::Rm); // may be shifted
        auto rd = adis::fldfrom_SUBS_64_addsub_shift(itext, adis::fields_t::Rd);
        auto imm6 = adis::fldfrom_SUBS_64_addsub_shift(itext, adis::fields_t::imm6); // shift/rotate amount
        auto lreg_srcm = archx::cvt_gpr(rm);
        auto lreg_srcn = archx::cvt_gpr(rn);
        const ArchState& input_state = instr->state_ref();
        std::uint64_t regval_srcm = rm == 31 ? 0 : input_state.read(lreg_srcm);
        std::uint64_t regval_srcn = rn == 31 ? 0 : input_state.read(lreg_srcn);
        std::uint64_t regval_srcm_shifted = 0;
        switch (shift_type) {
        case 0: // lsl
            regval_srcm_shifted = regval_srcm << imm6;
            break;
        case 1: // lsr
            regval_srcm_shifted = regval_srcm >> imm6;
            break;
        case 2: // asr
            regval_srcm_shifted = static_cast<std::int64_t>(regval_srcm) >> imm6;
            break;
        default:
            return false; // illegal, do not emulate
        }
        regval_srcm_shifted = ~regval_srcm_shifted; // not, invert
        const __uint128_t uresult = uresult128(regval_srcn, regval_srcm_shifted, 1);
        const std::uint64_t result = static_cast<std::uint64_t>(uresult);
        if (set_pstate_flags) {
            const std::uint64_t output_flags = flags64(regval_srcn, regval_srcm_shifted, 1);
            instr->write_flags(output_flags);
        }
        if (rd != 31) {
            auto lreg_dst = archx::cvt_gpr(rd);
            instr->write_gpr64(lreg_dst, result);
        }
        return true;
    }
    static bool emulate_sub_32_addsub_shift(archx::InstructionIS* instr, bool set_pstate_flags)
    {
        const auto itext = instr->opcode();
        const auto shift_type = adis::fldfrom_SUBS_32_addsub_shift(itext, adis::fields_t::shift); // shift/rotate type
        if (shift_type == 3) {
            return false; // undefined
        }
        const auto rn = adis::fldfrom_SUBS_32_addsub_shift(itext, adis::fields_t::Rn);
        const auto rm = adis::fldfrom_SUBS_32_addsub_shift(itext, adis::fields_t::Rm); // may be shifted
        const auto rd = adis::fldfrom_SUBS_32_addsub_shift(itext, adis::fields_t::Rd);
        const auto imm6 = adis::fldfrom_SUBS_32_addsub_shift(itext, adis::fields_t::imm6); // shift/rotate amount
        if (imm6 & 32) {
            return false; // illegal, do not emulate
        }

        const auto lreg_srcm = archx::cvt_gpr(rm);
        const auto lreg_srcn = archx::cvt_gpr(rn);
        const ArchState& input_state = instr->state_ref();
        const std::uint32_t regval_srcm = rm == 31 ? 0 : input_state.read(lreg_srcm);
        const std::uint32_t regval_srcn = rn == 31 ? 0 : input_state.read(lreg_srcn);
        std::uint32_t regval_srcm_shifted = 0;
        switch (shift_type) {
        case 0: // lsl
            regval_srcm_shifted = regval_srcm << imm6;
            break;
        case 1: // lsr
            regval_srcm_shifted = regval_srcm >> imm6;
            break;
        case 2: // asr
            regval_srcm_shifted = static_cast<std::int32_t>(regval_srcm) >> imm6;
            break;
        default:
            return false; // illegal, do not emulate
        }
        regval_srcm_shifted = ~regval_srcm_shifted; // not, invert
        if (set_pstate_flags) {
            const std::uint64_t output_flags = flags32(regval_srcn, regval_srcm_shifted, 1);
            instr->write_flags(output_flags);
        }
        if (rd != 31) {
            const std::uint32_t result = regval_srcn + regval_srcm_shifted + 1;
            const auto lreg_dst = archx::cvt_gpr(rd);
            instr->write_gpr32(lreg_dst, result);
        }
        return true;
    }

    static std::int64_t sresult64(std::int32_t op1, std::int32_t op2, std::int32_t carry = 0)
    {
        return static_cast<std::int64_t>(op1) + static_cast<std::int64_t>(op2) + carry;
    }
    static __int128_t sresult128(std::int64_t op1, std::int64_t op2, std::int64_t carry = 0)
    {
        return static_cast<__int128_t>(op1) + static_cast<__int128_t>(op2) + carry;
    }

    static std::uint64_t uresult64(std::uint32_t op1, std::uint32_t op2, std::uint32_t carry = 0)
    {
        return static_cast<std::uint64_t>(op1) + static_cast<std::uint64_t>(op2) + static_cast<uint64_t>(carry);
    }
    static __uint128_t uresult128(std::uint64_t op1, std::uint64_t op2, std::uint64_t carry = 0)
    {
        return static_cast<__uint128_t>(op1) + static_cast<__uint128_t>(op2) + static_cast<__uint128_t>(carry);
    }

    static std::uint64_t flags32(std::uint32_t op1, std::uint32_t op2, std::uint32_t carry = 0)
    {
        const std::uint64_t uresult = uresult64(op1, op2, carry);
        const std::int64_t sresult = sresult64(op1, op2, carry);
        const std::uint32_t result = static_cast<std::uint32_t>(uresult);
        // must cast result to signed before extending to signed 64b else we lose the sign extension
        const std::int32_t iresult = static_cast<std::int32_t>(result);
        const std::int64_t iresult_wide = static_cast<std::int64_t>(iresult);

        const std::uint32_t n = (result >> 31) & 1;
        const std::uint32_t z = (result == 0);
        const std::uint32_t c = (static_cast<std::uint64_t>(result) == uresult) ? 0 : 1;
        const std::uint32_t v = (iresult_wide == sresult) ? 0 : 1;
        const std::uint64_t output_flags = pstate_nzcv(n, z, c, v);
        return output_flags;
    }

    static std::uint64_t flags64(std::uint64_t op1, std::uint64_t op2, std::uint64_t carry = 0)
    {
        const __uint128_t uresult = uresult128(op1, op2, carry);
        const __int128_t sresult = sresult128(op1, op2, carry);
        const std::uint64_t result = static_cast<std::uint64_t>(uresult);
        // must cast result to signed before extending to signed 128b else we lose the sign extension
        const std::int64_t iresult = static_cast<std::int64_t>(result);
        const std::int64_t iresult_wide = static_cast<__int128_t>(iresult);
#if 0
        fmt::print(stderr, "flags64: uresult={:032x}\n", uresult);
        fmt::print(stderr, "flags64: sresult={:032x}\n", static_cast<__uint128_t>(sresult));
        fmt::print(stderr, "flags64:  result=                {:016x}\n", result);
        fmt::print(stderr, "flags64:  sif128={:032x}\n", static_cast<__uint128_t>(static_cast<__int128_t>(result)));
        fmt::print(stderr, "flags64:iresultw={:032x}\n", static_cast<__uint128_t>(iresult_wide));
#endif
        const std::uint32_t n = (result >> 63) & 1;
        const std::uint32_t z = (result == 0);
        const std::uint32_t c = (static_cast<__uint128_t>(result) == uresult) ? 0 : 1;
        const std::uint32_t v = (iresult_wide == sresult) ? 0 : 1;
        const std::uint64_t output_flags = pstate_nzcv(n, z, c, v);
        return output_flags;
    }

    static bool emulate_sub_64_addsub_imm(archx::InstructionIS* instr, bool set_pstate_flags)
    {
        const auto itext = instr->opcode();
        const auto rd = adis::fldfrom_SUBS_64S_addsub_imm(itext, adis::fields_t::Rd); // may be SP if not setting flags
        const auto rn = adis::fldfrom_SUBS_64S_addsub_imm(itext, adis::fields_t::Rn); // may be SP
        const auto imm12 = adis::fldfrom_SUBS_64S_addsub_imm(itext, adis::fields_t::imm12);
        const auto shift = adis::fldfrom_SUBS_64S_addsub_imm(itext, adis::fields_t::sh);
        const auto lreg_srcn = archx::cvt_gpr(rn, archx::LReg::SP);
        const ArchState& input_state = instr->state_ref();
        const std::uint64_t regval_srcn = input_state.read(lreg_srcn);

        const std::uint64_t imm_shifted = shift ? (imm12 << 12) : imm12;
        const std::uint64_t operand2 = ~imm_shifted;

        const __uint128_t uresult = uresult128(regval_srcn, operand2, 1);
        const std::uint64_t result = static_cast<std::uint64_t>(uresult);

#if 0
        fmt::print(stderr,"emulate_sub_64_addsub_imm: op1={:016x} op2={:016x} result={:016x} flags {:x}/{}\n",
                   regval_srcn, imm_shifted, result,
                   flags64(regval_srcn, operand2, 1)>>28,
                   archx::nzcv_str(flags64(regval_srcn, operand2, 1)));
#endif

        if (set_pstate_flags) {
            const std::uint64_t output_flags = flags64(regval_srcn, operand2, 1);
            instr->write_flags(output_flags);
        }
        if (rd != 31 || !set_pstate_flags) {
            const auto lreg_dst = archx::cvt_gpr(rd, archx::LReg::SP); // we drop rd==31 writes if setting flags
            instr->write_gpr64(lreg_dst, result);
        }
        return true;
    }

    static bool emulate_sub_32_addsub_imm(archx::InstructionIS* instr, bool set_pstate_flags)
    {
        const auto itext = instr->opcode();
        const auto rd = adis::fldfrom_SUBS_32S_addsub_imm(itext, adis::fields_t::Rd);
        const auto rn = adis::fldfrom_SUBS_32S_addsub_imm(itext, adis::fields_t::Rn); // may be SP
        const auto imm12 = adis::fldfrom_SUBS_32S_addsub_imm(itext, adis::fields_t::imm12);
        const auto shift = adis::fldfrom_SUBS_32S_addsub_imm(itext, adis::fields_t::sh);
        const auto lreg_srcn = archx::cvt_gpr(rn, archx::LReg::SP);
        const ArchState& input_state = instr->state_ref();
        const std::uint64_t regval_srcn = input_state.read(lreg_srcn);

        const std::uint64_t imm_shifted = shift ? (imm12 << 12) : imm12;
        const std::uint64_t operand2 = ~imm_shifted;
        const std::uint64_t uresult = uresult64(regval_srcn, operand2, 1);
        const std::uint32_t result = static_cast<std::uint64_t>(uresult);
        if (set_pstate_flags) {
            const std::uint64_t output_flags = flags32(regval_srcn, operand2, 1);
            instr->write_flags(output_flags);
        }
        if (rd != 31) {
            const auto lreg_dst = archx::cvt_gpr(rd); // wzr if 31
            instr->write_gpr32(lreg_dst, result);
        }
        return true;
    }
    static bool emulate_adds_32_addsub_imm(archx::InstructionIS* instr)
    {
        return emulate_add_32_addsub_imm_gen(instr, true);
    }
    static bool emulate_add_32_addsub_imm(archx::InstructionIS* instr)
    {
        return emulate_add_32_addsub_imm_gen(instr, false);
    }
    static bool emulate_add_32_addsub_imm_gen(archx::InstructionIS* instr, bool set_pstate_flags)
    {
        const auto itext = instr->opcode();
        const auto rn = adis::fldfrom_ADDS_32S_addsub_imm(itext, adis::fields_t::Rn); // may be SP
        const auto imm12 = adis::fldfrom_ADDS_32S_addsub_imm(itext, adis::fields_t::imm12);
        const auto shift = adis::fldfrom_ADDS_32S_addsub_imm(itext, adis::fields_t::sh);
        const auto lreg_srcn = archx::cvt_gpr(rn, archx::LReg::SP);
        const ArchState& input_state = instr->state_ref();
        const std::uint64_t regval_srcn = input_state.read(lreg_srcn);

        const std::uint64_t imm_shifted = shift ? (imm12 << 12) : imm12;
        const std::uint64_t operand2 = imm_shifted;
        const std::uint32_t result = regval_srcn + operand2;
        if (set_pstate_flags) {
            const std::uint64_t output_flags = flags32(regval_srcn, operand2);
            instr->write_flags(output_flags);
        }
        const auto rd = adis::fldfrom_ADDS_32S_addsub_imm(itext, adis::fields_t::Rd);
        if (rd != 31) {
            const auto lreg_dst = archx::cvt_gpr(rd);
            instr->write_gpr32(lreg_dst, result);
        }
        return true;
    }

    static bool emulate_add_64_addsub_imm(archx::InstructionIS* instr, bool set_pstate_flags)
    {
        // if set_flags is true && dest==31 -> drop result
        // if set_flags is false  && dest==31 -> write SP
        const auto itext = instr->opcode();
        const auto rn = adis::fldfrom_ADDS_64S_addsub_imm(itext, adis::fields_t::Rn); // may be SP
        const auto imm12 = adis::fldfrom_ADDS_64S_addsub_imm(itext, adis::fields_t::imm12);
        const auto shift = adis::fldfrom_ADDS_64S_addsub_imm(itext, adis::fields_t::sh);
        const auto lreg_srcn = archx::cvt_gpr(rn, archx::LReg::SP);
        const ArchState& input_state = instr->state_ref();
        const std::uint64_t regval_srcn = input_state.read(lreg_srcn);

        const std::uint64_t imm_shifted = shift ? (imm12 << 12) : imm12;
        const std::uint64_t operand2 = imm_shifted;
        const std::uint64_t result = regval_srcn + operand2;
        if (set_pstate_flags) {
            const std::uint64_t output_flags = flags64(regval_srcn, operand2);
            instr->write_flags(output_flags);
        }
        const auto rd = adis::fldfrom_ADDS_64S_addsub_imm(itext, adis::fields_t::Rd); // may be SP if not writing flags
        if (rd != 31 || !set_pstate_flags) {
            const auto lreg_dst = archx::cvt_gpr(rd, archx::LReg::SP); // we drop rd==31 writes if setting flags
            instr->write_gpr64(lreg_dst, result);
        }
        return true;
    }

    enum logical_enum_t {
        ORR,
        EOR,
        AND,
        ORN,
        BIC
    };

    static bool emulate_32b_logical_log_shift(archx::InstructionIS* instr,
                                              logical_enum_t logical_op,
                                              bool set_flags = false)
    {
        const auto itext = instr->opcode();
        const auto imm6 = adis::fldfrom_ORR_32_log_shift(itext, adis::fields_t::imm6); // shift/rotate amount
        if ((imm6 & 32) == 32) {
            return false; // undefined behavior, do not emulate
        }
        const auto rd = adis::fldfrom_ORR_32_log_shift(itext, adis::fields_t::Rd);

        const auto rn = adis::fldfrom_ORR_32_log_shift(itext, adis::fields_t::Rn);
        const auto rm = adis::fldfrom_ORR_32_log_shift(itext, adis::fields_t::Rm);            // shifted
        const auto shift_type = adis::fldfrom_ORR_32_log_shift(itext, adis::fields_t::shift); // shift/rotate type
        const auto lreg_srcm = archx::cvt_gpr(rm);
        const auto lreg_srcn = archx::cvt_gpr(rn);

        const ArchState& input_state = instr->state_ref();
        const std::uint32_t regval_srcm = rm == 31 ? 0 : input_state.read(lreg_srcm);
        const std::uint32_t regval_srcn = rn == 31 ? 0 : input_state.read(lreg_srcn);
        std::uint32_t regval_srcm_shifted = regval_srcm;
        switch (shift_type) {
        case 0: // lsl
            regval_srcm_shifted = regval_srcm << imm6;
            break;
        case 1: // lsr
            regval_srcm_shifted = regval_srcm >> imm6;
            break;
        case 2: // asr
            regval_srcm_shifted = static_cast<std::int32_t>(regval_srcm) >> imm6;
            break;
        case 3: // ror
            if (imm6) {
                regval_srcm_shifted = (regval_srcm >> imm6) | (regval_srcm << (32 - imm6));
            }
            break;
        default:
            return false; // illegal, do not emulate
        }
        std::uint32_t result = 0;
        switch (logical_op) {
        case logical_enum_t::ORR:
            result = regval_srcm_shifted | regval_srcn;
            break;
        case logical_enum_t::EOR:
            result = regval_srcm_shifted ^ regval_srcn;
            break;
        case logical_enum_t::AND:
            result = regval_srcm_shifted & regval_srcn;
            break;
        case logical_enum_t::ORN:
            result = (~regval_srcm_shifted) | regval_srcn;
            break;
        case logical_enum_t::BIC:
            result = (~regval_srcm_shifted) & regval_srcn;
            break;
        }
        if (rd != 31) {
            const auto lreg_dst = archx::cvt_gpr(rd);
            instr->write_gpr32(lreg_dst, result); // upcasts
        }
        if (set_flags) {
            const uint32_t n = (result >> 31) & 1;
            const uint32_t z = (result == 0);
            const std::uint64_t output_flags = pstate_nzcv(n, z, 0, 0);
            instr->write_flags(output_flags);
        }
        return true;
    }

    static bool emulate_64b_logical_log_shift(archx::InstructionIS* instr,
                                              logical_enum_t logical_op,
                                              bool set_flags = false)
    {
        const auto itext = instr->opcode();
        const auto rd = adis::fldfrom_ORR_64_log_shift(itext, adis::fields_t::Rd);

        const auto rn = adis::fldfrom_ORR_64_log_shift(itext, adis::fields_t::Rn);
        const auto rm = adis::fldfrom_ORR_64_log_shift(itext, adis::fields_t::Rm);            // shifted
        const auto imm6 = adis::fldfrom_ORR_64_log_shift(itext, adis::fields_t::imm6);        // shift/rotate amount
        const auto shift_type = adis::fldfrom_ORR_64_log_shift(itext, adis::fields_t::shift); // shift/rotate type
        const auto lreg_srcm = archx::cvt_gpr(rm);
        const auto lreg_srcn = archx::cvt_gpr(rn);

        const ArchState& input_state = instr->state_ref();
        const std::uint64_t regval_srcm = rm == 31 ? 0 : input_state.read(lreg_srcm);
        const std::uint64_t regval_srcn = rn == 31 ? 0 : input_state.read(lreg_srcn);
        std::uint64_t regval_srcm_shifted = regval_srcm;
        switch (shift_type) {
        case 0: // lsl
            regval_srcm_shifted = regval_srcm << imm6;
            break;
        case 1: // lsr
            regval_srcm_shifted = regval_srcm >> imm6;
            break;
        case 2: // asr
            regval_srcm_shifted = static_cast<std::int64_t>(regval_srcm) >> imm6;
            break;
        case 3: // ror
            if (imm6) {
                regval_srcm_shifted = (regval_srcm >> imm6) | (regval_srcm << (64 - imm6));
            }
            break;
        default:
            return false; // illegal, do not emulate
        }
        std::uint64_t result = 0;
        switch (logical_op) {
        case logical_enum_t::ORR:
            result = regval_srcm_shifted | regval_srcn;
            break;
        case logical_enum_t::EOR:
            result = regval_srcm_shifted ^ regval_srcn;
            break;
        case logical_enum_t::AND:
            result = regval_srcm_shifted & regval_srcn;
            break;
        case logical_enum_t::ORN:
            result = (~regval_srcm_shifted) | regval_srcn;
            break;
        case logical_enum_t::BIC:
            result = (~regval_srcm_shifted) & regval_srcn;
            break;
        }
        if (rd != 31) {
            const auto lreg_dst = archx::cvt_gpr(rd);
            instr->write_gpr64(lreg_dst, result);
        }
        if (set_flags) {
            const uint32_t n = (result >> 63) & 1;
            const uint32_t z = (result == 0);
            const std::uint64_t output_flags = pstate_nzcv(n, z, 0, 0);
            instr->write_flags(output_flags);
        }
        return true;
    }

    static bool emulate_32b_log_imm(archx::InstructionIS* instr, logical_enum_t logical_op, bool set_flags = false)
    {
        const auto itext = instr->opcode();
        const auto imms = adis::fldfrom_AND_32_log_imm(itext, adis::fields_t::imms);
        const auto immr = adis::fldfrom_AND_32_log_imm(itext, adis::fields_t::immr);
        const auto n = adis::fldfrom_AND_32_log_imm(itext, adis::fields_t::N);
        const auto imm13 = (n << 12) | (imms << 6) | immr;
        const adis::u32optpair_t vpair = adis::dec_imm13_32(imm13, /*logical*/ true);
        if (vpair.has_value()) {
            const auto rd = adis::fldfrom_AND_32_log_imm(itext, adis::fields_t::Rd); // may be SP
            const auto rn = adis::fldfrom_AND_32_log_imm(itext, adis::fields_t::Rn);
            const auto lreg_src = archx::cvt_gpr(rn);
            const auto lreg_dst = archx::cvt_gpr(rd, archx::LReg::SP);
            const ArchState& input_state = instr->state_ref();
            const auto regval_src = input_state.read(lreg_src);
            // const auto [wmask, tmask] = vpair.value(); // ignore tmask
            const std::uint32_t wmask = vpair.value().first;

            const std::uint32_t imm = wmask;
            std::uint32_t result = 0;
            switch (logical_op) {
            case logical_enum_t::AND:
                result = regval_src & imm;
                break;
            case logical_enum_t::ORR:
                result = regval_src | imm;
                break;
            case logical_enum_t::EOR:
                result = regval_src ^ imm;
                break;
            default:
                throw std::runtime_error("Should not occur");
                break;
            }
            // the version that sets flags does not write SP!
            // the version that does not set flags, can write SP!
            if ((rd == 31 && !set_flags) || rd != 31) {
                instr->write_gpr32(lreg_dst, result);
            }
            if (set_flags) {
                const uint32_t n = (result >> 31) & 1;
                const uint32_t z = (result == 0);
                const std::uint64_t output_flags = pstate_nzcv(n, z, 0, 0);
                instr->write_flags(output_flags);
            }
            return true;
        }
        return false;
    }
    static bool emulate_64b_log_imm(archx::InstructionIS* instr, logical_enum_t logical_op, bool set_flags = false)
    {
        const auto itext = instr->opcode();
        const auto imms = adis::fldfrom_AND_64_log_imm(itext, adis::fields_t::imms);
        const auto immr = adis::fldfrom_AND_64_log_imm(itext, adis::fields_t::immr);
        const auto n = adis::fldfrom_AND_64_log_imm(itext, adis::fields_t::N);
        const auto imm13 = (n << 12) | (imms << 6) | immr;
        const adis::u64optpair_t vpair = adis::dec_imm13_64(imm13, /*logical*/ true);
        if (vpair.has_value()) {
            const auto rd = adis::fldfrom_AND_64_log_imm(itext, adis::fields_t::Rd); // may be SP
            const auto rn = adis::fldfrom_AND_64_log_imm(itext, adis::fields_t::Rn);
            const auto lreg_src = archx::cvt_gpr(rn);
            const auto lreg_dst = archx::cvt_gpr(rd, archx::LReg::SP); // non-flags setters
            const ArchState& input_state = instr->state_ref();
            const auto regval_src = input_state.read(lreg_src);
            // const auto [wmask, tmask] = vpair.value(); // ignore tmask
            const std::uint64_t wmask = vpair.value().first;

            const std::uint64_t imm = wmask;
            std::uint64_t result = 0;
            switch (logical_op) {
            case logical_enum_t::AND:
                result = regval_src & imm;
                break;
            case logical_enum_t::ORR:
                result = regval_src | imm;
                break;
            case logical_enum_t::EOR:
                result = regval_src ^ imm;
                break;
            default:
                throw std::runtime_error("Should not occur");
                break;
            }
            // the version that sets flags does not write SP!
            // the version that does not set flags, can write SP!
            if ((rd == 31 && !set_flags) || rd != 31) {
                instr->write_gpr64(lreg_dst, result);
            }
            if (set_flags) {
                const uint32_t n = (result >> 63) & 1;
                const uint32_t z = (result == 0);
                const std::uint64_t output_flags = pstate_nzcv(n, z, 0, 0);
                instr->write_flags(output_flags);
            }
            return true;
        }
        return false;
    }
    static bool emulate_add_64_extended(archx::InstructionIS* instr, bool set_flags = false)
    {
        const auto itext = instr->opcode();
        const auto shift = adis::fldfrom_ADD_64_addsub_ext(itext, adis::fields_t::imm3);
        if (shift >= 5) {
            return false; // undefined
        }
        const auto rn = adis::fldfrom_ADD_64_addsub_ext(itext, adis::fields_t::Rn);
        const auto rm = adis::fldfrom_ADD_64_addsub_ext(itext, adis::fields_t::Rm);
        const ArchState& input_state = instr->state_ref();
        const auto nval = input_state.read(archx::cvt_gpr(rn, archx::LReg::SP));
        const auto mval = rm == 31 ? 0 : input_state.read(archx::cvt_gpr(rm));
        const auto option = adis::fldfrom_ADD_64_addsub_ext(itext, adis::fields_t::option);
        std::uint64_t mval_mod = 0;
        switch (option) { // modify mval
        case 0b000:       // UXTB
            mval_mod = mval & 0xFFULL;
            break;
        case 0b001: // UXTH
            mval_mod = mval & 0xFFFFULL;
            break;
        case 0b010: // UXTW
            mval_mod = mval & 0xFFFF'FFFFULL;
            break;
        case 0b011: // LSL/UXTX
            mval_mod = mval;
            break;
        case 0b100: // SXTB
            mval_mod = sign_extend(mval & 0xFFULL, 8);
            break;
        case 0b101: // SXTH
            mval_mod = sign_extend(mval & 0xFFFFULL, 16);
            break;
        case 0b110: // SXTW
            mval_mod = sign_extend(mval & 0xFFFF'FFFFULL, 32);
            break;
        case 0b111: // SXTX
            mval_mod = mval;
            break;
        default: // not possible
            break;
        }

        mval_mod = mval_mod << shift;
        std::uint64_t result = nval + mval_mod;

        // if set_flags == true && rd == 31, we do not write dest
        // if set_flags == false && rd == 31, we do not write SP
        const auto rd = adis::fldfrom_ADD_64_addsub_ext(itext, adis::fields_t::Rd);
        if (rd != 31 || !set_flags) {
            const auto lreg_dst = archx::cvt_gpr(rd, archx::LReg::SP); // we drop rd==31 writes if setting flags
            instr->write_gpr64(lreg_dst, result);
        }

        if (set_flags) {
            const std::uint64_t output_flags = flags64(nval, mval_mod);
            instr->write_flags(output_flags);
        }
        return true;
    }

    static bool emulate_add_32_extended(archx::InstructionIS* instr, bool set_flags = false)
    {
        const auto itext = instr->opcode();
        const auto shift = adis::fldfrom_ADD_32_addsub_ext(itext, adis::fields_t::imm3);
        if (shift >= 5) {
            return false; // undefined
        }
        const auto rn = adis::fldfrom_ADD_32_addsub_ext(itext, adis::fields_t::Rn);
        const auto rm = adis::fldfrom_ADD_32_addsub_ext(itext, adis::fields_t::Rm);
        const ArchState& input_state = instr->state_ref();
        const auto nval = input_state.read(archx::cvt_gpr(rn, archx::LReg::SP));
        const auto mval = rm == 31 ? 0 : input_state.read(archx::cvt_gpr(rm));
        const auto option = adis::fldfrom_ADD_32_addsub_ext(itext, adis::fields_t::option);
        std::uint32_t mval_mod = 0;
        switch (option) { // modify mval
        case 0b000:       // UXTB
            mval_mod = mval & 0xFFULL;
            break;
        case 0b001: // UXTH
            mval_mod = mval & 0xFFFFULL;
            break;
        case 0b010: // LSL/UXTW
            mval_mod = mval;
            break;
        case 0b011: // UXTX
            mval_mod = mval;
            break;
        case 0b100: // SXTB
            mval_mod = sign_extend(mval & 0xFFULL, 8);
            break;
        case 0b101: // SXTH
            mval_mod = sign_extend(mval & 0xFFFFULL, 16);
            break;
        case 0b110: // SXTW
            mval_mod = mval;
            break;
        case 0b111: // SXTX
            mval_mod = mval;
            break;
        default: // not possible
            break;
        }

        mval_mod = mval_mod << shift;
        std::uint32_t result = nval + mval_mod;

        // if set_flags == true && rd == 31, we do not write dest
        // if set_flags == false && rd == 31, we do not write SP
        const auto rd = adis::fldfrom_ADD_32_addsub_ext(itext, adis::fields_t::Rd);
        if (rd != 31 || !set_flags) {
            const auto lreg_dst = archx::cvt_gpr(rd, archx::LReg::SP); // we drop rd==31 writes if setting flags
            instr->write_gpr32(lreg_dst, result);
        }

        if (set_flags) {
            const std::uint64_t output_flags = flags32(nval, mval_mod);
            instr->write_flags(output_flags);
        }
        return true;
    }
    static bool emulate_ccmp64_reg(archx::InstructionIS* instr)
    {
        const auto itext = instr->opcode();
        const auto cond = adis::fldfrom_CCMP_64_condcmp_reg(itext, adis::fields_t::cond);
        if (condition_holds(cond, instr->pstate_nzcv())) {
            const auto rn = adis::fldfrom_CCMP_64_condcmp_reg(itext, adis::fields_t::Rn);
            const auto rm = adis::fldfrom_CCMP_64_condcmp_reg(itext, adis::fields_t::Rm);
            const ArchState& input_state = instr->state_ref();
            const std::uint64_t mval = rm == 31 ? 0 : input_state.read(archx::cvt_gpr(rm));
            const auto nval = rn == 31 ? 0 : input_state.read(archx::cvt_gpr(rn));
            const std::uint64_t output_flags = flags64(nval, ~mval, 1);
            instr->write_flags(output_flags);
        } else {
            const auto nzcv = adis::fldfrom_CCMP_64_condcmp_reg(itext, adis::fields_t::nzcv);
            const std::uint64_t output_flags = pstate_nzcv((nzcv >> 3) & 1, (nzcv >> 2) & 1, (nzcv >> 1) & 1, nzcv & 1);
            instr->write_flags(output_flags);
        }
        return true;
    }
    static bool emulate_ccmp32_reg(archx::InstructionIS* instr)
    {
        const auto itext = instr->opcode();
        const auto cond = adis::fldfrom_CCMP_32_condcmp_reg(itext, adis::fields_t::cond);
        if (condition_holds(cond, instr->pstate_nzcv())) {
            const auto rn = adis::fldfrom_CCMP_32_condcmp_reg(itext, adis::fields_t::Rn);
            const auto rm = adis::fldfrom_CCMP_32_condcmp_reg(itext, adis::fields_t::Rm);
            const ArchState& input_state = instr->state_ref();
            const std::uint32_t mval = rm == 31 ? 0 : input_state.read(archx::cvt_gpr(rm));
            const auto nval = rn == 31 ? 0 : input_state.read(archx::cvt_gpr(rn));
            const std::uint32_t output_flags = flags32(nval, ~mval, 1);
            instr->write_flags(output_flags);
        } else {
            const auto nzcv = adis::fldfrom_CCMP_32_condcmp_reg(itext, adis::fields_t::nzcv);
            const std::uint64_t output_flags = pstate_nzcv((nzcv >> 3) & 1, (nzcv >> 2) & 1, (nzcv >> 1) & 1, nzcv & 1);
            instr->write_flags(output_flags);
        }
        return true;
    }
    static bool emulate_ccmp64_imm(archx::InstructionIS* instr)
    {
        const auto itext = instr->opcode();
        const auto cond = adis::fldfrom_CCMP_64_condcmp_imm(itext, adis::fields_t::cond);
        if (condition_holds(cond, instr->pstate_nzcv())) {
            const auto rn = adis::fldfrom_CCMP_64_condcmp_imm(itext, adis::fields_t::Rn);
            const ArchState& input_state = instr->state_ref();
            const std::uint64_t mval = adis::fldfrom_CCMP_64_condcmp_imm(itext, adis::fields_t::imm5);
            const auto nval = rn == 31 ? 0 : input_state.read(archx::cvt_gpr(rn));
            const std::uint64_t output_flags = flags64(nval, ~mval, 1);
            instr->write_flags(output_flags);
        } else {
            const auto nzcv = adis::fldfrom_CCMP_64_condcmp_imm(itext, adis::fields_t::nzcv);
            const std::uint64_t output_flags = pstate_nzcv((nzcv >> 3) & 1, (nzcv >> 2) & 1, (nzcv >> 1) & 1, nzcv & 1);
            instr->write_flags(output_flags);
        }
        return true;
    }
    static bool emulate_ccmp32_imm(archx::InstructionIS* instr)
    {
        const auto itext = instr->opcode();
        const auto cond = adis::fldfrom_CCMP_32_condcmp_imm(itext, adis::fields_t::cond);
        if (condition_holds(cond, instr->pstate_nzcv())) {
            const auto rn = adis::fldfrom_CCMP_32_condcmp_imm(itext, adis::fields_t::Rn);
            const ArchState& input_state = instr->state_ref();
            const std::uint32_t mval = adis::fldfrom_CCMP_32_condcmp_imm(itext, adis::fields_t::imm5);
            const auto nval = rn == 31 ? 0 : input_state.read(archx::cvt_gpr(rn));
            const std::uint64_t output_flags = flags32(nval, ~mval, 1);
            instr->write_flags(output_flags);
        } else {
            const auto nzcv = adis::fldfrom_CCMP_32_condcmp_imm(itext, adis::fields_t::nzcv);
            const std::uint64_t output_flags = pstate_nzcv((nzcv >> 3) & 1, (nzcv >> 2) & 1, (nzcv >> 1) & 1, nzcv & 1);
            instr->write_flags(output_flags);
        }
        return true;
    }

    static bool emulate_ccmn64_imm(archx::InstructionIS* instr)
    {
        const auto itext = instr->opcode();
        const auto cond = adis::fldfrom_CCMN_64_condcmp_imm(itext, adis::fields_t::cond);
        if (condition_holds(cond, instr->pstate_nzcv())) {
            const auto rn = adis::fldfrom_CCMN_64_condcmp_imm(itext, adis::fields_t::Rn);
            const ArchState& input_state = instr->state_ref();
            const std::uint64_t mval = adis::fldfrom_CCMN_64_condcmp_imm(itext, adis::fields_t::imm5);
            const auto nval = rn == 31 ? 0 : input_state.read(archx::cvt_gpr(rn));
            const std::uint64_t output_flags = flags64(nval, mval, 0);
            instr->write_flags(output_flags);
        } else {
            const auto nzcv = adis::fldfrom_CCMN_64_condcmp_imm(itext, adis::fields_t::nzcv);
            const std::uint64_t output_flags = pstate_nzcv((nzcv >> 3) & 1, (nzcv >> 2) & 1, (nzcv >> 1) & 1, nzcv & 1);
            instr->write_flags(output_flags);
        }
        return true;
    }

    static bool emulate_ccmn32_imm(archx::InstructionIS* instr)
    {
        const auto itext = instr->opcode();
        const auto cond = adis::fldfrom_CCMN_32_condcmp_imm(itext, adis::fields_t::cond);
        if (condition_holds(cond, instr->pstate_nzcv())) {
            const auto rn = adis::fldfrom_CCMN_32_condcmp_imm(itext, adis::fields_t::Rn);
            const ArchState& input_state = instr->state_ref();
            const std::uint32_t mval = adis::fldfrom_CCMN_32_condcmp_imm(itext, adis::fields_t::imm5);
            const auto nval = rn == 31 ? 0 : input_state.read(archx::cvt_gpr(rn));
            const std::uint64_t output_flags = flags32(nval, mval, 0);
            instr->write_flags(output_flags);
        } else {
            const auto nzcv = adis::fldfrom_CCMN_32_condcmp_imm(itext, adis::fields_t::nzcv);
            const std::uint64_t output_flags = pstate_nzcv((nzcv >> 3) & 1, (nzcv >> 2) & 1, (nzcv >> 1) & 1, nzcv & 1);
            instr->write_flags(output_flags);
        }
        return true;
    }

    static bool emulate_strb32_reg(archx::InstructionIS* instr)
    {
        const auto itext = instr->opcode();
        const auto rn = adis::fldfrom_STRB_32B_ldst_regoff(itext, adis::fields_t::Rn); // may be SP
        const auto rm = adis::fldfrom_STRB_32B_ldst_regoff(itext, adis::fields_t::Rm);
        const auto shift = adis::fldfrom_STRB_32B_ldst_regoff(itext, adis::fields_t::S); // meaningless for STRB
        const auto option = adis::fldfrom_STRB_32B_ldst_regoff(itext, adis::fields_t::option);
        if (option == 0b011) {
            return false;
        }
        if (((option >> 1) & 1) == 0) { // undefined
            return false;
        }
        const ArchState& input_state = instr->state_ref();
        // the shift is hardcoded to zero for this so the value of shift is meaningless
        const auto addr = ld_regoff_common(input_state, rn, rm, option, shift, 0);
        if (addr.has_value()) {
            const VirtAddr va(addr.value());
            instr->add_emulated_virt_addr(va);
            const auto rt = adis::fldfrom_STRB_32B_ldst_regoff(itext, adis::fields_t::Rt);
            const std::uint8_t regvalt = rt == 31 ? 0 : input_state.read(archx::cvt_gpr(rt)); // value to store
            mem_write_8bit(instr, va, regvalt);
            return true;
        }
        return false;
    }
    static bool emulate_strb32_lsl_reg(archx::InstructionIS* instr)
    {
        const auto itext = instr->opcode();
        const auto rn = adis::fldfrom_STRB_32BL_ldst_regoff(itext, adis::fields_t::Rn); // may be SP
        const auto rm = adis::fldfrom_STRB_32BL_ldst_regoff(itext, adis::fields_t::Rm);
        const auto shift = adis::fldfrom_STRB_32BL_ldst_regoff(itext, adis::fields_t::S); // meaningless for STRB
        const auto option = adis::fldfrom_STRB_32BL_ldst_regoff(itext, adis::fields_t::option);
        if (option != 0b011) {
            return false;
        }
        const ArchState& input_state = instr->state_ref();
        // the shift is hardcoded to zero for this so the value of shift is meaningless
        const auto addr = ld_regoff_common(input_state, rn, rm, option, shift, 0);
        if (addr.has_value()) {
            const VirtAddr va(addr.value());
            instr->add_emulated_virt_addr(va);
            const auto rt = adis::fldfrom_STRB_32BL_ldst_regoff(itext, adis::fields_t::Rt);
            const std::uint8_t regvalt = rt == 31 ? 0 : input_state.read(archx::cvt_gpr(rt)); // value to store
            mem_write_8bit(instr, va, regvalt);
            return true;
        }
        return false;
    }

    static bool emulate_ldrsb_32_lsl_reg(archx::InstructionIS* instr)
    {
        return emulate_ldrsb_32_reg_common(instr, true);
    }
    static bool emulate_ldrsb_32_reg(archx::InstructionIS* instr) { return emulate_ldrsb_32_reg_common(instr, false); }
    static bool emulate_ldrsb_32_reg_common(archx::InstructionIS* instr, bool shifted)
    {
        // LDRSB_32B_ldst_regoff, LDRSB_32BL_ldst_regoff
        const auto itext = instr->opcode();
        const auto rn = adis::fldfrom_LDRSB_32B_ldst_regoff(itext, adis::fields_t::Rn); // may be SP
        const auto rm = adis::fldfrom_LDRSB_32B_ldst_regoff(itext, adis::fields_t::Rm);
        const auto shift = adis::fldfrom_LDRSB_32B_ldst_regoff(itext, adis::fields_t::S);
        const auto option = adis::fldfrom_LDRSB_32B_ldst_regoff(itext, adis::fields_t::option);
        if (shifted && option != 0b011) {
            return false;
        }
        if (((option >> 1) & 1) == 0) { // undefined
            return false;
        }
        const ArchState& input_state = instr->state_ref();
        // the shift is hardcoded to zero for this so the value of shift is meaningless
        const auto addr = ld_regoff_common(input_state, rn, rm, option, shift, 0);
        if (addr.has_value()) {
            const auto rt = adis::fldfrom_LDRSB_32B_ldst_regoff(itext, adis::fields_t::Rt);
            const VirtAddr va(addr.value());
            instr->add_emulated_virt_addr(va);
            if (rt != 31) {
                const auto mval = mem_read(instr, va, 1);
                if (mval.has_value()) {
                    const std::uint8_t bval = mval.value();
                    const std::uint32_t sval = sign_extend(bval, 8);
                    instr->write_gpr32(archx::cvt_gpr(rt), sval);
                }
            }
            return true;
        }

        return false;
    }

    static bool emulate_ldrb32_reg(archx::InstructionIS* instr, bool shifted)
    {
        // LDRB_32B_ldst_regoff, LDRB_32BL_ldst_regoff
        const auto itext = instr->opcode();
        const auto rn = adis::fldfrom_LDRB_32B_ldst_regoff(itext, adis::fields_t::Rn); // may be SP
        const auto rm = adis::fldfrom_LDRB_32B_ldst_regoff(itext, adis::fields_t::Rm);
        const auto shift = adis::fldfrom_LDRB_32B_ldst_regoff(itext, adis::fields_t::S); // meaningless for LDRB
        const auto option = adis::fldfrom_LDRB_32B_ldst_regoff(itext, adis::fields_t::option);

        if (shifted && option != 0b011) {
            return false;
        }
        if (((option >> 1) & 1) == 0) { // undefined
            return false;
        }

        const ArchState& input_state = instr->state_ref();
        // the shift is hardcoded to zero for this so the value of shift is meaningless
        const auto addr = ld_regoff_common(input_state, rn, rm, option, shift, 0);
        if (addr.has_value()) {
            const auto rt = adis::fldfrom_LDRB_32B_ldst_regoff(itext, adis::fields_t::Rt);
            const VirtAddr va(addr.value());
            instr->add_emulated_virt_addr(va);
            if (rt != 31) {
                const auto mval = mem_read(instr, va, 1);
                if (mval.has_value()) {
                    const std::uint8_t bval = mval.value();
                    instr->write_gpr32(archx::cvt_gpr(rt), bval);
                }
            }
            return true;
        }

        return false;
    }

    static bool emulate_strh_reg(archx::InstructionIS* instr)
    {
        const auto itext = instr->opcode();
        const auto rn = adis::fldfrom_STRH_32_ldst_regoff(itext, adis::fields_t::Rn); // may be SP
        const auto rm = adis::fldfrom_STRH_32_ldst_regoff(itext, adis::fields_t::Rm);
        const auto shift = adis::fldfrom_STRH_32_ldst_regoff(itext, adis::fields_t::S);
        const auto option = adis::fldfrom_STRH_32_ldst_regoff(itext, adis::fields_t::option);
        const ArchState& input_state = instr->state_ref();
        const auto addr = ld_regoff_common(input_state, rn, rm, option, shift, 1);
        if (addr.has_value()) {
            const VirtAddr va(addr.value());
            const auto rt = adis::fldfrom_STRH_32_ldst_regoff(itext, adis::fields_t::Rt);
            instr->add_emulated_virt_addr(va);
            const std::uint16_t regvalt = rt == 31 ? 0 : input_state.read(archx::cvt_gpr(rt)); // value to store
            mem_write_16bit(instr, va, regvalt);
            return true;
        }
        return false;
    }

    static bool emulate_str64_reg(archx::InstructionIS* instr)
    {
        const auto itext = instr->opcode();
        const auto rn = adis::fldfrom_STR_64_ldst_regoff(itext, adis::fields_t::Rn); // may be SP
        const auto rm = adis::fldfrom_STR_64_ldst_regoff(itext, adis::fields_t::Rm);
        const auto shift = adis::fldfrom_STR_64_ldst_regoff(itext, adis::fields_t::S);
        const auto option = adis::fldfrom_STR_64_ldst_regoff(itext, adis::fields_t::option);
        const ArchState& input_state = instr->state_ref();
        const auto addr = ld_regoff_common(input_state, rn, rm, option, shift, 3);
        if (addr.has_value()) {
            const VirtAddr va(addr.value());
            const auto rt = adis::fldfrom_STR_64_ldst_regoff(itext, adis::fields_t::Rt);
            instr->add_emulated_virt_addr(va);
            const std::uint64_t regvalt = rt == 31 ? 0 : input_state.read(archx::cvt_gpr(rt)); // value to store
            mem_write_64bit(instr, va, regvalt);
            return true;
        }
        return false;
    }

    static bool emulate_str32_reg(archx::InstructionIS* instr)
    {
        const auto itext = instr->opcode();
        const auto rn = adis::fldfrom_STR_32_ldst_regoff(itext, adis::fields_t::Rn); // may be SP
        const auto rm = adis::fldfrom_STR_32_ldst_regoff(itext, adis::fields_t::Rm);
        const auto shift = adis::fldfrom_STR_32_ldst_regoff(itext, adis::fields_t::S);
        const auto option = adis::fldfrom_STR_32_ldst_regoff(itext, adis::fields_t::option);
        const ArchState& input_state = instr->state_ref();
        const auto addr = ld_regoff_common(input_state, rn, rm, option, shift, 2);
        if (addr.has_value()) {
            const VirtAddr va(addr.value());
            const auto rt = adis::fldfrom_STR_32_ldst_regoff(itext, adis::fields_t::Rt);
            instr->add_emulated_virt_addr(va);
            const std::uint32_t regvalt = rt == 31 ? 0 : input_state.read(archx::cvt_gpr(rt)); // value to store
            mem_write_32bit(instr, va, regvalt);
            return true;
        }
        return false;
    }

    static bool emulate_strb32_pos(archx::InstructionIS* instr)
    { // STRB_32_ldst_pos
        const auto itext = instr->opcode();
        const auto rn = adis::fldfrom_STRB_32_ldst_pos(itext, adis::fields_t::Rn); // may be SP
        const auto rt = adis::fldfrom_STRB_32_ldst_pos(itext, adis::fields_t::Rt);
        const auto uimm12 = adis::fldfrom_STRB_32_ldst_pos(itext, adis::fields_t::imm12);
        const ArchState& input_state = instr->state_ref(); // input
        const auto regvaln = input_state.read(archx::cvt_gpr(rn, archx::LReg::SP));
        const std::uint8_t regvalt = rt == 31 ? 0 : input_state.read(archx::cvt_gpr(rt)); // value to store
        const VirtAddr va(regvaln + uimm12);                                              // no shift for bytes
        instr->add_emulated_virt_addr(va);
        mem_write_8bit(instr, va, regvalt);
        return true;
    }
    static bool emulate_strb32_pre(archx::InstructionIS* instr)
    {
        const auto itext = instr->opcode();
        const auto rn = adis::fldfrom_STRB_32_ldst_immpre(itext, adis::fields_t::Rn); // may be SP
        const auto rt = adis::fldfrom_STRB_32_ldst_immpre(itext, adis::fields_t::Rt);
        const auto imm = adis::fldfrom_STRB_32_ldst_immpre(itext, adis::fields_t::imm9);
        const ArchState& input_state = instr->state_ref(); // input
        const auto base_reg = archx::cvt_gpr(rn, archx::LReg::SP);
        const auto regvaln = input_state.read(base_reg);
        const std::uint8_t regvalt = rt == 31 ? 0 : input_state.read(archx::cvt_gpr(rt)); // value to store
        const VirtAddr va(regvaln + sign_extend(imm, 9));                                 // no shift for bytes
        instr->add_emulated_virt_addr(va);
        mem_write_8bit(instr, va, regvalt);
        instr->write_gpr64(base_reg, va.value());
        return true;
    }
    static bool emulate_strb32_post(archx::InstructionIS* instr)
    {
        const auto itext = instr->opcode();
        const auto rn = adis::fldfrom_STRB_32_ldst_immpre(itext, adis::fields_t::Rn); // may be SP
        const auto rt = adis::fldfrom_STRB_32_ldst_immpre(itext, adis::fields_t::Rt);
        const auto imm = adis::fldfrom_STRB_32_ldst_immpre(itext, adis::fields_t::imm9);
        const ArchState& input_state = instr->state_ref(); // input
        const auto base_reg = archx::cvt_gpr(rn, archx::LReg::SP);
        const auto regvaln = input_state.read(base_reg);
        const std::uint8_t regvalt = rt == 31 ? 0 : input_state.read(archx::cvt_gpr(rt)); // value to store
        const VirtAddr pre_va(regvaln);
        instr->add_emulated_virt_addr(pre_va);
        mem_write_8bit(instr, pre_va, regvalt);
        const VirtAddr post_va(regvaln + sign_extend(imm, 9)); // no shift for bytes
        instr->write_gpr64(base_reg, post_va.value());
        return true;
    }

    static bool emulate_ldurq(archx::InstructionIS* instr)
    { // LDUR_Q_ldst_unscaled
        const auto itext = instr->opcode();
        const auto rn = adis::fldfrom_LDUR_Q_ldst_unscaled(itext, adis::fields_t::Rn); // may be SP
        const auto rt = adis::fldfrom_LDUR_Q_ldst_unscaled(itext, adis::fields_t::Rt); // vector dest
        const auto simm9 = adis::fldfrom_LDUR_Q_ldst_unscaled(itext, adis::fields_t::imm9);
        const ArchState& input_state = instr->state_ref(); // input
        const auto regvaln = input_state.read(archx::cvt_gpr(rn, archx::LReg::SP));
        const VirtAddr va(regvaln + sign_extend(simm9, 9));
        instr->add_emulated_virt_addr(va);
        const auto mval = mem_read_128(instr, va);
        if (mval.has_value()) {
            const auto vec_dst = archx::cvt_vec(rt);
            instr->write_vfp(vec_dst, mval.value());
        }
        return true;
    }
    static bool emulate_ldurd(archx::InstructionIS* instr)
    {
        const auto itext = instr->opcode();
        const auto rn = adis::fldfrom_LDUR_D_ldst_unscaled(itext, adis::fields_t::Rn); // may be SP
        const auto rt = adis::fldfrom_LDUR_D_ldst_unscaled(itext, adis::fields_t::Rt); // vector dest
        const auto simm9 = adis::fldfrom_LDUR_D_ldst_unscaled(itext, adis::fields_t::imm9);
        const ArchState& input_state = instr->state_ref(); // input
        const auto regvaln = input_state.read(archx::cvt_gpr(rn, archx::LReg::SP));
        const VirtAddr va(regvaln + sign_extend(simm9, 9));
        instr->add_emulated_virt_addr(va);
        const auto mval = mem_read(instr, va, 8);
        if (mval.has_value()) {
            const auto vec_dst = archx::cvt_vec(rt);
            instr->write_vfp(vec_dst, mval.value());
        }
        return true;
    }

    static bool emulate_ldur64(archx::InstructionIS* instr)
    { // LDUR_64_ldst_unscaled
        const auto itext = instr->opcode();
        const auto rn = adis::fldfrom_LDUR_64_ldst_unscaled(itext, adis::fields_t::Rn); // may be SP
        const auto rt = adis::fldfrom_LDUR_64_ldst_unscaled(itext, adis::fields_t::Rt);
        const auto simm9 = adis::fldfrom_LDUR_64_ldst_unscaled(itext, adis::fields_t::imm9);
        const ArchState& input_state = instr->state_ref(); // input
        const auto regvaln = input_state.read(archx::cvt_gpr(rn, archx::LReg::SP));
        const VirtAddr va(regvaln + sign_extend(simm9, 9));
        instr->add_emulated_virt_addr(va);
        if (rt != 31) {
            const auto mval = mem_read(instr, va, 8);
            if (mval.has_value()) {
                instr->write_gpr64(archx::cvt_gpr(rt), mval.value());
            }
        }
        return true;
    }

    static bool emulate_ldursh32(archx::InstructionIS* instr)
    { // ENC_LDURSH_32_ldst_unscaled
        const auto itext = instr->opcode();
        const auto rn = adis::fldfrom_LDURSH_32_ldst_unscaled(itext, adis::fields_t::Rn); // may be SP
        const auto rt = adis::fldfrom_LDURSH_32_ldst_unscaled(itext, adis::fields_t::Rt);
        const auto simm9 = adis::fldfrom_LDURSH_32_ldst_unscaled(itext, adis::fields_t::imm9);
        const ArchState& input_state = instr->state_ref(); // input
        const auto regvaln = input_state.read(archx::cvt_gpr(rn, archx::LReg::SP));
        const VirtAddr va(regvaln + sign_extend(simm9, 9));
        instr->add_emulated_virt_addr(va);
        if (rt != 31) {
            const auto mval = mem_read(instr, va, 2);
            if (mval.has_value()) {
                const std::int16_t wval = static_cast<std::uint16_t>(mval.value());
                instr->write_gpr32(archx::cvt_gpr(rt), wval);
            }
        }
        return true;
    }
    static bool emulate_ldursh64(archx::InstructionIS* instr)
    { // ENC_LDURSH_64_ldst_unscaled
        const auto itext = instr->opcode();
        const auto rn = adis::fldfrom_LDURSH_64_ldst_unscaled(itext, adis::fields_t::Rn); // may be SP
        const auto rt = adis::fldfrom_LDURSH_64_ldst_unscaled(itext, adis::fields_t::Rt);
        const auto simm9 = adis::fldfrom_LDURSH_64_ldst_unscaled(itext, adis::fields_t::imm9);
        const ArchState& input_state = instr->state_ref(); // input
        const auto regvaln = input_state.read(archx::cvt_gpr(rn, archx::LReg::SP));
        const VirtAddr va(regvaln + sign_extend(simm9, 9));
        instr->add_emulated_virt_addr(va);
        if (rt != 31) {
            const auto mval = mem_read(instr, va, 2);
            if (mval.has_value()) {
                const std::int16_t wval = static_cast<std::uint16_t>(mval.value());
                instr->write_gpr64(archx::cvt_gpr(rt), wval);
            }
        }
        return true;
    }

    static bool emulate_ldurb(archx::InstructionIS* instr)
    {
        const auto itext = instr->opcode();
        const auto rn = adis::fldfrom_LDURB_32_ldst_unscaled(itext, adis::fields_t::Rn); // may be SP
        const auto rt = adis::fldfrom_LDURB_32_ldst_unscaled(itext, adis::fields_t::Rt);
        const auto simm9 = adis::fldfrom_LDURB_32_ldst_unscaled(itext, adis::fields_t::imm9);
        const ArchState& input_state = instr->state_ref(); // input
        const auto regvaln = input_state.read(archx::cvt_gpr(rn, archx::LReg::SP));
        const VirtAddr va(regvaln + sign_extend(simm9, 9));
        instr->add_emulated_virt_addr(va);
        if (rt != 31) {
            const auto mval = mem_read(instr, va, 1);
            if (mval.has_value()) {
                const std::uint8_t wval = mval.value();
                instr->write_gpr32(archx::cvt_gpr(rt), wval);
            }
        }
        return true;
    }
    static bool emulate_ldurh(archx::InstructionIS* instr)
    {
        const auto itext = instr->opcode();
        const auto rn = adis::fldfrom_LDURH_32_ldst_unscaled(itext, adis::fields_t::Rn); // may be SP
        const auto rt = adis::fldfrom_LDURH_32_ldst_unscaled(itext, adis::fields_t::Rt);
        const auto simm9 = adis::fldfrom_LDURH_32_ldst_unscaled(itext, adis::fields_t::imm9);
        const ArchState& input_state = instr->state_ref(); // input
        const auto regvaln = input_state.read(archx::cvt_gpr(rn, archx::LReg::SP));
        const VirtAddr va(regvaln + sign_extend(simm9, 9));
        instr->add_emulated_virt_addr(va);
        if (rt != 31) {
            const auto mval = mem_read(instr, va, 2);
            if (mval.has_value()) {
                const std::uint16_t hval = mval.value();
                instr->write_gpr32(archx::cvt_gpr(rt), hval);
            }
        }
        return true;
    }
    static bool emulate_ldur32(archx::InstructionIS* instr)
    { // LDUR_32_ldst_unscaled
        const auto itext = instr->opcode();
        const auto rn = adis::fldfrom_LDUR_32_ldst_unscaled(itext, adis::fields_t::Rn); // may be SP
        const auto rt = adis::fldfrom_LDUR_32_ldst_unscaled(itext, adis::fields_t::Rt);
        const auto simm9 = adis::fldfrom_LDUR_32_ldst_unscaled(itext, adis::fields_t::imm9);
        const ArchState& input_state = instr->state_ref(); // input
        const auto regvaln = input_state.read(archx::cvt_gpr(rn, archx::LReg::SP));
        const VirtAddr va(regvaln + sign_extend(simm9, 9));
        instr->add_emulated_virt_addr(va);
        if (rt != 31) {
            const auto mval = mem_read(instr, va, 4);
            if (mval.has_value()) {
                const std::uint32_t wval = mval.value();
                instr->write_gpr32(archx::cvt_gpr(rt), wval);
            }
        }
        return true;
    }
    static bool emulate_ldar32(archx::InstructionIS* instr)
    {
        const auto itext = instr->opcode();
        const auto rn = adis::fldfrom_LDAR_LR32_ldstord(itext, adis::fields_t::Rn); // may be SP
        const auto rt = adis::fldfrom_LDAR_LR32_ldstord(itext, adis::fields_t::Rt);
        const ArchState& input_state = instr->state_ref(); // input
        const auto regvaln = input_state.read(archx::cvt_gpr(rn, archx::LReg::SP));
        const VirtAddr va(regvaln);
        instr->add_emulated_virt_addr(va);
        if (rt != 31) {
            const auto mval = mem_read(instr, va, 4);
            if (mval.has_value()) {
                const std::uint32_t wval = mval.value();
                instr->write_gpr32(archx::cvt_gpr(rt), wval);
            }
        }
        return true;
    }
    static bool emulate_ldar64(archx::InstructionIS* instr)
    {
        const auto itext = instr->opcode();
        const auto rn = adis::fldfrom_LDAR_LR64_ldstord(itext, adis::fields_t::Rn); // may be SP
        const auto rt = adis::fldfrom_LDAR_LR64_ldstord(itext, adis::fields_t::Rt);
        const ArchState& input_state = instr->state_ref(); // input
        const auto regvaln = input_state.read(archx::cvt_gpr(rn, archx::LReg::SP));
        const VirtAddr va(regvaln);
        instr->add_emulated_virt_addr(va);
        if (rt != 31) {
            const auto mval = mem_read(instr, va, 8);
            if (mval.has_value()) {
                instr->write_gpr64(archx::cvt_gpr(rt), mval.value());
            }
        }
        return true;
    }
    static bool emulate_str_q_pos(archx::InstructionIS* instr)
    { // STR_Q_ldst_pos
        const auto itext = instr->opcode();
        const auto uimm12 = adis::fldfrom_STR_Q_ldst_pos(itext, adis::fields_t::imm12);
        const auto rn = adis::fldfrom_STR_Q_ldst_pos(itext, adis::fields_t::Rn); // can be SP
        const ArchState& input_state = instr->state_ref();                       // input
        const auto regvaln = input_state.read(archx::cvt_gpr(rn, archx::LReg::SP));
        const VirtAddr va(regvaln + (uimm12 << 4)); // *16
        instr->add_emulated_virt_addr(va);
        const auto rt = adis::fldfrom_STR_Q_ldst_pos(itext, adis::fields_t::Rt); // vector dest
        const auto vec_lreg_src = archx::cvt_vec(rt);
        const auto src_data = input_state.read_vfp(vec_lreg_src);
        mem_write_64bit(instr, va, src_data[0]);
        mem_write_64bit(instr, va + 8, src_data[1]);
        return true;
    }
    static bool emulate_str_q_pre(archx::InstructionIS* instr)
    {
        const auto itext = instr->opcode();
        const auto imm = adis::fldfrom_STR_Q_ldst_immpre(itext, adis::fields_t::imm9);
        const auto rn = adis::fldfrom_STR_Q_ldst_immpre(itext, adis::fields_t::Rn); // can be SP
        const ArchState& input_state = instr->state_ref();                          // input
        const auto base_reg = archx::cvt_gpr(rn, archx::LReg::SP);
        const auto regvaln = input_state.read(base_reg);
        const VirtAddr va(regvaln + sign_extend(imm, 9) * 16);
        instr->add_emulated_virt_addr(va);
        const auto rt = adis::fldfrom_STR_Q_ldst_immpre(itext, adis::fields_t::Rt); // vector dest
        const auto vec_lreg_src = archx::cvt_vec(rt);
        const auto src_data = input_state.read_vfp(vec_lreg_src);
        mem_write_128bit(instr, va, src_data);
        instr->write_gpr64(base_reg, va.value());
        return true;
    }
    static bool emulate_str_q_post(archx::InstructionIS* instr)
    {
        const auto itext = instr->opcode();
        const auto imm = adis::fldfrom_STR_Q_ldst_immpost(itext, adis::fields_t::imm9);
        const auto rn = adis::fldfrom_STR_Q_ldst_immpost(itext, adis::fields_t::Rn); // can be SP
        const ArchState& input_state = instr->state_ref();                           // input
        const auto base_reg = archx::cvt_gpr(rn, archx::LReg::SP);
        const auto regvaln = input_state.read(base_reg);
        const VirtAddr pre_va(regvaln);
        instr->add_emulated_virt_addr(pre_va);
        const auto rt = adis::fldfrom_STR_Q_ldst_immpost(itext, adis::fields_t::Rt); // vector dest
        const auto vec_lreg_src = archx::cvt_vec(rt);
        const auto src_data = input_state.read_vfp(vec_lreg_src);
        mem_write_128bit(instr, pre_va, src_data);
        const VirtAddr post_va(regvaln + sign_extend(imm, 9) * 16);
        instr->write_gpr64(base_reg, post_va.value());
        return true;
    }

    static bool emulate_ldr_q_pos(archx::InstructionIS* instr)
    { // LDR_Q_ldst_pos
        const auto itext = instr->opcode();
        const auto uimm12 = adis::fldfrom_LDR_Q_ldst_pos(itext, adis::fields_t::imm12);
        const auto rn = adis::fldfrom_LDR_Q_ldst_pos(itext, adis::fields_t::Rn); // can be SP
        const ArchState& input_state = instr->state_ref();                       // input
        const auto regvaln = input_state.read(archx::cvt_gpr(rn, archx::LReg::SP));
        const VirtAddr va(regvaln + (uimm12 << 4)); // *16
        instr->add_emulated_virt_addr(va);
        const auto mval = mem_read_128(instr, va);
        if (mval.has_value()) {
            const auto rt = adis::fldfrom_LDR_Q_ldst_pos(itext, adis::fields_t::Rt); // vector dest
            const auto vec_dst = archx::cvt_vec(rt);
            instr->write_vfp(vec_dst, mval.value());
        }
        return true;
    }
    static bool emulate_ldr_q_pre(archx::InstructionIS* instr)
    {
        const auto itext = instr->opcode();
        const auto imm = adis::fldfrom_LDR_Q_ldst_immpre(itext, adis::fields_t::imm9);
        const auto rn = adis::fldfrom_LDR_Q_ldst_immpre(itext, adis::fields_t::Rn); // can be SP
        const ArchState& input_state = instr->state_ref();                          // input
        const auto base_reg = archx::cvt_gpr(rn, archx::LReg::SP);
        const auto regvaln = input_state.read(base_reg);
        const VirtAddr va(regvaln + sign_extend(imm, 9) * 16);
        instr->add_emulated_virt_addr(va);
        const auto mval = mem_read_128(instr, va);
        if (mval.has_value()) {
            const auto rt = adis::fldfrom_LDR_Q_ldst_immpre(itext, adis::fields_t::Rt); // vector dest
            const auto vec_dst = archx::cvt_vec(rt);
            instr->write_vfp(vec_dst, mval.value());
        }
        instr->write_gpr64(base_reg, va.value());
        return true;
    }
    static bool emulate_ldr_q_post(archx::InstructionIS* instr)
    {
        const auto itext = instr->opcode();
        const auto imm = adis::fldfrom_LDR_Q_ldst_immpost(itext, adis::fields_t::imm9);
        const auto rn = adis::fldfrom_LDR_Q_ldst_immpost(itext, adis::fields_t::Rn); // can be SP
        const ArchState& input_state = instr->state_ref();                           // input
        const auto base_reg = archx::cvt_gpr(rn, archx::LReg::SP);
        const auto regvaln = input_state.read(base_reg);
        const VirtAddr pre_va(regvaln);
        instr->add_emulated_virt_addr(pre_va);
        const auto mval = mem_read_128(instr, pre_va);
        if (mval.has_value()) {
            const auto rt = adis::fldfrom_LDR_Q_ldst_immpost(itext, adis::fields_t::Rt); // vector dest
            const auto vec_dst = archx::cvt_vec(rt);
            instr->write_vfp(vec_dst, mval.value());
        }
        const VirtAddr post_va(regvaln + sign_extend(imm, 9) * 16);
        instr->write_gpr64(base_reg, post_va.value());
        return true;
    }

    static bool emulate_ldr_d_post(archx::InstructionIS* instr)
    {
        const auto itext = instr->opcode();
        const std::uint64_t imm = adis::fldfrom_LDR_D_ldst_immpost(itext, adis::fields_t::imm9);
        const auto rn = adis::fldfrom_LDR_D_ldst_immpost(itext, adis::fields_t::Rn); // can be SP
        const ArchState& input_state = instr->state_ref();                           // input
        const auto base_reg = archx::cvt_gpr(rn, archx::LReg::SP);
        const auto regvaln = input_state.read(base_reg);
        const VirtAddr base_va(regvaln);
        instr->add_emulated_virt_addr(base_va);
        const auto mval = mem_read(instr, base_va, 8);
        if (mval.has_value()) {
            const auto rt = adis::fldfrom_LDR_D_ldst_immpost(itext, adis::fields_t::Rt); // vector dest
            const auto vec_dst = archx::cvt_vec(rt);
            instr->write_vfp(vec_dst, mval.value());
        }
        const VirtAddr post_va(regvaln + sign_extend(imm, 9) * 8);
        instr->write_gpr64(base_reg, post_va.value());
        return true;
    }
    static bool emulate_ldr_d_pre(archx::InstructionIS* instr)
    {
        const auto itext = instr->opcode();
        const std::uint64_t imm = adis::fldfrom_LDR_D_ldst_immpre(itext, adis::fields_t::imm9);
        const auto rn = adis::fldfrom_LDR_D_ldst_immpre(itext, adis::fields_t::Rn); // can be SP
        const ArchState& input_state = instr->state_ref();                          // input
        const auto base_reg = archx::cvt_gpr(rn, archx::LReg::SP);
        const auto regvaln = input_state.read(base_reg);
        const VirtAddr va(regvaln + sign_extend(imm, 9) * 8);
        instr->add_emulated_virt_addr(va);
        const auto mval = mem_read(instr, va, 8);
        if (mval.has_value()) {
            const auto rt = adis::fldfrom_LDR_D_ldst_immpre(itext, adis::fields_t::Rt); // vector dest
            const auto vec_dst = archx::cvt_vec(rt);
            instr->write_vfp(vec_dst, mval.value());
        }
        instr->write_gpr64(base_reg, va.value());
        return true;
    }
    static bool emulate_ldr_d_pos(archx::InstructionIS* instr)
    { // LDR_D_ldst_pos
        const auto itext = instr->opcode();
        const auto uimm12 = adis::fldfrom_LDR_D_ldst_pos(itext, adis::fields_t::imm12);
        const auto rn = adis::fldfrom_LDR_D_ldst_pos(itext, adis::fields_t::Rn); // can be SP
        const ArchState& input_state = instr->state_ref();                       // input
        const auto regvaln = input_state.read(archx::cvt_gpr(rn, archx::LReg::SP));
        const VirtAddr va(regvaln + (uimm12 << 3)); // *8
        instr->add_emulated_virt_addr(va);
        const auto mval = mem_read(instr, va, 8);
        if (mval.has_value()) {
            const auto rt = adis::fldfrom_LDR_D_ldst_pos(itext, adis::fields_t::Rt); // vector dest
            const auto vec_dst = archx::cvt_vec(rt);
            instr->write_vfp(vec_dst, mval.value());
        }
        return true;
    }

    static bool emulate_ldr_s_pos(archx::InstructionIS* instr)
    { // LDR_S_ldst_pos
        const auto itext = instr->opcode();
        const auto uimm12 = adis::fldfrom_LDR_S_ldst_pos(itext, adis::fields_t::imm12);
        const auto rn = adis::fldfrom_LDR_S_ldst_pos(itext, adis::fields_t::Rn); // can be SP
        const ArchState& input_state = instr->state_ref();                       // input
        const auto regvaln = input_state.read(archx::cvt_gpr(rn, archx::LReg::SP));
        const VirtAddr va(regvaln + (uimm12 << 2)); // *4
        instr->add_emulated_virt_addr(va);
        const auto mval = mem_read(instr, va, 4);
        if (mval.has_value()) {
            const auto rt = adis::fldfrom_LDR_S_ldst_pos(itext, adis::fields_t::Rt); // vector dest
            const auto vec_dst = archx::cvt_vec(rt);
            instr->write_vfp(vec_dst, mval.value());
        }
        return true;
    }

    // right shifts variable
    static bool emulate_lsrv64(archx::InstructionIS* instr)
    { // LSRV_64_dp_2src
        const auto itext = instr->opcode();
        const auto rd = adis::fldfrom_LSRV_64_dp_2src(itext, adis::fields_t::Rd);
        if (rd != 31) {
            const auto rn = adis::fldfrom_LSRV_64_dp_2src(itext, adis::fields_t::Rn);
            const auto rm = adis::fldfrom_LSRV_64_dp_2src(itext, adis::fields_t::Rm);
            const ArchState& input_state = instr->state_ref();
            const auto nval = rn == 31 ? 0 : input_state.read(archx::cvt_gpr(rn));
            const auto mval = rm == 31 ? 0 : input_state.read(archx::cvt_gpr(rm));
            const std::uint64_t result = nval >> (mval % 64);
            instr->write_gpr64(archx::cvt_gpr(rd), result);
        }
        return true;
    }
    static bool emulate_lsrv32(archx::InstructionIS* instr)
    { // LSRV_32_dp_2src
        const auto itext = instr->opcode();
        const auto rd = adis::fldfrom_LSRV_32_dp_2src(itext, adis::fields_t::Rd);
        if (rd != 31) {
            const auto rn = adis::fldfrom_LSRV_32_dp_2src(itext, adis::fields_t::Rn);
            const auto rm = adis::fldfrom_LSRV_32_dp_2src(itext, adis::fields_t::Rm);
            const ArchState& input_state = instr->state_ref();
            const std::uint32_t nval = rn == 31 ? 0 : input_state.read(archx::cvt_gpr(rn));
            const std::uint32_t mval = rm == 31 ? 0 : input_state.read(archx::cvt_gpr(rm));
            const std::uint32_t result = nval >> (mval % 32);
            instr->write_gpr32(archx::cvt_gpr(rd), result);
        }
        return true;
    }

    // left shifts variable
    static bool emulate_lslv64(archx::InstructionIS* instr)
    { // LSLV_64_dp_2src
        const auto itext = instr->opcode();
        const auto rd = adis::fldfrom_LSLV_64_dp_2src(itext, adis::fields_t::Rd);
        if (rd != 31) {
            const auto rn = adis::fldfrom_LSLV_64_dp_2src(itext, adis::fields_t::Rn);
            const auto rm = adis::fldfrom_LSLV_64_dp_2src(itext, adis::fields_t::Rm);
            const ArchState& input_state = instr->state_ref();
            const auto nval = rn == 31 ? 0 : input_state.read(archx::cvt_gpr(rn));
            const auto mval = rm == 31 ? 0 : input_state.read(archx::cvt_gpr(rm));
            const std::uint64_t result = nval << (mval % 64);
            instr->write_gpr64(archx::cvt_gpr(rd), result);
        }
        return true;
    }
    static bool emulate_lslv32(archx::InstructionIS* instr)
    { // LSLV_32_dp_2src
        const auto itext = instr->opcode();
        const auto rd = adis::fldfrom_LSLV_32_dp_2src(itext, adis::fields_t::Rd);
        if (rd != 31) {
            const auto rn = adis::fldfrom_LSLV_32_dp_2src(itext, adis::fields_t::Rn);
            const auto rm = adis::fldfrom_LSLV_32_dp_2src(itext, adis::fields_t::Rm);
            const ArchState& input_state = instr->state_ref();
            const std::uint32_t nval = rn == 31 ? 0 : input_state.read(archx::cvt_gpr(rn));
            const std::uint32_t mval = rm == 31 ? 0 : input_state.read(archx::cvt_gpr(rm));
            const std::uint32_t result = nval << (mval % 32);
            instr->write_gpr32(archx::cvt_gpr(rd), result);
        }
        return true;
    }

    static bool emulate_str_s(archx::InstructionIS* instr)
    { // STR_S_ldst_regoff
        const auto itext = instr->opcode();
        const auto rt = adis::fldfrom_STR_S_ldst_regoff(itext, adis::fields_t::Rt); // vpr
        const auto rn = adis::fldfrom_STR_S_ldst_regoff(itext, adis::fields_t::Rn); // may be SP
        const auto rm = adis::fldfrom_STR_S_ldst_regoff(itext, adis::fields_t::Rm);
        const auto shift = adis::fldfrom_STR_S_ldst_regoff(itext, adis::fields_t::S);
        const auto option = adis::fldfrom_STR_S_ldst_regoff(itext, adis::fields_t::option);
        const ArchState& input_state = instr->state_ref();
        const auto addr = ld_regoff_common(input_state, rn, rm, option, shift, 2);
        if (addr.has_value()) {
            const VirtAddr va(addr.value());
            instr->add_emulated_virt_addr(va);
            const auto vec_lreg_src = archx::cvt_vec(rt);
            const auto src_data = input_state.read_vfp(vec_lreg_src);
            mem_write_32bit(instr, va, src_data[0]);
            return true;
        }
        return false;
    }

    static bool emulate_ldrh_reg(archx::InstructionIS* instr)
    { // LDRH_32_ldst_regoff
        const auto itext = instr->opcode();
        const auto rt = adis::fldfrom_LDRH_32_ldst_regoff(itext, adis::fields_t::Rt);
        const auto rn = adis::fldfrom_LDRH_32_ldst_regoff(itext, adis::fields_t::Rn);
        const auto rm = adis::fldfrom_LDRH_32_ldst_regoff(itext, adis::fields_t::Rm);
        const auto option = adis::fldfrom_LDRH_32_ldst_regoff(itext, adis::fields_t::option);
        const auto shift = adis::fldfrom_LDRH_32_ldst_regoff(itext, adis::fields_t::S);
        const ArchState& input_state = instr->state_ref();
        const auto addr = ld_regoff_common(input_state, rn, rm, option, shift, 1);
        if (addr.has_value()) {
            const VirtAddr va(addr.value());
            instr->add_emulated_virt_addr(va);
            const auto mval = mem_read(instr, va, 2);
            if (mval.has_value() && rt != 31) {
                const std::uint16_t data = mval.value();
                instr->write_gpr32(archx::cvt_gpr(rt), data);
            }
            return true;
        }
        return false;
    }

    static bool emulate_strd_post(archx::InstructionIS* instr)
    {
        const auto itext = instr->opcode();
        const auto rt = adis::fldfrom_STR_D_ldst_immpre(itext, adis::fields_t::Rt); // vpr
        const auto rn = adis::fldfrom_STR_D_ldst_immpre(itext, adis::fields_t::Rn); // can be SP
        const std::uint64_t imm = adis::fldfrom_STR_D_ldst_immpre(itext, adis::fields_t::imm9);
        const ArchState& input_state = instr->state_ref();
        const auto base_reg = archx::cvt_gpr(rn, archx::LReg::SP);
        const auto regvaln = input_state.read(base_reg);
        const VirtAddr base_va(regvaln);
        instr->add_emulated_virt_addr(base_va);
        const auto src_data = input_state.read_vfp(archx::cvt_vec(rt));
        mem_write_64bit(instr, base_va, src_data[0]);
        const VirtAddr post_va(regvaln + sign_extend(imm, 9) * 8);
        instr->write_gpr64(base_reg, post_va.value());
        return true;
    }
    static bool emulate_strd_pre(archx::InstructionIS* instr)
    {
        const auto itext = instr->opcode();
        const auto rt = adis::fldfrom_STR_D_ldst_immpre(itext, adis::fields_t::Rt); // vpr
        const auto rn = adis::fldfrom_STR_D_ldst_immpre(itext, adis::fields_t::Rn); // can be SP
        const std::uint64_t imm = adis::fldfrom_STR_D_ldst_immpre(itext, adis::fields_t::imm9);
        const ArchState& input_state = instr->state_ref();
        const auto base_reg = archx::cvt_gpr(rn, archx::LReg::SP);
        const auto regvaln = input_state.read(base_reg);
        const VirtAddr va(regvaln + sign_extend(imm, 9) * 8);
        instr->add_emulated_virt_addr(va);
        const auto src_data = input_state.read_vfp(archx::cvt_vec(rt));
        mem_write_64bit(instr, va, src_data[0]);
        instr->write_gpr64(base_reg, va.value());
        return true;
    }

    static bool emulate_strd_pos(archx::InstructionIS* instr)
    {
        const auto itext = instr->opcode();
        const auto rt = adis::fldfrom_STR_D_ldst_pos(itext, adis::fields_t::Rt); // vpr
        const auto rn = adis::fldfrom_STR_D_ldst_pos(itext, adis::fields_t::Rn); // can be SP
        const std::uint64_t imm = adis::fldfrom_STR_D_ldst_pos(itext, adis::fields_t::imm12);
        const ArchState& input_state = instr->state_ref();
        const auto regvaln = input_state.read(archx::cvt_gpr(rn, archx::LReg::SP));
        const VirtAddr va(regvaln + (imm << 3)); // *8
        instr->add_emulated_virt_addr(va);
        const auto src_data = input_state.read_vfp(archx::cvt_vec(rt));
        mem_write_64bit(instr, va, src_data[0]);
        return true;
    }

    static bool emulate_strs_post(archx::InstructionIS* instr)
    {
        const auto itext = instr->opcode();
        const auto rt = adis::fldfrom_STR_S_ldst_immpre(itext, adis::fields_t::Rt); // vpr
        const auto rn = adis::fldfrom_STR_S_ldst_immpre(itext, adis::fields_t::Rn); // can be SP
        const std::uint64_t imm = adis::fldfrom_STR_S_ldst_immpre(itext, adis::fields_t::imm9);
        const ArchState& input_state = instr->state_ref();
        const auto base_reg = archx::cvt_gpr(rn, archx::LReg::SP);
        const auto regvaln = input_state.read(base_reg);
        const VirtAddr base_va(regvaln);
        instr->add_emulated_virt_addr(base_va);
        const auto src_data = input_state.read_vfp(archx::cvt_vec(rt));
        mem_write_32bit(instr, base_va, src_data[0]); // truncates
        const VirtAddr post_va(regvaln + sign_extend(imm, 9) * 4);
        instr->write_gpr64(base_reg, post_va.value());
        return true;
    }

    static bool emulate_strs_pre(archx::InstructionIS* instr)
    {
        const auto itext = instr->opcode();
        const auto rt = adis::fldfrom_STR_S_ldst_immpre(itext, adis::fields_t::Rt); // vpr
        const auto rn = adis::fldfrom_STR_S_ldst_immpre(itext, adis::fields_t::Rn); // can be SP
        const std::uint64_t imm = adis::fldfrom_STR_S_ldst_immpre(itext, adis::fields_t::imm9);
        const ArchState& input_state = instr->state_ref();
        const auto base_reg = archx::cvt_gpr(rn, archx::LReg::SP);
        const auto regvaln = input_state.read(base_reg);
        const VirtAddr va(regvaln + sign_extend(imm, 9) * 4);
        instr->add_emulated_virt_addr(va);
        const auto src_data = input_state.read_vfp(archx::cvt_vec(rt));
        mem_write_32bit(instr, va, src_data[0]); // truncates
        instr->write_gpr64(base_reg, va.value());
        return true;
    }

    static bool emulate_strs_pos(archx::InstructionIS* instr)
    {
        const auto itext = instr->opcode();
        const auto rt = adis::fldfrom_STR_S_ldst_pos(itext, adis::fields_t::Rt); // vpr
        const auto rn = adis::fldfrom_STR_S_ldst_pos(itext, adis::fields_t::Rn); // can be SP
        const std::uint64_t imm = adis::fldfrom_STR_S_ldst_pos(itext, adis::fields_t::imm12);
        const ArchState& input_state = instr->state_ref();
        const auto regvaln = input_state.read(archx::cvt_gpr(rn, archx::LReg::SP));
        const VirtAddr va(regvaln + (imm << 2)); // *4
        instr->add_emulated_virt_addr(va);
        const auto src_data = input_state.read_vfp(archx::cvt_vec(rt));
        mem_write_32bit(instr, va, src_data[0]); // truncates
        return true;
    }

    static bool emulate_strd_reg(archx::InstructionIS* instr)
    { // STR_D_ldst_regoff
        const auto itext = instr->opcode();
        const auto rt = adis::fldfrom_STR_D_ldst_regoff(itext, adis::fields_t::Rt); // vpr
        const auto rn = adis::fldfrom_STR_D_ldst_regoff(itext, adis::fields_t::Rn);
        const auto rm = adis::fldfrom_STR_D_ldst_regoff(itext, adis::fields_t::Rm);
        const auto option = adis::fldfrom_STR_D_ldst_regoff(itext, adis::fields_t::option);
        const auto shift = adis::fldfrom_STR_D_ldst_regoff(itext, adis::fields_t::S);
        const ArchState& input_state = instr->state_ref();
        const auto addr = ld_regoff_common(input_state, rn, rm, option, shift, 3);
        if (addr.has_value()) {
            const VirtAddr va(addr.value());
            instr->add_emulated_virt_addr(va);
            const auto src_data = input_state.read_vfp(archx::cvt_vec(rt));
            mem_write_64bit(instr, va, src_data[0]);
            return true;
        }
        return false;
    }

    static std::uint64_t fp32_compare(const fp32_t src1, const fp32_t src2)
    {
        std::uint64_t result_nzcv = 0;
        if (src1.nan() || src2.nan()) {
            result_nzcv = pstate_nzcv(0, 0, 1, 1);
        } else if (src1.zero() && src2.zero()) { // ensure ignoring sign
            result_nzcv = pstate_nzcv(0, 1, 1, 0);
        } else if (src1.f == src2.f) {
            result_nzcv = pstate_nzcv(0, 1, 1, 0);
        } else if (src1.f < src2.f) {
            result_nzcv = pstate_nzcv(1, 0, 0, 0);
        } else { // src1 > src2
            result_nzcv = pstate_nzcv(0, 0, 1, 0);
        }
        return result_nzcv;
    }

    static std::uint64_t fp64_compare(const fp64_t src1, const fp64_t src2)
    {
        std::uint64_t result_nzcv = 0;
        if (src1.nan() || src2.nan()) {
            result_nzcv = pstate_nzcv(0, 0, 1, 1);
        } else if (src1.zero() && src2.zero()) { // ensure ignoring sign
            result_nzcv = pstate_nzcv(0, 1, 1, 0);
        } else if (src1.f == src2.f) {
            result_nzcv = pstate_nzcv(0, 1, 1, 0);
        } else if (src1.f < src2.f) {
            result_nzcv = pstate_nzcv(1, 0, 0, 0);
        } else { // src1 > src2
            result_nzcv = pstate_nzcv(0, 0, 1, 0);
        }
        return result_nzcv;
    }

    static bool emulate_FCMP_S_floatcmp(archx::InstructionIS* instr)
    {
        const auto itext = instr->opcode();
        const auto rn = adis::fldfrom_FCMP_S_floatcmp(itext, adis::fields_t::Rn);
        const auto rm = adis::fldfrom_FCMP_S_floatcmp(itext, adis::fields_t::Rm);
        const ArchState& input_state = instr->state_ref();
        const auto src1_vecval = input_state.read_vfp(archx::cvt_vec(rn));
        const auto src2_vecval = input_state.read_vfp(archx::cvt_vec(rm));
        fp32_t src1, src2;
        src1.u = src1_vecval[0];
        src2.u = src2_vecval[0];
        // FIXME: denorm flushing
        std::uint64_t result_nzcv = fp32_compare(src1, src2);
        instr->write_flags(result_nzcv);
        return true;
    }

    static bool emulate_FCMP_D_floatcmp(archx::InstructionIS* instr)
    {
        const auto itext = instr->opcode();
        const auto rn = adis::fldfrom_FCMP_D_floatcmp(itext, adis::fields_t::Rn);
        const auto rm = adis::fldfrom_FCMP_D_floatcmp(itext, adis::fields_t::Rm);
        const ArchState& input_state = instr->state_ref();
        const auto src1_vecval = input_state.read_vfp(archx::cvt_vec(rn));
        const auto src2_vecval = input_state.read_vfp(archx::cvt_vec(rm));
        fp64_t src1, src2;
        src1.u = src1_vecval[0];
        src2.u = src2_vecval[0];
        // FIXME: denorm flushing
        std::uint64_t result_nzcv = fp64_compare(src1, src2);
        instr->write_flags(result_nzcv);
        return true;
    }

    static bool emulate_FCMP_SZ_floatcmp(archx::InstructionIS* instr)
    {
        const auto itext = instr->opcode();
        const auto rn = adis::fldfrom_FCMP_SZ_floatcmp(itext, adis::fields_t::Rn);
        const ArchState& input_state = instr->state_ref();
        const auto src1_vecval = input_state.read_vfp(archx::cvt_vec(rn));
        fp32_t src1, src2;
        src1.u = src1_vecval[0];
        src2.u = 0;
        // FIXME: denorm flushing
        std::uint64_t result_nzcv = fp32_compare(src1, src2);
        instr->write_flags(result_nzcv);
        return true;
    }

    static bool emulate_FCMP_DZ_floatcmp(archx::InstructionIS* instr)
    {
        const auto itext = instr->opcode();
        const auto rn = adis::fldfrom_FCMP_DZ_floatcmp(itext, adis::fields_t::Rn);
        const ArchState& input_state = instr->state_ref();
        const auto src1_vecval = input_state.read_vfp(archx::cvt_vec(rn));
        fp64_t src1, src2;
        src1.u = src1_vecval[0];
        src2.u = 0;
        // FIXME: denorm flushing
        std::uint64_t result_nzcv = fp64_compare(src1, src2);
        instr->write_flags(result_nzcv);
        return true;
    }

    static bool emulate_FCMPE_D_floatcmp(archx::InstructionIS* instr)
    {
        // FIXME: no exception signalling
        return emulate_FCMP_D_floatcmp(instr);
    }

    static bool emulate_FCMPE_S_floatcmp(archx::InstructionIS* instr)
    {
        // FIXME: no exception signalling
        return emulate_FCMP_S_floatcmp(instr);
    }

    static bool emulate_FCMPE_SZ_floatcmp(archx::InstructionIS* instr)
    {
        // FIXME: no exception signalling
        return emulate_FCMP_SZ_floatcmp(instr);
    }

    static bool emulate_FCMPE_DZ_floatcmp(archx::InstructionIS* instr)
    {
        // FIXME: no exception signalling
        return emulate_FCMP_DZ_floatcmp(instr);
    }
    static bool emulate_pass(archx::InstructionIS* instr [[maybe_unused]])
    {
        return true; // do nothing
    }

    static bool emulate_csinc64(archx::InstructionIS* instr)
    {
        const auto itext = instr->opcode();
        const auto rd = adis::fldfrom_CSINC_64_condsel(itext, adis::fields_t::Rd);
        if (rd != 31) {
            const auto cond = adis::fldfrom_CSINC_64_condsel(itext, adis::fields_t::cond);
            std::uint64_t result;
            const ArchState& input_state = instr->state_ref(); // input
            if (condition_holds(cond, instr->pstate_nzcv())) {
                const auto rn = adis::fldfrom_CSINC_64_condsel(itext, adis::fields_t::Rn);
                result = (rn == 31) ? 0 : input_state.read(archx::cvt_gpr(rn));
            } else {
                const auto rm = adis::fldfrom_CSINC_64_condsel(itext, adis::fields_t::Rm);
                result = (rm == 31) ? 0 : input_state.read(archx::cvt_gpr(rm)) + 1;
            }
            instr->write_gpr64(archx::cvt_gpr(rd), result);
        }
        return true;
    }

    static bool emulate_csinc32(archx::InstructionIS* instr)
    {
        const auto itext = instr->opcode();
        const auto rd = adis::fldfrom_CSINC_32_condsel(itext, adis::fields_t::Rd);
        if (rd != 31) {
            const auto cond = adis::fldfrom_CSINC_32_condsel(itext, adis::fields_t::cond);
            std::uint32_t result;
            const ArchState& input_state = instr->state_ref(); // input
            if (condition_holds(cond, instr->pstate_nzcv())) {
                const auto rn = adis::fldfrom_CSINC_32_condsel(itext, adis::fields_t::Rn);
                result = (rn == 31) ? 0 : input_state.read(archx::cvt_gpr(rn));
            } else {
                const auto rm = adis::fldfrom_CSINC_32_condsel(itext, adis::fields_t::Rm);
                result = (rm == 31) ? 0 : input_state.read(archx::cvt_gpr(rm)) + 1;
            }
            instr->write_gpr32(archx::cvt_gpr(rd), result);
        }
        return true;
    }

    static bool emulate_csinv64(archx::InstructionIS* instr)
    {
        const auto itext = instr->opcode();
        const auto rd = adis::fldfrom_CSINV_64_condsel(itext, adis::fields_t::Rd);
        if (rd != 31) {
            const auto cond = adis::fldfrom_CSINV_64_condsel(itext, adis::fields_t::cond);
            std::uint64_t result;
            const ArchState& input_state = instr->state_ref(); // input
            if (condition_holds(cond, instr->pstate_nzcv())) {
                const auto rn = adis::fldfrom_CSINV_64_condsel(itext, adis::fields_t::Rn);
                result = (rn == 31) ? 0 : input_state.read(archx::cvt_gpr(rn));
            } else {
                const auto rm = adis::fldfrom_CSINV_64_condsel(itext, adis::fields_t::Rm);
                result = (rm == 31) ? 0 : input_state.read(archx::cvt_gpr(rm)) + 1;
                result = ~result;
            }
            instr->write_gpr64(archx::cvt_gpr(rd), result);
        }
        return true;
    }

    static bool emulate_csinv32(archx::InstructionIS* instr)
    {
        const auto itext = instr->opcode();
        const auto rd = adis::fldfrom_CSINV_32_condsel(itext, adis::fields_t::Rd);
        if (rd != 31) {
            const auto cond = adis::fldfrom_CSINV_32_condsel(itext, adis::fields_t::cond);
            std::uint32_t result;
            const ArchState& input_state = instr->state_ref(); // input
            if (condition_holds(cond, instr->pstate_nzcv())) {
                const auto rn = adis::fldfrom_CSINV_32_condsel(itext, adis::fields_t::Rn);
                result = (rn == 31) ? 0 : input_state.read(archx::cvt_gpr(rn));
            } else {
                const auto rm = adis::fldfrom_CSINV_32_condsel(itext, adis::fields_t::Rm);
                result = (rm == 31) ? 0 : input_state.read(archx::cvt_gpr(rm)) + 1;
                result = ~result;
            }
            instr->write_gpr32(archx::cvt_gpr(rd), result);
        }
        return true;
    }

    static bool emulate_csel32(archx::InstructionIS* instr)
    {
        const auto itext = instr->opcode();
        const auto rd = adis::fldfrom_CSEL_32_condsel(itext, adis::fields_t::Rd);
        if (rd != 31) {
            const auto cond = adis::fldfrom_CSEL_32_condsel(itext, adis::fields_t::cond);
            std::uint32_t result;
            const ArchState& input_state = instr->state_ref(); // input
            if (condition_holds(cond, instr->pstate_nzcv())) {
                const auto rn = adis::fldfrom_CSEL_32_condsel(itext, adis::fields_t::Rn);
                result = (rn == 31) ? 0 : input_state.read(archx::cvt_gpr(rn));
            } else {
                const auto rm = adis::fldfrom_CSEL_32_condsel(itext, adis::fields_t::Rm);
                result = (rm == 31) ? 0 : input_state.read(archx::cvt_gpr(rm));
            }
            instr->write_gpr32(archx::cvt_gpr(rd), result);
        }
        return true;
    }

    static bool emulate_csel64(archx::InstructionIS* instr)
    {
        const auto itext = instr->opcode();
        const auto rd = adis::fldfrom_CSEL_64_condsel(itext, adis::fields_t::Rd);
        if (rd != 31) {
            const auto cond = adis::fldfrom_CSEL_64_condsel(itext, adis::fields_t::cond);
            std::uint64_t result;
            const ArchState& input_state = instr->state_ref(); // input
            if (condition_holds(cond, instr->pstate_nzcv())) {
                const auto rn = adis::fldfrom_CSEL_64_condsel(itext, adis::fields_t::Rn);
                result = (rn == 31) ? 0 : input_state.read(archx::cvt_gpr(rn));
            } else {
                const auto rm = adis::fldfrom_CSEL_64_condsel(itext, adis::fields_t::Rm);
                result = (rm == 31) ? 0 : input_state.read(archx::cvt_gpr(rm));
            }
            instr->write_gpr64(archx::cvt_gpr(rd), result);
        }
        return true;
    }

    static bool emulate_bfm32(archx::InstructionIS* instr)
    { // ENC_BFM_32M_bitfield
        const auto itext = instr->opcode();
        const auto n = adis::fldfrom_BFM_32M_bitfield(itext, adis::fields_t::N);
        const auto imms = adis::fldfrom_BFM_32M_bitfield(itext, adis::fields_t::imms);
        const auto immr = adis::fldfrom_BFM_32M_bitfield(itext, adis::fields_t::immr);
        const auto imm13 = (n << 12) | (imms << 6) | immr;
        const adis::u32optpair_t vpair = adis::dec_imm13_32(imm13, /*logical*/ false);
        if (vpair.has_value()) {
            // const auto [wmask, tmask] = vpair.value();
            const std::uint32_t wmask = vpair.value().first;
            const std::uint32_t tmask = vpair.value().second;

            const auto rd = adis::fldfrom_BFM_32M_bitfield(itext, adis::fields_t::Rd);
            if (rd != 31) {
                const auto rn = adis::fldfrom_BFM_32M_bitfield(itext, adis::fields_t::Rn);
                const ArchState& input_state = instr->state_ref(); // input
                const auto dst_reg = archx::cvt_gpr(rd);
                const std::uint32_t input_val = (rn == 31) ? 0 : input_state.read(archx::cvt_gpr(rn));
                const std::uint32_t dst_val = (rd == 31) ? 0 : input_state.read(dst_reg);
                const std::uint32_t bot = (dst_val & ~wmask) | (ror_32(input_val, immr, 32) & wmask);
                const std::uint32_t output_val = (dst_val & ~tmask) | (bot & tmask);
                instr->write_gpr32(dst_reg, output_val);
            }
            return true;
        }
        return false;
    }
    static bool emulate_bfm64(archx::InstructionIS* instr)
    { // ENC_BFM_64M_bitfield
        const auto itext = instr->opcode();
        const auto n = adis::fldfrom_BFM_64M_bitfield(itext, adis::fields_t::N);
        const auto imms = adis::fldfrom_BFM_64M_bitfield(itext, adis::fields_t::imms);
        const auto immr = adis::fldfrom_BFM_64M_bitfield(itext, adis::fields_t::immr);
        const auto imm13 = (n << 12) | (imms << 6) | immr;
        const adis::u64optpair_t vpair = adis::dec_imm13_64(imm13, /*logical*/ false);
        if (vpair.has_value()) {
            // const auto [wmask, tmask] = vpair.value();
            const std::uint64_t wmask = vpair.value().first;
            const std::uint64_t tmask = vpair.value().second;

            const auto rd = adis::fldfrom_BFM_64M_bitfield(itext, adis::fields_t::Rd);
            if (rd != 31) {
                const auto rn = adis::fldfrom_BFM_64M_bitfield(itext, adis::fields_t::Rn);
                const ArchState& input_state = instr->state_ref(); // input
                const auto dst_reg = archx::cvt_gpr(rd);
                const std::uint64_t input_val = (rn == 31) ? 0 : input_state.read(archx::cvt_gpr(rn));
                const std::uint64_t dst_val = (rd == 31) ? 0 : input_state.read(dst_reg);
                const std::uint64_t bot = (dst_val & ~wmask) | (ror_64(input_val, immr, 64) & wmask);
                const std::uint64_t output_val = (dst_val & ~tmask) | (bot & tmask);
                instr->write_gpr64(dst_reg, output_val);
            }
            return true;
        }
        return false;
    }

    static bool emulate_ubfm32(archx::InstructionIS* instr)
    {
        const auto itext = instr->opcode();
        const auto n = adis::fldfrom_UBFM_32M_bitfield(itext, adis::fields_t::N);
        const auto imms = adis::fldfrom_UBFM_32M_bitfield(itext, adis::fields_t::imms);
        const auto immr = adis::fldfrom_UBFM_32M_bitfield(itext, adis::fields_t::immr);
        const auto imm13 = (n << 12) | (imms << 6) | immr;
        const adis::u32optpair_t vpair = adis::dec_imm13_32(imm13, /*logical*/ false);
        if (vpair.has_value()) {
            // const auto [wmask, tmask] = vpair.value();
            const std::uint32_t wmask = vpair.value().first;
            const std::uint32_t tmask = vpair.value().second;

            const auto rn = adis::fldfrom_UBFM_32M_bitfield(itext, adis::fields_t::Rn);
            const auto src_reg = archx::cvt_gpr(rn);
            const ArchState& input_state = instr->state_ref(); // input
            const auto input_val = rn == 31 ? 0 : input_state.read(src_reg);
            const std::uint32_t output_val = ror_32(input_val, immr, 32) & wmask & tmask;
            const auto rd = adis::fldfrom_UBFM_32M_bitfield(itext, adis::fields_t::Rd);
            if (rd != 31) {
                const auto dst_reg = archx::cvt_gpr(rd);
                instr->write_gpr32(dst_reg, output_val);
            }
            return true;
        }
        return false;
    }

    static bool emulate_ubfm64(archx::InstructionIS* instr)
    {
        const auto itext = instr->opcode();
        const auto n = adis::fldfrom_UBFM_64M_bitfield(itext, adis::fields_t::N);
        const auto imms = adis::fldfrom_UBFM_64M_bitfield(itext, adis::fields_t::imms);
        const auto immr = adis::fldfrom_UBFM_64M_bitfield(itext, adis::fields_t::immr);
        const auto imm13 = (n << 12) | (imms << 6) | immr;
        const adis::u64optpair_t vpair = adis::dec_imm13_64(imm13, /*logical*/ false);
        if (vpair.has_value()) {
            // const auto [wmask, tmask] = vpair.value(); // interacting poorly with gdb
            const std::uint64_t wmask = vpair.value().first;
            const std::uint64_t tmask = vpair.value().second;
            const auto rn = adis::fldfrom_UBFM_64M_bitfield(itext, adis::fields_t::Rn);
            const auto src_reg = archx::cvt_gpr(rn);
            const ArchState& input_state = instr->state_ref(); // input
            const auto input_val = rn == 31 ? 0 : input_state.read(src_reg);
            const auto output_val = ror_64(input_val, immr, 64) & wmask & tmask;
            const auto rd = adis::fldfrom_UBFM_64M_bitfield(itext, adis::fields_t::Rd);
            if (rd != 31) {
                const auto dst_reg = archx::cvt_gpr(rd);
                instr->write_gpr64(dst_reg, output_val);
            }
            return true;
        }
        return false;
    }
    static bool emulate_sbfm64(archx::InstructionIS* instr)
    {
        const auto itext = instr->opcode();
        const auto n = adis::fldfrom_SBFM_64M_bitfield(itext, adis::fields_t::N);
        const auto imms = adis::fldfrom_SBFM_64M_bitfield(itext, adis::fields_t::imms);
        const auto immr = adis::fldfrom_SBFM_64M_bitfield(itext, adis::fields_t::immr);
        const auto imm13 = (n << 12) | (imms << 6) | immr;
        const adis::u64optpair_t vpair = adis::dec_imm13_64(imm13, /*logical*/ false);
        if (vpair.has_value()) {
            // const auto [wmask, tmask] = vpair.value();
            const std::uint64_t wmask = vpair.value().first;
            const std::uint64_t tmask = vpair.value().second;

            const auto rn = adis::fldfrom_SBFM_64M_bitfield(itext, adis::fields_t::Rn);
            const auto src_reg = archx::cvt_gpr(rn);
            const ArchState& input_state = instr->state_ref(); // input
            const auto input_val = rn == 31 ? 0 : input_state.read(src_reg);
            const auto bot = ror_64(input_val, immr, 64) & wmask;
            const auto top = ((input_val >> imms) & 1) ? ~0ULL : 0;
            const auto output_val = (top & ~tmask) | (bot & tmask);
            const auto rd = adis::fldfrom_SBFM_64M_bitfield(itext, adis::fields_t::Rd);
            if (rd != 31) {
                const auto dst_reg = archx::cvt_gpr(rd);
                instr->write_gpr64(dst_reg, output_val);
            }
            return true;
        }
        return false;
    }

    static bool emulate_sbfm32(archx::InstructionIS* instr)
    {
        const auto itext = instr->opcode();
        const auto n = adis::fldfrom_SBFM_32M_bitfield(itext, adis::fields_t::N);
        const auto imms = adis::fldfrom_SBFM_32M_bitfield(itext, adis::fields_t::imms);
        const auto immr = adis::fldfrom_SBFM_32M_bitfield(itext, adis::fields_t::immr);
        const auto imm13 = (n << 12) | (imms << 6) | immr;
        const adis::u32optpair_t vpair = adis::dec_imm13_32(imm13, /*logical*/ false);
        if (vpair.has_value()) {
            // const auto [wmask, tmask] = vpair.value();
            const std::uint32_t wmask = vpair.value().first;
            const std::uint32_t tmask = vpair.value().second;

            const auto rn = adis::fldfrom_SBFM_32M_bitfield(itext, adis::fields_t::Rn);
            const auto src_reg = archx::cvt_gpr(rn);
            const ArchState& input_state = instr->state_ref(); // input
            const std::uint32_t input_val = rn == 31 ? 0 : input_state.read(src_reg);
            const std::uint32_t bot = ror_32(input_val, immr, 32) & wmask;
            const std::uint32_t zero = 0;
            const std::uint32_t top = ((input_val >> imms) & 1) ? ~zero : 0;
            const std::uint32_t output_val = (top & ~tmask) | (bot & tmask);
            const auto rd = adis::fldfrom_SBFM_32M_bitfield(itext, adis::fields_t::Rd);
            if (rd != 31) {
                const auto dst_reg = archx::cvt_gpr(rd);
                instr->write_gpr32(dst_reg, output_val);
            }
            return true;
        }
        return false;
    }

    static bool emulate_adrp(archx::InstructionIS* instr)
    {
        const auto itext = instr->opcode();
        const auto rd = adis::fldfrom_ADRP_only_pcreladdr(itext, adis::fields_t::Rd);
        if (rd != 31) {
            const std::uint64_t immlo_2b = adis::fldfrom_ADRP_only_pcreladdr(itext, adis::fields_t::immlo);
            const std::uint64_t immhi_19b = adis::fldfrom_ADRP_only_pcreladdr(itext, adis::fields_t::immhi);
            const std::int64_t offset = ((immhi_19b << 2) | immlo_2b) << 12;
            const std::int64_t offset_sext = sign_extend(offset, 21 + 12);
            // fmt::print("ADRP {:016x} -> {:016x}\n", offset, offset_sext);
            const auto lreg_dst = archx::cvt_gpr(rd);
            ProgramCounter output = instr->program_counter().page_address_4k() + offset_sext;
            instr->write_gpr64(lreg_dst, output.value());
        }
        return true;
    }
    static bool emulate_adr(archx::InstructionIS* instr)
    {
        const auto itext = instr->opcode();
        const auto rd = adis::fldfrom_ADRP_only_pcreladdr(itext, adis::fields_t::Rd);
        if (rd != 31) {
            const std::uint64_t immlo_2b = adis::fldfrom_ADR_only_pcreladdr(itext, adis::fields_t::immlo);
            const std::uint64_t immhi_19b = adis::fldfrom_ADR_only_pcreladdr(itext, adis::fields_t::immhi);
            const std::int64_t offset = (immhi_19b << 2) | immlo_2b;
            const std::int64_t offset_sext = sign_extend(offset, 21);
            const auto lreg_dst = archx::cvt_gpr(rd);
            ProgramCounter output(static_cast<std::int64_t>(instr->program_counter().value()) + offset_sext);
            instr->write_gpr64(lreg_dst, output.value());
        }
        return true;
    }
    static bool emulate_ldrb_post(archx::InstructionIS* instr)
    {
        const auto itext = instr->opcode();
        const auto rn = adis::fldfrom_LDRB_32_ldst_immpost(itext, adis::fields_t::Rn); // could be SP
        const auto rt = adis::fldfrom_LDRB_32_ldst_immpost(itext, adis::fields_t::Rt);
        const std::uint64_t imm = adis::fldfrom_LDRB_32_ldst_immpost(itext, adis::fields_t::imm9);
        const auto base_reg = archx::cvt_gpr(rn, archx::LReg::SP);
        const ArchState& input_state = instr->state_ref(); // input
        const auto regval = input_state.read(base_reg);
        const VirtAddr base_va(regval);
        instr->add_emulated_virt_addr(base_va);
        const auto mval = mem_read(instr, base_va, 1);
        if (mval.has_value() && rt != 31) {
            instr->write_gpr32(archx::cvt_gpr(rt), mval.value());
        }
        const VirtAddr post_va(regval + sign_extend(imm, 9));
        instr->write_gpr64(base_reg, post_va.value());
        return true;
    }

    static bool emulate_ldrb_pre(archx::InstructionIS* instr)
    {
        const auto itext = instr->opcode();
        const auto rn = adis::fldfrom_LDRB_32_ldst_immpre(itext, adis::fields_t::Rn); // could be SP
        const auto rt = adis::fldfrom_LDRB_32_ldst_immpre(itext, adis::fields_t::Rt);
        const std::uint64_t imm = adis::fldfrom_LDRB_32_ldst_immpre(itext, adis::fields_t::imm9);
        const auto base_reg = archx::cvt_gpr(rn, archx::LReg::SP);
        const ArchState& input_state = instr->state_ref(); // input
        const auto regval = input_state.read(base_reg);
        const VirtAddr va(regval + sign_extend(imm, 9));
        instr->add_emulated_virt_addr(va);
        const auto mval = mem_read(instr, va, 1);
        if (mval.has_value() && rt != 31) {
            instr->write_gpr32(archx::cvt_gpr(rt), mval.value());
        }
        instr->write_gpr64(base_reg, va.value());
        return true;
    }
    static bool emulate_ldrb_pos(archx::InstructionIS* instr)
    {
        const auto itext = instr->opcode();
        const auto rn = adis::fldfrom_LDRB_32_ldst_pos(itext, adis::fields_t::Rn); // could be SP
        const auto rt = adis::fldfrom_LDRB_32_ldst_pos(itext, adis::fields_t::Rt);
        const auto imm = adis::fldfrom_LDRB_32_ldst_pos(itext, adis::fields_t::imm12);
        const auto lreg = archx::cvt_gpr(rn, archx::LReg::SP);
        const ArchState& input_state = instr->state_ref(); // input
        const auto regval = input_state.read(lreg);
        const VirtAddr va(regval + imm);
        instr->add_emulated_virt_addr(va);
        const auto mval = mem_read(instr, va, 1);
        if (mval.has_value() && rt != 31) {
            instr->write_gpr32(archx::cvt_gpr(rt), mval.value());
        }
        return true;
    }
    static bool emulate_ldrh_pos(archx::InstructionIS* instr)
    {
        const auto itext = instr->opcode();
        const ArchState& input_state = instr->state_ref();                         // input
        const auto rn = adis::fldfrom_LDRH_32_ldst_pos(itext, adis::fields_t::Rn); // could be SP
        const auto rt = adis::fldfrom_LDRH_32_ldst_pos(itext, adis::fields_t::Rt);
        const auto imm = adis::fldfrom_LDRH_32_ldst_pos(itext, adis::fields_t::imm12);
        const auto lreg = archx::cvt_gpr(rn, archx::LReg::SP);
        const auto regval = input_state.read(lreg);
        const VirtAddr va(regval + (imm << 1)); // *2
        instr->add_emulated_virt_addr(va);
        const auto mval = mem_read(instr, va, 2);
        if (mval.has_value() && rt != 31) {
            instr->write_gpr32(archx::cvt_gpr(rt), mval.value());
        }
        return true;
    }
    static bool emulate_ldrh_pre(archx::InstructionIS* instr)
    {
        const auto itext = instr->opcode();
        const ArchState& input_state = instr->state_ref();                            // input
        const auto rn = adis::fldfrom_LDRH_32_ldst_immpre(itext, adis::fields_t::Rn); // could be SP
        const auto rt = adis::fldfrom_LDRH_32_ldst_immpre(itext, adis::fields_t::Rt);
        const auto imm = adis::fldfrom_LDRH_32_ldst_immpre(itext, adis::fields_t::imm9);
        const auto base_reg = archx::cvt_gpr(rn, archx::LReg::SP);
        const auto regval = input_state.read(base_reg);
        const VirtAddr va(regval + (sign_extend(imm, 9) * 2));
        instr->add_emulated_virt_addr(va);
        const auto mval = mem_read(instr, va, 2);
        if (mval.has_value() && rt != 31) {
            instr->write_gpr32(archx::cvt_gpr(rt), mval.value());
        }
        instr->write_gpr64(base_reg, va.value());
        return true;
    }
    static bool emulate_ldrh_post(archx::InstructionIS* instr)
    {
        const auto itext = instr->opcode();
        const ArchState& input_state = instr->state_ref();                             // input
        const auto rn = adis::fldfrom_LDRH_32_ldst_immpost(itext, adis::fields_t::Rn); // could be SP
        const auto rt = adis::fldfrom_LDRH_32_ldst_immpost(itext, adis::fields_t::Rt);
        const auto imm = adis::fldfrom_LDRH_32_ldst_immpost(itext, adis::fields_t::imm9);
        const auto base_reg = archx::cvt_gpr(rn, archx::LReg::SP);
        const auto regval = input_state.read(base_reg);
        const VirtAddr va(regval);
        instr->add_emulated_virt_addr(va);
        const auto mval = mem_read(instr, va, 2);
        if (mval.has_value() && rt != 31) {
            instr->write_gpr32(archx::cvt_gpr(rt), mval.value());
        }
        const VirtAddr post_va(regval + (sign_extend(imm, 9) * 2));
        instr->write_gpr64(base_reg, post_va.value());
        return true;
    }

    static bool emulate_strq_reg(archx::InstructionIS* instr)
    {
        const auto itext = instr->opcode();
        const ArchState& input_state = instr->state_ref();                          // input
        const auto rt = adis::fldfrom_STR_Q_ldst_regoff(itext, adis::fields_t::Rt); // vpr
        const auto rn = adis::fldfrom_STR_Q_ldst_regoff(itext, adis::fields_t::Rn); // may be SP
        const auto rm = adis::fldfrom_STR_Q_ldst_regoff(itext, adis::fields_t::Rm);
        const auto shift = adis::fldfrom_STR_Q_ldst_regoff(itext, adis::fields_t::S);
        const auto option = adis::fldfrom_STR_Q_ldst_regoff(itext, adis::fields_t::option);
        const auto addr = ld_regoff_common(input_state, rn, rm, option, shift, 4);
        if (addr.has_value()) {
            const VirtAddr va(addr.value());
            instr->add_emulated_virt_addr(va);
            const auto vec_lreg_src = archx::cvt_vec(rt);
            const auto src_data = input_state.read_vfp(vec_lreg_src);
            mem_write_64bit(instr, va, src_data[0]);
            mem_write_64bit(instr, va + 8, src_data[1]);
            return true;
        }
        return false;
    }

    static bool emulate_ldrq_reg(archx::InstructionIS* instr)
    {
        const auto itext = instr->opcode();
        const ArchState& input_state = instr->state_ref();                          // input
        const auto rt = adis::fldfrom_LDR_Q_ldst_regoff(itext, adis::fields_t::Rt); // vpr
        const auto rn = adis::fldfrom_LDR_Q_ldst_regoff(itext, adis::fields_t::Rn); // may be SP
        const auto rm = adis::fldfrom_LDR_Q_ldst_regoff(itext, adis::fields_t::Rm);
        const auto shift = adis::fldfrom_LDR_Q_ldst_regoff(itext, adis::fields_t::S);
        const auto option = adis::fldfrom_LDR_Q_ldst_regoff(itext, adis::fields_t::option);
        const auto addr = ld_regoff_common(input_state, rn, rm, option, shift, 4);
        if (addr.has_value()) {
            const VirtAddr va(addr.value());
            instr->add_emulated_virt_addr(va);
            const auto mval = mem_read_128(instr, va);
            if (mval.has_value()) {
                const auto vec_dst = archx::cvt_vec(rt);
                instr->write_vfp(vec_dst, mval.value());
            }
            return true;
        }
        return false;
    }

    static bool emulate_ldrd_reg(archx::InstructionIS* instr)
    {
        const auto itext = instr->opcode();
        const ArchState& input_state = instr->state_ref();                          // input
        const auto rt = adis::fldfrom_LDR_D_ldst_regoff(itext, adis::fields_t::Rt); // vpr
        const auto rn = adis::fldfrom_LDR_D_ldst_regoff(itext, adis::fields_t::Rn); // may be SP
        const auto rm = adis::fldfrom_LDR_D_ldst_regoff(itext, adis::fields_t::Rm);
        const auto shift = adis::fldfrom_LDR_D_ldst_regoff(itext, adis::fields_t::S);
        const auto option = adis::fldfrom_LDR_D_ldst_regoff(itext, adis::fields_t::option);
        const auto addr = ld_regoff_common(input_state, rn, rm, option, shift, 3);
        if (addr.has_value()) {
            const VirtAddr va(addr.value());
            instr->add_emulated_virt_addr(va);
            const auto mval = mem_read(instr, va, 8);
            if (mval.has_value()) {
                const auto vec_dst = archx::cvt_vec(rt);
                instr->write_vfp(vec_dst, mval.value());
            }
            return true;
        }
        return false;
    }

    static bool emulate_add_32_addsub_shift(archx::InstructionIS* instr)
    {
        return emulate_add_32_addsub_shift_gen(instr, false);
    }
    static bool emulate_adds_32_addsub_shift(archx::InstructionIS* instr)
    {
        return emulate_add_32_addsub_shift_gen(instr, true);
    }
    static bool emulate_add_32_addsub_shift_gen(archx::InstructionIS* instr, bool set_flags)
    {
        const auto itext = instr->opcode();
        const ArchState& input_state = instr->state_ref();                                // input
        const auto imm6 = adis::fldfrom_ADD_32_addsub_shift(itext, adis::fields_t::imm6); // shift/rotate amount
        if ((imm6 >> 5) & 1) {
            return false; // undefined
        }
        const auto shift_type = adis::fldfrom_ADD_32_addsub_shift(itext, adis::fields_t::shift); // shift/rotate type
        if (shift_type == 3) {
            return false; // undefined
        }
        const auto rd = adis::fldfrom_ADD_32_addsub_shift(itext, adis::fields_t::Rd);
        const auto rn = adis::fldfrom_ADD_32_addsub_shift(itext, adis::fields_t::Rn);
        const auto rm = adis::fldfrom_ADD_32_addsub_shift(itext, adis::fields_t::Rm); // may be shifted
        const auto lreg_srcm = archx::cvt_gpr(rm);
        const auto lreg_srcn = archx::cvt_gpr(rn);
        const auto lreg_dst = archx::cvt_gpr(rd);
        const std::uint32_t regval_srcm = rm == 31 ? 0 : input_state.read(lreg_srcm);
        const std::uint32_t regval_srcn = rn == 31 ? 0 : input_state.read(lreg_srcn);
        std::uint32_t regval_srcm_shifted = 0;
        switch (shift_type) {
        case 0: // lsl
            regval_srcm_shifted = regval_srcm << imm6;
            break;
        case 1: // lsr
            regval_srcm_shifted = regval_srcm >> imm6;
            break;
        case 2: // asr
            regval_srcm_shifted = static_cast<std::int64_t>(regval_srcm) >> imm6;
            break;
        default:
            return false; // illegal, do not emulate
        }
        const std::uint32_t result = regval_srcn + regval_srcm_shifted;
        if (rd != 31) {
            instr->write_gpr32(lreg_dst, result);
        }
        if (set_flags) {
            const std::uint64_t output_flags = flags32(regval_srcn, regval_srcm_shifted, 0);
            instr->write_flags(output_flags);
        }
        return true;
    }
    static bool emulate_add_64_addsub_shift(archx::InstructionIS* instr)
    {
        return emulate_add_64_addsub_shift_gen(instr, false);
    }
    static bool emulate_adds_64_addsub_shift(archx::InstructionIS* instr)
    {
        return emulate_add_64_addsub_shift_gen(instr, true);
    }
    static bool emulate_add_64_addsub_shift_gen(archx::InstructionIS* instr, bool set_flags)
    {
        const auto itext = instr->opcode();
        const ArchState& input_state = instr->state_ref();                                       // input
        const auto shift_type = adis::fldfrom_ADD_64_addsub_shift(itext, adis::fields_t::shift); // shift/rotate type
        if (shift_type == 3) {
            return false; // undefined
        }
        const auto rd = adis::fldfrom_ADD_64_addsub_shift(itext, adis::fields_t::Rd);
        const auto rn = adis::fldfrom_ADD_64_addsub_shift(itext, adis::fields_t::Rn);
        const auto rm = adis::fldfrom_ADD_64_addsub_shift(itext, adis::fields_t::Rm); // may be shifted
        const std::uint64_t imm6 =
            adis::fldfrom_ADD_64_addsub_shift(itext, adis::fields_t::imm6); // shift/rotate amount
        const auto lreg_srcm = archx::cvt_gpr(rm);
        const auto lreg_srcn = archx::cvt_gpr(rn);
        const auto lreg_dst = archx::cvt_gpr(rd);
        const std::uint64_t regval_srcm = rm == 31 ? 0 : input_state.read(lreg_srcm);
        const std::uint64_t regval_srcn = rn == 31 ? 0 : input_state.read(lreg_srcn);
        std::uint64_t regval_srcm_shifted = 0;
        switch (shift_type) {
        case 0: // lsl
            regval_srcm_shifted = regval_srcm << imm6;
            break;
        case 1: // lsr
            regval_srcm_shifted = regval_srcm >> imm6;
            break;
        case 2: // asr
            regval_srcm_shifted = static_cast<std::int64_t>(regval_srcm) >> imm6;
            break;
        default:
            return false; // illegal, do not emulate
        }
        const std::uint64_t result = regval_srcn + regval_srcm_shifted;
        if (rd != 31) {
            instr->write_gpr64(lreg_dst, result);
        }
        if (set_flags) {
            const std::uint64_t output_flags = flags64(regval_srcn, regval_srcm_shifted, 0);
            instr->write_flags(output_flags);
        }
        return true;
    }

    static bool emulate_ldrsb_32_pos(archx::InstructionIS* instr)
    {
        const auto itext = instr->opcode();
        const ArchState& input_state = instr->state_ref(); // input
        const auto rt = adis::fldfrom_LDRSB_32_ldst_pos(itext, adis::fields_t::Rt);
        const auto rn = adis::fldfrom_LDRSB_32_ldst_pos(itext, adis::fields_t::Rn); // could be SP

        const auto uimm12 = adis::fldfrom_LDRSB_32_ldst_pos(itext, adis::fields_t::imm12);
        const auto lreg_base = archx::cvt_gpr(rn, archx::LReg::SP);
        const auto pre_addr = input_state.read(lreg_base);
        const auto post_addr = pre_addr + uimm12;

        auto va = VirtAddr(post_addr);
        instr->add_emulated_virt_addr(va);
        const auto mval = mem_read(instr, va, 1);
        if (mval.has_value() && rt != 31) {
            const std::uint32_t tval = mval.value();
            const std::uint32_t oval = sign_extend(tval, 8);
            instr->write_gpr32(archx::cvt_gpr(rt), oval);
        }
        return true;
    }

    static bool emulate_ldrsh_32_pos(archx::InstructionIS* instr)
    {
        const auto itext = instr->opcode();
        const ArchState& input_state = instr->state_ref(); // input
        const auto rt = adis::fldfrom_LDRSH_32_ldst_pos(itext, adis::fields_t::Rt);
        const auto rn = adis::fldfrom_LDRSH_32_ldst_pos(itext, adis::fields_t::Rn); // could be SP

        const auto uimm12 = adis::fldfrom_LDRSH_32_ldst_pos(itext, adis::fields_t::imm12);
        const auto lreg_base = archx::cvt_gpr(rn, archx::LReg::SP);
        const auto pre_addr = input_state.read(lreg_base);
        const auto post_addr = pre_addr + (uimm12 * 2);

        auto va = VirtAddr(post_addr);
        instr->add_emulated_virt_addr(va);
        const auto mval = mem_read(instr, va, 2);
        if (mval.has_value() && rt != 31) {
            const std::uint32_t tval = mval.value();
            const std::uint32_t oval = sign_extend(tval, 16);
            instr->write_gpr32(archx::cvt_gpr(rt), oval);
        }
        return true;
    }

    static bool emulate_ldrsh_64_pos(archx::InstructionIS* instr)
    {
        const auto itext = instr->opcode();
        const ArchState& input_state = instr->state_ref(); // input
        const auto rt = adis::fldfrom_LDRSH_64_ldst_pos(itext, adis::fields_t::Rt);
        const auto rn = adis::fldfrom_LDRSH_64_ldst_pos(itext, adis::fields_t::Rn); // could be SP

        const auto uimm12 = adis::fldfrom_LDRSH_64_ldst_pos(itext, adis::fields_t::imm12);
        const auto lreg_base = archx::cvt_gpr(rn, archx::LReg::SP);
        const auto pre_addr = input_state.read(lreg_base);
        const auto post_addr = pre_addr + (uimm12 * 2);

        auto va = VirtAddr(post_addr);
        instr->add_emulated_virt_addr(va);
        const auto mval = mem_read(instr, va, 2);
        if (mval.has_value() && rt != 31) {
            const std::uint32_t tval = mval.value();
            const std::uint64_t oval = sign_extend(tval, 16);
            instr->write_gpr64(archx::cvt_gpr(rt), oval);
        }
        return true;
    }

    static bool emulate_ldrsw_64_pos(archx::InstructionIS* instr)
    {
        const auto itext = instr->opcode();
        const ArchState& input_state = instr->state_ref(); // input
        const auto rt = adis::fldfrom_LDRSW_64_ldst_pos(itext, adis::fields_t::Rt);
        const auto rn = adis::fldfrom_LDRSW_64_ldst_pos(itext, adis::fields_t::Rn); // could be SP

        const auto uimm12 = adis::fldfrom_LDRSW_64_ldst_pos(itext, adis::fields_t::imm12);
        const auto lreg_base = archx::cvt_gpr(rn, archx::LReg::SP);
        const auto pre_addr = input_state.read(lreg_base);
        const auto post_addr = pre_addr + (uimm12 * 4);

        auto va = VirtAddr(post_addr);
        instr->add_emulated_virt_addr(va);
        const auto mval = mem_read(instr, va, 4);
        if (mval.has_value() && rt != 31) {
            const std::uint32_t tval = mval.value();
            const std::uint64_t oval = sign_extend(tval, 32);
            instr->write_gpr64(archx::cvt_gpr(rt), oval);
        }
        return true;
    }

    static bool emulate_ldrsw_64_immpost(archx::InstructionIS* instr)
    {
        const auto itext = instr->opcode();
        const ArchState& input_state = instr->state_ref(); // input
        const auto rt = adis::fldfrom_LDRSW_64_ldst_immpost(itext, adis::fields_t::Rt);
        const auto rn = adis::fldfrom_LDRSW_64_ldst_immpost(itext, adis::fields_t::Rn); // could be SP

        const auto simm9 = adis::fldfrom_LDRSW_64_ldst_immpost(itext, adis::fields_t::imm9);
        const auto lreg_base = archx::cvt_gpr(rn, archx::LReg::SP);
        const auto pre_addr = input_state.read(lreg_base);
        const auto post_addr = pre_addr + sign_extend(simm9, 9);

        auto va = VirtAddr(pre_addr);      // use addr before updating
        instr->add_emulated_virt_addr(va); // store VA so that we can find it later
        const auto mval = mem_read(instr, va, 4);
        if (mval.has_value() && rt != 31) {
            const std::uint32_t tval = mval.value();
            const std::uint64_t oval = sign_extend(tval, 32);
            instr->write_gpr64(archx::cvt_gpr(rt), oval);
        }
        instr->write_gpr64(lreg_base, post_addr);
        return true;
    }

    static bool emulate_ldrsh_32_reg(archx::InstructionIS* instr)
    {
        const auto itext = instr->opcode();
        const ArchState& input_state = instr->state_ref();                             // input
        const auto rn = adis::fldfrom_LDRSH_32_ldst_regoff(itext, adis::fields_t::Rn); // could be SP
        const auto rm = adis::fldfrom_LDRSH_32_ldst_regoff(itext, adis::fields_t::Rm);
        const auto option = adis::fldfrom_LDRSH_32_ldst_regoff(itext, adis::fields_t::option);
        const auto shift = adis::fldfrom_LDRSH_32_ldst_regoff(itext, adis::fields_t::S);
        const auto addr = ld_regoff_common(input_state, rn, rm, option, shift, 1);
        if (addr.has_value()) {
            const VirtAddr va(addr.value());
            instr->add_emulated_virt_addr(va);
            const auto rt = adis::fldfrom_LDRSH_32_ldst_regoff(itext, adis::fields_t::Rt);
            if (rt != 31) {
                const auto mval = mem_read(instr, va, 2);
                if (mval.has_value()) {
                    const std::uint16_t tval = mval.value();
                    const std::uint32_t oval = sign_extend(tval, 16);
                    instr->write_gpr32(archx::cvt_gpr(rt), oval);
                }
            }
            return true;
        }
        return false;
    }

    static bool emulate_ldrsh_64_reg(archx::InstructionIS* instr)
    {
        const auto itext = instr->opcode();
        const ArchState& input_state = instr->state_ref();                             // input
        const auto rn = adis::fldfrom_LDRSH_64_ldst_regoff(itext, adis::fields_t::Rn); // could be SP
        const auto rm = adis::fldfrom_LDRSH_64_ldst_regoff(itext, adis::fields_t::Rm);
        const auto option = adis::fldfrom_LDRSH_64_ldst_regoff(itext, adis::fields_t::option);
        const auto shift = adis::fldfrom_LDRSH_64_ldst_regoff(itext, adis::fields_t::S);
        const auto addr = ld_regoff_common(input_state, rn, rm, option, shift, 1);
        if (addr.has_value()) {
            const VirtAddr va(addr.value());
            instr->add_emulated_virt_addr(va);
            const auto rt = adis::fldfrom_LDRSH_64_ldst_regoff(itext, adis::fields_t::Rt);
            if (rt != 31) {
                const auto mval = mem_read(instr, va, 2);
                if (mval.has_value()) {
                    const std::uint16_t tval = mval.value();
                    const std::uint64_t oval = sign_extend(tval, 16);
                    instr->write_gpr64(archx::cvt_gpr(rt), oval);
                }
            }
            return true;
        }
        return false;
    }

    static bool emulate_ldrsw_64_reg(archx::InstructionIS* instr)
    {
        const auto itext = instr->opcode();
        const ArchState& input_state = instr->state_ref(); // input
        const auto addr = ld_regoff(input_state, itext, 2);
        if (addr.has_value()) {
            const VirtAddr va(addr.value());
            instr->add_emulated_virt_addr(va);
            const auto rt = adis::fldfrom_LDRSW_64_ldst_regoff(itext, adis::fields_t::Rt);
            if (rt != 31) {
                const auto mval = mem_read(instr, va, 4);
                if (mval.has_value()) {
                    const std::uint32_t tval = mval.value();
                    const std::uint64_t oval = sign_extend(tval, 32);
                    instr->write_gpr64(archx::cvt_gpr(rt), oval);
                }
            }
            return true;
        }
        return false;
    }

    static bool emulate_ldr_64_reg(archx::InstructionIS* instr)
    {
        const auto itext = instr->opcode();
        const ArchState& input_state = instr->state_ref(); // input
        const auto addr = ld_regoff(input_state, itext, 3);
        if (addr.has_value()) {
            const VirtAddr va(addr.value());
            instr->add_emulated_virt_addr(va);
            const auto rt = adis::fldfrom_LDR_64_ldst_regoff(itext, adis::fields_t::Rt);
            if (rt != 31) {
                const auto mval = mem_read(instr, va, 8);
                if (mval.has_value()) {
                    instr->write_gpr64(archx::cvt_gpr(rt), mval.value());
                }
            }
            return true;
        }
        return false;
    }

    static bool emulate_ldr_64_post(archx::InstructionIS* instr)
    {
        const auto itext = instr->opcode();
        const ArchState& input_state = instr->state_ref();                            // input
        const auto rn = adis::fldfrom_LDR_64_ldst_immpost(itext, adis::fields_t::Rn); // could be SP
        const auto rt = adis::fldfrom_LDR_64_ldst_immpost(itext, adis::fields_t::Rt);
        const std::uint64_t imm = adis::fldfrom_LDR_64_ldst_immpost(itext, adis::fields_t::imm9);
        const auto base_reg = archx::cvt_gpr(rn, archx::LReg::SP);
        const auto regval = input_state.read(base_reg);
        const VirtAddr base_va(regval);
        instr->add_emulated_virt_addr(base_va);
        if (rt != 31) {
            const auto mval = mem_read(instr, base_va, 8);
            if (mval.has_value()) {
                instr->write_gpr64(archx::cvt_gpr(rt), mval.value());
            }
        }
        const VirtAddr post_va(regval + sign_extend(imm, 9));
        instr->write_gpr64(base_reg, post_va.value());
        return true;
    }
    static bool emulate_ldr_64_pre(archx::InstructionIS* instr)
    {
        const auto itext = instr->opcode();
        const ArchState& input_state = instr->state_ref();                           // input
        const auto rn = adis::fldfrom_LDR_64_ldst_immpre(itext, adis::fields_t::Rn); // could be SP
        const auto rt = adis::fldfrom_LDR_64_ldst_immpre(itext, adis::fields_t::Rt);
        const std::uint64_t imm = adis::fldfrom_LDR_64_ldst_immpre(itext, adis::fields_t::imm9);
        const auto base_reg = archx::cvt_gpr(rn, archx::LReg::SP);
        const auto regval = input_state.read(base_reg);
        const std::int64_t offset = sign_extend(imm, 9);
        const VirtAddr va(regval + offset);
        instr->add_emulated_virt_addr(va);
        if (rt != 31) {
            const auto mval = mem_read(instr, va, 8);
            if (mval.has_value()) {
                instr->write_gpr64(archx::cvt_gpr(rt), mval.value());
            }
        }
        instr->write_gpr64(base_reg, va.value());
        return true;
    }
    static bool emulate_ldr_64_pos(archx::InstructionIS* instr)
    {
        const auto itext = instr->opcode();
        const ArchState& input_state = instr->state_ref();                        // input
        const auto rn = adis::fldfrom_LDR_64_ldst_pos(itext, adis::fields_t::Rn); // could be SP
        const auto rt = adis::fldfrom_LDR_64_ldst_pos(itext, adis::fields_t::Rt);
        const std::uint64_t imm = adis::fldfrom_LDR_64_ldst_pos(itext, adis::fields_t::imm12);
        const auto base_reg = archx::cvt_gpr(rn, archx::LReg::SP);
        const auto regval = input_state.read(base_reg);
        const VirtAddr va(regval + imm * 8);
        instr->add_emulated_virt_addr(va);
        if (rt != 31) {
            const auto mval = mem_read(instr, va, 8);
            if (mval.has_value()) {
                instr->write_gpr64(archx::cvt_gpr(rt), mval.value());
            }
        }
        return true;
    }

    static bool emulate_ldr_32_post(archx::InstructionIS* instr)
    {
        const auto itext = instr->opcode();
        const ArchState& input_state = instr->state_ref();                            // input
        const auto rn = adis::fldfrom_LDR_32_ldst_immpost(itext, adis::fields_t::Rn); // could be SP
        const std::uint64_t imm = adis::fldfrom_LDR_32_ldst_immpost(itext, adis::fields_t::imm9);
        const auto base_reg = archx::cvt_gpr(rn, archx::LReg::SP);
        const auto regval = input_state.read(base_reg);
        const VirtAddr base_va(regval);
        instr->add_emulated_virt_addr(base_va);
        const auto rt = adis::fldfrom_LDR_32_ldst_immpost(itext, adis::fields_t::Rt);
        if (rt != 31) {
            const auto mval = mem_read(instr, base_va, 4);
            if (mval.has_value()) {
                instr->write_gpr32(archx::cvt_gpr(rt), mval.value());
            }
        }
        const VirtAddr post_va(regval + sign_extend(imm, 9));
        instr->write_gpr64(base_reg, post_va.value());
        return true;
    }
    static bool emulate_ldr_32_pre(archx::InstructionIS* instr)
    {
        const auto itext = instr->opcode();
        const ArchState& input_state = instr->state_ref();                           // input
        const auto rn = adis::fldfrom_LDR_32_ldst_immpre(itext, adis::fields_t::Rn); // could be SP
        const std::uint64_t imm = adis::fldfrom_LDR_32_ldst_immpre(itext, adis::fields_t::imm9);
        const auto base_reg = archx::cvt_gpr(rn, archx::LReg::SP);
        const auto regval = input_state.read(base_reg);
        const VirtAddr va(regval + sign_extend(imm, 9));
        instr->add_emulated_virt_addr(va);
        const auto rt = adis::fldfrom_LDR_32_ldst_immpre(itext, adis::fields_t::Rt);
        if (rt != 31) {
            const auto mval = mem_read(instr, va, 4);
            if (mval.has_value()) {
                instr->write_gpr32(archx::cvt_gpr(rt), mval.value());
            }
        }
        instr->write_gpr64(base_reg, va.value());
        return true;
    }
    static bool emulate_ldr_32_pos(archx::InstructionIS* instr)
    {
        const auto itext = instr->opcode();
        const ArchState& input_state = instr->state_ref();                        // input
        const auto rn = adis::fldfrom_LDR_32_ldst_pos(itext, adis::fields_t::Rn); // could be SP
        const auto imm = adis::fldfrom_LDR_32_ldst_pos(itext, adis::fields_t::imm12);
        const auto lreg = archx::cvt_gpr(rn, archx::LReg::SP);
        const auto regval = input_state.read(lreg);
        const VirtAddr va(regval + (imm << 2)); // *4
        instr->add_emulated_virt_addr(va);
        const auto rt = adis::fldfrom_LDR_32_ldst_pos(itext, adis::fields_t::Rt);
        if (rt != 31) {
            const auto mval = mem_read(instr, va, 4);
            if (mval.has_value()) {
                instr->write_gpr32(archx::cvt_gpr(rt), mval.value());
            }
        }
        return true;
    }

    static bool emulate_ldr_32_reg(archx::InstructionIS* instr)
    {
        const auto itext = instr->opcode();
        const ArchState& input_state = instr->state_ref(); // input
        const auto addr = ld_regoff(input_state, itext, 2);
        if (addr.has_value()) {
            const VirtAddr va(addr.value());
            instr->add_emulated_virt_addr(va);
            const auto rt = adis::fldfrom_LDR_32_ldst_regoff(itext, adis::fields_t::Rt);
            if (rt != 31) {
                const auto mval = mem_read(instr, va, 4);
                if (mval.has_value()) {
                    instr->write_gpr32(archx::cvt_gpr(rt), mval.value());
                }
            }
            return true;
        }
        return false;
    }

    static bool emulate_ldr_s_reg(archx::InstructionIS* instr)
    {
        const auto itext = instr->opcode();
        const ArchState& input_state = instr->state_ref();                          // input
        const auto rn = adis::fldfrom_LDR_S_ldst_regoff(itext, adis::fields_t::Rn); // could be SP
        const auto rm = adis::fldfrom_LDR_S_ldst_regoff(itext, adis::fields_t::Rm);
        const auto option = adis::fldfrom_LDR_S_ldst_regoff(itext, adis::fields_t::option);
        const auto shift = adis::fldfrom_LDR_S_ldst_regoff(itext, adis::fields_t::S);
        const auto addr = ld_regoff_common(input_state, rn, rm, option, shift, 2);
        if (addr.has_value()) {
            const VirtAddr va(addr.value());
            instr->add_emulated_virt_addr(va);
            const auto mval = mem_read(instr, va, 4);
            if (mval.has_value()) {
                const auto rt = adis::fldfrom_LDR_S_ldst_regoff(itext, adis::fields_t::Rt);
                instr->write_vfp(archx::cvt_vec(rt), mval.value());
            }
            return true;
        }
        return false;
    }

    static std::uint64_t byte_rev64(std::uint64_t src) { return __builtin_bswap64(src); }
    static std::uint64_t byte_rev32(std::uint64_t src) { return __builtin_bswap32(src); }

    static bool emulate_rev64(archx::InstructionIS* instr)
    {
        const auto itext = instr->opcode();
        const ArchState& input_state = instr->state_ref(); // input
        const auto rd = adis::fldfrom_REV_64_dp_1src(itext, adis::fields_t::Rd);
        if (rd != 31) {
            const auto rn = adis::fldfrom_REV_64_dp_1src(itext, adis::fields_t::Rn);
            const std::uint64_t regval_src = rn == 31 ? 0 : input_state.read(archx::cvt_gpr(rn));
            const std::uint64_t result = byte_rev64(regval_src);
            instr->write_gpr64(archx::cvt_gpr(rd), result);
        }
        return true;
    }
    static bool emulate_rev32(archx::InstructionIS* instr)
    {
        const auto itext = instr->opcode();
        const ArchState& input_state = instr->state_ref(); // input
        const auto rd = adis::fldfrom_REV_32_dp_1src(itext, adis::fields_t::Rd);
        if (rd != 31) {
            const auto rn = adis::fldfrom_REV_32_dp_1src(itext, adis::fields_t::Rn);
            const std::uint32_t regval_src = rn == 31 ? 0 : input_state.read(archx::cvt_gpr(rn));
            const std::uint32_t result = byte_rev32(regval_src);
            instr->write_gpr32(archx::cvt_gpr(rd), result);
        }
        return true;
    }

    static std::uint64_t bit_rev64(std::uint64_t src)
    {
        const std::uint32_t upper32 = src >> 32ULL;
        const std::uint32_t lower32 = src;
        const std::uint64_t upper_rev = bit_rev32(upper32);
        const std::uint64_t lower_rev = bit_rev32(lower32);
        const std::uint64_t result = (lower_rev << 32) | upper_rev;
        return result;
    }
    static std::uint32_t bit_rev32(std::uint32_t src)
    {
        auto a = src;
        a = ((a & 0x5555'5555) << 1) | ((a & 0xAAAA'AAAA) >> 1);
        a = ((a & 0x3333'3333) << 2) | ((a & 0xCCCC'CCCC) >> 2);
        a = ((a & 0x0F0F'0F0F) << 4) | ((a & 0xF0F0'F0F0) >> 4);
        a = ((a & 0x00FF'00FF) << 8) | ((a & 0xFF00'FF00) >> 8);
        a = ((a & 0x0000'FFFF) << 16) | ((a & 0xFFFF'0000) >> 16);
        return a;
    }
    static bool emulate_rbit64(archx::InstructionIS* instr)
    {
        const auto itext = instr->opcode();
        const ArchState& input_state = instr->state_ref(); // input
        const auto rd = adis::fldfrom_RBIT_64_dp_1src(itext, adis::fields_t::Rd);
        if (rd != 31) {
            const auto rn = adis::fldfrom_RBIT_64_dp_1src(itext, adis::fields_t::Rn);
            const std::uint64_t regval_src = rn == 31 ? 0 : input_state.read(archx::cvt_gpr(rn));
            const std::uint64_t result = bit_rev64(regval_src);
            instr->write_gpr64(archx::cvt_gpr(rd), result);
        }
        return true;
    }
    static bool emulate_rbit32(archx::InstructionIS* instr)
    {
        const auto itext = instr->opcode();
        const ArchState& input_state = instr->state_ref(); // input
        const auto rd = adis::fldfrom_RBIT_32_dp_1src(itext, adis::fields_t::Rd);
        if (rd != 31) {
            const auto rn = adis::fldfrom_RBIT_32_dp_1src(itext, adis::fields_t::Rn);
            const std::uint32_t regval_src = rn == 31 ? 0 : input_state.read(archx::cvt_gpr(rn));
            const std::uint32_t result = bit_rev32(regval_src);
            instr->write_gpr32(archx::cvt_gpr(rd), result);
        }
        return true;
    }

    static bool emulate_asrv32(archx::InstructionIS* instr)
    {
        const auto itext = instr->opcode();
        const ArchState& input_state = instr->state_ref(); // input
        const auto rd = adis::fldfrom_ASRV_32_dp_2src(itext, adis::fields_t::Rd);
        if (rd != 31) {
            const auto rn = adis::fldfrom_ASRV_32_dp_2src(itext, adis::fields_t::Rn);
            const auto rm = adis::fldfrom_ASRV_32_dp_2src(itext, adis::fields_t::Rm);
            const std::int32_t nval = rn == 31 ? 0 : input_state.read(archx::cvt_gpr(rn));
            const std::uint32_t mval = rm == 31 ? 0 : input_state.read(archx::cvt_gpr(rm));
            const std::int32_t result = nval >> (mval & 0x1F);
            instr->write_gpr64(archx::cvt_gpr(rd), result);
        }
        return true;
    }
    static bool emulate_asrv64(archx::InstructionIS* instr)
    {
        const auto itext = instr->opcode();
        const ArchState& input_state = instr->state_ref(); // input
        const auto rd = adis::fldfrom_ASRV_64_dp_2src(itext, adis::fields_t::Rd);
        if (rd != 31) {
            const auto rn = adis::fldfrom_ASRV_64_dp_2src(itext, adis::fields_t::Rn);
            const auto rm = adis::fldfrom_ASRV_64_dp_2src(itext, adis::fields_t::Rm);
            const std::int64_t nval = rn == 31 ? 0 : input_state.read(archx::cvt_gpr(rn));
            const std::uint32_t mval = rm == 31 ? 0 : input_state.read(archx::cvt_gpr(rm));
            const std::int64_t result = nval >> (mval & 0x3F);
            instr->write_gpr64(archx::cvt_gpr(rd), result);
        }
        return true;
    }
    static bool emulate_clz64(archx::InstructionIS* instr)
    {
        const auto itext = instr->opcode();
        const ArchState& input_state = instr->state_ref(); // input
        const auto rd = adis::fldfrom_CLZ_64_dp_1src(itext, adis::fields_t::Rd);
        if (rd != 31) {
            const auto rn = adis::fldfrom_CLZ_64_dp_1src(itext, adis::fields_t::Rn);
            const std::uint64_t regval_src = rn == 31 ? 0 : input_state.read(archx::cvt_gpr(rn));
            const std::uint64_t result = portable_clz64(regval_src);
            instr->write_gpr64(archx::cvt_gpr(rd), result);
        }
        return true;
    }
    static bool emulate_clz32(archx::InstructionIS* instr)
    {
        const auto itext = instr->opcode();
        const ArchState& input_state = instr->state_ref(); // input
        const auto rd = adis::fldfrom_CLZ_32_dp_1src(itext, adis::fields_t::Rd);
        if (rd != 31) {
            const auto rn = adis::fldfrom_CLZ_32_dp_1src(itext, adis::fields_t::Rn);
            const std::uint32_t regval_src = rn == 31 ? 0 : input_state.read(archx::cvt_gpr(rn));
            const std::uint32_t result = portable_clz32(regval_src);
            instr->write_gpr32(archx::cvt_gpr(rd), result);
        }
        return true;
    }
    static bool emulate_madd32(archx::InstructionIS* instr)
    {
        const auto itext = instr->opcode();
        const ArchState& input_state = instr->state_ref(); // input
        const auto rd = adis::fldfrom_MADD_32A_dp_3src(itext, adis::fields_t::Rd);
        if (rd != 31) {
            const auto rn = adis::fldfrom_MADD_32A_dp_3src(itext, adis::fields_t::Rn);
            const auto rm = adis::fldfrom_MADD_32A_dp_3src(itext, adis::fields_t::Rm);
            const auto ra = adis::fldfrom_MADD_32A_dp_3src(itext, adis::fields_t::Ra);
            const std::uint32_t nval = (rn == 31) ? 0 : input_state.read(archx::cvt_gpr(rn));
            const std::uint32_t mval = (rm == 31) ? 0 : input_state.read(archx::cvt_gpr(rm));
            const std::uint32_t aval = (ra == 31) ? 0 : input_state.read(archx::cvt_gpr(ra));
            const std::uint32_t result = aval + (mval * nval);
            instr->write_gpr32(archx::cvt_gpr(rd), result);
        }
        return true;
    }

    static bool emulate_madd64(archx::InstructionIS* instr)
    {
        const auto itext = instr->opcode();
        const ArchState& input_state = instr->state_ref(); // input
        const auto rd = adis::fldfrom_MADD_64A_dp_3src(itext, adis::fields_t::Rd);
        if (rd != 31) {
            const auto rn = adis::fldfrom_MADD_64A_dp_3src(itext, adis::fields_t::Rn);
            const auto rm = adis::fldfrom_MADD_64A_dp_3src(itext, adis::fields_t::Rm);
            const auto ra = adis::fldfrom_MADD_64A_dp_3src(itext, adis::fields_t::Ra);
            const std::uint64_t nval = (rn == 31) ? 0 : input_state.read(archx::cvt_gpr(rn));
            const std::uint64_t mval = (rm == 31) ? 0 : input_state.read(archx::cvt_gpr(rm));
            const std::uint64_t aval = (ra == 31) ? 0 : input_state.read(archx::cvt_gpr(ra));
            const std::uint64_t result = aval + (mval * nval);
            instr->write_gpr64(archx::cvt_gpr(rd), result);
        }
        return true;
    }

    static bool emulate_msub32(archx::InstructionIS* instr)
    {
        const auto itext = instr->opcode();
        const ArchState& input_state = instr->state_ref(); // input
        const auto rd = adis::fldfrom_MSUB_32A_dp_3src(itext, adis::fields_t::Rd);
        if (rd != 31) {
            const auto rn = adis::fldfrom_MSUB_32A_dp_3src(itext, adis::fields_t::Rn);
            const auto rm = adis::fldfrom_MSUB_32A_dp_3src(itext, adis::fields_t::Rm);
            const auto ra = adis::fldfrom_MSUB_32A_dp_3src(itext, adis::fields_t::Ra);
            const std::uint32_t nval = (rn == 31) ? 0 : input_state.read(archx::cvt_gpr(rn));
            const std::uint32_t mval = (rm == 31) ? 0 : input_state.read(archx::cvt_gpr(rm));
            const std::uint32_t aval = (ra == 31) ? 0 : input_state.read(archx::cvt_gpr(ra));
            const std::uint32_t result = aval - (mval * nval);
            instr->write_gpr32(archx::cvt_gpr(rd), result);
        }
        return true;
    }

    static bool emulate_msub64(archx::InstructionIS* instr)
    {
        const auto itext = instr->opcode();
        const ArchState& input_state = instr->state_ref(); // input
        const auto rd = adis::fldfrom_MSUB_64A_dp_3src(itext, adis::fields_t::Rd);
        if (rd != 31) {
            const auto rn = adis::fldfrom_MSUB_64A_dp_3src(itext, adis::fields_t::Rn);
            const auto rm = adis::fldfrom_MSUB_64A_dp_3src(itext, adis::fields_t::Rm);
            const auto ra = adis::fldfrom_MSUB_64A_dp_3src(itext, adis::fields_t::Ra);
            const std::uint64_t nval = (rn == 31) ? 0 : input_state.read(archx::cvt_gpr(rn));
            const std::uint64_t mval = (rm == 31) ? 0 : input_state.read(archx::cvt_gpr(rm));
            const std::uint64_t aval = (ra == 31) ? 0 : input_state.read(archx::cvt_gpr(ra));
            const std::uint64_t result = aval - (mval * nval);
            instr->write_gpr64(archx::cvt_gpr(rd), result);
        }
        return true;
    }

    static bool emulate_udiv32(archx::InstructionIS* instr)
    {
        const auto itext = instr->opcode();
        const ArchState& input_state = instr->state_ref(); // input
        const auto rd = adis::fldfrom_UDIV_32_dp_2src(itext, adis::fields_t::Rd);
        if (rd != 31) {
            const auto rn = adis::fldfrom_UDIV_32_dp_2src(itext, adis::fields_t::Rn);
            const auto rm = adis::fldfrom_UDIV_32_dp_2src(itext, adis::fields_t::Rm);
            const std::uint32_t nval = (rn == 31) ? 0 : input_state.read(archx::cvt_gpr(rn));
            const std::uint32_t mval = (rm == 31) ? 0 : input_state.read(archx::cvt_gpr(rm));
            std::uint32_t result = 0;
            if (mval) {
                result = nval / mval;
            }
            instr->write_gpr32(archx::cvt_gpr(rd), result);
        }
        return true;
    }

    static bool emulate_udiv64(archx::InstructionIS* instr)
    {
        const auto itext = instr->opcode();
        const ArchState& input_state = instr->state_ref(); // input
        const auto rd = adis::fldfrom_UDIV_64_dp_2src(itext, adis::fields_t::Rd);
        if (rd != 31) {
            const auto rn = adis::fldfrom_UDIV_64_dp_2src(itext, adis::fields_t::Rn);
            const auto rm = adis::fldfrom_UDIV_64_dp_2src(itext, adis::fields_t::Rm);
            const std::uint64_t nval = (rn == 31) ? 0 : input_state.read(archx::cvt_gpr(rn));
            const std::uint64_t mval = (rm == 31) ? 0 : input_state.read(archx::cvt_gpr(rm));
            std::uint64_t result = 0;
            if (mval) {
                result = nval / mval;
            }
            instr->write_gpr64(archx::cvt_gpr(rd), result);
        }
        return true;
    }

    static bool emulate_sdiv32(archx::InstructionIS* instr)
    {
        const auto itext = instr->opcode();
        const ArchState& input_state = instr->state_ref(); // input
        const auto rd = adis::fldfrom_SDIV_32_dp_2src(itext, adis::fields_t::Rd);
        if (rd != 31) {
            const auto rn = adis::fldfrom_SDIV_32_dp_2src(itext, adis::fields_t::Rn);
            const auto rm = adis::fldfrom_SDIV_32_dp_2src(itext, adis::fields_t::Rm);
            const std::int32_t nval = (rn == 31) ? 0 : input_state.read(archx::cvt_gpr(rn));
            const std::int32_t mval = (rm == 31) ? 0 : input_state.read(archx::cvt_gpr(rm));
            std::int32_t result = 0;
            if (mval) {
                result = nval / mval;
            }
            instr->write_gpr32(archx::cvt_gpr(rd), result);
        }
        return true;
    }

    static bool emulate_sdiv64(archx::InstructionIS* instr)
    {
        const auto itext = instr->opcode();
        const ArchState& input_state = instr->state_ref(); // input
        const auto rd = adis::fldfrom_SDIV_64_dp_2src(itext, adis::fields_t::Rd);
        if (rd != 31) {
            const auto rn = adis::fldfrom_SDIV_64_dp_2src(itext, adis::fields_t::Rn);
            const auto rm = adis::fldfrom_SDIV_64_dp_2src(itext, adis::fields_t::Rm);
            const std::int64_t nval = (rn == 31) ? 0 : input_state.read(archx::cvt_gpr(rn));
            const std::int64_t mval = (rm == 31) ? 0 : input_state.read(archx::cvt_gpr(rm));
            std::int64_t result = 0;
            if (mval) {
                result = nval / mval;
            }
            instr->write_gpr64(archx::cvt_gpr(rd), result);
        }
        return true;
    }

    static bool emulate_umaddl(archx::InstructionIS* instr)
    {
        const auto itext = instr->opcode();
        const ArchState& input_state = instr->state_ref(); // input
        const auto rd = adis::fldfrom_UMADDL_64WA_dp_3src(itext, adis::fields_t::Rd);
        if (rd != 31) {
            const auto rn = adis::fldfrom_UMADDL_64WA_dp_3src(itext, adis::fields_t::Rn);
            const auto rm = adis::fldfrom_UMADDL_64WA_dp_3src(itext, adis::fields_t::Rm);
            const auto ra = adis::fldfrom_UMADDL_64WA_dp_3src(itext, adis::fields_t::Ra);
            const std::uint32_t nval = (rn == 31) ? 0 : input_state.read(archx::cvt_gpr(rn));
            const std::uint32_t mval = (rm == 31) ? 0 : input_state.read(archx::cvt_gpr(rm));
            const std::uint64_t aval = (ra == 31) ? 0 : input_state.read(archx::cvt_gpr(ra));
            const std::uint64_t nval64 = static_cast<std::uint64_t>(nval);
            const std::uint64_t mval64 = static_cast<std::uint64_t>(mval);
            const std::uint64_t result = aval + (mval64 * nval64);
            instr->write_gpr64(archx::cvt_gpr(rd), result);
        }
        return true;
    }
    static bool emulate_smaddl(archx::InstructionIS* instr)
    {
        const auto itext = instr->opcode();
        const ArchState& input_state = instr->state_ref(); // input
        const auto rd = adis::fldfrom_SMADDL_64WA_dp_3src(itext, adis::fields_t::Rd);
        if (rd != 31) {
            const auto rn = adis::fldfrom_SMADDL_64WA_dp_3src(itext, adis::fields_t::Rn);
            const auto rm = adis::fldfrom_SMADDL_64WA_dp_3src(itext, adis::fields_t::Rm);
            const auto ra = adis::fldfrom_SMADDL_64WA_dp_3src(itext, adis::fields_t::Ra);
            const std::int32_t nval = (rn == 31) ? 0 : input_state.read(archx::cvt_gpr(rn));
            const std::int32_t mval = (rm == 31) ? 0 : input_state.read(archx::cvt_gpr(rm));
            const std::int64_t aval = (ra == 31) ? 0 : input_state.read(archx::cvt_gpr(ra));
            const std::int64_t nval64 = static_cast<std::int64_t>(nval);
            const std::int64_t mval64 = static_cast<std::int64_t>(mval);
            const std::int64_t result = aval + (mval64 * nval64);
            instr->write_gpr64(archx::cvt_gpr(rd), result);
        }
        return true;
    }
    static bool emulate_umsubl(archx::InstructionIS* instr)
    {
        const auto itext = instr->opcode();
        const ArchState& input_state = instr->state_ref(); // input
        const auto rd = adis::fldfrom_UMSUBL_64WA_dp_3src(itext, adis::fields_t::Rd);
        if (rd != 31) {
            const auto rn = adis::fldfrom_UMSUBL_64WA_dp_3src(itext, adis::fields_t::Rn);
            const auto rm = adis::fldfrom_UMSUBL_64WA_dp_3src(itext, adis::fields_t::Rm);
            const auto ra = adis::fldfrom_UMSUBL_64WA_dp_3src(itext, adis::fields_t::Ra);
            const std::uint32_t nval = (rn == 31) ? 0 : input_state.read(archx::cvt_gpr(rn));
            const std::uint32_t mval = (rm == 31) ? 0 : input_state.read(archx::cvt_gpr(rm));
            const std::uint64_t aval = (ra == 31) ? 0 : input_state.read(archx::cvt_gpr(ra));
            const std::uint64_t nval64 = static_cast<std::uint64_t>(nval);
            const std::uint64_t mval64 = static_cast<std::uint64_t>(mval);
            const std::uint64_t result = aval - (mval64 * nval64);
            instr->write_gpr64(archx::cvt_gpr(rd), result);
        }
        return true;
    }
    static bool emulate_smsubl(archx::InstructionIS* instr)
    {
        const auto itext = instr->opcode();
        const ArchState& input_state = instr->state_ref(); // input
        const auto rd = adis::fldfrom_SMSUBL_64WA_dp_3src(itext, adis::fields_t::Rd);
        if (rd != 31) {
            const auto rn = adis::fldfrom_SMSUBL_64WA_dp_3src(itext, adis::fields_t::Rn);
            const auto rm = adis::fldfrom_SMSUBL_64WA_dp_3src(itext, adis::fields_t::Rm);
            const auto ra = adis::fldfrom_SMSUBL_64WA_dp_3src(itext, adis::fields_t::Ra);
            const std::int32_t nval = (rn == 31) ? 0 : input_state.read(archx::cvt_gpr(rn));
            const std::int32_t mval = (rm == 31) ? 0 : input_state.read(archx::cvt_gpr(rm));
            const std::int64_t aval = (ra == 31) ? 0 : input_state.read(archx::cvt_gpr(ra));
            const std::int64_t nval64 = static_cast<std::int64_t>(nval);
            const std::int64_t mval64 = static_cast<std::int64_t>(mval);
            const std::int64_t result = aval - (mval64 * nval64);
            instr->write_gpr64(archx::cvt_gpr(rd), result);
        }
        return true;
    }

    static bool emulate_stlxrb(archx::InstructionIS* instr)
    {
        const auto itext = instr->opcode();
        const auto rn = adis::fldfrom_STLXRB_SR32_ldstexclr(itext, adis::fields_t::Rn); // may be SP
        const ArchState& input_state = instr->state_ref();                              // input
        const auto regvaln = input_state.read(archx::cvt_gpr(rn, archx::LReg::SP));
        const VirtAddr va(regvaln);
        instr->add_emulated_virt_addr(va);
        const bool failing = instr->wrong_path(); // always fail on the wrong path, succeed on correct path
        if (!failing) {
            const auto rt = adis::fldfrom_STLXRB_SR32_ldstexclr(itext, adis::fields_t::Rt);
            const std::uint8_t regvalt = (rt == 31) ? 0 : input_state.read(archx::cvt_gpr(rt));
            mem_write_8bit(instr, va, regvalt);
        }
        const auto rs = adis::fldfrom_STLXRB_SR32_ldstexclr(itext, adis::fields_t::Rs);
        if (rs != 31) {
            instr->write_gpr64(archx::cvt_gpr(rs), (failing ? 0 : 1));
        }
        return true;
    }

    static bool emulate_stlxr32(archx::InstructionIS* instr)
    {
        const auto itext = instr->opcode();
        const auto rn = adis::fldfrom_STLXR_SR32_ldstexclr(itext, adis::fields_t::Rn); // may be SP
        const ArchState& input_state = instr->state_ref();                             // input
        const auto regvaln = input_state.read(archx::cvt_gpr(rn, archx::LReg::SP));
        const VirtAddr va(regvaln);
        instr->add_emulated_virt_addr(va);
        const bool failing = instr->wrong_path(); // always fail on the wrong path, succeed on correct path
        if (!failing) {
            const auto rt = adis::fldfrom_STLXR_SR32_ldstexclr(itext, adis::fields_t::Rt);
            const std::uint32_t regvalt = rt == 31 ? 0 : input_state.read(archx::cvt_gpr(rt));
            mem_write_32bit(instr, va, regvalt);
        }
        const auto rs = adis::fldfrom_STLXR_SR32_ldstexclr(itext, adis::fields_t::Rs);
        if (rs != 31) {
            instr->write_gpr64(archx::cvt_gpr(rs), (failing ? 0 : 1));
        }
        return true;
    }

    static bool emulate_stlxr64(archx::InstructionIS* instr)
    {
        const auto itext = instr->opcode();
        const auto rn = adis::fldfrom_STLXR_SR64_ldstexclr(itext, adis::fields_t::Rn); // may be SP
        const ArchState& input_state = instr->state_ref();                             // input
        const auto regvaln = input_state.read(archx::cvt_gpr(rn, archx::LReg::SP));
        const VirtAddr va(regvaln);
        instr->add_emulated_virt_addr(va);
        const bool failing = instr->wrong_path(); // always fail on the wrong path, succeed on correct path
        if (!failing) {
            const auto rt = adis::fldfrom_STLXR_SR64_ldstexclr(itext, adis::fields_t::Rt);
            const std::uint64_t regvalt = rt == 31 ? 0 : input_state.read(archx::cvt_gpr(rt));
            mem_write_64bit(instr, va, regvalt);
        }
        const auto rs = adis::fldfrom_STLXR_SR64_ldstexclr(itext, adis::fields_t::Rs);
        if (rs != 31) {
            instr->write_gpr64(archx::cvt_gpr(rs), (failing ? 0 : 1));
        }
        return true;
    }

    static bool emulate_ldxrb(archx::InstructionIS* instr)
    {
        const auto itext = instr->opcode();
        const auto rn = adis::fldfrom_LDXRB_LR32_ldstexclr(itext, adis::fields_t::Rn); // may be SP
        const auto rt = adis::fldfrom_LDXRB_LR32_ldstexclr(itext, adis::fields_t::Rt);
        const ArchState& input_state = instr->state_ref(); // input
        const auto regvaln = input_state.read(archx::cvt_gpr(rn, archx::LReg::SP));
        const VirtAddr va(regvaln);
        instr->add_emulated_virt_addr(va);
        if (rt != 31) {
            const auto mval = mem_read(instr, va, 1);
            if (mval.has_value()) {
                const std::uint8_t bval = mval.value();
                instr->write_gpr32(archx::cvt_gpr(rt), bval);
            }
        }
        return true;
    }

    static bool emulate_ldxr32(archx::InstructionIS* instr)
    {
        const auto itext = instr->opcode();
        const auto rn = adis::fldfrom_LDXR_LR32_ldstexclr(itext, adis::fields_t::Rn); // may be SP
        const auto rt = adis::fldfrom_LDXR_LR32_ldstexclr(itext, adis::fields_t::Rt);
        const ArchState& input_state = instr->state_ref(); // input
        const auto regvaln = input_state.read(archx::cvt_gpr(rn, archx::LReg::SP));
        const VirtAddr va(regvaln);
        instr->add_emulated_virt_addr(va);
        if (rt != 31) {
            const auto mval = mem_read(instr, va, 4);
            if (mval.has_value()) {
                const std::uint32_t wval = mval.value();
                instr->write_gpr32(archx::cvt_gpr(rt), wval);
            }
        }
        return true;
    }
    static bool emulate_ldxr64(archx::InstructionIS* instr)
    {
        const auto itext = instr->opcode();
        const auto rn = adis::fldfrom_LDXR_LR64_ldstexclr(itext, adis::fields_t::Rn); // may be SP
        const auto rt = adis::fldfrom_LDXR_LR64_ldstexclr(itext, adis::fields_t::Rt);
        const ArchState& input_state = instr->state_ref(); // input
        const auto regvaln = input_state.read(archx::cvt_gpr(rn, archx::LReg::SP));
        const VirtAddr va(regvaln);
        instr->add_emulated_virt_addr(va);
        if (rt != 31) {
            const auto mval = mem_read(instr, va, 8);
            if (mval.has_value()) {
                instr->write_gpr32(archx::cvt_gpr(rt), mval.value());
            }
        }
        return true;
    }

    static bool emulate_prfm_reg(archx::InstructionIS* instr)
    {
        const auto itext = instr->opcode();
        const auto rn = adis::fldfrom_PRFM_P_ldst_regoff(itext, adis::fields_t::Rn); // could be SP
        const auto rm = adis::fldfrom_PRFM_P_ldst_regoff(itext, adis::fields_t::Rm);
        const auto option = adis::fldfrom_PRFM_P_ldst_regoff(itext, adis::fields_t::option);
        const auto shift = adis::fldfrom_PRFM_P_ldst_regoff(itext, adis::fields_t::S);
        const ArchState& input_state = instr->state_ref(); // input
        const auto addr_opt = ld_regoff_common(input_state, rn, rm, option, shift, 3);
        if (addr_opt.has_value()) {
            instr->add_emulated_virt_addr(VirtAddr(addr_opt.value()));
        }
        return true;
    }
    static bool emulate_prfm_lit(archx::InstructionIS* instr)
    {
        const ProgramCounter pc = instr->program_counter();
        const auto itext = instr->opcode();
        const std::uint32_t imm19 = adis::fldfrom_PRFM_P_loadlit(itext, adis::fields_t::imm19);
        const VirtAddr va(pc.value() + sign_extend(imm19, 19) * 4);
        instr->add_emulated_virt_addr(va);
        return true;
    }
    static bool emulate_prfm_pos(archx::InstructionIS* instr)
    {
        const auto itext = instr->opcode();
        const std::uint64_t imm12 = adis::fldfrom_PRFM_P_ldst_pos(itext, adis::fields_t::imm12);
        const auto rn = adis::fldfrom_PRFM_P_ldst_pos(itext, adis::fields_t::Rn);
        const ArchState& input_state = instr->state_ref(); // input
        const auto regvaln = input_state.read(archx::cvt_gpr(rn, archx::LReg::SP));
        const VirtAddr va(regvaln + imm12 * 8);
        instr->add_emulated_virt_addr(va);
        return true;
    }
    static bool emulate_prfum(archx::InstructionIS* instr)
    {
        const auto itext = instr->opcode();
        const ArchState& input_state = instr->state_ref(); // input
        const auto rn = adis::fldfrom_PRFUM_P_ldst_unscaled(itext, adis::fields_t::Rn);
        const std::uint64_t imm9 = adis::fldfrom_PRFUM_P_ldst_unscaled(itext, adis::fields_t::imm9);
        const auto regvaln = input_state.read(archx::cvt_gpr(rn, archx::LReg::SP));
        const VirtAddr va(regvaln + imm9);
        instr->add_emulated_virt_addr(va);
        return true;
    }

public:
    // static member functions pointers are just normal function pointers
    using MemFuncPtr = bool (*)(archx::InstructionIS* instr);

private:
    // set up the associations, then copy to the emu_fnptrs array.
    // requires common fn signature, so would have to factor secondary enum & bool args)
    static const std::map<adis::encoding_enum_t, MemFuncPtr> fmap;
    static MemFuncPtr emu_fnptrs[adis::ENC_LAST];

    static void initialize_emu_fnptrs()
    {
        for (auto& ptr : emu_fnptrs) {
            ptr = nullptr;
        }
        for (auto const& [key, fn] : fmap) {
            emu_fnptrs[key] = fn;
        }
    }

public:
    static bool emulate(archx::InstructionIS* instr)
    {
        // return true if emulated (usual case), false otherwise
        const auto adis_info = instr->adis_info();
        if (emu_fnptrs[adis_info]) {
            return (*emu_fnptrs[adis_info])(instr);
        }
        switch (adis_info) { // SECONDARY DISPATCH (for different emulation function signatures)
        case adis::ENC_ADDS_64S_addsub_imm:
            return emulate_add_64_addsub_imm(instr, true /* set flags */);
        case adis::ENC_ADD_64_addsub_imm:
            return emulate_add_64_addsub_imm(instr, false /* do not set flags */);
        case adis::ENC_EOR_32_log_imm:
            return emulate_32b_log_imm(instr, logical_enum_t::EOR);
        case adis::ENC_EOR_64_log_imm:
            return emulate_64b_log_imm(instr, logical_enum_t::EOR);
        case adis::ENC_ORR_32_log_imm:
            return emulate_32b_log_imm(instr, logical_enum_t::ORR);
        case adis::ENC_ORR_64_log_imm:
            return emulate_64b_log_imm(instr, logical_enum_t::ORR);
        case adis::ENC_AND_32_log_imm:
            return emulate_32b_log_imm(instr, logical_enum_t::AND);
        case adis::ENC_AND_64_log_imm:
            return emulate_64b_log_imm(instr, logical_enum_t::AND);
        case adis::ENC_ANDS_32S_log_imm:
            return emulate_32b_log_imm(instr, logical_enum_t::AND, true /* set flags*/);
        case adis::ENC_ANDS_64S_log_imm:
            return emulate_64b_log_imm(instr, logical_enum_t::AND, true /*set flags*/);
        case adis::ENC_ANDS_64_log_shift:
            return emulate_64b_logical_log_shift(instr, logical_enum_t::AND, true /*set flags*/);
        case adis::ENC_BICS_64_log_shift:
            return emulate_64b_logical_log_shift(instr, logical_enum_t::BIC, true /*set flags*/);
        case adis::ENC_ANDS_32_log_shift:
            return emulate_32b_logical_log_shift(instr, logical_enum_t::AND, true /*set flags*/);
        case adis::ENC_BICS_32_log_shift:
            return emulate_32b_logical_log_shift(instr, logical_enum_t::BIC, true /*set flags*/);
        case adis::ENC_BIC_64_log_shift:
            return emulate_64b_logical_log_shift(instr, logical_enum_t::BIC);
        case adis::ENC_ORN_64_log_shift:
            return emulate_64b_logical_log_shift(instr, logical_enum_t::ORN);
        case adis::ENC_ORR_64_log_shift:
            return emulate_64b_logical_log_shift(instr, logical_enum_t::ORR);
        case adis::ENC_EOR_64_log_shift:
            return emulate_64b_logical_log_shift(instr, logical_enum_t::EOR);
        case adis::ENC_AND_64_log_shift:
            return emulate_64b_logical_log_shift(instr, logical_enum_t::AND);
        case adis::ENC_BIC_32_log_shift:
            return emulate_32b_logical_log_shift(instr, logical_enum_t::BIC);
        case adis::ENC_ORN_32_log_shift:
            return emulate_32b_logical_log_shift(instr, logical_enum_t::ORN);
        case adis::ENC_ORR_32_log_shift:
            return emulate_32b_logical_log_shift(instr, logical_enum_t::ORR);
        case adis::ENC_EOR_32_log_shift:
            return emulate_32b_logical_log_shift(instr, logical_enum_t::EOR);
        case adis::ENC_AND_32_log_shift:
            return emulate_32b_logical_log_shift(instr, logical_enum_t::AND);
        case adis::ENC_SUB_64_addsub_imm:
            return emulate_sub_64_addsub_imm(instr, false /* do not set flags */);
        case adis::ENC_SUBS_64S_addsub_imm:
            return emulate_sub_64_addsub_imm(instr, true /* set flags */);

        case adis::ENC_SUB_32_addsub_imm:
            return emulate_sub_32_addsub_imm(instr, false /* do not set flags */);
        case adis::ENC_SUBS_32S_addsub_imm:
            return emulate_sub_32_addsub_imm(instr, true /* set flags */);

        case adis::ENC_SUB_32_addsub_shift:
            return emulate_sub_32_addsub_shift(instr, false /* do not set flags */);
        case adis::ENC_SUBS_32_addsub_shift:
            return emulate_sub_32_addsub_shift(instr, true /* set flags */);

        case adis::ENC_SUB_64_addsub_shift:
            return emulate_sub_64_addsub_shift(instr, false /* do not set flags */);
        case adis::ENC_SUBS_64_addsub_shift:
            return emulate_sub_64_addsub_shift(instr, true /* set flags */);

        case adis::ENC_STP_32_ldstpair_off:
            return emulate_stp_32(instr);
        case adis::ENC_STP_32_ldstpair_pre:
            return emulate_stp_32(instr, addressing_mode_t::PRE_INCREMENT);
        case adis::ENC_STP_32_ldstpair_post:
            return emulate_stp_32(instr, addressing_mode_t::POST_INCREMENT);

        case adis::ENC_STP_64_ldstpair_off:
            return emulate_stp_64(instr);
        case adis::ENC_STP_64_ldstpair_pre:
            return emulate_stp_64(instr, addressing_mode_t::PRE_INCREMENT);
        case adis::ENC_STP_64_ldstpair_post:
            return emulate_stp_64(instr, addressing_mode_t::POST_INCREMENT);

        case adis::ENC_LDP_64_ldstpair_off:
            return emulate_ldp_64(instr);
        case adis::ENC_LDP_64_ldstpair_pre:
            return emulate_ldp_64(instr, addressing_mode_t::PRE_INCREMENT);
        case adis::ENC_LDP_64_ldstpair_post:
            return emulate_ldp_64(instr, addressing_mode_t::POST_INCREMENT);

        case adis::ENC_LDP_32_ldstpair_off:
            return emulate_ldp_32(instr);
        case adis::ENC_LDP_32_ldstpair_pre:
            return emulate_ldp_32(instr, addressing_mode_t::PRE_INCREMENT);
        case adis::ENC_LDP_32_ldstpair_post:
            return emulate_ldp_32(instr, addressing_mode_t::POST_INCREMENT);

        case adis::ENC_LDP_D_ldstpair_off:
            return emulate_ldp_d(instr);
        case adis::ENC_LDP_D_ldstpair_pre:
            return emulate_ldp_d(instr, addressing_mode_t::PRE_INCREMENT);
        case adis::ENC_LDP_D_ldstpair_post:
            return emulate_ldp_d(instr, addressing_mode_t::POST_INCREMENT);

        case adis::ENC_MOVZ_32_movewide:
            return emulate_movz(instr, true /* 32b */);
        case adis::ENC_MOVZ_64_movewide:
            return emulate_movz(instr, false /* 64b */);

        case adis::ENC_MOVK_32_movewide:
            return emulate_movk(instr, true /* 32b */);
        case adis::ENC_MOVK_64_movewide:
            return emulate_movk(instr, false /* 64b */);

        case adis::ENC_MOVN_32_movewide:
            return emulate_movn(instr, true /* 32b */);
        case adis::ENC_MOVN_64_movewide:
            return emulate_movn(instr, false /* 64b */);

        case adis::ENC_ADD_64_addsub_ext:
            return emulate_add_64_extended(instr);
        case adis::ENC_ADDS_64S_addsub_ext:
            return emulate_add_64_extended(instr, true /* sets flags */);
        case adis::ENC_ADD_32_addsub_ext:
            return emulate_add_32_extended(instr);
        case adis::ENC_ADDS_32S_addsub_ext:
            return emulate_add_32_extended(instr, true /* sets flags */);
        case adis::ENC_LDRB_32B_ldst_regoff:
            return emulate_ldrb32_reg(instr, false /* not shifted version */);
        case adis::ENC_LDRB_32BL_ldst_regoff:
            return emulate_ldrb32_reg(instr, true /* shift version, option=3 */);

        default:
            return false;
        }
        return false;
    }
}; // class Aarch64Emulate;

Aarch64Emulate::MemFuncPtr Aarch64Emulate::emu_fnptrs[adis::ENC_LAST];
const std::map<adis::encoding_enum_t, Aarch64Emulate::MemFuncPtr> Aarch64Emulate::fmap = {
    //{ adis::ENC_LDRSB_32_ldst_immpost, Aarch64Emulate::emulate_ldrsb_32_post },
    //{ adis::ENC_LDRSB_32_ldst_immpre,  Aarch64Emulate::emulate_ldrsb_32_pre },
    //{ adis::ENC_LDRSH_32_ldst_immpost, Aarch64Emulate::emulate_ldrsh_32_post },
    //{ adis::ENC_LDRSH_32_ldst_immpre,  Aarch64Emulate::emulate_ldrsh_32_pre },
    //{ adis::ENC_LDRSH_64_ldst_immpost, Aarch64Emulate::emulate_ldrsh_64_post },
    //{ adis::ENC_LDRSH_64_ldst_immpre,  Aarch64Emulate::emulate_ldrsh_64_pre },
    {adis::ENC_ADDS_32S_addsub_imm, Aarch64Emulate::emulate_adds_32_addsub_imm},
    {adis::ENC_ADDS_32_addsub_shift, Aarch64Emulate::emulate_adds_32_addsub_shift},
    {adis::ENC_ADDS_64_addsub_shift, Aarch64Emulate::emulate_adds_64_addsub_shift},
    {adis::ENC_ADD_32_addsub_imm, Aarch64Emulate::emulate_add_32_addsub_imm},
    {adis::ENC_ADD_32_addsub_shift, Aarch64Emulate::emulate_add_32_addsub_shift},
    {adis::ENC_ADD_64_addsub_shift, Aarch64Emulate::emulate_add_64_addsub_shift},
    {adis::ENC_ADR_only_pcreladdr, Aarch64Emulate::emulate_adr},
    {adis::ENC_ADRP_only_pcreladdr, Aarch64Emulate::emulate_adrp},
    {adis::ENC_ASRV_32_dp_2src, Aarch64Emulate::emulate_asrv32},
    {adis::ENC_ASRV_64_dp_2src, Aarch64Emulate::emulate_asrv64},
    {adis::ENC_BLR_64_branch_reg, Aarch64Emulate::emulate_blr},
    {adis::ENC_BL_only_branch_imm, Aarch64Emulate::emulate_bl},
    {adis::ENC_BR_64_branch_reg, Aarch64Emulate::emulate_br},
    {adis::ENC_B_only_branch_imm, Aarch64Emulate::emulate_b_uncond},
    {adis::ENC_B_only_condbranch, Aarch64Emulate::emulate_bcond},
    {adis::ENC_BFM_32M_bitfield, Aarch64Emulate::emulate_bfm32},
    {adis::ENC_BFM_64M_bitfield, Aarch64Emulate::emulate_bfm64},
    {adis::ENC_CBNZ_32_compbranch, Aarch64Emulate::emulate_cbnz32},
    {adis::ENC_CBNZ_64_compbranch, Aarch64Emulate::emulate_cbnz64},
    {adis::ENC_CBZ_32_compbranch, Aarch64Emulate::emulate_cbz32},
    {adis::ENC_CBZ_64_compbranch, Aarch64Emulate::emulate_cbz64},
    {adis::ENC_CCMN_32_condcmp_imm, Aarch64Emulate::emulate_ccmn32_imm},
    {adis::ENC_CCMN_64_condcmp_imm, Aarch64Emulate::emulate_ccmn64_imm},
    {adis::ENC_CCMP_32_condcmp_imm, Aarch64Emulate::emulate_ccmp32_imm},
    {adis::ENC_CCMP_32_condcmp_reg, Aarch64Emulate::emulate_ccmp32_reg},
    {adis::ENC_CCMP_64_condcmp_imm, Aarch64Emulate::emulate_ccmp64_imm},
    {adis::ENC_CCMP_64_condcmp_reg, Aarch64Emulate::emulate_ccmp64_reg},
    {adis::ENC_CLZ_32_dp_1src, Aarch64Emulate::emulate_clz32},
    {adis::ENC_CLZ_64_dp_1src, Aarch64Emulate::emulate_clz64},
    {adis::ENC_CSEL_32_condsel, Aarch64Emulate::emulate_csel32},
    {adis::ENC_CSEL_64_condsel, Aarch64Emulate::emulate_csel64},
    {adis::ENC_CSINC_32_condsel, Aarch64Emulate::emulate_csinc32},
    {adis::ENC_CSINC_64_condsel, Aarch64Emulate::emulate_csinc64},
    {adis::ENC_CSINV_32_condsel, Aarch64Emulate::emulate_csinv32},
    {adis::ENC_CSINV_64_condsel, Aarch64Emulate::emulate_csinv64},
    {adis::ENC_CSNEG_32_condsel, Aarch64Emulate::emulate_csneg32},
    {adis::ENC_CSNEG_64_condsel, Aarch64Emulate::emulate_csneg64},
    {adis::ENC_DMB_BO_barriers, Aarch64Emulate::emulate_pass},
    {adis::ENC_FCMPE_DZ_floatcmp, Aarch64Emulate::emulate_FCMPE_DZ_floatcmp},
    {adis::ENC_FCMPE_D_floatcmp, Aarch64Emulate::emulate_FCMPE_D_floatcmp},
    {adis::ENC_FCMPE_SZ_floatcmp, Aarch64Emulate::emulate_FCMPE_SZ_floatcmp},
    {adis::ENC_FCMPE_S_floatcmp, Aarch64Emulate::emulate_FCMPE_S_floatcmp},
    {adis::ENC_FCMP_DZ_floatcmp, Aarch64Emulate::emulate_FCMP_DZ_floatcmp},
    {adis::ENC_FCMP_D_floatcmp, Aarch64Emulate::emulate_FCMP_D_floatcmp},
    {adis::ENC_FCMP_SZ_floatcmp, Aarch64Emulate::emulate_FCMP_SZ_floatcmp},
    {adis::ENC_FCMP_S_floatcmp, Aarch64Emulate::emulate_FCMP_S_floatcmp},
    {adis::ENC_LDAR_LR64_ldstord, Aarch64Emulate::emulate_ldar64},
    {adis::ENC_LDAR_LR32_ldstord, Aarch64Emulate::emulate_ldar32},
    {adis::ENC_LDP_S_ldstpair_off, Aarch64Emulate::emulate_ldp_s},
    {adis::ENC_LDP_S_ldstpair_pre, Aarch64Emulate::emulate_ldp_s_pre},
    {adis::ENC_LDP_S_ldstpair_post, Aarch64Emulate::emulate_ldp_s_post},
    {adis::ENC_LDP_Q_ldstpair_off, Aarch64Emulate::emulate_ldp_q},
    {adis::ENC_LDP_Q_ldstpair_pre, Aarch64Emulate::emulate_ldp_q_pre},
    {adis::ENC_LDP_Q_ldstpair_post, Aarch64Emulate::emulate_ldp_q_post},
    {adis::ENC_LDPSW_64_ldstpair_off, Aarch64Emulate::emulate_ldpsw},
    {adis::ENC_LDPSW_64_ldstpair_pre, Aarch64Emulate::emulate_ldpsw_pre},
    {adis::ENC_LDPSW_64_ldstpair_post, Aarch64Emulate::emulate_ldpsw_post},

    {adis::ENC_LDRB_32_ldst_immpost, Aarch64Emulate::emulate_ldrb_post},
    {adis::ENC_LDRB_32_ldst_immpre, Aarch64Emulate::emulate_ldrb_pre},
    {adis::ENC_LDRB_32_ldst_pos, Aarch64Emulate::emulate_ldrb_pos},
    {adis::ENC_LDRH_32_ldst_pos, Aarch64Emulate::emulate_ldrh_pos},
    {adis::ENC_LDRH_32_ldst_immpre, Aarch64Emulate::emulate_ldrh_pre},
    {adis::ENC_LDRH_32_ldst_immpost, Aarch64Emulate::emulate_ldrh_post},
    {adis::ENC_LDRH_32_ldst_regoff, Aarch64Emulate::emulate_ldrh_reg},
    {adis::ENC_LDRSB_32BL_ldst_regoff, Aarch64Emulate::emulate_ldrsb_32_lsl_reg},
    {adis::ENC_LDRSB_32B_ldst_regoff, Aarch64Emulate::emulate_ldrsb_32_reg},
    {adis::ENC_LDRSB_32_ldst_pos, Aarch64Emulate::emulate_ldrsb_32_pos},
    {adis::ENC_LDRSH_32_ldst_pos, Aarch64Emulate::emulate_ldrsh_32_pos},
    {adis::ENC_LDRSH_32_ldst_regoff, Aarch64Emulate::emulate_ldrsh_32_reg},
    {adis::ENC_LDRSH_64_ldst_pos, Aarch64Emulate::emulate_ldrsh_64_pos},
    {adis::ENC_LDRSH_64_ldst_regoff, Aarch64Emulate::emulate_ldrsh_64_reg},
    {adis::ENC_LDRSW_64_ldst_immpost, Aarch64Emulate::emulate_ldrsw_64_immpost},
    {adis::ENC_LDRSW_64_ldst_pos, Aarch64Emulate::emulate_ldrsw_64_pos},
    {adis::ENC_LDRSW_64_ldst_regoff, Aarch64Emulate::emulate_ldrsw_64_reg},
    {adis::ENC_LDR_32_ldst_immpost, Aarch64Emulate::emulate_ldr_32_post},
    {adis::ENC_LDR_32_ldst_immpre, Aarch64Emulate::emulate_ldr_32_pre},
    {adis::ENC_LDR_32_ldst_pos, Aarch64Emulate::emulate_ldr_32_pos},
    {adis::ENC_LDR_32_ldst_regoff, Aarch64Emulate::emulate_ldr_32_reg},
    {adis::ENC_LDR_64_ldst_immpost, Aarch64Emulate::emulate_ldr_64_post},
    {adis::ENC_LDR_64_ldst_immpre, Aarch64Emulate::emulate_ldr_64_pre},
    {adis::ENC_LDR_64_ldst_pos, Aarch64Emulate::emulate_ldr_64_pos},
    {adis::ENC_LDR_64_ldst_regoff, Aarch64Emulate::emulate_ldr_64_reg},
    {adis::ENC_LDR_D_ldst_immpost, Aarch64Emulate::emulate_ldr_d_post},
    {adis::ENC_LDR_D_ldst_immpre, Aarch64Emulate::emulate_ldr_d_pre},
    {adis::ENC_LDR_D_ldst_pos, Aarch64Emulate::emulate_ldr_d_pos},
    {adis::ENC_LDR_D_ldst_regoff, Aarch64Emulate::emulate_ldrd_reg},
    {adis::ENC_LDR_Q_ldst_pos, Aarch64Emulate::emulate_ldr_q_pos},
    {adis::ENC_LDR_Q_ldst_immpre, Aarch64Emulate::emulate_ldr_q_pre},
    {adis::ENC_LDR_Q_ldst_immpost, Aarch64Emulate::emulate_ldr_q_post},
    {adis::ENC_LDR_Q_ldst_regoff, Aarch64Emulate::emulate_ldrq_reg},
    {adis::ENC_LDR_S_ldst_pos, Aarch64Emulate::emulate_ldr_s_pos},
    {adis::ENC_LDR_S_ldst_regoff, Aarch64Emulate::emulate_ldr_s_reg},
    {adis::ENC_LDURB_32_ldst_unscaled, Aarch64Emulate::emulate_ldurb},
    {adis::ENC_LDUR_32_ldst_unscaled, Aarch64Emulate::emulate_ldur32},
    {adis::ENC_LDUR_64_ldst_unscaled, Aarch64Emulate::emulate_ldur64},
    {adis::ENC_LDUR_Q_ldst_unscaled, Aarch64Emulate::emulate_ldurq},
    {adis::ENC_LDUR_D_ldst_unscaled, Aarch64Emulate::emulate_ldurd},
    {adis::ENC_LDURH_32_ldst_unscaled, Aarch64Emulate::emulate_ldurh},
    {adis::ENC_LDURSH_32_ldst_unscaled, Aarch64Emulate::emulate_ldursh32},
    {adis::ENC_LDURSH_64_ldst_unscaled, Aarch64Emulate::emulate_ldursh64},

    {adis::ENC_PRFM_P_ldst_regoff, Aarch64Emulate::emulate_prfm_reg},
    {adis::ENC_PRFM_P_loadlit, Aarch64Emulate::emulate_prfm_lit},
    {adis::ENC_PRFUM_P_ldst_unscaled, Aarch64Emulate::emulate_prfum},
    {adis::ENC_PRFM_P_ldst_pos, Aarch64Emulate::emulate_prfm_pos},

    {adis::ENC_STLXR_SR64_ldstexclr, Aarch64Emulate::emulate_stlxr64},
    {adis::ENC_STLXR_SR32_ldstexclr, Aarch64Emulate::emulate_stlxr32},
    {adis::ENC_STLXRB_SR32_ldstexclr, Aarch64Emulate::emulate_stlxrb},
    {adis::ENC_LDXR_LR32_ldstexclr, Aarch64Emulate::emulate_ldxr32},
    {adis::ENC_LDXR_LR64_ldstexclr, Aarch64Emulate::emulate_ldxr64},
    {adis::ENC_LDXRB_LR32_ldstexclr, Aarch64Emulate::emulate_ldxrb},

    {adis::ENC_LSLV_32_dp_2src, Aarch64Emulate::emulate_lslv32},
    {adis::ENC_LSLV_64_dp_2src, Aarch64Emulate::emulate_lslv64},
    {adis::ENC_LSRV_32_dp_2src, Aarch64Emulate::emulate_lsrv32},
    {adis::ENC_LSRV_64_dp_2src, Aarch64Emulate::emulate_lsrv64},
    {adis::ENC_MADD_32A_dp_3src, Aarch64Emulate::emulate_madd32},
    {adis::ENC_MADD_64A_dp_3src, Aarch64Emulate::emulate_madd64},
    {adis::ENC_MSUB_32A_dp_3src, Aarch64Emulate::emulate_msub32},
    {adis::ENC_MSUB_64A_dp_3src, Aarch64Emulate::emulate_msub64},
    {adis::ENC_NOP_HI_hints, Aarch64Emulate::emulate_pass},
    {adis::ENC_RBIT_32_dp_1src, Aarch64Emulate::emulate_rbit32},
    {adis::ENC_RBIT_64_dp_1src, Aarch64Emulate::emulate_rbit64},
    {adis::ENC_RET_64R_branch_reg, Aarch64Emulate::emulate_ret},
    {adis::ENC_REV_32_dp_1src, Aarch64Emulate::emulate_rev32},
    {adis::ENC_REV_64_dp_1src, Aarch64Emulate::emulate_rev64},
    {adis::ENC_SBFM_32M_bitfield, Aarch64Emulate::emulate_sbfm32},
    {adis::ENC_SBFM_64M_bitfield, Aarch64Emulate::emulate_sbfm64},
    {adis::ENC_STP_D_ldstpair_off, Aarch64Emulate::emulate_stp_d_off},
    {adis::ENC_STP_D_ldstpair_post, Aarch64Emulate::emulate_stp_d_post},
    {adis::ENC_STP_D_ldstpair_pre, Aarch64Emulate::emulate_stp_d_pre},
    {adis::ENC_STP_S_ldstpair_off, Aarch64Emulate::emulate_stp_s_off},
    {adis::ENC_STP_S_ldstpair_post, Aarch64Emulate::emulate_stp_s_post},
    {adis::ENC_STP_S_ldstpair_pre, Aarch64Emulate::emulate_stp_s_pre},
    {adis::ENC_STP_Q_ldstpair_off, Aarch64Emulate::emulate_stp_q_off},
    {adis::ENC_STP_Q_ldstpair_post, Aarch64Emulate::emulate_stp_q_post},
    {adis::ENC_STP_Q_ldstpair_pre, Aarch64Emulate::emulate_stp_q_pre},
    {adis::ENC_STRB_32BL_ldst_regoff, Aarch64Emulate::emulate_strb32_lsl_reg},
    {adis::ENC_STRB_32B_ldst_regoff, Aarch64Emulate::emulate_strb32_reg},
    {adis::ENC_STRB_32_ldst_immpost, Aarch64Emulate::emulate_strb32_post},
    {adis::ENC_STRB_32_ldst_immpre, Aarch64Emulate::emulate_strb32_pre},
    {adis::ENC_STRB_32_ldst_pos, Aarch64Emulate::emulate_strb32_pos},
    {adis::ENC_STRH_32_ldst_immpost, Aarch64Emulate::emulate_strh_post},
    {adis::ENC_STRH_32_ldst_immpre, Aarch64Emulate::emulate_strh_pre},
    {adis::ENC_STRH_32_ldst_pos, Aarch64Emulate::emulate_strh_pos},
    {adis::ENC_STRH_32_ldst_regoff, Aarch64Emulate::emulate_strh_reg},
    {adis::ENC_STR_32_ldst_immpost, Aarch64Emulate::emulate_str_32_ldst_immpre},
    {adis::ENC_STR_32_ldst_immpre, Aarch64Emulate::emulate_str_32_ldst_immpre},
    {adis::ENC_STR_32_ldst_pos, Aarch64Emulate::emulate_str_32_ldst_pos},
    {adis::ENC_STR_32_ldst_regoff, Aarch64Emulate::emulate_str32_reg},
    {adis::ENC_STR_64_ldst_immpost, Aarch64Emulate::emulate_str_64_ldst_post},
    {adis::ENC_STR_64_ldst_immpre, Aarch64Emulate::emulate_str_64_ldst_pre},
    {adis::ENC_STR_64_ldst_pos, Aarch64Emulate::emulate_str_64_ldst_pos},
    {adis::ENC_STR_64_ldst_regoff, Aarch64Emulate::emulate_str64_reg},
    {adis::ENC_STR_D_ldst_immpost, Aarch64Emulate::emulate_strd_post},
    {adis::ENC_STR_D_ldst_immpre, Aarch64Emulate::emulate_strd_pre},
    {adis::ENC_STR_D_ldst_pos, Aarch64Emulate::emulate_strd_pos},
    {adis::ENC_STR_D_ldst_regoff, Aarch64Emulate::emulate_strd_reg},
    {adis::ENC_STR_Q_ldst_immpost, Aarch64Emulate::emulate_str_q_post},
    {adis::ENC_STR_Q_ldst_immpre, Aarch64Emulate::emulate_str_q_pre},
    {adis::ENC_STR_Q_ldst_pos, Aarch64Emulate::emulate_str_q_pos},
    {adis::ENC_STR_Q_ldst_regoff, Aarch64Emulate::emulate_strq_reg},
    {adis::ENC_STR_S_ldst_immpost, Aarch64Emulate::emulate_strs_post},
    {adis::ENC_STR_S_ldst_immpre, Aarch64Emulate::emulate_strs_pre},
    {adis::ENC_STR_S_ldst_pos, Aarch64Emulate::emulate_strs_pos},
    {adis::ENC_STR_S_ldst_regoff, Aarch64Emulate::emulate_str_s},
    {adis::ENC_STURB_32_ldst_unscaled, Aarch64Emulate::emulate_sturb_gpr},
    {adis::ENC_STUR_32_ldst_unscaled, Aarch64Emulate::emulate_stur_32},
    {adis::ENC_STUR_64_ldst_unscaled, Aarch64Emulate::emulate_stur_64},
    {adis::ENC_STUR_Q_ldst_unscaled, Aarch64Emulate::emulate_sturq},
    {adis::ENC_SUBS_32S_addsub_ext, Aarch64Emulate::emulate_subs_32_ext},
    {adis::ENC_SUBS_64S_addsub_ext, Aarch64Emulate::emulate_subs_64_ext},
    {adis::ENC_SUB_32_addsub_ext, Aarch64Emulate::emulate_sub_32_ext},
    {adis::ENC_SUB_64_addsub_ext, Aarch64Emulate::emulate_sub_64_ext},
    {adis::ENC_TBNZ_only_testbranch, Aarch64Emulate::emulate_tbnz},
    {adis::ENC_TBZ_only_testbranch, Aarch64Emulate::emulate_tbz},
    {adis::ENC_UBFM_32M_bitfield, Aarch64Emulate::emulate_ubfm32},
    {adis::ENC_UBFM_64M_bitfield, Aarch64Emulate::emulate_ubfm64},
    {adis::ENC_UDIV_64_dp_2src, Aarch64Emulate::emulate_udiv64},
    {adis::ENC_UDIV_32_dp_2src, Aarch64Emulate::emulate_udiv32},
    {adis::ENC_SDIV_64_dp_2src, Aarch64Emulate::emulate_sdiv64},
    {adis::ENC_SDIV_32_dp_2src, Aarch64Emulate::emulate_sdiv32},
    {adis::ENC_UMADDL_64WA_dp_3src, Aarch64Emulate::emulate_umaddl},
    {adis::ENC_SMADDL_64WA_dp_3src, Aarch64Emulate::emulate_smaddl},
    {adis::ENC_UMSUBL_64WA_dp_3src, Aarch64Emulate::emulate_umsubl},
    {adis::ENC_SMSUBL_64WA_dp_3src, Aarch64Emulate::emulate_smsubl},

    // CASAL_C64_comswap
    // CASL_C64_comswap

};

static const Aarch64Emulate emulator;

void emulate(archx::InstructionIS* instr)
{
    // now all updates should do a std::move() of the arch_state_output
    if (Aarch64Emulate::emulate(instr)) {
        instr->set_emulated();
    }
}

void emulate_correct_path(archx::InstructionIS* instr)
{
    if (Aarch64Emulate::emulate(instr)) {
        // mark instr as emulated so we know to read arch_state_output
        // for a correct path instruction.
        instr->set_emulated();
    }
}

} // namespace ampere::archx::emulate
