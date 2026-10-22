//
// Copyright (c) 2018-2023 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//
#pragma once
#include <cstdint>
#include "ampere/archx/addr.h"
#include "ampere/archx/bitfield.h"

namespace ampere::archx {

// Hierarchical access permissions (apply to all subsequent levels of lookup)
enum TableAccessPermissions : uint8_t {
    NO_EFFECT = 0, // No additional restrictions added
    NO_EL0 = 1,    // No access at EL0
    RO = 2,        // No write access at any EL
    RO_NO_EL0 = 3, // No access at EL0, no write access at any EL
};

enum Shareability : uint8_t {
    NON_SHAREABLE = 0,
    RESERVED = 1,
    OUTER_SHAREABLE = 2,
    INNER_SHAREABLE = 3
};

enum AccessPermissions : uint8_t {
    RW_EL1 = 0,
    RW_EL01 = 1,
    RO_EL1 = 2,
    RO_EL01 = 3
};

struct Untranslated
{
    inline static constexpr unsigned bits = 12;
    inline static constexpr uint64_t mask = 0xfff;
};

union TableDescriptorAttributes
{
public:
    TableDescriptorAttributes(std::uint64_t v = 0) : u64(v) {}
    std::uint64_t value() const { return u64; }
    struct
    {
        std::uint64_t valid : 1;      // 0
        std::uint64_t descriptor : 1; // 1
        std::uint64_t ignored3 : 6;   // 7-2
        std::uint64_t upper2_52 : 2;  // 9-8 for 52b output addr
        std::uint64_t ignored2 : 2;   // 11-10
        // bits 15-12 are ta[51:48] in 52b output addr
        // otherwise shift addr by granule-12
        std::uint64_t next_table_addr : 38; // 49-12 based on granule
        std::uint64_t res0 : 1;             // 50
        std::uint64_t ignored1 : 8;         // 58-51
        std::uint64_t PXNTable : 1;         // 59
        std::uint64_t UXNTable : 1;         // 60
        std::uint64_t APTable : 2;          // 62-61
        std::uint64_t NSTable : 1;          // 63
    } s;
    std::uint64_t u64;
};

class TableDescriptor
{
public:
    // Create a descriptor where you know what you want to map, but don't know
    // the bits
    TableDescriptor(PhysAddr next_table_pa,
                    uint8_t granule_size,
                    bool NSTable = false,
                    TableAccessPermissions APTable = NO_EFFECT,
                    bool XNTable = false,
                    bool PXNTable = false,
                    bool valid = true)
        : m_granule_size{granule_size}, m_next_table_pa{next_table_pa}
    {
        tda.s.NSTable = NSTable ? 1 : 0;
        tda.s.UXNTable = XNTable ? 1 : 0;
        tda.s.PXNTable = PXNTable ? 1 : 0;
        tda.s.APTable = static_cast<std::uint64_t>(APTable);
        tda.s.descriptor = valid ? 1 : 0;
        tda.s.valid = valid ? 1 : 0;
        tda.s.next_table_addr = next_table_pa.extract_range(47, granule_size) << (granule_size - 12);
        if (granule_size == 16) {
            // This assumes ARMv8.2-LPA is implemented
            tda.s.next_table_addr |= next_table_pa.extract_range(51, 48);
        }
    }

    // Create a descriptor object for a descriptor with a known value
    TableDescriptor(uint8_t granule_size, uint64_t value) : tda(value), m_granule_size(granule_size)
    {
        m_next_table_pa = calculate_pa();
    }

    // True if valid *table* descriptor; not true for valid *block* descriptors.
    bool valid() const { return tda.s.valid && tda.s.descriptor; }
    std::uint64_t value() const { return tda.u64; }

    uint8_t granule_size() const { return m_granule_size; }
    PhysAddr next_table_pa() const { return m_next_table_pa; }
    bool NSTable() const { return tda.s.NSTable; }
    TableAccessPermissions APTable() const { return static_cast<TableAccessPermissions>(tda.s.APTable); }
    bool XNTable() const { return tda.s.UXNTable; }
    bool PXNTable() const { return tda.s.PXNTable; }

private:
    TableDescriptorAttributes tda;
    uint8_t m_granule_size;
    PhysAddr m_next_table_pa;

    PhysAddr calculate_pa()
    {
        Bitfield value(tda.value());
        auto next_table_pa = PhysAddr{value.extract_range(47, granule_size()) << granule_size()};
        if (granule_size() == 16) // 64k pages need the TA[51:48] bits when ARMv8.2-LPA is implemented
            next_table_pa = PhysAddr{next_table_pa.value() | value.extract_range(15, 12) << 48};
        return next_table_pa;
    }
};

class BlockDescriptor : public Bitfield
{
public:
    // Create a descriptor where you know what you want to map, but don't know
    // the bits
    BlockDescriptor(PhysAddr pa,
                    uint8_t granule_size,
                    uint8_t level,
                    bool XN = false,  /* execute-never */
                    bool PXN = false, /* privileged execute-never */
                    bool contiguous = false,
                    bool DBM = false, /* dirty bit modifier */
                    bool nT = false,  /* ARMv8.4-TTRem */
                    bool nG = true,   /* not global */
                    bool AF = true,   /* access flag */
                    Shareability SH = INNER_SHAREABLE,
                    AccessPermissions AP = RO_EL01,
                    bool NS = true /* non-secure */,
                    bool is_valid = true)
        : Bitfield{(XN ? 1ULL : 0ULL) << 54 | (PXN ? 1ULL : 0ULL) << 53 | (contiguous ? 1ULL : 0ULL) << 52
                   | (DBM ? 1ULL : 0ULL) << 51 | (nT ? 1ULL : 0ULL) << 16
                   | pa.extract_range(47, n(granule_size, level)) << n(granule_size, level) | (nG ? 1ULL : 0ULL) << 11
                   | (AF ? 1ULL : 0ULL) << 10 | (uint64_t)SH << 8 | (uint64_t)AP << 6 | (NS ? 1ULL : 0ULL) << 5
                   | (is_valid ? 0b01 : 0) /* valid block descriptor */},
          pa{pa.page_address(PageSize(granule_size))},
          granule_size{granule_size},
          level{level},
          XN{XN},
          PXN{PXN},
          contiguous{contiguous},
          DBM{DBM},
          nT{nT},
          nG{nG},
          AF{AF},
          SH{SH},
          AP{AP},
          NS{NS},
          is_valid{is_valid}
    {
    }

    // Create a descriptor object for a descriptor with a known value
    BlockDescriptor(uint8_t granule_size, uint8_t level, uint64_t value)
        : Bitfield{value}, granule_size{granule_size}, level{level}
    {
        XN = extract_bit(54);
        PXN = extract_bit(53);
        contiguous = extract_bit(52);
        DBM = extract_bit(51);
        nT = extract_bit(16);
        pa = PhysAddr{extract_range(47, n(granule_size, level)) << n(granule_size, level)};
        nG = extract_bit(11);
        AF = extract_bit(10);
        SH = (Shareability)extract_range(9, 8);
        AP = (AccessPermissions)extract_range(7, 6);
        NS = extract_bit(5);
        is_valid = extract_range(1, 0) == 0b01;
    }

    std::pair<PhysAddr, PageSize> result() const
    {
        assert(valid());
        return std::pair<PhysAddr, PageSize>(pa, PageSize(n(granule_size, level)));
    }
    bool valid() const { return is_valid; }
    bool access_flag() const { return AF; }

private:
    uint8_t n(uint8_t granule_size, uint8_t level) const
    {
        switch (granule_size) {
        case 12:
            return level == 1 ? 30 : 21;
        case 14:
            return 25;
        case 16:
            return 29;
        default:
            throw std::runtime_error("BlockDescriptor passed invalid granule_size and/or level");
        }
    }

    PhysAddr pa;
    uint8_t granule_size; // number of untranslated bits (i.e. granule_size == 16 means 64kb granule)
    uint8_t level;
    bool XN;  /* execute-never */
    bool PXN; /* privileged execute-never */
    bool contiguous;
    bool DBM; /* dirty bit modifier */
    bool nT;  /* ARMv8.4-TTRem */
    bool nG;  /* not global */
    bool AF;  /* access flag */
    Shareability SH;
    AccessPermissions AP;
    bool NS; /* non-secure */
    bool is_valid;
};

class PageDescriptor : public Bitfield
{
public:
    // Create a descriptor where you know what you want to map, but don't know
    // the bits
    PageDescriptor(PhysAddr pa,
                   uint8_t granule_size,
                   bool XN = false,  /* execute-never */
                   bool PXN = false, /* privileged execute-never */
                   bool contiguous = false,
                   bool DBM = false, /* dirty bit modifier */
                   bool nG = true,   /* not global */
                   bool AF = true,   /* access flag */
                   Shareability SH = INNER_SHAREABLE,
                   AccessPermissions AP = RO_EL01,
                   bool NS = true /* non-secure */,
                   bool is_valid = true)
        : Bitfield{(XN ? 1ULL : 0ULL) << 54 | (PXN ? 1ULL : 0ULL) << 53 | (contiguous ? 1ULL : 0ULL) << 52
                   | (DBM ? 1ULL : 0ULL) << 51 | pa.extract_range(47, granule_size) << granule_size |
                   // This assumes ARMv8.2-LPA is implemented
                   (granule_size == 16 ? pa.extract_range(51, 48) : 0) << 12 | (nG ? 1ULL : 0ULL) << 11
                   | (AF ? 1ULL : 0ULL) << 10 | (uint64_t)SH << 8 | (uint64_t)AP << 6 | (NS ? 1ULL : 0ULL) << 5
                   | (is_valid ? 0b11 : 0) /* valid page descriptor */},
          pa{pa.page_address(PageSize(granule_size))},
          granule_size{granule_size},
          XN{XN},
          PXN{PXN},
          contiguous{contiguous},
          DBM{DBM},
          nG{nG},
          AF{AF},
          SH{SH},
          AP{AP},
          NS{NS},
          is_valid{is_valid}
    {
    }

    // Create a descriptor object for a descriptor with a known value
    PageDescriptor(uint8_t granule_size, uint64_t value) : Bitfield{value}, granule_size{granule_size}
    {
        XN = extract_bit(54);
        PXN = extract_bit(53);
        contiguous = extract_bit(52);
        DBM = extract_bit(51);
        pa = PhysAddr{extract_range(47, granule_size) << granule_size};
        if (granule_size == 16) // 64k pages need the TA[51:48] bits when ARMv8.2-LPA is implemented
            pa = PhysAddr{pa.value() | extract_range(15, 12) << 48};
        nG = extract_bit(11);
        AF = extract_bit(10);
        SH = (Shareability)extract_range(9, 8);
        AP = (AccessPermissions)extract_range(7, 6);
        NS = extract_bit(5);
        is_valid = extract_range(1, 0) == 0b11;
    }

    std::pair<PhysAddr, PageSize> result() const
    {
        assert(valid());
        return std::pair<PhysAddr, PageSize>(pa, PageSize(granule_size));
    }
    bool valid() const { return is_valid; }
    bool access_flag() const { return AF; }

private:
    PhysAddr pa;
    uint8_t granule_size;
    bool XN;  /* execute-never */
    bool PXN; /* privileged execute-never */
    bool contiguous;
    bool DBM; /* dirty bit modifier */
    bool nG;  /* not global */
    bool AF;  /* access flag */
    Shareability SH;
    AccessPermissions AP;
    bool NS; /* non-secure */
    bool is_valid;
};

class TranslationContext
{
public:
    inline static constexpr uint8_t VIRT_ADDR_BITS = 64;
    inline static constexpr uint8_t MAX_VIRT_ADDR_SPACE = 56;
    inline static constexpr uint8_t TOP_VIRT_ADDR_BIT = MAX_VIRT_ADDR_SPACE - 1;

    TranslationContext(VirtAddr va, Bitfield tcr_el1, bool is_instr = false)
        : is_instr{is_instr},
          va{va},
          tcr_el1{tcr_el1},
          granule_size{calculate_granule_size()},
          input_size{calculate_input_size()},
          output_size{calculate_output_size()}
    {
    }

    TranslationContext(ProgramCounter pc, Bitfield tcr_el1) : TranslationContext(VirtAddr(pc.value()), tcr_el1, true) {}

    // inputs
    const bool is_instr;
    const VirtAddr va;
    const Bitfield tcr_el1;

    // computed values
    const uint8_t granule_size;
    const uint8_t input_size;
    const uint8_t output_size;

    /* Returns true for TTBR0, false for TTBR1 */
    inline static bool uses_ttbr0(VirtAddr virt_addr) { return !virt_addr[TOP_VIRT_ADDR_BIT]; }

    bool using_ttbr0() const { return uses_ttbr0(va); }

    bool tbi() const
    {
        return using_ttbr0() ? tcr_el1[37]  /*TBI0*/
                             : tcr_el1[38]; /*TBI1*/
    }

    bool tbid() const
    {
        return using_ttbr0() ? tcr_el1[51]  /*TBID0*/
                             : tcr_el1[52]; /*TBID1*/
    }

    bool canonical() const
    {
        auto [hi, lo] = upper_virt_addr_bit_range();
        return range_is_consistent(hi, lo);
    }

    bool update_access_flag() const
    {
        /* TCR_EL1.HA - Hardware Access flag update in stage 1 translations from EL0 and EL1 */
        return tcr_el1[39];
    }

protected:
    uint8_t top_virt_addr_bit() const
    {
        return (tbi() && (!tbid() || !is_instr)) ? TOP_VIRT_ADDR_BIT : (VIRT_ADDR_BITS - 1);
    }

    uint8_t upper_virt_addr_start_bit() const
    {
        assert(granule_size);
        return granule_size == 16 ? 52 : 48;
    }

    bool range_is_consistent(uint8_t hi, uint8_t lo) const
    {
        auto upper = va.extract_range(hi, lo);
        return upper == 0 || upper == ((1ULL << (hi - lo + 1)) - 1ULL);
    }

    std::pair<uint8_t, uint8_t> upper_virt_addr_bit_range() const
    {
        return {top_virt_addr_bit(), upper_virt_addr_start_bit()};
    }

    uint8_t calculate_granule_size() const
    {
        if (using_ttbr0()) {
            switch (tcr_el1.extract_range(15, 14) /*TG0*/) {
            case 0b00:
                return 12; // log2(4kb)
            case 0b01:
                return 16; // log2(64kb)
            case 0b10:
                return 14; // log2(16kb)
            default:
                throw std::runtime_error("Unexpected TG0 value for TCR_EL1");
            }
        } else {
            switch (tcr_el1.extract_range(31, 30) /*TG1*/) {
            case 0b01:
                return 14; // log2(16kb)
            case 0b10:
                return 12; // log2(4kb)
            case 0b11:
                return 16; // log2(64kb)
            default:
                throw std::runtime_error("Unexpected TG1 value for TCR_EL1");
            }
        }
    }

    uint8_t calculate_input_size() const
    {
        return VIRT_ADDR_BITS
               - (using_ttbr0() ? tcr_el1.extract_range(5, 0)     /*T0SZ*/
                                : tcr_el1.extract_range(21, 16)); /*T1SZ*/
    }

    uint8_t calculate_output_size() const
    {
        assert(granule_size);

        switch (tcr_el1.extract_range(34, 32) /*IPS*/) {
        case 0b000:
            return 32;
        case 0b001:
            return 36;
        case 0b010:
            return 40;
        case 0b011:
            return 42;
        case 0b100:
            return 44;
        case 0b101:
            return 48;
        case 0b110:
            // assumes 52-bit support
            return (granule_size == 16) ? 52 : 48;
        default:
            return 48;
        }
    }
};

class Translation : public TranslationContext
{
public:
    Translation(VirtAddr va, Bitfield tcr_el1, Bitfield ttbr0_el1, Bitfield ttbr1_el1, bool is_instr = false)
        : TranslationContext(va, tcr_el1, is_instr),
          ttbr(using_ttbr0() ? ttbr0_el1 : ttbr1_el1),
          level{starting_level()},
          ptes{0}
    {
    }

    Translation(ProgramCounter pc, Bitfield tcr_el1, Bitfield ttbr0_el1, Bitfield ttbr1_el1)
        : Translation(VirtAddr{pc.value()}, tcr_el1, ttbr0_el1, ttbr1_el1, true)
    {
    }

    const Bitfield ttbr;

    uint8_t stride() const
    {
        assert(granule_size);

        return granule_size - 3;
    }

    // Return the index of this translation into the specified level of page
    // tables
    uint16_t index(uint8_t level) const
    {
        assert(granule_size && input_size);
        assert(level >= starting_level() && level <= 3);

        uint8_t addrselecttop, addrselectbottom;

        if (level == starting_level()) {
            addrselecttop = input_size - 1;
        } else {
            addrselecttop = (4 - level) * stride() + granule_size - 1;
        }
        addrselectbottom = (3 - level) * stride() + granule_size;

        return va.extract_range(addrselecttop, addrselectbottom);
    }

    /*
     * Pseudocode for stepping a translation through its state machine:
     *
     * auto tr = Translation(...);
     *
     * while (!tr.done()) {
     *     PhysAddr pte_addr = tr.next_pte();
     *     uint64_t pte_contents = ... ; // fetch PTE contents via `pte_addr`
     *     tr.pte_fetched(pte_contents);
     * }
     */

    uint8_t current_level() const { return level; }

    bool done() const { return tr_done; }

    bool faulted() const { return tr_faulted; }

    PhysAddr next_pte() const
    {
        assert(!done());
        assert(level <= 3);

        uint64_t baseaddress;

        if (level == starting_level()) {
            /* Calculate the base address for the top-level table from TTBR */
            uint64_t baselowerbound = 3 + input_size - ((3 - level) * stride() + granule_size);
            if (output_size == 52) {
                uint8_t z = (baselowerbound < 6) ? 6 : baselowerbound;
                baseaddress = ttbr.extract_range(5, 2) << 47 | ttbr.extract_range(47, z) << z;
            } else {
                baseaddress = ttbr.extract_range(47, baselowerbound) << baselowerbound;
            }
        } else {
            auto descriptor = Bitfield{ptes[level - 1]};
            baseaddress = descriptor.extract_range(47, granule_size) << granule_size;
            if (output_size == 52) {
                baseaddress |= descriptor.extract_range(15, 12) << 47;
            }
        }

        return PhysAddr{baseaddress | index(level) << 3};
    }

    bool pte_fetched(uint64_t contents)
    {
        assert(level <= 3);

        ptes[level] = contents;

        auto table_desc = TableDescriptor(granule_size, contents);
        auto block_desc = BlockDescriptor(granule_size, level, contents);
        auto page_desc = PageDescriptor(granule_size, contents);

        if (level < 3 && table_desc.valid()) {
            level++;
        } else if (block_desc.valid()) {
            tr_done = true;
            // Fault if hardware is not managing access flags and it isn't set
            if (!update_access_flag() && !block_desc.access_flag()) {
                tr_faulted = true;
            }
        } else if (page_desc.valid()) {
            tr_done = true;
            // Fault if hardware is not managing access flags and it isn't set
            if (!update_access_flag() && !page_desc.access_flag()) {
                tr_faulted = true;
            }
        } else if (!(contents & 0b1)) {
            tr_faulted = true;
            tr_done = true;
        }

        assert(level <= 3);
        return tr_done;
    }

    std::pair<PhysAddr, PageSize> result() const
    {
        assert(done() && !faulted());
        auto pd = PageDescriptor(granule_size, ptes[level]);
        if (pd.valid())
            return pd.result();
        auto bd = BlockDescriptor(granule_size, level, ptes[level]);
        return bd.result();
    }

    PhysAddr result_address() const
    {
        auto [pa, page_size] = result();
        auto untranslated_bits = va.extract_range(page_size.page_bits() - 1, 0);
        return PhysAddr{pa.page_address(page_size).value() | untranslated_bits};
    }

private:
    uint8_t starting_level() const
    {
        assert(granule_size && input_size);

        uint8_t level = 4 - ((input_size - granule_size) / stride());
        assert(level <= 3);
        if ((input_size - granule_size) % stride()) {
            assert(level > 0);
            level--;
        }
        assert(level <= 3);
        return level;
    }

    uint8_t level;
    uint64_t ptes[4];
    bool tr_done{false};
    bool tr_faulted{false};
};

} // namespace ampere::archx

/*
 * vim: sw=4:ts=4:et:si:cindent:cino=\:0,(0,g0,t0:
 */
