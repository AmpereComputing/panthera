//
// Copyright (c) 2019-2022 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//

#include "ampere/archx/fusion_opnd_check.h"

#include "uopcodes.h" // generated

namespace ampere::archx {
std::pair<FusionType, UopSet> fusion_opnd_check(UopSet& uops,
                                                std::uint32_t* insn_bytes,
                                                bool skip_checks) // skip SOME checks
{
    UopSet empty;

    switch (uops[0]->opcode) {
        // ADDS/SUBS CSET
    case Uopcode::SUBS32__CSET_NE:
    case Uopcode::SUBS32__CSET_CC:
    case Uopcode::SUBS32__CSET_PL:
    case Uopcode::SUBS32__CSET_VC:
    case Uopcode::SUBS32__CSET_LS:
    case Uopcode::SUBS32__CSET_LT:
    case Uopcode::SUBS32__CSET_LE:
    case Uopcode::SUBS32__CSET_EQ:
    case Uopcode::SUBS32__CSET_CS:
    case Uopcode::SUBS32__CSET_MI:
    case Uopcode::SUBS32__CSET_VS:
    case Uopcode::SUBS32__CSET_HI:
    case Uopcode::SUBS32__CSET_GE:
    case Uopcode::SUBS32__CSET_GT:
    case Uopcode::SUBS64__CSET_NE:
    case Uopcode::SUBS64__CSET_CC:
    case Uopcode::SUBS64__CSET_PL:
    case Uopcode::SUBS64__CSET_VC:
    case Uopcode::SUBS64__CSET_LS:
    case Uopcode::SUBS64__CSET_LT:
    case Uopcode::SUBS64__CSET_LE:
    case Uopcode::SUBS64__CSET_EQ:
    case Uopcode::SUBS64__CSET_CS:
    case Uopcode::SUBS64__CSET_MI:
    case Uopcode::SUBS64__CSET_VS:
    case Uopcode::SUBS64__CSET_HI:
    case Uopcode::SUBS64__CSET_GE:
    case Uopcode::SUBS64__CSET_GT:
    case Uopcode::ADDS32__CSET_NE:
    case Uopcode::ADDS32__CSET_CC:
    case Uopcode::ADDS32__CSET_PL:
    case Uopcode::ADDS32__CSET_VC:
    case Uopcode::ADDS32__CSET_LS:
    case Uopcode::ADDS32__CSET_LT:
    case Uopcode::ADDS32__CSET_LE:
    case Uopcode::ADDS32__CSET_EQ:
    case Uopcode::ADDS32__CSET_CS:
    case Uopcode::ADDS32__CSET_MI:
    case Uopcode::ADDS32__CSET_VS:
    case Uopcode::ADDS32__CSET_HI:
    case Uopcode::ADDS32__CSET_GE:
    case Uopcode::ADDS32__CSET_GT:
    case Uopcode::ADDS64__CSET_NE:
    case Uopcode::ADDS64__CSET_CC:
    case Uopcode::ADDS64__CSET_PL:
    case Uopcode::ADDS64__CSET_VC:
    case Uopcode::ADDS64__CSET_LS:
    case Uopcode::ADDS64__CSET_LT:
    case Uopcode::ADDS64__CSET_LE:
    case Uopcode::ADDS64__CSET_EQ:
    case Uopcode::ADDS64__CSET_CS:
    case Uopcode::ADDS64__CSET_MI:
    case Uopcode::ADDS64__CSET_VS:
    case Uopcode::ADDS64__CSET_HI:
    case Uopcode::ADDS64__CSET_GE:
    case Uopcode::ADDS64__CSET_GT: {
        std::uint32_t f1_Rd = extract(insn_bytes[0], 4, 0);
        std::uint32_t f2_Rd = extract(insn_bytes[1], 4, 0);
        std::uint32_t f2_Rn = extract(insn_bytes[1], 9, 5);
        std::uint32_t f2_Rm = extract(insn_bytes[1], 20, 16);
        std::uint32_t f2_cond = extract(insn_bytes[1], 15, 12);

        // 1) CSET is an alias of CSINC when both srcs are ZR and cond != 111x
        // 2) Either dests match or flag writer has dest=ZR
        if (((f2_Rn == 0x1F) && (f2_Rm == 0x1F) && ((f2_cond & 0xE) != 0xE)) && ((f1_Rd == f2_Rd) || (f1_Rd == 0x1F)))
            return std::make_pair(FusionType::ADDSSUBS_CSET, std::move(uops));
        else
            return std::make_pair(FusionType::INVALID, std::move(empty));
    }

        // ADD/SUB 24-bit immediate fusion
    case Uopcode::ADD64__IMM24:
    case Uopcode::ADD32__IMM24:
    case Uopcode::SUB64__IMM24:
    case Uopcode::SUB32__IMM24: {
        std::uint32_t f1_Rd = extract(insn_bytes[0], 4, 0);
        std::uint32_t f2_Rd = extract(insn_bytes[1], 4, 0);
        std::uint32_t f2_Rn = extract(insn_bytes[1], 9, 5);
        std::uint32_t f1_sh = extract(insn_bytes[0], 22, 22);
        std::uint32_t f2_sh = extract(insn_bytes[1], 22, 22);

        // 1) The two instructions must both write the same dest
        // 2) The dest of the first must be the source of the second
        // 3) One immediate must be shifted by 12 bits, and the other not
        if (f1_Rd == f2_Rd && f1_Rd == f2_Rn && f1_sh != f2_sh)
            return std::make_pair(FusionType::ADDSUB_IMM24, std::move(uops));
        else
            return std::make_pair(FusionType::INVALID, std::move(empty));
    }

        // ALU + CBZ Fusion
    case Uopcode::SUB64_CBNZ:
    case Uopcode::AND64_CBNZ:
    case Uopcode::EOR64_CBNZ:
    case Uopcode::AND64_IMM_CBNZ:
    case Uopcode::EOR64_IMM_CBNZ:
    case Uopcode::SUB64_CBZ:
    case Uopcode::AND64_CBZ:
    case Uopcode::EOR64_CBZ:
    case Uopcode::AND64_IMM_CBZ:
    case Uopcode::EOR64_IMM_CBZ:
    case Uopcode::SUB32_CBNZ:
    case Uopcode::AND32_CBNZ:
    case Uopcode::EOR32_CBNZ:
    case Uopcode::AND32_IMM_CBNZ:
    case Uopcode::EOR32_IMM_CBNZ:
    case Uopcode::SUB32_CBZ:
    case Uopcode::AND32_CBZ:
    case Uopcode::EOR32_CBZ:
    case Uopcode::AND32_IMM_CBZ:
    case Uopcode::EOR32_IMM_CBZ: {
        std::uint32_t f_Rd = extract(insn_bytes[0], 4, 0);
        std::uint32_t f1_Rt = extract(insn_bytes[1], 4, 0);

        // Destination of the ALU instruction must match source of the compare-branch
        // Dest is not x31 (interpret x31 differently)
        if ((f_Rd == f1_Rt) && (f_Rd != 0x1F))
            return std::make_pair(FusionType::CBZ, std::move(uops));
        else
            return std::make_pair(FusionType::INVALID, std::move(empty));
    }

        // ADD+ADD+1 and SUB-SUB-1 fusion
    case Uopcode::ADD64_P1:
    case Uopcode::ADD32_P1:
    case Uopcode::SUB64_M1:
    case Uopcode::SUB32_M1: {
        std::uint32_t f_Rd = extract(insn_bytes[0], 4, 0);
        std::uint32_t f1_Rd = extract(insn_bytes[1], 4, 0);
        std::uint32_t f1_Rn = extract(insn_bytes[1], 9, 5);
        std::uint32_t f1_imm = extract(insn_bytes[1], 21, 10);
        std::uint32_t f1_shift = extract(insn_bytes[1], 22, 22);

        // Both instructions write the same dest
        // Dest of the first instruction is source of the second
        // Second instruction has a non-shifted immediate of 1
        if (f_Rd == f1_Rd && f_Rd == f1_Rn && f1_imm == 0x1 && f1_shift == 0)
            return std::make_pair(FusionType::ADDP1, std::move(uops));
        else
            return std::make_pair(FusionType::INVALID, std::move(empty));
    }

        // ADR Fusion
    case Uopcode::ADR: {
        std::uint32_t f_Rd = extract(insn_bytes[0], 4, 0);
        std::uint32_t f1_Rd = extract(insn_bytes[1], 4, 0);
        std::uint32_t f1_Rn = extract(insn_bytes[1], 9, 5);
        std::uint32_t f1_shift = extract(insn_bytes[1], 23, 22);

        // 1. Dest Match
        // 2. Dest of 1st is src1 of 2nd
        // 3. Dest is not x31
        // 4. AddImm shift == 0
        if ((f_Rd == f1_Rd && f_Rd == f1_Rn && f1_shift == 0) && (f_Rd != 0x1F))
            return std::make_pair(FusionType::ADR, std::move(uops));
        else
            return std::make_pair(FusionType::INVALID, std::move(empty));
    }

        // AES Fusion
    case Uopcode::AESDF_B_LV:
    case Uopcode::AESEF_B_LV: {
        std::uint32_t f_Rd = extract(insn_bytes[0], 4, 0);
        std::uint32_t f1_Rd = extract(insn_bytes[1], 4, 0);
        std::uint32_t f1_Rn = extract(insn_bytes[1], 9, 5);

        // 1) Dests match
        // 2) Dest of 1st is src1 of 2nd
        if (f_Rd == f1_Rd && f_Rd == f1_Rn)
            return std::make_pair(FusionType::AES, std::move(uops));
        else
            return std::make_pair(FusionType::INVALID, std::move(empty));
    }

    case Uopcode::MOVKK64_0B:
    case Uopcode::MOVKK64_4B:
    case Uopcode::MOVZK64_0B:
    case Uopcode::MOVZK64_4B:
    case Uopcode::MOVZK64_2B:
    case Uopcode::MOVKK64_2B: {
        std::uint32_t f_Rd = extract(insn_bytes[0], 4, 0);
        std::uint32_t f_hw = extract(insn_bytes[0], 22, 21);
        std::uint32_t f1_Rd = extract(insn_bytes[1], 4, 0);
        std::uint32_t f1_hw = extract(insn_bytes[1], 22, 21);

        // 1) Dests match
        // 2) Shift of 2nd needs to be offset by 1 from 1st
        if (f_Rd == f1_Rd) {
            if ((f_hw == 0 && f1_hw == 1) || (f_hw == 2 && f1_hw == 3) || skip_checks)
                return std::make_pair(FusionType::MOV_MOV, std::move(uops));
            if ((f_hw == 1 && f1_hw == 0) || (f_hw == 1 && f1_hw == 2) || skip_checks)
                return std::make_pair(FusionType::MOV_MOV_TRINITY, std::move(uops));
        }

        return std::make_pair(FusionType::INVALID, std::move(empty));
    }

    case Uopcode::MOVZK32_0B: {
        std::uint32_t f_Rd = extract(insn_bytes[0], 4, 0);
        std::uint32_t f_hw = extract(insn_bytes[0], 22, 21);
        std::uint32_t f1_Rd = extract(insn_bytes[1], 4, 0);
        std::uint32_t f1_hw = extract(insn_bytes[1], 22, 21);

        // 1) Dests match
        // 2) Second instruction is shifted and first is not
        if (f_Rd == f1_Rd) {
            if ((f_hw == 0 && f1_hw == 1) || skip_checks)
                return std::make_pair(FusionType::MOV_MOV, std::move(uops));
        }

        return std::make_pair(FusionType::INVALID, std::move(empty));
    }

    case Uopcode::ADDS32__BC_AL:
    case Uopcode::ADDS32__BC_CC:
    case Uopcode::ADDS32__BC_CS:
    case Uopcode::ADDS32__BC_EQ:
    case Uopcode::ADDS32__BC_GE:
    case Uopcode::ADDS32__BC_GT:
    case Uopcode::ADDS32__BC_HI:
    case Uopcode::ADDS32__BC_LE:
    case Uopcode::ADDS32__BC_LS:
    case Uopcode::ADDS32__BC_LT:
    case Uopcode::ADDS32__BC_MI:
    case Uopcode::ADDS32__BC_NE:
    case Uopcode::ADDS32__BC_PL:
    case Uopcode::ADDS32__BC_VC:
    case Uopcode::ADDS32__BC_VS:
    case Uopcode::ADDS64__BC_AL:
    case Uopcode::ADDS64__BC_CC:
    case Uopcode::ADDS64__BC_CS:
    case Uopcode::ADDS64__BC_EQ:
    case Uopcode::ADDS64__BC_GE:
    case Uopcode::ADDS64__BC_GT:
    case Uopcode::ADDS64__BC_HI:
    case Uopcode::ADDS64__BC_LE:
    case Uopcode::ADDS64__BC_LS:
    case Uopcode::ADDS64__BC_LT:
    case Uopcode::ADDS64__BC_MI:
    case Uopcode::ADDS64__BC_NE:
    case Uopcode::ADDS64__BC_PL:
    case Uopcode::ADDS64__BC_VC:
    case Uopcode::ADDS64__BC_VS:
    case Uopcode::ANDS32__BC_AL:
    case Uopcode::ANDS32__BC_CC:
    case Uopcode::ANDS32__BC_CS:
    case Uopcode::ANDS32__BC_EQ:
    case Uopcode::ANDS32__BC_GE:
    case Uopcode::ANDS32__BC_GT:
    case Uopcode::ANDS32__BC_HI:
    case Uopcode::ANDS32__BC_LE:
    case Uopcode::ANDS32__BC_LS:
    case Uopcode::ANDS32__BC_LT:
    case Uopcode::ANDS32__BC_MI:
    case Uopcode::ANDS32__BC_NE:
    case Uopcode::ANDS32__BC_PL:
    case Uopcode::ANDS32__BC_VC:
    case Uopcode::ANDS32__BC_VS:
    case Uopcode::ANDS64__BC_AL:
    case Uopcode::ANDS64__BC_CC:
    case Uopcode::ANDS64__BC_CS:
    case Uopcode::ANDS64__BC_EQ:
    case Uopcode::ANDS64__BC_GE:
    case Uopcode::ANDS64__BC_GT:
    case Uopcode::ANDS64__BC_HI:
    case Uopcode::ANDS64__BC_LE:
    case Uopcode::ANDS64__BC_LS:
    case Uopcode::ANDS64__BC_LT:
    case Uopcode::ANDS64__BC_MI:
    case Uopcode::ANDS64__BC_NE:
    case Uopcode::ANDS64__BC_PL:
    case Uopcode::ANDS64__BC_VC:
    case Uopcode::ANDS64__BC_VS:
    case Uopcode::BICS32__BC_AL:
    case Uopcode::BICS32__BC_CC:
    case Uopcode::BICS32__BC_CS:
    case Uopcode::BICS32__BC_EQ:
    case Uopcode::BICS32__BC_GE:
    case Uopcode::BICS32__BC_GT:
    case Uopcode::BICS32__BC_HI:
    case Uopcode::BICS32__BC_LE:
    case Uopcode::BICS32__BC_LS:
    case Uopcode::BICS32__BC_LT:
    case Uopcode::BICS32__BC_MI:
    case Uopcode::BICS32__BC_NE:
    case Uopcode::BICS32__BC_PL:
    case Uopcode::BICS32__BC_VC:
    case Uopcode::BICS32__BC_VS:
    case Uopcode::BICS64__BC_AL:
    case Uopcode::BICS64__BC_CC:
    case Uopcode::BICS64__BC_CS:
    case Uopcode::BICS64__BC_EQ:
    case Uopcode::BICS64__BC_GE:
    case Uopcode::BICS64__BC_GT:
    case Uopcode::BICS64__BC_HI:
    case Uopcode::BICS64__BC_LE:
    case Uopcode::BICS64__BC_LS:
    case Uopcode::BICS64__BC_LT:
    case Uopcode::BICS64__BC_MI:
    case Uopcode::BICS64__BC_NE:
    case Uopcode::BICS64__BC_PL:
    case Uopcode::BICS64__BC_VC:
    case Uopcode::BICS64__BC_VS:
    case Uopcode::CCMN32__BC_AL:
    case Uopcode::CCMN32__BC_CC:
    case Uopcode::CCMN32__BC_CS:
    case Uopcode::CCMN32__BC_EQ:
    case Uopcode::CCMN32__BC_GE:
    case Uopcode::CCMN32__BC_GT:
    case Uopcode::CCMN32__BC_HI:
    case Uopcode::CCMN32__BC_LE:
    case Uopcode::CCMN32__BC_LS:
    case Uopcode::CCMN32__BC_LT:
    case Uopcode::CCMN32__BC_MI:
    case Uopcode::CCMN32__BC_NE:
    case Uopcode::CCMN32__BC_PL:
    case Uopcode::CCMN32__BC_VC:
    case Uopcode::CCMN32__BC_VS:
    case Uopcode::CCMN64__BC_AL:
    case Uopcode::CCMN64__BC_CC:
    case Uopcode::CCMN64__BC_CS:
    case Uopcode::CCMN64__BC_EQ:
    case Uopcode::CCMN64__BC_GE:
    case Uopcode::CCMN64__BC_GT:
    case Uopcode::CCMN64__BC_HI:
    case Uopcode::CCMN64__BC_LE:
    case Uopcode::CCMN64__BC_LS:
    case Uopcode::CCMN64__BC_LT:
    case Uopcode::CCMN64__BC_MI:
    case Uopcode::CCMN64__BC_NE:
    case Uopcode::CCMN64__BC_PL:
    case Uopcode::CCMN64__BC_VC:
    case Uopcode::CCMN64__BC_VS:
    case Uopcode::CCMP32__BC_AL:
    case Uopcode::CCMP32__BC_CC:
    case Uopcode::CCMP32__BC_CS:
    case Uopcode::CCMP32__BC_EQ:
    case Uopcode::CCMP32__BC_GE:
    case Uopcode::CCMP32__BC_GT:
    case Uopcode::CCMP32__BC_HI:
    case Uopcode::CCMP32__BC_LE:
    case Uopcode::CCMP32__BC_LS:
    case Uopcode::CCMP32__BC_LT:
    case Uopcode::CCMP32__BC_MI:
    case Uopcode::CCMP32__BC_NE:
    case Uopcode::CCMP32__BC_PL:
    case Uopcode::CCMP32__BC_VC:
    case Uopcode::CCMP32__BC_VS:
    case Uopcode::CCMP64__BC_AL:
    case Uopcode::CCMP64__BC_CC:
    case Uopcode::CCMP64__BC_CS:
    case Uopcode::CCMP64__BC_EQ:
    case Uopcode::CCMP64__BC_GE:
    case Uopcode::CCMP64__BC_GT:
    case Uopcode::CCMP64__BC_HI:
    case Uopcode::CCMP64__BC_LE:
    case Uopcode::CCMP64__BC_LS:
    case Uopcode::CCMP64__BC_LT:
    case Uopcode::CCMP64__BC_MI:
    case Uopcode::CCMP64__BC_NE:
    case Uopcode::CCMP64__BC_PL:
    case Uopcode::CCMP64__BC_VC:
    case Uopcode::CCMP64__BC_VS:
    case Uopcode::SUBS32__BC_AL:
    case Uopcode::SUBS32__BC_CC:
    case Uopcode::SUBS32__BC_CS:
    case Uopcode::SUBS32__BC_EQ:
    case Uopcode::SUBS32__BC_GE:
    case Uopcode::SUBS32__BC_GT:
    case Uopcode::SUBS32__BC_HI:
    case Uopcode::SUBS32__BC_LE:
    case Uopcode::SUBS32__BC_LS:
    case Uopcode::SUBS32__BC_LT:
    case Uopcode::SUBS32__BC_MI:
    case Uopcode::SUBS32__BC_NE:
    case Uopcode::SUBS32__BC_PL:
    case Uopcode::SUBS32__BC_VC:
    case Uopcode::SUBS32__BC_VS:
    case Uopcode::SUBS64__BC_AL:
    case Uopcode::SUBS64__BC_CC:
    case Uopcode::SUBS64__BC_CS:
    case Uopcode::SUBS64__BC_EQ:
    case Uopcode::SUBS64__BC_GE:
    case Uopcode::SUBS64__BC_GT:
    case Uopcode::SUBS64__BC_HI:
    case Uopcode::SUBS64__BC_LE:
    case Uopcode::SUBS64__BC_LS:
    case Uopcode::SUBS64__BC_LT:
    case Uopcode::SUBS64__BC_MI:
    case Uopcode::SUBS64__BC_NE:
    case Uopcode::SUBS64__BC_PL:
    case Uopcode::SUBS64__BC_VC:
    case Uopcode::SUBS64__BC_VS:
        return std::make_pair(FusionType::BC, std::move(uops));
    default:
        // Unknown fusion found
        return std::make_pair(FusionType::INVALID, std::move(uops));
    } // switch
} // fusion_opnd_check fn
} // namespace ampere::archx
