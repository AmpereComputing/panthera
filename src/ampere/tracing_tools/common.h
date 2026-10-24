//
// Copyright (c) 2020-2024 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//

#pragma once

#include <cstdint>
#include <iostream>
#include <map>
#include <set>
#include <stdexcept>
#include <optional>

#include <boost/property_tree/ptree.hpp>
#include <boost/property_tree/xml_parser.hpp>
#include <fmt/format.h>

extern "C" {
#include <qemu-plugin.h>
}

#include "ampere/archx/el.h"
#include "ampere/archx/pred_reg.h"
#include "ampere/archx/vector.h"
#include "ampere/archx/sysreg.h"

namespace ampere::tracer {
using namespace archx;

typedef std::map<unsigned, uint64_t> gpr_map_t;
typedef std::map<unsigned, ampere::archx::pred_reg_t> pred_map_t;
// using same map for SVE and AdvSIMD registers while only supporing VL=128
typedef std::map<unsigned, std::array<uint8_t, ampere::archx::MAXVL>> vec_map_t;

void log(const char* str, ...);
#define logf(format_str, ...)                                                                                     \
    log(fmt::format(                                                                                              \
            "{}:{}:{}:\n\t{}\n", __FILE__, __LINE__, __PRETTY_FUNCTION__, fmt::format(format_str, ##__VA_ARGS__)) \
            .c_str())

bool qemu_plugin_read_reg64(uint64_t* dest, int reg);
bool qemu_plugin_read_reg32(uint32_t* dest, int reg);

// reg should be 0...15. This function adds appropriate offset to get
// to QEMU's GDB register name.
void read_pred_from_qemu(ampere::archx::preg_t& dest, unsigned regid, unsigned first_predicate_idx);
void read_zreg_from_qemu(ampere::archx::vector_t& dest, unsigned regid, unsigned first_zreg_idx);

#define THROW(fmtstr, ...) \
    throw std::runtime_error(fmt::format("{}:{}: " fmtstr "\n", __FILE__, __LINE__, ##__VA_ARGS__))

const std::set<SysReg> required_tracing_sysregs = {
    CONTEXTIDR_EL1,
    VBAR_EL1,
    SCTLR_EL1,
    TCR_EL1,
    TTBR0_EL1,
    TTBR1_EL1,
    TPIDRRO_EL0,
    TPIDR_EL0,
    TPIDR_EL1,
    CPACR_EL1,
    FPCR,
    ELR_EL1,
    SP_EL0,
    SP_EL1,
    FAR_EL1,
    ESR_EL1,
    SPSR_EL1,
    CNTKCTL_EL1,
};
const std::set<SysReg> optional_tracing_sysregs = {
    VBAR_EL2,      VBAR_EL3,        SCTLR_EL2,     SCTLR_EL3,     TCR_EL2,       TCR_EL3,
    VTCR_EL2,      SCXTNUM_EL0,     SCXTNUM_EL1,   SCXTNUM_EL2,   SCXTNUM_EL3,   TTBR0_EL2,
    TTBR0_EL3,     TTBR1_EL2,       VTTBR_EL2,     TPIDR_EL2,     TPIDR_EL3,     CPTR_EL2,
    CPTR_EL3,      ELR_EL2,         SP_EL2,        CNTHCTL_EL2,   ZCR_EL1,       ZCR_EL2,
    ZCR_EL3,       ID_AA64ZFR0_EL1, APDAKeyHi_EL1, APDAKeyLo_EL1, APDBKeyHi_EL1, APDBKeyLo_EL1,
    APGBKeyHi_EL1, APGBKeyLo_EL1,   APIAKeyHi_EL1, APIAKeyLo_EL1, APIBKeyHi_EL1, APIBKeyLo_EL1,
};

class MemoryAccessor
{
public:
    /*
     * Attempt to read a region of physical memory beginning at physical
     * address `pa` and extending for `len` bytes into a buffer starting at
     * `result`, returning true if the memory read was successful.
     */
    virtual bool read_memory(uint64_t pa, uint8_t* result, size_t len) = 0;
};

class StateAccessor : public MemoryAccessor
{
public:
    virtual std::optional<gpr_map_t> read_gp_registers() = 0;

    // FIXME: this will work for SVE VL=128, but for SVE VL>128, we'll
    // need something else.
    virtual std::optional<vec_map_t> read_vfp_registers() = 0;
    virtual std::optional<pred_map_t> read_pred_registers() = 0;
    virtual std::optional<std::map<SysReg, uint64_t>> read_sysregs() = 0;
    virtual std::optional<uint64_t> read_sysreg(SysReg sysreg) = 0;
    virtual std::optional<uint64_t> read_sp(const archx::EL el, bool spsel) = 0;
    virtual std::optional<archx::EL> read_el() = 0;
    virtual std::optional<uint32_t> read_pstate() = 0;
    virtual std::optional<uint32_t> read_fpsr() = 0;
    virtual ampere::archx::vector_t read_zreg(unsigned r) = 0;
    virtual ampere::archx::preg_t read_pred_register(unsigned r) = 0;
};

class QEMUMemoryAccessor : public MemoryAccessor
{
public:
    QEMUMemoryAccessor() = default;

    bool read_memory(uint64_t pa, uint8_t* result, size_t len) override
    {
        return qemu_plugin_read_phys_memory(pa, result, len, 0 /* ARMASIdx_NS */) == 0;
    }
};

class QEMUStateAccessor : public StateAccessor, public QEMUMemoryAccessor
{
public:
    QEMUStateAccessor()
    {
        build_sysreg_index_map();
        find_vector_register_names();
        find_core_register_names();
    }

    bool read_memory(uint64_t pa, uint8_t* result, size_t len) override
    {
        return QEMUMemoryAccessor::read_memory(pa, result, len);
    }

    std::optional<gpr_map_t> read_gp_registers() override
    {
        gpr_map_t gprs;

        for (unsigned r = 0; r <= 32; r++) {
            uint64_t val;
            // 30 == LR
            // 31 == SP_EL0
            // 32 == PC
            if (!qemu_plugin_read_reg64(&val, r)) {
                log(fmt::format("Failed to read gpr x{} from QEMU\n", r).c_str());
                exit(0);
            }
            gprs.emplace(r, val);
        }
        return gprs;
    }

    ampere::archx::vector_t read_zreg(unsigned r) override
    {
        ampere::archx::vector_t zreg;
        read_zreg_from_qemu(zreg, r, first_vector_reg_idx);
        return zreg;
    }

    ampere::archx::preg_t read_pred_register(unsigned r) override
    {
        ampere::archx::preg_t pval;
        read_pred_from_qemu(pval, r, first_pred_reg_idx);
        return pval;
    }

    std::optional<pred_map_t> read_pred_registers() override
    {
        pred_map_t pmap;

        for (unsigned r = 0; r < ampere::archx::MAXVL; r++) {
            ampere::archx::preg_t pval = read_pred_register(r);
            pmap.emplace(r, pval.p);
        }
        return pmap;
    }

    std::optional<vec_map_t> read_vfp_registers() override
    {
        vec_map_t vfp_regs;
        GByteArray* regval;
        unsigned read_bytes;

        regval = g_byte_array_new();

        // AdvSIMD(VFP) regs v0-v31 (and SVE lower 128b)
        for (unsigned v = 0; v < 32; v++) {
            std::array<uint8_t, ampere::archx::MAXVL> value;
            g_byte_array_set_size(regval, 0);
            /* SVE and AdvSIMD/VFP start at same offset */
            read_bytes = qemu_plugin_read_register(regval, v + first_vector_reg_idx);
            assert(read_bytes == regval->len);
            if (read_bytes != regval->len || regval->len < ampere::archx::MAXVL) {
                // Note, we can expect to read up to 256 bytes here because
                // QEMU stores them using the maximum bit width of SVE
                // internally, 2048 bits.
                log("Failed to read VFP register v%" PRIu8 " when starting tracing\n", v);
                g_byte_array_free(regval, true);
                return std::nullopt;
            }
            // TODO We ignore the higher bits of these registers, assuming SVE
            // is not implemented. We should likely make sure that is true or
            // store more bits.
            for (size_t i = 0; i < ampere::archx::MAXVL; i++)
                value.data()[i] = regval->data[i];
            vfp_regs.emplace(v, value);
        }
        g_byte_array_free(regval, true);
        return vfp_regs;
    }

    std::optional<std::map<SysReg, uint64_t>> read_sysregs() override
    {
        std::map<SysReg, uint64_t> sysreg_values;
        for (const auto& [sysreg, index] : sysreg_index_map) {
            uint64_t val;
            if (!qemu_plugin_read_reg64(&val, index)) {
                log(fmt::format("Failed to read sysreg {} from QEMU\n", sysreg.name).c_str());
                exit(0);
            }
            sysreg_values.emplace(sysreg, val);
        }
        return sysreg_values;
    }

    std::optional<uint64_t> read_sysreg(SysReg sysreg) override
    {
        auto sr_idx = sysreg_index_map.find(sysreg);

        if (sr_idx != sysreg_index_map.end()) {
            uint64_t val;
            if (!qemu_plugin_read_reg64(&val, sr_idx->second)) {
                log(fmt::format("Failed to read sysreg {} from QEMU\n", sysreg.name).c_str());
                exit(0);
            }

            return val;
        }

        return std::nullopt;
    }

    std::optional<uint64_t> read_sp(archx::EL el, bool spsel) override
    {
        if (spsel) {
            switch (el) {
            case EL0:
                return this->read_sysreg(SP_EL0);
            case EL1:
                return this->read_sysreg(SP_EL1);
            case EL2:
                return this->read_sysreg(SP_EL2);
            case EL3: // EL3 not currently supported
                log("Error tried to read SP from EL3 (currently unsupported)\n");
                exit(0);
            }
        } else {
            return this->read_sysreg(SP_EL0);
        }

        return std::nullopt;
    }

    std::optional<archx::EL> read_el() override
    {
        uint32_t pstate;
        if (!qemu_plugin_read_reg32(&pstate, cpsr_reg_idx)) {
            log("Failed to read PSTATE to get current EL\n");
            exit(0);
        }
        uint32_t current_el = (pstate >> 2) & 0x3;
        return (archx::EL)current_el;
    }

    std::optional<uint32_t> read_pstate() override
    {
        uint32_t pstate;

        if (!qemu_plugin_read_reg32(&pstate, cpsr_reg_idx)) {
            log("Failed to read PSTATE 'register' when starting tracing.\n");
            return std::nullopt;
        }

        return pstate;
    }

    std::optional<uint32_t> read_fpsr() override
    {
        uint32_t fpsr;

        if (!qemu_plugin_read_reg32(&fpsr, fpsr_reg_idx)) {
            log("Failed to read FPSR 'register' when starting tracing.\n");
            return std::nullopt;
        }

        return fpsr;
    }

    virtual ~QEMUStateAccessor() = default;

    unsigned get_first_vector_reg_index() { return first_vector_reg_idx; }

private:
    void build_sysreg_index_map()
    {
        // create a set with all sysregs (required or optional)
        std::set<SysReg> tracing_sysregs(required_tracing_sysregs);
        tracing_sysregs.insert(optional_tracing_sysregs.begin(), optional_tracing_sysregs.end());

        const char* xml = qemu_plugin_read_gdb_xml("system-registers.xml");
        if (xml) {
            boost::property_tree::ptree pt;
            auto str = std::stringstream(xml);
            boost::property_tree::xml_parser::read_xml(str, pt, boost::property_tree::xml_parser::trim_whitespace);
            auto elements = pt.get_child("feature");
            for (auto& element : elements) {
                if (element.first == "reg") {
                    auto xml_sysreg = element.second;
                    auto xml_name = xml_sysreg.get<std::string>("<xmlattr>.name");
                    for (auto& tracing_sysreg : tracing_sysregs) {
                        if (xml_name == tracing_sysreg.name) {
                            size_t regnum = xml_sysreg.get<size_t>("<xmlattr>.regnum");
                            size_t bitsize = xml_sysreg.get<size_t>("<xmlattr>.bitsize");
                            // We assume elsewhere that all sysreg accesses are 64-bit
                            if (bitsize == 64)
                                sysreg_index_map.emplace(tracing_sysreg, regnum);
                        }
                    }
                }
            }
        }

        // Ensure we found indices for all the sysregs we need to report
        for (auto& tracing_sysreg : required_tracing_sysregs) {
            if (sysreg_index_map.count(tracing_sysreg) != 1) {
                log(fmt::format("Failed to find mapping in QEMU's GDB XML for sysreg {}\n", tracing_sysreg.name)
                        .c_str());
                exit(0);
            }
        }
    }

    std::map<SysReg, size_t> sysreg_index_map;

    // safe defaults when XML not available.
    unsigned first_vector_reg_idx{34};
    unsigned sve_vector_bits{128};
    unsigned first_pred_reg_idx{34 + 34};
    unsigned fpsr_reg_idx{66};
    unsigned fpcr_reg_idx{67};
    unsigned ffr_reg_idx{84};

    // CPSR does not exist in aarch64, however QEMU still provides a CSPR-like register and we use it.
    unsigned cpsr_reg_idx{33};

    void find_vector_register_names()
    {
        // If one specifies "-cpu max " or "-cpu max,sve128=on" (or
        // some variant with a different SVE vector length) that turns
        // on SVE when invoking QEMU, sve-registers.xml will be
        // used. Otherwise aarch64-fpu.xml is used as a fallback.
        const char* xml = qemu_plugin_read_gdb_xml("sve-registers.xml");
        if (!xml) {
            xml = qemu_plugin_read_gdb_xml("aarch64-fpu.xml");
        }
        if (!xml) {
            log(fmt::format("\n\nWARNING: No SVE or AdvSIMD XML found. Using default values that *hopefully* work.\n\n")
                    .c_str());
        }
        if (xml) {
            boost::property_tree::ptree pt;
            auto str = std::stringstream(xml);
            boost::property_tree::xml_parser::read_xml(str, pt, boost::property_tree::xml_parser::trim_whitespace);
            auto elements = pt.get_child("feature");
            unsigned last_regidx = 0;
            for (auto& element : elements) {
                if (element.first == "reg") {
                    // pick off SVE regs (z0...z31, p0...p15, ffr) or AdvSIMD regs (v0...v31), and fpcr, fscr
                    auto xml_reg = element.second;
                    auto xml_name = xml_reg.get<std::string>("<xmlattr>.name");
                    unsigned regidx = xml_reg.get<unsigned>("<xmlattr>.regnum", last_regidx + 1);
                    unsigned bitsize = xml_reg.get<unsigned>("<xmlattr>.bitsize");
                    last_regidx = regidx;
                    // code assumes vector and predicate regs are sequentially indexed (and checks)
                    if (xml_name == "v0") {
                        first_vector_reg_idx = regidx;
                    } else if (xml_name != "vg"
                               && (xml_name[0] == 'v' || xml_name[0] == 'z')) { // check sequential register assumption
                        unsigned reg_num = std::stol(xml_name.substr(1));
                        if (first_vector_reg_idx + reg_num != regidx) {
                            THROW("Assumption about sequentially numbered vector registers violated: {}", xml_name);
                        }
                    } else if (xml_name == "z0") {
                        first_vector_reg_idx = regidx;
                        sve_vector_bits = bitsize;
                    } else if (xml_name == "p0") {
                        first_pred_reg_idx = regidx;
                    } else if (xml_name[0] == 'p') { // check sequential register assumption
                        unsigned reg_num = std::stol(xml_name.substr(1));
                        if (first_pred_reg_idx + reg_num != regidx) {
                            THROW("Assumption about sequentially numbered predicate registers violated: {}", xml_name);
                        }
                    } else if (xml_name == "ffr") {
                        ffr_reg_idx = regidx;
                    } else if (xml_name == "fpsr") {
                        fpsr_reg_idx = regidx;
                    } else if (xml_name == "fpcr") {
                        fpcr_reg_idx = regidx;
                    }
                }
            }
        } // xml
    }

    void find_core_register_names()
    {
        const char* xml_fn = "aarch64-core.xml";
        const char* xml = qemu_plugin_read_gdb_xml(xml_fn);
        if (!xml) {
            THROW("Cannot read {}", xml_fn);
        }
        boost::property_tree::ptree pt;
        auto str = std::stringstream(xml);
        boost::property_tree::xml_parser::read_xml(str, pt, boost::property_tree::xml_parser::trim_whitespace);
        auto elements = pt.get_child("feature");
        unsigned last_regidx = 0;
        for (auto& element : elements) {
            if (element.first == "reg") {
                auto xml_reg = element.second;
                auto xml_name = xml_reg.get<std::string>("<xmlattr>.name");
                // this core XML does not have explicit reg index ... so we start from 0
                unsigned regidx = last_regidx;
                last_regidx++;
                if (xml_name == "CPSR") {
                    cpsr_reg_idx = regidx;
                    break;
                }
            }
        }
    }
};

} // namespace ampere::tracer

/*
 * vim: sw=4:ts=4:et:si:cindent:cino=\:0,(0,g0,t0:
 */
