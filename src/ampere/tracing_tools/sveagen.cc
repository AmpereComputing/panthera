//
// Copyright (c) 2022 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//

#include <stdexcept>
#include <optional>
#include <fmt/format.h>
extern "C" {
#include <qemu-plugin.h>
}

#include "ampere/instruction_stream/instruction_stream.h"
#include "ampere/archx/prefetch_address.h"
#include "ampere/archx/translation.h"
#include "ampere/archx/addr.h"
#include "ampere/sc/report.h"
#include "tracer.h"
#include "common.h"
#include "ampere/adis/src/adis.h"
#include "sveagen.h"

extern std::uint32_t sve_agen_debug;

#define CLOG(level, ...)               \
    do {                               \
        if (sve_agen_debug >= level) { \
            logf(__VA_ARGS__);         \
        }                              \
    } while (0)

#define TRC_CHK(x)                            \
    do {                                      \
        if ((x) == 0) {                       \
            logf("assertion failed: {}", #x); \
            exit(1);                          \
        }                                     \
    } while (0)

using namespace ampere;
using namespace tracer;

namespace ampere::sveagen {

enum struct MemopType : unsigned {
    Invalid,
    Load,
    Store,
    Prefetch,
    LoadPredicate,
    StorePredicate,
    LoadVectorUnpredicated,
    StoreVectorUnpredicated,
    Gather,
    Scatter,
    GatherPrefetch,
    Last
};

static const char* memop_type_str(MemopType m)
{
    static const char* memop_strings[] = {"invalid",
                                          "ld",
                                          "st",
                                          "prf",
                                          "ld-pred",
                                          "st-pred",
                                          "ldvec-unpred",
                                          "stvec-unpred",
                                          "gather",
                                          "scatter",
                                          "gather-prf"};

    if (m < MemopType::Last)
        return memop_strings[static_cast<std::size_t>(m)];
    THROW("Invalid memop type, integer = {}", static_cast<unsigned>(m));
}

static std::optional<ampere::archx::PhysAddr> v2p(std::uint64_t va)
{
    std::uint64_t phys_addr = 0;
    if (!qemu_plugin_virt_to_phys(va, &phys_addr)) {
        logf("v2p failed for {:016x} -> {:016x}\n", va, phys_addr);
        return std::nullopt;
    } else {
        CLOG(5, "v2p  {:016x} -> {:016x}\n", va, phys_addr);
    }
    return ampere::archx::PhysAddr(phys_addr);
}

static const unsigned max_regids = 2;
struct reg_imm_info_t
{
    reg_imm_info_t()
        : regcount(0), has_simm(false), has_uimm(false), s64(0), u64(0), preg{0}, preg_id(0), saw_predicate(false)
    {
    }

    std::uint64_t regvals[max_regids];
    unsigned regcount;
    bool has_simm;
    bool has_uimm;
    std::int64_t s64;
    std::uint64_t u64;

    ampere::archx::vector_t zreg;
    ampere::archx::preg_t preg;
    unsigned preg_id;
    bool saw_predicate;

    ampere::archx::pred_reg_t get_preg() const { return preg.p; }
};

std::optional<unsigned> governing_predicate(uint32_t encoding, const adis::encoding_enum_t adis_info)
{
    for (adis::vdata_iter_t iter(encoding, adis_info); !iter.done(); iter.advance()) {
        adis::adis_vdata_element_t elem = iter.get();
        if (adis::adis_vdata_element_get_enum(elem) == adis::ADIS_VDATA_PREDREG_GOVERNING) {
            return adis::adis_vdata_element_get_id(elem);
        }
    } /* for */
    return std::nullopt;
}

static void reg_imm_info(tracer::QEMUStateAccessor* state_accessor,
                         const tracer::Instruction& inst,
                         const adis::encoding_enum_t adis_info,
                         MemopType memop_type,
                         reg_imm_info_t& result)
{
    // walk the ADIS operands and pluck out the XREGs indices, extract
    // their values from QEMU, and also record the SIMM/UIMM values,
    // writing them to "result". We also want the last ZREG for scatters/gathers
    const bool sparse =
        memop_type == MemopType::Gather || memop_type == MemopType::Scatter || memop_type == MemopType::GatherPrefetch;

    std::optional<unsigned> last_zreg_id; // gather/scatter index vector reg id
    for (adis::vdata_iter_t iter(inst.opcode, adis_info); !iter.done(); iter.advance()) {
        adis::adis_vdata_element_t elem = iter.get();
        switch (adis::adis_vdata_element_get_enum(elem)) {
        case adis::ADIS_VDATA_ZREG:
            if (sparse) {
                last_zreg_id = adis::adis_vdata_element_get_id(elem);
            }
            break;
        case adis::ADIS_VDATA_PREDREG_GOVERNING:
        case adis::ADIS_VDATA_PREDREG: {
            unsigned regid = adis::adis_vdata_element_get_id(elem);
            result.preg = state_accessor->read_pred_register(regid);
            result.preg_id = regid;
            result.saw_predicate = true;
            CLOG(9, "predval p{} = {:04x}\n", regid, result.get_preg());
            break;
        }
        case adis::ADIS_VDATA_WZR:
        case adis::ADIS_VDATA_XZR: {
            if (result.regcount >= max_regids) {
                THROW("Too many regids for {:08x} {}", inst.opcode, adis::encoding_name(adis_info));
            }
            result.regvals[result.regcount] = 0;
            CLOG(10, "regid xzr/wzr = 0\n");
            result.regcount++;
            break;
        }
        case adis::ADIS_VDATA_WSP: {
            const unsigned regid = 31;
            std::uint32_t u32 = 0;
            if (result.regcount >= max_regids) {
                THROW("Too many regids for {:08x} {}", inst.opcode, adis::encoding_name(adis_info));
            }
            if (!qemu_plugin_read_reg32(&u32, regid)) {
                THROW("Failed to read register W{}", regid);
            }
            result.regvals[result.regcount] = adis::sign_extend32(u32);
            CLOG(10, "regid wsp {} = {:016x}\n", regid, result.regvals[result.regcount]);
            result.regcount++;
            break;
        }
        case adis::ADIS_VDATA_SP: {
            const unsigned regid = 31;
            std::uint64_t u64 = 0;
            if (result.regcount >= max_regids) {
                THROW("Too many regids for {:08x} {}", inst.opcode, adis::encoding_name(adis_info));
            }
            if (!qemu_plugin_read_reg64(&u64, regid)) {
                THROW("Failed to read register X{}", regid);
            }
            result.regvals[result.regcount] = u64;
            CLOG(10, "regid sp {} = {:016x}\n", regid, result.regvals[result.regcount]);
            result.regcount++;
            break;
        }
        case adis::ADIS_VDATA_WREG: {
            unsigned regid = adis::adis_vdata_element_get_id(elem);
            std::uint32_t u32 = 0;
            if (result.regcount >= max_regids) {
                THROW("Too many regids for {:08x} {}", inst.opcode, adis::encoding_name(adis_info));
            }
            if (regid == 31) { //  WZR.  sometimes ADIS does not tag WZR as WZR
                u32 = 0;
            } else if (!qemu_plugin_read_reg32(&u32, regid)) {
                THROW("Failed to read register X{}", regid);
            }
            result.regvals[result.regcount] = adis::sign_extend32(u32);
            CLOG(10, "regid {} = {:016x}\n", regid, u32);
            result.regcount++;
            break;
        }
        case adis::ADIS_VDATA_XREG: {
            unsigned regid = adis::adis_vdata_element_get_id(elem);
            std::uint64_t u64 = 0;
            if (result.regcount >= max_regids) {
                THROW("Too many regids for {:08x} {}", inst.opcode, adis::encoding_name(adis_info));
            }
            if (regid == 31) { //  XZR.  sometimes ADIS does not tag XZR as XZR
                u64 = 0;
            } else if (!qemu_plugin_read_reg64(&u64, regid)) {
                THROW("Failed to read register X{}", regid);
            }
            result.regvals[result.regcount] = u64;
            CLOG(10, "regid {} = {:016x}\n", regid, u64);
            result.regcount++;
            break;
        }
        case adis::ADIS_VDATA_IMM: {
            result.u64 = adis::adis_vdata_element_get_u32(elem);
            result.has_uimm = true;
            CLOG(10, "u64 {}\n", result.u64);
            break;
        }
        case adis::ADIS_VDATA_SIMM: {
            result.s64 = adis::adis_vdata_element_get_i32(elem);
            result.has_simm = true;
            CLOG(10, "s64 {}\n", result.s64);
            break;
        }
        default:
            break;
        } // switch
    } // for

    if (sparse) {
        if (!last_zreg_id.has_value()) {
            THROW("Did not find zreg for spase op {:08x} {}", inst.opcode, adis::encoding_name(adis_info));
        }
        // read zreg from qemu and write it to result.zreg
        result.zreg = state_accessor->read_zreg(last_zreg_id.value());
        CLOG(10,
             "zreg z{} = {:08x} {:08x} {:08x} {:08x}\n",
             last_zreg_id.value(),
             result.zreg.u32[3],
             result.zreg.u32[2],
             result.zreg.u32[1],
             result.zreg.u32[0]);
    }
}

static void fill_memop_data(tracer::MemOp& memop)
{
    if (memop.bytes > sizeof(memop.value)) {
        log("SVE address generation: Unexpected memory access of > 128 bytes");
        exit(0);
    }
    if (!memop.is_io && qemu_plugin_read_phys_memory(memop.pa->value(), memop.value, memop.bytes, 0 /* ARMASIdx_NS */))
    {
        log("SVE address generation: Failed to read memory backing memory access at PA: 0x%" PRIx64 ", size %u, %s\n",
            memop.pa->value(),
            memop.bytes,
            memop.is_store ? "store" : "load");
        exit(0);
    }
}

static void record_memop_check_split_sub(adis::encoding_enum_t adis_info,
                                         tracer::Instruction& inst,
                                         std::uint64_t vaddr,
                                         unsigned size, // bytes
                                         MemopType memop_type,
                                         bool predicated_off)
{
    const bool is_io = false;
    auto paddr = v2p(vaddr);
    if (!paddr.has_value()) {
        logf("v2p fail BAILING sve{} {:016x}: {:08x} {}\n",
             memop_type_str(memop_type),
             inst.pc.value(),
             inst.opcode,
             adis::encoding_name(adis_info));
        // i THINK when v2p fails, this is a page fault. The tracer
        // appears to look for ld/st instr without any MemOp()s set in
        // it to detect faults. So we just bail when this occurs.
        return;
    }
    // FIXME: what is the page size???? assuming 4k.
    if (size == 0) {
        logf("error: zero size:  sve{} {:08x} {}\n",
             memop_type_str(memop_type),
             inst.opcode,
             adis::encoding_name(adis_info));
    }

    auto paddr2 = v2p(vaddr + size - 1); // last byte
    if (!paddr2.has_value()) {
        logf(
            "v2p fail info sve{} {:08x} {}\n", memop_type_str(memop_type), inst.opcode, adis::encoding_name(adis_info));
    }
    bool store = memop_type == MemopType::Store || memop_type == MemopType::Scatter;
    if (!paddr.has_value() || !paddr2.has_value()) {
        auto memop = tracer::MemOp(vaddr, 0, size, store, is_io, predicated_off);
        fill_memop_data(memop);
        inst.memops.emplace_back(std::move(memop));
        return;
    } else if (paddr.value().same_page_4k(paddr2.value())) {
        auto memop = tracer::MemOp(vaddr, paddr.value().value(), size, store, is_io, predicated_off);
        fill_memop_data(memop);
        inst.memops.emplace_back(std::move(memop));
        return;
    }
    // page split
    const std::uint64_t rest = (vaddr + size) % 4096;
    const std::uint64_t first = size - rest;
    if (rest >= size || first >= size)
        THROW("page split mishandled 1st chunk size: {} 2nd chunk size: {}", first, rest);

    auto memop1 = tracer::MemOp(vaddr, paddr.value().value(), first, store, is_io, predicated_off);
    auto memop2 =
        tracer::MemOp(vaddr + first, paddr2.value().page_address_4k().value(), rest, store, is_io, predicated_off);
    fill_memop_data(memop1);
    fill_memop_data(memop2);
    inst.memops.emplace_back(std::move(memop1));
    inst.memops.emplace_back(std::move(memop2));
    logf("page split size {} = {}+{}\n", size, first, rest);
}

static void record_memop_check_split(adis::encoding_enum_t adis_info,
                                     tracer::Instruction& inst,
                                     std::uint64_t vaddr,
                                     unsigned size, // bytes
                                     MemopType memop_type,
                                     bool predicated_off)
{
    // For SVE memops, we break them down into multiple chunks of at
    // most 16 bytes (maxvl).  If <=16 bytes, we execute this once,
    // which is the normal case. For {ld,st}{2,3,4} we'll emit one
    // memop per 16B chunk.
    for (unsigned chunk = 0; chunk < size; chunk += ampere::archx::MAXVL) {
        unsigned chunk_size = std::min<unsigned>(ampere::archx::MAXVL, size);
        record_memop_check_split_sub(adis_info, inst, vaddr + chunk, chunk_size, memop_type, predicated_off);
    }
}

static MemopType find_memop_type(adis::encoding_enum_t adis_info)
{
    if (adis_info == adis::ENC_ldr_p_bi_)
        return MemopType::LoadPredicate;
    if (adis_info == adis::ENC_str_p_bi_)
        return MemopType::StorePredicate;
    if (adis_info == adis::ENC_ldr_z_bi_)
        return MemopType::LoadVectorUnpredicated;
    if (adis_info == adis::ENC_str_z_bi_)
        return MemopType::StoreVectorUnpredicated;
    if (adis::load(adis_info))
        return MemopType::Load;
    if (adis::store(adis_info))
        return MemopType::Store;
    if (adis::prefetch(adis_info))
        return MemopType::Prefetch;
    if (adis::gather(adis_info))
        return MemopType::Gather;
    if (adis::scatter(adis_info))
        return MemopType::Scatter;
    if (adis::gather_prefetch(adis_info))
        return MemopType::GatherPrefetch;
    return MemopType::Invalid;
}

[[maybe_unused]] static void record_src_predicate_value(tracer::Instruction& inst,
                                                        uint8_t pred_reg_idx,
                                                        ampere::archx::preg_t pval)
{
    inst.source_pred_regs.emplace(pred_reg_idx, pval.p);
}

void handle_sve_memops(tracer::QEMUStateAccessor* state_accessor, tracer::Instruction& inst)
{
    // figure out addressing mode  // ADIS
    // get memsize, elem size, nregs, imm, scale, etc.  // ADIS
    // figure out what gprs are needed    // ADIS
    // figure out what predicate is needed // ADIS
    // get gpr registers
    // get predicate register from QEMU
    // compute VA
    // get PA from qemu (or two if crosses pages)
    // record info in the xInstruction (VA, PA+, len, read/write)

    // prefetches treated reads. tracer::Memop() only makes a
    // boolean ld-vs-store distinction currently.

    if (!state_accessor) {
        THROW("state_accessor not initialized\n");
    }
    const auto adis_info = inst.adis_info;
    const std::uint32_t itext = inst.opcode;

    if (adis::sve_family(adis_info) && adis::memop(adis_info)) {
        const MemopType memop_type = find_memop_type(adis_info);
        CLOG(1,
             "IREC sve  pc: {:016x} op: {:08x} memtyp: {:12} enc: {:35s} {}\n",
             inst.pc.value(),
             itext,
             memop_type_str(memop_type),
             adis::encoding_name(adis_info),
             adis::disasm(itext, adis_info, inst.pc.value()));
        if (memop_type == MemopType::Invalid)
            THROW("Unhandled memop for {}, itext={:08x}", adis::encoding_name(adis_info), itext);
        reg_imm_info_t ii;
        reg_imm_info(state_accessor, inst, adis_info, memop_type, /*output*/ ii);
        const bool reg_reg = adis::sve_agen_reg_reg(adis_info);
        const bool reg_imm = adis::sve_agen_reg_imm(adis_info);
        if (!reg_reg && !reg_imm) {
            logf("sveagen unhandled addressing mode {:08x} {}\n", itext, adis::encoding_name(adis_info));
        }
        switch (memop_type) {
        case MemopType::LoadPredicate: // unpredicated memops
        case MemopType::StorePredicate:
        case MemopType::LoadVectorUnpredicated:
        case MemopType::StoreVectorUnpredicated:
            // QEMU already knows how to generate addresses
            // for unpredicated LDR/STR predicate and
            // unpredicated LDR/STR vector accesses so we do
            // not need to do so here.
            return;
        case MemopType::Load: // predicated vectors
        case MemopType::Store:
        case MemopType::Prefetch: {
            const unsigned nregs = adis::nregs(adis_info);
            unsigned esize_bits = adis::memop_esize(adis_info);
            unsigned msize_bits = adis::memop_msize(adis_info);

            CLOG(9,
                 "IREC sve {} {:016x}: {:08x} nregs={} esize_bits={} msize_bits={} preg={:04x} {}\n",
                 memop_type_str(memop_type),
                 inst.pc.value(),
                 itext,
                 nregs,
                 esize_bits,
                 msize_bits,
                 ii.get_preg(),
                 adis::encoding_name(adis_info));
            if (esize_bits == 0) {
                unsigned size_field = (itext >> 21) & 3; // bits 22:21
                esize_bits = 8 << size_field;
                CLOG(7, "RR dynamic esize {}\n", esize_bits);
            }
            if (msize_bits == 0) {
                msize_bits = esize_bits;
                CLOG(7, "RR dynamic msize {}\n", msize_bits);
            }

            const bool pg_all_zero = ampere::archx::pred_all_zero(ii.get_preg(), esize_bits);
            if (pg_all_zero) {
                CLOG(7, "the relevant governing predicate bits are all zero!\n");
            }
            unsigned nbytes; // number of bytes in memory
            if (nregs > 1)
                nbytes = ampere::archx::MAXVL * nregs;
            else
                nbytes = ampere::archx::MAXVL_BITS / esize_bits * msize_bits / 8; // nelem * mbytes

            if (reg_reg) {
                if (ii.regcount != max_regids)
                    THROW("NEED {} REGS for {:08x} {}", max_regids, itext, adis::encoding_name(adis_info));
                std::uint64_t scale = msize_bits / 8; // cvt bits to bytes
                std::uint64_t vaddr = ii.regvals[0] + (ii.regvals[1] * scale);

                CLOG(7,
                     "RR Address computed {:016x}+({:016x}*{}) = {:016x} nbytes={} bytes\n",
                     ii.regvals[0],
                     ii.regvals[1],
                     scale,
                     vaddr,
                     nbytes);
                record_memop_check_split(adis_info, inst, vaddr, nbytes, memop_type, pg_all_zero);
            } else { // reg_imm
                if (ii.regcount != 1 && (ii.has_uimm || ii.has_simm))
                    THROW("NEED 1 reg and 1 imm for {:08x} {}", itext, adis::encoding_name(adis_info));

                std::uint64_t scale = nbytes;
                std::uint64_t vaddr = ii.regvals[0];
                if (ii.has_uimm) {
                    vaddr += (ii.u64 * scale);
                    CLOG(7,
                         "RIu Address computed {:016x}+{}*{} = {:016x} nbytes={}\n",
                         ii.regvals[0],
                         ii.u64,
                         scale,
                         vaddr,
                         nbytes);
                } else if (ii.has_simm) {
                    vaddr += (ii.s64 * scale);
                    CLOG(7,
                         "RIs Address computed {:016x}+{}*{} = {:016x} nbytes={}\n",
                         ii.regvals[0],
                         ii.s64,
                         scale,
                         vaddr,
                         nbytes);
                }

                record_memop_check_split(adis_info, inst, vaddr, nbytes, memop_type, pg_all_zero);
            }
            TRC_CHK(ii.saw_predicate);
            return;
        }
        case MemopType::Gather:
        case MemopType::Scatter:
        case MemopType::GatherPrefetch: {
            const bool dsz_indx = adis::sparse_dsz_index(adis_info);
            const bool packed = adis::sparse_packed(adis_info);
            const unsigned memops = dsz_indx ? 2 : 4;
            const unsigned mbytes = adis::memop_msize(adis_info) / 8;
            bool pg_bit = false;
            CLOG(1,
                 "GREC sparse dsz_indx: {} packed: {} memops: {} mbytes: {} preg: {:04x}\n",
                 dsz_indx,
                 packed,
                 memops,
                 mbytes,
                 ii.get_preg());

            for (unsigned i = 0; i < memops; i++) {
                std::uint64_t offset;
                if (dsz_indx) {              // u64 indices
                    offset = ii.zreg.u64[i]; // always unsigned
                    pg_bit = ampere::archx::get_pred_bit64(ii.get_preg(), i);
                } else if (!packed) { // unpacked 32b indices
                    std::uint32_t uva = ii.zreg.u32[2 * i];
                    bool signed_var = adis::field_xs_22_1(itext);
                    if (signed_var)
                        offset = adis::sign_extend32(uva);
                    else
                        offset = uva;
                    pg_bit = ampere::archx::get_pred_bit64(ii.get_preg(), i);
                } else { // packed 32b indices
                    std::uint32_t uva = ii.zreg.u32[i];
                    bool signed_var = adis::field_xs_22_1(itext);
                    if (signed_var)
                        offset = adis::sign_extend32(uva);
                    else
                        offset = uva;
                    pg_bit = ampere::archx::get_pred_bit32(ii.get_preg(), i);
                }
                std::uint64_t vaddr;
                if (reg_reg) {
                    const bool scaled = !adis::sparse_unscaled(adis_info);
                    std::uint64_t scale = scaled ? mbytes : 1;
                    vaddr = ii.regvals[0] + offset * scale;
                    CLOG(1,
                         "sparse rr address computed {:016x}+({:016x}*{}) = {:016x} size={} bytes pg_bit={}\n",
                         ii.regvals[0],
                         offset,
                         scale,
                         vaddr,
                         mbytes,
                         pg_bit);
                } else { // reg_imm
                    vaddr = offset + ii.u64 * mbytes;
                    CLOG(1,
                         "sparse ri address computed {:016x}+({:016x}*{}) = {:016x} size={} bytes pg_bit={}\n",
                         offset,
                         ii.u64,
                         mbytes,
                         vaddr,
                         mbytes,
                         pg_bit);
                }
                record_memop_check_split(adis_info, inst, vaddr, mbytes, memop_type, pg_bit == 0);
            } // for
            TRC_CHK(ii.saw_predicate);
            return;
        } // sparse case
        default: // not reached
            break;
        } // switch
    } // if sve and memop
    return;
} // handle_sve_memops()

} // namespace ampere::sveagen

