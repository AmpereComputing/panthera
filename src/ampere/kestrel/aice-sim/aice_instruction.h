//
// Copyright (c) 2024-2025 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//
#pragma once
#include <cstdint>
#include <vector>
#include <string>
#include <format>
#include <iostream>
#include "ampere/util/enum.h"
#include "ampere/sc/credit.h"
#include "ampere/sc/transaction.h"
#include "ampere/kestrel/aice-sim/aice_common.h"
#include "ampere/kestrel/aice-sim/dtype.hh"

// GPR_IMR and GPR_AMR are GPRs specifying IMR and AMR registers indirectly.

make_ns_enum(ampere::kestrel::aice, operand_t, INVALID, GPR, GPR_IMR, GPR_SCALE, GPR_AMR, IMM, MAR_REG);

#include "aice_opcode_enum.h"

namespace ampere::kestrel::aice {

static std::string join_chunks(const std::vector<std::string>& chunks, const std::string& delim)
{
    std::string out;
    bool first = true;
    for (const auto& vv : chunks) {
        if (first) {
            first = false;
        } else {
            out += delim;
        }
        out += vv;
    }
    return out;
}

class aice_operand_t
{
public:
    operand_t op_type{operand_t::INVALID};
    bool read{false};
    bool written{false};
    regidx_t gpr_idx{0}; // gpr register index
    imm_t val{0};        // imm value or imr/amr register index

    aice_operand_t() = default;
    aice_operand_t(operand_t op_type, bool read, bool written, regidx_t gpr_idx, imm_t val)
        : // gpr is read
          op_type(op_type),
          read(read),
          written(written),
          gpr_idx(gpr_idx),
          val(val)
    {
    }
    aice_operand_t(bool read, bool written, regidx_t gpr_idx, imm_t val)
        : // gpr is read or written
          op_type(operand_t::GPR),
          read(read),
          written(written),
          gpr_idx(gpr_idx),
          val(val)
    {
    }
    aice_operand_t(imm_t val)
        : // imm
          op_type(operand_t::IMM),
          read(true),
          written(false),
          gpr_idx(0),
          val(val)
    {
    }

    std::string disasm() const
    {
        std::vector<std::string> chunks;
        // chunks.emplace_back(util::enum_to_string<operand_t>(op_type));
        if (op_type != operand_t::IMM) {
            if (read && written) {
                chunks.emplace_back("RW");
            } else if (read) {
                chunks.emplace_back("R");
            } else if (written) {
                chunks.emplace_back("W");
            }
        }
        switch (op_type) {
        case operand_t::IMM:
            chunks.emplace_back(std::format("{:#x}", val));
            break;
        case operand_t::MAR_REG:
            chunks.emplace_back(std::format("r{}=mar{}", gpr_idx, val));
            break;
        case operand_t::GPR:
            chunks.emplace_back(std::format("r{}={:#x}", gpr_idx, val));
            break;
        case operand_t::GPR_IMR:
            chunks.emplace_back(std::format("r{}=imr{}", gpr_idx, val));
            break;
        case operand_t::GPR_AMR:
            chunks.emplace_back(std::format("r{}=amr{}", gpr_idx, val));
            break;
        default:
            break;
        }
        return join_chunks(chunks, " ");
    }
};

static inline aice_operand_t mk_operand_rd(operand_t op_type, regidx_t gpr_idx, imm_t val)
{
    return aice_operand_t(op_type, true, false, gpr_idx, val);
}
static inline aice_operand_t mk_operand_wr(operand_t op_type, regidx_t gpr_idx, imm_t val)
{
    return aice_operand_t(op_type, false, true, gpr_idx, val);
}
static inline aice_operand_t mk_operand(operand_t op_type, regidx_t gpr_idx, imm_t val)
{
    return aice_operand_t(op_type, true, true, gpr_idx, val);
}

static inline aice_operand_t mk_gpr_rd(regidx_t gpr_idx, imm_t val = 0)
{
    return aice_operand_t(true, false, gpr_idx, val);
}
static inline aice_operand_t mk_mar_rd(regidx_t widx, imm_t val = 0)
{
    return mk_operand_rd(operand_t::MAR_REG, widx, val);
}
static inline aice_operand_t mk_imr_rd(regidx_t gpr_idx, imm_t val)
{
    return mk_operand_rd(operand_t::GPR_IMR, gpr_idx, val);
}
static inline aice_operand_t mk_scale_rd(regidx_t gpr_idx, imm_t val)
{
    return mk_operand_rd(operand_t::GPR_SCALE, gpr_idx, val);
}
static inline aice_operand_t mk_amr_rd(regidx_t gpr_idx, imm_t val)
{
    return mk_operand_rd(operand_t::GPR_AMR, gpr_idx, val);
}

static inline aice_operand_t mk_imm(imm_t val)
{
    return aice_operand_t(val);
}

static inline aice_operand_t mk_gpr_wr(regidx_t gpr_idx, imm_t val = 0)
{
    return aice_operand_t(false, true, gpr_idx, val);
}
static inline aice_operand_t mk_mar_wr(regidx_t widx, imm_t val = 0)
{
    return mk_operand_wr(operand_t::MAR_REG, widx, val);
}
static inline aice_operand_t mk_imr_wr(regidx_t gpr_idx, imm_t val)
{ // wr on the IMR, gpr is rd only
    return mk_operand_wr(operand_t::GPR_IMR, gpr_idx, val);
}
static inline aice_operand_t mk_scale_wr(regidx_t gpr_idx, imm_t val)
{ // wr on the SCALE, gpr is rd only
    return mk_operand_wr(operand_t::GPR_SCALE, gpr_idx, val);
}
static inline aice_operand_t mk_amr_wr(regidx_t gpr_idx, imm_t val)
{ // wr on the AMR, gpr is rd only
    return mk_operand_wr(operand_t::GPR_AMR, gpr_idx, val);
}

static inline aice_operand_t mk_gpr(regidx_t gpr_idx, imm_t val = 0)
{ // rd+wr
    return aice_operand_t(true, true, gpr_idx, val);
}
static inline aice_operand_t mk_imr(regidx_t gpr_idx, imm_t val)
{ // rd+wr on the IMR, gpr is rd only
    return mk_operand(operand_t::GPR_IMR, gpr_idx, val);
}
static inline aice_operand_t mk_scale(regidx_t gpr_idx, imm_t val)
{ // rd+wr on the SCALE, gpr is rd only
    return mk_operand(operand_t::GPR_SCALE, gpr_idx, val);
}
static inline aice_operand_t mk_amr(regidx_t gpr_idx, imm_t val)
{ // rd+wr on the AMR, gpr is rd only
    return mk_operand(operand_t::GPR_AMR, gpr_idx, val);
}

class aice_instruction_obj_t : public ampere::sc::Transaction
{
    static std::uint64_t inum_counter;

public:
    std::uint64_t inum;
    addr_t pc{0};
    opcode_t opcode{opcode_t::UNDEFINED};
    std::vector<aice_operand_t> operands;
    bool transpose{false};
    unsigned nscales{0};
    aice_emu::dtype_t dtype{aice_emu::dtype_t::invalid}; // element type from load* instr

    unsigned _latency{0};
    unsigned _mar_latency{0};
    unsigned _matmula_latency_to_last_overwrite_of_first_output_group{0};

    sc::Credit decoder_credit{nullptr};

    aice_instruction_obj_t(addr_t pc = 0, opcode_t opcode = opcode_t::UNDEFINED) : pc(pc), opcode(opcode)
    {
        inum = inum_counter++;
    }
    aice_instruction_obj_t(addr_t pc, opcode_t opcode, aice_operand_t&& op1) : pc(pc), opcode(opcode)
    {
        inum = inum_counter++;
        operands.emplace_back(op1);
    }
    aice_instruction_obj_t(addr_t pc, opcode_t opcode, aice_operand_t&& op1, aice_operand_t&& op2)
        : pc(pc), opcode(opcode)
    {
        inum = inum_counter++;
        operands.emplace_back(op1);
        operands.emplace_back(op2);
    }
    aice_instruction_obj_t(addr_t pc, opcode_t opcode, aice_operand_t&& op1, aice_operand_t&& op2, aice_operand_t&& op3)
        : pc(pc), opcode(opcode)
    {
        inum = inum_counter++;
        operands.emplace_back(op1);
        operands.emplace_back(op2);
        operands.emplace_back(op3);
    }
    aice_instruction_obj_t(addr_t pc,
                           opcode_t opcode,
                           aice_operand_t&& op1,
                           aice_operand_t&& op2,
                           aice_operand_t&& op3,
                           aice_operand_t&& op4)
        : pc(pc), opcode(opcode)
    {
        inum = inum_counter++;
        operands.emplace_back(op1);
        operands.emplace_back(op2);
        operands.emplace_back(op3);
        operands.emplace_back(op4);
    }
    aice_instruction_obj_t(addr_t pc,
                           opcode_t opcode,
                           aice_operand_t&& op1,
                           aice_operand_t&& op2,
                           aice_operand_t&& op3,
                           aice_operand_t&& op4,
                           aice_operand_t&& op5)
        : pc(pc), opcode(opcode)
    {
        inum = inum_counter++;
        operands.emplace_back(op1);
        operands.emplace_back(op2);
        operands.emplace_back(op3);
        operands.emplace_back(op4);
        operands.emplace_back(op5);
    }
    std::uint64_t get_inum() const { return inum; }
    void set_latency(unsigned latency) { _latency = latency; }
    unsigned latency() const { return _latency; }
    void set_mar_latency(unsigned mar_latency) { _mar_latency = mar_latency; }
    unsigned mar_latency() const { return _mar_latency; }
    void set_matmula_latency_to_last_overwrite_of_first_output_group(unsigned latency)
    {
        _matmula_latency_to_last_overwrite_of_first_output_group = latency;
    }
    unsigned matmula_latency_to_last_overwrite_of_first_output_group() const
    {
        return _matmula_latency_to_last_overwrite_of_first_output_group;
    }

    bool is_transpose() const { return transpose; }
    bool is_branch() const { return opcode == opcode_t::BLT || opcode == opcode_t::BGE || opcode == opcode_t::BIR; }
    bool is_synchalt() const { return opcode == opcode_t::SYNCHALT; }
    bool is_movb() const { return opcode == opcode_t::MOVB; }
    bool is_matmula() const { return opcode == opcode_t::MATMULA; }
    bool is_mul() const
    {
        return opcode == ampere::kestrel::aice::opcode_t::MUL || opcode == ampere::kestrel::aice::opcode_t::MULI;
    }

    aice_emu::dtype_t loadimr_dtype() const { return dtype; }
    imm_t loadimr_dst_imr() const { return operands[0].val; }
    imm_t loadimr_addr() const { return operands[1].val; }
    imm_t loadimr_nrows() const { return operands[3].val; }
    imm_t loadimr_stride() const { return operands[2].val; }

    imm_t movb_src_imr() const { return operands[1].val; }
    imm_t dst_mar_reg() const
    { // movb or loadmar
        return operands[0].val;
    }

    imm_t storeimr_src_imr() const { return operands[2].val; }
    imm_t storeimr_addr() const { return operands[0].val; }
    imm_t storeimr_stride() const { return operands[1].val; }

    imm_t storeamr_src_amr() const { return operands[2].val; }
    imm_t storeamr_addr() const { return operands[0].val; }
    imm_t storeamr_stride() const { return operands[1].val; }

    regidx_t matmul_amr_dest() const { return operands[0].val; }
    regidx_t matmul_imr_src() const { return operands[1].val; }
    regidx_t matmul_mar_src() const { return operands[2].val; }
    std::string disasm() const
    {
        std::vector<std::string> chunks;
        chunks.emplace_back(util::enum_to_string<opcode_t>(opcode));
        chunks.emplace_back(" ");
        bool first = true;
        for (const auto& opnd : operands) {
            if (first) {
                first = false;
            } else {
                chunks.emplace_back(", ");
            }
            chunks.emplace_back(opnd.disasm());
        }
        return join_chunks(chunks, "");
    }
};

using aice_instruction_t = ampere::sc::transaction_ptr<aice_instruction_obj_t>;
using const_aice_instruction_t = ampere::sc::const_ptr<aice_instruction_t>;

static inline aice_instruction_obj_t make_undefined(addr_t pc)
{
    return aice_instruction_obj_t(aice_instruction_obj_t(pc, opcode_t::UNDEFINED));
}
static inline aice_instruction_obj_t make_synchalt(addr_t pc)
{
    return aice_instruction_obj_t(pc, opcode_t::SYNCHALT);
}

static inline aice_instruction_obj_t make_scalar_rrrr(
    addr_t pc, opcode_t opcode, regidx_t gpr_dst, regidx_t gpr_src1, regidx_t gpr_src2, regidx_t gpr_src3)
{
    return aice_instruction_obj_t(
        pc, opcode, mk_gpr_wr(gpr_dst), mk_gpr_rd(gpr_src1), mk_gpr_rd(gpr_src2), mk_gpr_rd(gpr_src3));
}
static inline aice_instruction_obj_t make_scalar_rrr(
    addr_t pc, opcode_t opcode, regidx_t gpr_dst, regidx_t gpr_src1, regidx_t gpr_src2)
{
    return aice_instruction_obj_t(pc, opcode, mk_gpr_wr(gpr_dst), mk_gpr_rd(gpr_src1), mk_gpr_rd(gpr_src2));
}
static inline aice_instruction_obj_t make_scalar_rri(
    addr_t pc, opcode_t opcode, regidx_t gpr_dst, regidx_t gpr_src1, imm_t imm)
{
    return aice_instruction_obj_t(pc, opcode, mk_gpr_wr(gpr_dst), mk_gpr_rd(gpr_src1), mk_imm(imm));
}

static inline aice_instruction_obj_t make_cseltgz(
    addr_t pc, regidx_t gpr_dst, regidx_t gpr_src1, regidx_t gpr_src2, regidx_t gpr_src3)
{
    return make_scalar_rrrr(pc, opcode_t::CSELGTZ, gpr_dst, gpr_src1, gpr_src2, gpr_src3);
}

static inline aice_instruction_obj_t make_cselz(
    addr_t pc, regidx_t gpr_dst, regidx_t gpr_src1, regidx_t gpr_src2, regidx_t gpr_src3)
{
    return make_scalar_rrrr(pc, opcode_t::CSELZ, gpr_dst, gpr_src1, gpr_src2, gpr_src3);
}

static inline aice_instruction_obj_t make_add(addr_t pc, regidx_t gpr_dst, regidx_t gpr_src1, regidx_t gpr_src2)
{
    return make_scalar_rrr(pc, opcode_t::ADD, gpr_dst, gpr_src1, gpr_src2);
}
static inline aice_instruction_obj_t make_addi(addr_t pc, regidx_t gpr_dst, regidx_t gpr_src1, imm_t imm)
{
    return make_scalar_rri(pc, opcode_t::ADDI, gpr_dst, gpr_src1, imm);
}
static inline aice_instruction_obj_t make_and(addr_t pc, regidx_t gpr_dst, regidx_t gpr_src1, regidx_t gpr_src2)
{
    return make_scalar_rrr(pc, opcode_t::AND, gpr_dst, gpr_src1, gpr_src2);
}
static inline aice_instruction_obj_t make_andi(addr_t pc, regidx_t gpr_dst, regidx_t gpr_src1, imm_t imm)
{
    return make_scalar_rri(pc, opcode_t::ANDI, gpr_dst, gpr_src1, imm);
}
static inline aice_instruction_obj_t make_sub(addr_t pc, regidx_t gpr_dst, regidx_t gpr_src1, regidx_t gpr_src2)
{
    return make_scalar_rrr(pc, opcode_t::SUB, gpr_dst, gpr_src1, gpr_src2);
}
static inline aice_instruction_obj_t make_mul(addr_t pc, regidx_t gpr_dst, regidx_t gpr_src1, regidx_t gpr_src2)
{
    return make_scalar_rrr(pc, opcode_t::MUL, gpr_dst, gpr_src1, gpr_src2);
}
static inline aice_instruction_obj_t make_muli(addr_t pc, regidx_t gpr_dst, regidx_t gpr_src1, imm_t imm)
{
    return make_scalar_rri(pc, opcode_t::MULI, gpr_dst, gpr_src1, imm);
}

static inline aice_instruction_obj_t make_lshift(addr_t pc, regidx_t gpr_dst, regidx_t gpr_src1, regidx_t gpr_src2)
{
    return make_scalar_rrr(pc, opcode_t::LSHIFT, gpr_dst, gpr_src1, gpr_src2);
}
static inline aice_instruction_obj_t make_lshifti(addr_t pc, regidx_t gpr_dst, regidx_t gpr_src1, imm_t imm)
{
    return make_scalar_rri(pc, opcode_t::LSHIFTI, gpr_dst, gpr_src1, imm);
}
static inline aice_instruction_obj_t make_rashift(addr_t pc, regidx_t gpr_dst, regidx_t gpr_src1, regidx_t gpr_src2)
{
    return make_scalar_rrr(pc, opcode_t::RASHIFT, gpr_dst, gpr_src1, gpr_src2);
}
static inline aice_instruction_obj_t make_rashifti(addr_t pc, regidx_t gpr_dst, regidx_t gpr_src1, imm_t imm)
{
    return make_scalar_rri(pc, opcode_t::RASHIFTI, gpr_dst, gpr_src1, imm);
}

static inline aice_instruction_obj_t make_blt(addr_t pc, regidx_t gpr_src1, regidx_t gpr_src2, imm_t tgt)
{
    return aice_instruction_obj_t(pc, opcode_t::BLT, mk_gpr_rd(gpr_src1), mk_gpr_rd(gpr_src2), mk_imm(tgt));
}
static inline aice_instruction_obj_t make_bge(addr_t pc, regidx_t gpr_src1, regidx_t gpr_src2, imm_t tgt)
{
    return aice_instruction_obj_t(pc, opcode_t::BGE, mk_gpr_rd(gpr_src1), mk_gpr_rd(gpr_src2), mk_imm(tgt));
}
static inline aice_instruction_obj_t make_bir(addr_t pc, regidx_t gpr_src1)
{
    return aice_instruction_obj_t(pc, opcode_t::BIR, mk_gpr_rd(gpr_src1));
}
static inline aice_instruction_obj_t make_loadimr(addr_t pc,
                                                  regidx_t gpr_imr,
                                                  addr_t imr_idx,
                                                  regidx_t gpr_src,
                                                  addr_t src_mem_start,
                                                  regidx_t gpr_stride,
                                                  imm_t stride,
                                                  regidx_t gpr_nrows,
                                                  addr_t nrows,
                                                  aice_emu::dtype_t dtype,
                                                  bool transpose = false)
{
    auto ai = aice_instruction_obj_t(pc,
                                     opcode_t::LOADIMR,
                                     mk_imr_wr(gpr_imr, imr_idx),
                                     mk_gpr_rd(gpr_src, src_mem_start),
                                     mk_gpr_rd(gpr_stride, stride),
                                     mk_gpr_rd(gpr_nrows, nrows));
    ai.dtype = dtype;
    ai.transpose = transpose;
    return ai;
}

static inline aice_instruction_obj_t make_loadmx(addr_t pc,
                                                 regidx_t gpr_imr,
                                                 addr_t imr_idx,
                                                 regidx_t gpr_src,
                                                 addr_t src_mem_start,
                                                 regidx_t gpr_stride,
                                                 imm_t stride,
                                                 regidx_t gpr_nrows,
                                                 addr_t nrows,
                                                 aice_emu::dtype_t dtype,
                                                 bool transpose = false)
{
    auto ai = aice_instruction_obj_t(pc,
                                     opcode_t::LOADMX,
                                     mk_imr_wr(gpr_imr, imr_idx),
                                     mk_gpr_rd(gpr_src, src_mem_start),
                                     mk_gpr_rd(gpr_stride, stride),
                                     mk_gpr_rd(gpr_nrows, nrows));
    ai.transpose = transpose;
    ai.dtype = dtype;
    return ai;
}
static inline aice_instruction_obj_t make_loadmxu_fp8(addr_t pc,
                                                      regidx_t gpr_imr,
                                                      addr_t imr_idx,
                                                      regidx_t gpr_src,
                                                      addr_t src_mem_start,
                                                      regidx_t gpr_stride,
                                                      imm_t stride,
                                                      regidx_t gpr_nrows,
                                                      addr_t nrows,
                                                      aice_emu::dtype_t dtype)
{
    auto ai = aice_instruction_obj_t(pc,
                                     opcode_t::LOADMXU_FP8,
                                     mk_imr_wr(gpr_imr, imr_idx),
                                     mk_gpr_rd(gpr_src, src_mem_start),
                                     mk_gpr_rd(gpr_stride, stride),
                                     mk_gpr_rd(gpr_nrows, nrows));
    ai.dtype = dtype;
    return ai;
}

static inline aice_instruction_obj_t make_loadmxu_bf16(addr_t pc,
                                                       regidx_t gpr_imr,
                                                       addr_t imr_idx,
                                                       regidx_t gpr_src,
                                                       addr_t src_mem_start,
                                                       regidx_t gpr_stride,
                                                       imm_t stride,
                                                       regidx_t gpr_nrows,
                                                       addr_t nrows,
                                                       aice_emu::dtype_t dtype)
{
    auto ai = aice_instruction_obj_t(pc,
                                     opcode_t::LOADMXU_BF16,
                                     mk_imr_wr(gpr_imr, imr_idx),
                                     mk_gpr_rd(gpr_src, src_mem_start),
                                     mk_gpr_rd(gpr_stride, stride),
                                     mk_gpr_rd(gpr_nrows, nrows));
    ai.dtype = dtype;
    return ai;
}

static inline aice_instruction_obj_t make_loadscale_128(
    addr_t pc, regidx_t gpr_imr, addr_t imr_idx, regidx_t gpr_src, addr_t src_mem_start)
{
    auto inst = aice_instruction_obj_t(
        pc, opcode_t::LOADSCALE_128, mk_scale_wr(gpr_imr, imr_idx), mk_gpr_rd(gpr_src, src_mem_start));
    inst.nscales = 2;
    return inst;
}
static inline aice_instruction_obj_t make_loadscale_256(
    addr_t pc, regidx_t gpr_imr, addr_t imr_idx, regidx_t gpr_src, addr_t src_mem_start)
{
    auto inst = aice_instruction_obj_t(
        pc, opcode_t::LOADSCALE_256, mk_scale_wr(gpr_imr, imr_idx), mk_gpr_rd(gpr_src, src_mem_start));
    inst.nscales = 2;
    return inst;
}
static inline aice_instruction_obj_t make_loadscale_512(
    addr_t pc, regidx_t gpr_imr, addr_t imr_idx, regidx_t gpr_src, addr_t src_mem_start)
{
    auto inst = aice_instruction_obj_t(
        pc, opcode_t::LOADSCALE_512, mk_scale_wr(gpr_imr, imr_idx), mk_gpr_rd(gpr_src, src_mem_start));
    inst.nscales = 2;
    return inst;
}

static inline aice_instruction_obj_t make_storeimr(addr_t pc,
                                                   regidx_t gpr_dst,
                                                   addr_t dst_mem_start,
                                                   regidx_t gpr_stride,
                                                   addr_t stride,
                                                   regidx_t gpr_imr,
                                                   addr_t imr_idx)
{
    return aice_instruction_obj_t(pc,
                                  opcode_t::STOREIMR,
                                  mk_gpr_rd(gpr_dst, dst_mem_start),
                                  mk_gpr_rd(gpr_stride, stride),
                                  mk_imr_rd(gpr_imr, imr_idx));
}
static inline aice_instruction_obj_t make_storeamr(addr_t pc,
                                                   regidx_t gpr_dst,
                                                   addr_t dst_mem_start,
                                                   regidx_t gpr_stride,
                                                   addr_t stride,
                                                   regidx_t gpr_amr,
                                                   addr_t amr_idx,
                                                   aice_emu::dtype_t dtype,
                                                   imm_t accumulators)
{
    opcode_t opcode;
    switch (accumulators) {
    case 64:
        opcode = opcode_t::STOREAMR64;
        break;
    case 32:
        opcode = opcode_t::STOREAMR32;
        break;
    case 16:
        opcode = opcode_t::STOREAMR16;
        break;
    case 8:
        opcode = opcode_t::STOREAMR8;
        break;
    default:
        THROW("Unexpected value for storeamr #accumulators {}", accumulators);
    }
    auto ai = aice_instruction_obj_t(
        pc, opcode, mk_gpr_rd(gpr_dst, dst_mem_start), mk_gpr_rd(gpr_stride, stride), mk_amr_rd(gpr_amr, amr_idx));
    ai.dtype = dtype;
    return ai;
}
static inline aice_instruction_obj_t make_storeamrv(
    addr_t pc, regidx_t gpr_dst, addr_t dst_mem_start, regidx_t gpr_amr, addr_t amr_idx, aice_emu::dtype_t dtype)
{
    auto ai =
        aice_instruction_obj_t(pc, opcode_t::STOREAMRV, mk_gpr_rd(gpr_dst, dst_mem_start), mk_imr_rd(gpr_amr, amr_idx));
    ai.dtype = dtype;
    return ai;
}

static inline aice_instruction_obj_t make_debug(addr_t pc)
{
    return aice_instruction_obj_t(pc, opcode_t::DEBUG);
}
static inline aice_instruction_obj_t make_syncwait(addr_t pc)
{
    return aice_instruction_obj_t(pc, opcode_t::SYNCWAIT);
}
static inline aice_instruction_obj_t make_syncreaddone(addr_t pc)
{
    return aice_instruction_obj_t(pc, opcode_t::SYNCREADDONE);
}
static inline aice_instruction_obj_t make_syncwritedone(addr_t pc)
{
    return aice_instruction_obj_t(pc, opcode_t::SYNCWRITEDONE);
}

static inline aice_instruction_obj_t make_restartable(addr_t pc)
{
    return aice_instruction_obj_t(pc, opcode_t::RESTARTABLE);
}

static inline aice_instruction_obj_t make_movb(
    addr_t pc, regidx_t gpr_mar, regidx_t mar_idx, regidx_t gpr_imr, regidx_t imr_idx)
{
    return aice_instruction_obj_t(pc, opcode_t::MOVB, mk_mar_wr(gpr_mar, mar_idx), mk_imr_rd(gpr_imr, imr_idx));
}

static inline aice_instruction_obj_t make_matmula(addr_t pc,
                                                  regidx_t gpr_amr,
                                                  regidx_t amr_idx,
                                                  regidx_t gpr_imr,
                                                  regidx_t imr_idx,
                                                  regidx_t gpr_mar,
                                                  regidx_t mar_idx,
                                                  regidx_t gpr_acc,
                                                  imm_t accumulate)
{
    return aice_instruction_obj_t(pc,
                                  opcode_t::MATMULA,
                                  // rd+wr vs write-only
                                  accumulate ? mk_amr(gpr_amr, amr_idx) : mk_amr_wr(gpr_amr, amr_idx),
                                  mk_imr_rd(gpr_imr, imr_idx),
                                  mk_mar_rd(gpr_mar, mar_idx),
                                  mk_gpr_rd(gpr_acc, accumulate));
}

} // namespace ampere::kestrel::aice
