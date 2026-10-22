// Copyright (c) 2021-2025 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause

#include <fmt/core.h>
#include <cstdint>
#include <stdexcept>
#include "adis-encoding-fields.h"
#include "adis-static-iinfo.h"
#include "adis-branch-target.h"
#include "adis-signext.h"

namespace adis {

std::uint64_t branch_target(encoding_enum_t inum, std::uint32_t itext, std::uint64_t branch_pc) {
    std::int64_t target = 0;
    std::int32_t offset = 0; // unshifted
    switch(inum) {
    case ENC_TBNZ_only_testbranch:
        offset = sign_extend(fldfrom_TBNZ_only_testbranch(itext, fields_t::imm14), 14);
        break;
    case ENC_TBZ_only_testbranch:
        offset = sign_extend(fldfrom_TBZ_only_testbranch(itext, fields_t::imm14), 14);
        break;
    case ENC_CBNZ_32_compbranch:
        offset = sign_extend(fldfrom_CBNZ_32_compbranch(itext, fields_t::imm19), 19);
        break;
    case ENC_CBNZ_64_compbranch:
        offset = sign_extend(fldfrom_CBNZ_64_compbranch(itext, fields_t::imm19), 19);
        break;
    case ENC_CBZ_32_compbranch:
        offset = sign_extend(fldfrom_CBZ_32_compbranch(itext, fields_t::imm19), 19);
        break;
    case ENC_CBZ_64_compbranch:
        offset = sign_extend(fldfrom_CBZ_64_compbranch(itext, fields_t::imm19), 19);
        break;
    case ENC_B_only_condbranch:
        offset = sign_extend(fldfrom_B_only_condbranch(itext, fields_t::imm19), 19);
        break;
    case ENC_BC_only_condbranch:
        offset = sign_extend(fldfrom_BC_only_condbranch(itext, fields_t::imm19), 19);
        break;
    case ENC_B_only_branch_imm:
        offset = sign_extend(fldfrom_B_only_branch_imm(itext, fields_t::imm26), 26);
        break;
    case ENC_BL_only_branch_imm:
        offset = sign_extend(fldfrom_BL_only_branch_imm(itext, fields_t::imm26), 26);
        break;
    case ENC_CBBGT_8_regs: // all same format  // FEAT_CMPBR
    case ENC_CBBGE_8_regs:
    case ENC_CBBHI_8_regs:
    case ENC_CBBHS_8_regs:
    case ENC_CBBEQ_8_regs:
    case ENC_CBBNE_8_regs:
        offset = sign_extend(fldfrom_CBBGT_8_regs(itext, fields_t::imm9), 9);
        break;
    case ENC_CBGT_32_regs:
    case ENC_CBGE_32_regs:
    case ENC_CBHI_32_regs:
    case ENC_CBHS_32_regs:
    case ENC_CBEQ_32_regs:
    case ENC_CBNE_32_regs:
    case ENC_CBGT_64_regs:
    case ENC_CBGE_64_regs:
    case ENC_CBHI_64_regs:
    case ENC_CBHS_64_regs:
    case ENC_CBEQ_64_regs:
    case ENC_CBNE_64_regs:
        offset = sign_extend(fldfrom_CBNE_64_regs(itext, fields_t::imm9), 9);
        break;
    case ENC_CBGT_32_imm:
    case ENC_CBLT_32_imm:
    case ENC_CBHI_32_imm:
    case ENC_CBLO_32_imm:
    case ENC_CBEQ_32_imm:
    case ENC_CBNE_32_imm:
    case ENC_CBGT_64_imm:
    case ENC_CBLT_64_imm:
    case ENC_CBHI_64_imm:
    case ENC_CBLO_64_imm:
    case ENC_CBEQ_64_imm:
    case ENC_CBNE_64_imm:
        offset = sign_extend(fldfrom_CBNE_64_imm(itext, fields_t::imm9), 9);
        break;
    case ENC_CBHGT_16_regs:
    case ENC_CBHGE_16_regs:
    case ENC_CBHHI_16_regs:
    case ENC_CBHHS_16_regs:
    case ENC_CBHEQ_16_regs:
    case ENC_CBHNE_16_regs:
        offset = sign_extend(fldfrom_CBHNE_16_regs(itext, fields_t::imm9), 9);
        break;
    default:
        throw std::runtime_error(std::string(fmt::format("Not a branch: {}", encoding_name(inum))));
        break;
    }
    target = branch_pc + (offset << 2);
    return target;
}

} // namespace
