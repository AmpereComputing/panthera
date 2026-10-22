//
// Copyright (c) 2018-2022 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//
#pragma once

#include <fmt/core.h>
#include <fmt/ostream.h>
#include <cstdint>
#include <iostream>
#include <memory>
#include <vector>
#include <cassert>

#include "ampere/archx/register.h"
#include "ampere/archx/uop_info.h"
#include "uopcodes.h"

namespace ampere::archx {

enum Flag {
    C = 0b0001,
    N = 0b0010,
    V = 0b0100,
    Z = 0b1000,
};

enum UopUarch {
    UNKNOWN,
    FURIOSA
};

struct Uop
{
    Uopcode opcode{Uopcode::INVALID};
    UopUarch uarch{UopUarch::UNKNOWN};
    LReg dst{LReg::INVALID};
    LReg dst2{LReg::INVALID};
    LReg pred{LReg::INVALID};
    LReg src1{LReg::INVALID};
    LReg src2{LReg::INVALID};
    LReg src3{LReg::INVALID};
    int imm{0};
    uint64_t imm_width{0};
    uint64_t imm_shift{0};
    bool imm_signext{false};
    // start_inst: Indicates this is the first uop of an instruction
    bool start_inst{false};
    // end_inst: number of instructions to pop from the instruction fifo.
    // Can be > 1 when fusion occurs
    uint8_t end_inst{0};
    UopInfo* info = nullptr;
    bool produce_flags{false};
    bool produce_tmpflags{false};
    bool ldp_2nd_uop{false};
    bool isolate{false};
    // pre_drain: xlat output, backend drain required before uop is dispatched
    bool pre_drain{false};
    bool use_pc{false};
    bool set_imm_to_pc_next{false};
    bool no_cleave{false};
    bool cleavlet0_only{false};
    bool cleavlet0{false};
    bool cleavlet1{false};

    bool is_predicated() const { return info->is_predicated; }
    bool is_predicated_sampled() const { return info->is_predicated_sampled; }
    archx::Uopunit unit() const { return info->unit; }
    bool is_ixu() const { return unit() == archx::Uopunit::Integer || unit() == archx::Uopunit::Branch; }

    bool is_single_uop_store() const
    {
        return (info->store_type == archx::StoreType::SUS || info->store_type == archx::StoreType::STZ
                || info->store_type == archx::StoreType::SPECIAL);
    }

    bool is_lsu() const { return unit() == archx::Uopunit::Load || unit() == archx::Uopunit::Store; }
    bool is_fsu() const { return unit() == archx::Uopunit::FpSIMD; }
    bool is_load_fsu() const { return is_lsu() && (is_vector(dst) || is_vector(dst2)); }
    bool is_store_with_dest() const { return info->store_with_dest; }
    unsigned vector_element_size() const { return info->vector_element_size; }
    unsigned uop_op_count() const { return info->uop_op_count; }

    MultipleMemopType multiple_memop() const
    { // for {ld,st}{2,3,4} and gather/scatter
        return info->multiple_memop;
    }

    bool is_gather() const { return info->is_gather4 || info->is_gather2; }
    bool is_scatter() const { return info->is_scatter2 || info->is_scatter4; }

    unsigned sequence_number() const
    { // for {ld,st}{2,3,4} and gather/scatter
        assert(is_gather() || is_scatter() || multiple_memop() != MultipleMemopType::NONE);
        return info->sequence_number;
    }

    std::string disasm() const;
};

using UopSet = std::vector<std::unique_ptr<Uop>>;
std::ostream& operator<<(std::ostream& o, const struct Uop& uop);

inline std::uint32_t mask(int hi, int lo)
{
    if (hi == 31 && lo == 0)
        return 0xffffffff;
    else
        return ((1u << (hi - lo + 1)) - 1) << lo;
}

inline std::uint32_t extract(std::uint32_t insn_bytes, int hi, int lo)
{
    return (mask(hi, lo) & insn_bytes) >> lo;
}

} // namespace ampere::archx

template<>
struct fmt::formatter<ampere::archx::Uop> : fmt::ostream_formatter
{};

/*
 * vim: sw=4:ts=4:et:si:cindent:cino=\:0,(0,g0,t0:
 */
