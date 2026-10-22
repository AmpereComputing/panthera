//
// Copyright (c) 2019-2020 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//

#pragma once

#include <cstdint>
#include "ampere/archx/addr.h"
#include "ampere/archx/page_size.h"
#include "ampere/archx/translation.h"
#include "ampere/core/tlb.h"

namespace ampere::core {

using namespace uarch_common;
using namespace archx;

class L2TLBTag : public TLBTag<VirtAddr>
{
public:
    using addr_t = VirtAddr;
    inline static constexpr uint8_t ttbrsel = Translation::TOP_VIRT_ADDR_BIT;

    L2TLBTag() = delete;
    L2TLBTag(VirtAddr va, uint16_t asid, PageSize ps, bool is_instr, int set_bits)
        : TLBTag<VirtAddr>(va, asid), ps(ps), is_instr(is_instr)
    {
        if (set_bits == 7)
            index = hash_7();
        else if (set_bits == 8)
            index = hash_8();
        else
            index = hash_9();
    }

    bool instr() const { return is_instr; }
    Index index_hash() const { return index; }

    inline bool operator==(const L2TLBTag& other) const
    {
        return ps == other.ps && same_page(other, ps) && asid == other.asid && is_instr == other.is_instr;
    }
    inline bool operator!=(const L2TLBTag& other) const { return !this->operator==(other); }
    inline bool operator<(const L2TLBTag& other) const
    {
        if (ps != other.ps)
            return ps < other.ps;
        if (!same_page(other, ps))
            return page_number(ps) < other.page_number(ps);
        if (asid != other.asid)
            return asid < other.asid;
        if (is_instr != other.is_instr)
            return is_instr;
        return false;
    }
    inline bool operator>(const L2TLBTag& other) const { return other.operator<(*this); }
    inline bool operator<=(const L2TLBTag& other) const { return !(this->operator>(other)); }
    inline bool operator>=(const L2TLBTag& other) const { return !(this->operator<(other)); }

private:
    Index hash_7()
    {
        switch (ps.index()) {
        case uint8_t(PageSize::Size::PS_4KB):
            return extract_range(18, 12) ^ extract_range(25, 19) ^ extract_bits({ttbrsel, 44, 40, 37, 34, 31, 28});
        case uint8_t(PageSize::Size::PS_64KB):
            return extract_range(22, 16) ^ extract_range(29, 23) ^ extract_bits({ttbrsel, 45, 42, 39, 36, 33, 31});
        case uint8_t(PageSize::Size::PS_2MB):
            return extract_range(27, 21) ^ extract_range(34, 28) ^ extract_bits({ttbrsel, 46, 44, 42, 40, 38, 36});
        case uint8_t(PageSize::Size::PS_512MB):
            return extract_range(35, 29) ^ extract_range(42, 36) ^ extract_bits({ttbrsel, 47, 46, 45, 44, 43});
        case uint8_t(PageSize::Size::PS_1GB):
            return extract_range(36, 30) ^ extract_range(43, 37) ^ extract_bits({ttbrsel, 47, 46, 45, 44});
        default:
            throw std::runtime_error("L2TLBTag:instr_hash unexpected PageSize");
        }
    }

    Index hash_8()
    {
        switch (ps.index()) {
        case uint8_t(PageSize::Size::PS_4KB):
            return extract_range(19, 12) ^ extract_range(27, 20) ^ extract_bits({47, 44, 41, 38, 35, 32, 29, 28});
        case uint8_t(PageSize::Size::PS_64KB):
            return extract_range(23, 16) ^ extract_range(31, 24) ^ extract_bits({47, 45, 42, 40, 37, 35, 33, 32});
        case uint8_t(PageSize::Size::PS_2MB):
            return extract_range(28, 21) ^ extract_range(36, 29) ^ extract_bits({47, 46, 44, 42, 41, 40, 38, 37});
        case uint8_t(PageSize::Size::PS_512MB):
            return extract_range(36, 29) ^ extract_range(44, 37) ^ extract_bits({ttbrsel, 47, 46, 45});
        case uint8_t(PageSize::Size::PS_1GB):
            return extract_range(37, 30) ^ extract_range(45, 38) ^ extract_bits({ttbrsel, 47, 46});
        default:
            throw std::runtime_error("L2TLBTag:data_hash unexpected PageSize");
        }
    }

    /*
     * Design team's hash function includes hashes for 16KB and 32MB page sizes,
     * which we currently don't support. These are commented out in case we decide to
     * support them in the future
     */
    Index hash_9()
    {
        switch (ps.index()) {
        case uint8_t(PageSize::Size::PS_4KB):
            return extract_range(20, 12) ^ extract_range(29, 21) ^ extract_bits({47, 44, 41, 39, 37, 35, 33, 31, 30});
        case uint8_t(PageSize::Size::PS_2MB):
            return extract_range(29, 21) ^ extract_range(38, 30) ^ extract_range(47, 39);
        case uint8_t(PageSize::Size::PS_1GB):
            return extract_range(38, 30) ^ extract_range(47, 39) ^ extract_bits({48});
        /*
        case uint8_t(PageSize::Size::PS_16KB):
            return extract_range(22, 14) ^
                extract_range(31, 23) ^
                extract_bits({47, 46, 44, 42, 40, 38, 36, 34, 32});
        case uint8_t(PageSize::Size::PS_32M):
            return extract_range(33, 25) ^
                extract_range(42, 34) ^
                extract_bits({48, 47, 45, 42, 41, 39, 37, 35, 33});
        */
        case uint8_t(PageSize::Size::PS_64KB):
            return extract_range(24, 16) ^ extract_range(33, 25) ^ extract_bits({48, 47, 45, 42, 41, 39, 37, 35, 33});
        case uint8_t(PageSize::Size::PS_512MB):
            return extract_range(37, 29) ^ extract_range(46, 38) ^ extract_bits({52, 51, 50, 49, 48, 47});
        case uint8_t(PageSize::Size::PS_4TB):
            return extract_range(52, 49) ^ extract_range(48, 45) ^ extract_range(44, 42);
        default:
            throw std::runtime_error("L2TLBTag:data_hash unexpected PageSize");
        }
    }

    PageSize ps;
    bool is_instr;
    Index index;
};

struct L2TLBEntry
{
    L2TLBEntry(PhysAddr pa, PageSize ps, bool is_instr) : phys_addr(pa), page_size(ps), is_instr{is_instr} {}
    PhysAddr phys_addr{0};
    PageSize page_size{PageSize::Size::INVALID};
    bool is_instr{false};
};

class L2TLB : public TLB<L2TLBTag, L2TLBEntry, TLBType::BOTH>
{
    using Base = TLB<L2TLBTag, L2TLBEntry, TLBType::BOTH>;

public:
    explicit L2TLB(const sc_core::sc_module_name& name)
        : Base(name, L2TLBEntry{PhysAddr{0}, PageSize::Size::PS_4KB, false})
    {
        if (this->get_environment()->parameters.help_requested())
            return;

        // 2 bits to select the bank, 5/6 bits for the set in the bank (instruction/data)
        if (set_bits < 7 || set_bits > 9)
            throw std::runtime_error("L2TLB set index hash only supports exactly 7, 8, or 9 bits.");
    }

protected:
    Index compute_set(L2TLBTag tag) const override { return tag.index_hash(); }
};

} // namespace ampere::core

/*
 * vim: sw=4:ts=4:et:si:cindent:cino=\:0,(0,g0,t0:
 */
