//
// Copyright (c) 2019 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//

/*
 * Completeness:
 *
 * This implementation is far from complete. It is missing (at the very least)
 * - ASID/VMID tracking
 * - TLBI maintenance operations
 * - EL3, EL3 translations (assumes EL1 or EL0)
 * - stage2 translations
 * - handling the many disable/trap bits
 * - secure vs. nonsecure handling
 * - address tagging
 * - access permissions (including hierarchical)
 * - detection of many faulting conditions
 * - endianness of table entries
 */

#include <inttypes.h>
extern "C" {
#include <qemu-plugin.h>
}
#include "ampere/archx/page_size.h"
#include "tlb.h"

#define BITS(val, start, end) ((val >> start) & (~0ull >> (64 - (end - start + 1))))
#define TCR_T0SZ(tcr) BITS(tcr, 0, 5)
#define TCR_TG0(tcr) BITS(tcr, 14, 15)
#define TCR_T1SZ(tcr) BITS(tcr, 16, 21)
#define TCR_TG1(tcr) BITS(tcr, 30, 31)
#define TCR_IPS(tcr) BITS(tcr, 32, 34)
#define TCR_TBI0(tcr) BITS(tcr, 37, 37)
#define TCR_TBI1(tcr) BITS(tcr, 38, 38)
#define TCR_TBID0(tcr) BITS(tcr, 51, 51)
#define TCR_TBID1(tcr) BITS(tcr, 52, 52)

namespace ampere {
namespace tlb {

using namespace archx;

void TLBTracker::write_sysreg(SysReg reg, uint64_t value)
{
    if (writing) {
        assert(writer);
        is::SysReg sysreg;
        sysreg.set_type(is::SysReg_Type_WRITE);
        sysreg.set_op0(reg.op0);
        sysreg.set_op1(reg.op1);
        sysreg.set_crn(reg.crn);
        sysreg.set_crm(reg.crm);
        sysreg.set_op2(reg.op2);
        sysreg.set_value(value);
        writer->write(sysreg);
    }
}

/*
 * Query our descriptor cache for a descriptor at the given physical address,
 * accessing main memory if it is not present.
 */
uint64_t TLBTracker::fetch_descriptor(uint64_t pa)
{
    assert(memory);

    uint64_t descriptor;
    auto desc = descriptors.find(pa);
    if (desc != descriptors.end()) {
        return desc->second;
    }

    memory->read_memory(pa, (uint8_t *)&descriptor, 8);
    return descriptor;
}

/*
 * Attempt to cache the 64-bit descriptor at the given physical address,
 * emitting a Memory event to the instruction stream if it was not already
 * present in the cache.
 */
void TLBTracker::cache_descriptor(uint64_t pa, uint64_t val)
{
    assert(writing && writer);

    auto result = descriptors.emplace(pa, val);

    if (result.second) {
        is::Memory memory;
        memory.mutable_address()->set_pa(pa);
        memory.set_value((char *)&val, 8 /*length in bytes*/);
        writer->write(memory);
    }
}

/*
 * Clear the internal cache of descriptors, ensuring that the memory for all
 * descriptors accessed by page table walks after this point are emitted to the
 * memory stream again, even if they've already been seen.
 */
void TLBTracker::clear_descriptor_cache()
{
    descriptors.clear();
}

/*
 * Translate the supplied virtual address to a physical address, recording any
 * stages of the translation walk which have not previously been seen to the
 * instruction stream, and recording those accesses internally.
 */
std::optional<Translation> TLBTracker::translate(uint64_t va, bool is_instr)
{
    uint8_t top_bit, input_size, output_size, stride, level, granule_size = 12 /*4kb*/;
    uint64_t ttbr;

    assert(writing && writer);

    assert(el == 1 || el == 0); // Only support EL0 or EL1 translations

    /* Determine which bit selects TTBR0 vs. TTBR1. We're assuming we support
     * PAC (pointer authentication/address tagging) */
    bool tbi = BITS(va, 55, 55) ? TCR_TBI1(tcr_el1) : TCR_TBI0(tcr_el1);
    bool tbid = BITS(va, 55, 55) ? TCR_TBID1(tcr_el1) : TCR_TBID0(tcr_el1);
    top_bit = (tbi && (!tbid || !is_instr)) ? 55 : 63;

    if (va & (1ull << top_bit)) {
        ttbr = ttbr1_el1;
        input_size = 64 - TCR_T1SZ(tcr_el1);
        switch (TCR_TG1(tcr_el1)) {
        case 0b01:
            granule_size = 14; // log2(16kb)
            break;
        case 0b10:
            granule_size = 12; // log2(4kb)
            break;
        case 0b11:
            granule_size = 16; // log2(64kb)
            break;
        }
    } else {
        ttbr = ttbr0_el1;
        input_size = 64 - TCR_T0SZ(tcr_el1);
        switch (TCR_TG0(tcr_el1)) {
        case 0b00:
            granule_size = 12; // log2(4kb)
            break;
        case 0b01:
            granule_size = 16; // log2(64kb)
            break;
        case 0b10:
            granule_size = 14; // log2(16kb)
            break;
        }
    }

    switch (TCR_IPS(tcr_el1)) {
    case 0b000:
        output_size = 32;
        break;
    case 0b001:
        output_size = 36;
        break;
    case 0b010:
        output_size = 40;
        break;
    case 0b011:
        output_size = 42;
        break;
    case 0b100:
        output_size = 44;
        break;
    case 0b101:
        output_size = 48;
        break;
    case 0b110:
        // assumes 52-bit support
        output_size = (granule_size == 16) ? 52 : 48;
        break;
    default:
        output_size = 48;
        break;
    }

    stride = granule_size - 3;

    /* Determine the starting level of the walk */
    level = 4 - ((input_size - granule_size) / stride);
    if ((input_size - granule_size) % stride) {
        level--;
    }

    /* Determine the address of the original table */
    uint64_t baseaddress;
    uint64_t baselowerbound = 3 + input_size - ((3 - level) * stride + granule_size);
    if (output_size == 52) {
        uint8_t z = (baselowerbound < 6) ? 6 : baselowerbound;
        baseaddress = BITS(ttbr, 2, 5) << 47 | BITS(ttbr, z, 47) << z;
    } else {
        baseaddress = BITS(ttbr, baselowerbound, 47) << baselowerbound;
    }

    uint8_t addrselectbottom, addrselecttop = input_size - 1;
    uint64_t descriptor, index, descriptor_address, output_address;

    while (true) {
        addrselectbottom = (3 - level) * stride + granule_size;
        index = BITS(va, addrselectbottom, addrselecttop) << 3;

        /* Get contents of descriptor for the current level of the walk */
        descriptor_address = baseaddress | index;
        descriptor = fetch_descriptor(descriptor_address);

        /* Detect invalid/empty descriptors */
        if (!BITS(descriptor, 0, 0) || (level == 3 && BITS(descriptor, 0, 1) == 0b10)) {
            return std::nullopt;
        }

        /* Because this descriptor was valid, cache it (emitting it to the
         * instruction stream if it wasn't already in the cache) */
        cache_descriptor(descriptor_address, descriptor);

        if (BITS(descriptor, 0, 1) == 0b01 || level == 3) {
            /* This is a block or page descriptor, so fall out of the loop */
            break;
        } else {
            /* This descriptor points to another table. Calculate the base
             * address of the next table  */
            baseaddress = BITS(descriptor, granule_size, 47) << granule_size;
            if (output_size == 52) {
                baseaddress |= BITS(descriptor, 12, 15) << 47;
            }
        }

        level++;
        addrselecttop = addrselectbottom - 1;
    }

    output_address = BITS(descriptor, addrselectbottom, 47) << addrselectbottom | BITS(va, 0, addrselectbottom - 1);
    if (output_size == 52) {
        output_address |= BITS(descriptor, 12, 15) << 47;
    }

    return std::optional<Translation>{Translation(output_address, addrselectbottom)};
}

} // namespace tlb
} // namespace ampere

/*
 * vim: sw=4:ts=4:et:si:cindent:cino=\:0,(0,g0,t0:
 */
