//
// Copyright (c) 2019 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//

#include "ampere/archx/arch_state.h"
#include "ampere/sc/report.h"
#include <inttypes.h>
#include <fmt/format.h>

namespace ampere {
namespace archx {

ArchState ArchState::raw_write(PhysAddr addr, uint64_t value, uint64_t mask = UINT64_MAX) const
{
    ArchState state = *this;

    uint64_t aligned = addr.value() & 0xfffffffffffffff8;
    uint64_t prev = raw_read(aligned);
    size_t shift = addr.extract_range(2, 0) * 8;
    mask = mask << shift;
    state.memory = std::move(state.memory).set(aligned, (prev & ~mask) | ((uint64_t)value << shift));

    return state;
}

ArchState ArchState::write(PhysAddr addr, uint8_t value) const
{
    return raw_write(addr, uint64_t(value), uint64_t(UINT8_MAX));
}

ArchState ArchState::write(PhysAddr addr, uint16_t value) const
{
    assert(!addr.extract_range(0, 0));
    return raw_write(addr, uint64_t(value), uint64_t(UINT16_MAX));
}

ArchState ArchState::write(PhysAddr addr, uint32_t value) const
{
    assert(!addr.extract_range(1, 0));
    return raw_write(addr, uint64_t(value), uint64_t(UINT32_MAX));
}

ArchState ArchState::write(PhysAddr addr, uint64_t value) const
{
    assert(!addr.extract_range(2, 0));

    ArchState state = *this;
    state.memory = std::move(state.memory).set(addr.value(), value);
    return state;
}

ArchState ArchState::write(PhysAddr addr, const uint8_t *data, size_t size) const
{
    ArchState state = *this;

    while (size) {
        size_t written = 0;
        if (addr.extract_range(0, 0) || size < 2) {
            state = std::move(state).write(addr, *data);
            written = 1;
        } else if (addr.extract_range(1, 0) || size < 4) {
            state = std::move(state).write(addr, *(uint16_t *)data);
            written = 2;
        } else if (addr.extract_range(2, 0) || size < 8) {
            state = std::move(state).write(addr, *(uint32_t *)data);
            written = 4;
        } else {
            state = std::move(state).write(addr, *(uint64_t *)data);
            written = 8;
        }
        size -= written;
        data += written;
        addr += written;
    }
    return state;
}

ArchState ArchState::load_memory(std::istream &is) const
{
    ArchState state = *this;
    std::vector<std::uint8_t> data;

    while (is) {
        std::uint64_t address;
        std::uint32_t size;

        is.read((char *)&address, sizeof(address));
        is.read((char *)&size, sizeof(size));

        data.resize(size);
        is.read((char *)data.data(), size);

        state = std::move(state).write(PhysAddr{address}, data.data(), size);
    }

    return state;
}

ArchState ArchState::load_elf(ELFIO::elfio &reader) const
{
    const uint64_t one = 1;
    bool host_little_endian = (((uint8_t *)&one)[0] == 1);
    bool elf_little_endian = (reader.get_encoding() == ELFDATA2LSB);
    ArchState state = *this;

    if (host_little_endian != elf_little_endian)
        throw std::runtime_error("Host and ELF byte order do not match");

    for (int i = 0; i < reader.segments.size(); ++i) {
        const ELFIO::segment *seg = reader.segments[i];
        if (seg->get_type() != PT_LOAD)
            continue;

        state = std::move(state).write(
            PhysAddr{seg->get_physical_address()}, (const uint8_t *)seg->get_data(), seg->get_memory_size());
    }
    return state;
}

void ArchState::dump_memory() const
{
    for (auto &entry : memory)
        fmt::print("0x{:016x}: 0x{:016x}\n", entry.first, entry.second);
}

uint64_t ArchState::read(PhysAddr addr, size_t size) const
{
    return raw_read(addr.value(), size);
}

uint64_t ArchState::raw_read(uint64_t addr, size_t size) const
{
    if (size > 8)
        AMPERE_ERROR("Invalid read size = {} for ArchState::raw_read(uint64_t addr, size_t size). Max read size is 8.",
                     size);

    uint64_t val = 0;
    size_t read_so_far = 0;

    while (size) {
        size_t read = 0;
        uint64_t aligned = addr & (~(uint64_t)0x7);
        Bitfield aligned_8B_val = Bitfield(memory[aligned]);
        uint64_t distance_to_boundary = aligned + 8 - addr;

        unsigned lo = (addr - aligned) * 8;
        read = std::min(size, distance_to_boundary);
        unsigned hi = lo + read * 8 - 1;

        val += aligned_8B_val.extract_range(hi, lo) << read_so_far;

        read_so_far += read * 8;
        size -= read;
        addr += read;
    }

    return val;
}

uint64_t ArchState::read(LReg regno) const
{
    return gp_registers[regno];
}

ArchState ArchState::write(LReg regno, uint64_t value) const
{
    ArchState state = *this;
    state.gp_registers = std::move(state.gp_registers).set(regno, value);
    return state;
}

std::array<uint64_t, 2> ArchState::read_vfp(LReg regno) const
{
    return vfp_registers[regno];
}

ArchState ArchState::write_vfp(LReg regno, std::array<uint64_t, 2> value) const
{
    ArchState state = *this;
    state.vfp_registers = std::move(state.vfp_registers).set(regno, value);
    return state;
}

ArchState ArchState::write_vfp(LReg regno, __uint128_t value) const
{
    std::array<std::uint64_t, 2> vecval{static_cast<std::uint64_t>(value), static_cast<std::uint64_t>(value >> 64)};
    return write_vfp(regno, vecval);
}

pred_reg_t ArchState::read_pred(LReg regno) const
{
    return pred_registers[regno];
}

ArchState ArchState::write_pred(LReg regno, pred_reg_t value) const
{
    ArchState state = *this;
    state.pred_registers = std::move(state.pred_registers).set(regno, value);
    return state;
}

ArchState ArchState::write(SysReg sysreg, uint64_t value) const
{
    ArchState state = *this;
    state.sysregs = std::move(state.sysregs).set(sysreg.addr(), value);
    return state;
}

uint64_t ArchState::read(SysReg sysreg) const
{
    return sysregs[sysreg.addr()];
}

EL ArchState::exception_level() const
{
    return el;
}

ArchState ArchState::set_exception_level(EL e) const
{
    ArchState state = *this;
    state.el = e;
    return state;
}

} // namespace archx
} // namespace ampere

/*
 * vim: sw=4:ts=4:et:si:cindent:cino=\:0,(0,g0,t0:
 */
