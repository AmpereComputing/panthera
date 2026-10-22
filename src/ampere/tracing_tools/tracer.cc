//
// Copyright (c) 2019 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//

#include "tracer.h"
#include "bs_tracer.h"
#include "elf_tracer.h"
#include "is_tracer.h"
#include "ms_tracer.h"
#include "ampere/archx/prefetch_address.h"

namespace ampere::tracer {

std::unique_ptr<Tracer> Tracer::make_tracer(const Tracer::Type type,
                                            const std::string filename_base,
                                            StateAccessor* stateaccessor,
                                            bool flat_map,
                                            uint64_t elf_entry_point,
                                            bool debug,
                                            bool record_destination_registers,
                                            bool sve_mode,
                                            uint32_t dcache_size_bytes, /* dcache size to filter mem stream through*/
                                            uint32_t icache_size_bytes, /* icache size to filter mem stream through*/
                                            uint64_t elf_section_span)
{
    // The next block of accesses will throw std::bad_optional_access if these
    // accesses fail. However, they should never fail during normal operation,
    // so it is acceptable for the plugin to die.
    gpr_map_t gprs = stateaccessor->read_gp_registers().value();
    vec_map_t vfp_regs = stateaccessor->read_vfp_registers().value();
    pred_map_t predicates = stateaccessor->read_pred_registers().value();
    std::map<archx::SysReg, uint64_t> sysregs = stateaccessor->read_sysregs().value();
    archx::EL el = stateaccessor->read_el().value();
    uint32_t pstate = stateaccessor->read_pstate().value();
    uint32_t fpsr = stateaccessor->read_fpsr().value();

    switch (type) {
    case Tracer::Type::BS:
        return std::make_unique<BSTracer>(fmt::format("{}.bs.xz", filename_base), sysregs, el, stateaccessor);
    case Tracer::Type::ELF:
        return std::make_unique<ELFTracer>(fmt::format("{}.elf", filename_base),
                                           fmt::format("{}.iss_terminal", filename_base),
                                           fmt::format("{}.cosim_sysregs", filename_base),
                                           gprs,
                                           vfp_regs,
                                           predicates,
                                           sysregs,
                                           el,
                                           pstate,
                                           fpsr,
                                           stateaccessor,
                                           flat_map,
                                           elf_entry_point,
                                           elf_section_span,
                                           debug);
    case Tracer::Type::IS:
        return std::make_unique<ISTracer>(
            fmt::format("{}.is.xz", filename_base), sysregs, el, stateaccessor, debug, record_destination_registers);
    case Tracer::Type::MS:
        return std::make_unique<MSTracer>(
            fmt::format("{}.ms.xz", filename_base), sysregs, stateaccessor, dcache_size_bytes, icache_size_bytes);
    default:
        throw std::runtime_error("Unexpected Tracer::Type");
    }
}

std::pair<bool, std::optional<archx::VirtAddr>> prefetch_va(const std::string& tracer_name,
                                                            const Instruction& instruction,
                                                            const adis::encoding_enum_t adis_info)
{
    // return a pair: bool indicating success or failure and an
    // optional va when we handle the prefetch address generation.
    // (Some prefetches, like SVE prefetches are handled elsewhere, like normal memops.)

    unsigned base_regno = PREF_RN(instruction.opcode);
    if (base_regno == 31) {
        base_regno = LReg::SP;
    }
    if (adis::prefetch(adis_info)) {
        switch (adis_info) {
        case adis::ENC_PRFM_P_ldst_pos:
            return std::make_pair(
                true, prefetch_immediate_address(instruction.opcode, instruction.source_regs.at(base_regno)));

        case adis::ENC_PRFM_P_loadlit:
            return std::make_pair(true, prefetch_literal_address(instruction.pc, instruction.opcode));

        case adis::ENC_PRFM_P_ldst_regoff:
            return std::make_pair(true,
                                  prefetch_register_address(instruction.opcode,
                                                            instruction.source_regs.at(base_regno),
                                                            instruction.source_regs.at(PREF_RM(instruction.opcode))));

        case adis::ENC_PRFUM_P_ldst_unscaled:
            return std::make_pair(
                true, prefetch_unscaled_address(instruction.opcode, instruction.source_regs.at(base_regno)));

        default:
            if (!adis::sve_family(adis_info)) {
                logf("{} saw prefetch instruction of unexpected type (PC {:016x}, opcode {:08x})",
                     tracer_name,
                     instruction.pc.value(),
                     instruction.opcode);
                return std::make_pair(false, std::nullopt); // return failure
            }
        }
    }
    return std::make_pair(true, std::nullopt); // ok, not a prefetch or an sve prefetch handled elsewhere
}

} // namespace ampere::tracer

/*
 * vim: sw=4:ts=4:et:si:cindent:cino=\:0,(0,g0,t0:
 */
