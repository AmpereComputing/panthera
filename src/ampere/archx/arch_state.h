//
// Copyright (c) 2019-2022 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//
#pragma once
#include <cstdint>

#include <immer/map.hpp>
#include <elfio/elfio.hpp>

#include "ampere/sc/report.h"
#include "ampere/archx/addr.h"
#include "ampere/archx/register.h"
#include "ampere/archx/sysreg.h"
#include "ampere/archx/el.h"
#include "ampere/archx/pred_reg.h"
#include "ampere/archx/vector.h"

namespace ampere::archx {

class ArchState
{
public:
    ArchState() : memory(), gp_registers(), vfp_registers(), pred_registers(), sysregs(), el(EL::EL0) {};

    uint64_t read(PhysAddr addr, size_t size = 8) const;
    ArchState write(PhysAddr addr, const uint8_t* data, size_t size) const;
    ArchState write(PhysAddr addr, uint8_t value) const;
    ArchState write(PhysAddr addr, uint16_t value) const;
    ArchState write(PhysAddr addr, uint32_t value) const;
    ArchState write(PhysAddr addr, uint64_t value) const;
    ArchState load_memory(std::istream& is) const;
    ArchState load_elf(ELFIO::elfio& reader) const;

    // GPRs
    uint64_t read(LReg regno) const;
    ArchState write(LReg regno, uint64_t value) const;

    // VFP registers
    std::array<uint64_t, 2> read_vfp(LReg regno) const;
    ArchState write_vfp(LReg regno, std::array<uint64_t, 2> value) const;
    ArchState write_vfp(LReg regno, __uint128_t value) const; // simpler interface

    // Predicate registers
    pred_reg_t read_pred(LReg regno) const;
    ArchState write_pred(LReg regno, pred_reg_t value) const;

    uint64_t read(SysReg sysreg) const;
    ArchState write(SysReg sysreg, uint64_t value) const;

    EL exception_level() const;
    ArchState set_exception_level(EL e) const;

    void dump_memory() const;

private:
    using memory_policy =
        immer::memory_policy<immer::default_heap_policy, immer::unsafe_refcount_policy, immer::default_lock_policy>;
    template<typename KT, typename VT>
    using unsafe_immer_map = immer::map<KT, VT, std::hash<KT>, std::equal_to<KT>, memory_policy>;

    using memory_map = unsafe_immer_map<uint64_t, uint64_t>;
    using sysreg_map = unsafe_immer_map<uint16_t, uint64_t>;
    using gp_reg_map = unsafe_immer_map<LReg, uint64_t>;
    using vfp_reg_map = unsafe_immer_map<LReg, std::array<uint64_t, 2>>;
    using pred_reg_map = unsafe_immer_map<LReg, pred_reg_t>;

    ArchState raw_write(PhysAddr addr, uint64_t value, uint64_t mask) const;
    uint64_t raw_read(uint64_t addr, size_t size = 8) const;

    memory_map memory;
    gp_reg_map gp_registers;
    vfp_reg_map vfp_registers;
    pred_reg_map pred_registers;
    sysreg_map sysregs;
    EL el;
};

} // namespace ampere::archx

/*
 * vim: sw=4:ts=4:et:si:cindent:cino=\:0,(0,g0,t0:
 */
