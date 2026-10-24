//
// Copyright (c) 2018-2022 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//

// define NO_CONSISTENCY_CHECKS to bypass consistency checks (EL0/EL1 only, no
// asynchronous exceptions, memory accesses to RAM only) to allow an ELF to be
// generated for debugging. The resulting ELF file won't be functional, but you
// get to see the ELF file for debugging.

// #define NO_CONSISTENCY_CHECKS

#include <iostream>
#include <fstream>
#include <list>

#include <elfio/elfio.hpp>

#include "ampere/archx/addr.h"
#include "ampere/archx/sysreg.h"
#include "ampere/archx/translation.h"
#include "encodings.h"
#include "tracer.h"
#include "memory.h"
#include "semihosting.h"

namespace ampere {
namespace tracer {

#define RAM_BEGIN (1ULL << 30)     // 1Gib (QEMU's default beginning of RAM)
#define RAM_END ((4ULL << 30) - 1) // 4Gib (no reason)
struct WalkInfo
{
    WalkInfo(PhysAddr& pa, std::uint64_t content) : pte_addr(pa), pte_content(content) {}
    PhysAddr pte_addr{0};
    std::uint64_t pte_content{0};
};

class ElfSection
{
public:
    // ensure sections start on a 64-byte boundary (this wasn't picked for
    // any particular reason)
    static const size_t ALIGNMENT_BITS = 6;

    ElfSection(ELFIO::elfio* writer) : writer{writer} {}

    void add_data(PhysAddr pa, uint64_t value)
    {
        if (!section_initialized)
            init_section(pa);

        assert(next_addr <= pa);
        while (next_addr < pa) {
            append_data(0);
        }
        append_data(value);
        assert(next_addr == (pa + 8));
    }

    bool initialized() { return section_initialized; }
    PhysAddr end() { return next_addr - 1; }

    void finalize()
    {
        assert(!finalized);
        if (!section_initialized)
            return;

        auto segment = writer->segments.add();
        segment->set_type(PT_LOAD);
        segment->set_virtual_address(init_addr.value());
        segment->set_physical_address(init_addr.value());
        segment->set_flags(PF_R | PF_W | PF_X);
        segment->set_align(1ULL << ALIGNMENT_BITS);
        segment->add_section_index(section->get_index(), section->get_addr_align());

        finalized = true;
    }

    ~ElfSection()
    {
        if (!finalized)
            finalize();

        // reclaiming this memory is the responsibility of the writer,
        // which the caller of this ElfSection() constructor should keep a
        // reference to
        writer = nullptr;
        section = nullptr;
    }

private:
    void init_section(PhysAddr initial_pa)
    {
        assert(!section_initialized);

        init_addr = PhysAddr{initial_pa.extract_range(63, ALIGNMENT_BITS) << ALIGNMENT_BITS};
        next_addr = init_addr;
        char section_name[32];
        snprintf(section_name, sizeof(section_name), "mem_%" PRIx64, init_addr.value());
        section = writer->sections.add(section_name);
        section->set_type(SHT_PROGBITS);
        section->set_flags(SHF_WRITE | SHF_EXECINSTR | SHF_ALLOC);
        section->set_addr_align(0x10);

        section_initialized = true;
    }

    void append_data(uint64_t value)
    {
        assert(!finalized);

        next_addr += 8;
        if (!data_initialized) {
            section->set_data((char*)&value, 8);
            data_initialized = true;
        } else {
            section->append_data((char*)&value, 8);
        }
    }

    ELFIO::elfio* writer;
    bool section_initialized = false;
    bool data_initialized = false;
    bool finalized = false;
    ELFIO::section* section;
    PhysAddr init_addr, next_addr;
};

class InstructionWriter
{
public:
    InstructionWriter(ProgramCounter pc, PhysPage& page, PageContents contents)
        : pc{pc}, pa{page.start}, pc_end{pc + page.size}, pa_end{pa + page.size}, contents{contents}
    {
    }

    enum Condition {
        EQ = 0b0000,
        NE = 0b0001,
        CS_HS = 0b0010,
        CC_LO = 0b0011,
        MI = 0b0100,
        PL = 0b0101,
        VS = 0b0110,
        VC = 0b0111,
        HI = 0b1000,
        LS = 0b1001,
        GE = 0b1010,
        LT = 0b1011,
        GT = 0b1100,
        LE = 0b1101,
        AL = 0b1110,
        NV = 0b1111
    };

    struct Label
    {
        ProgramCounter pc;
        PhysAddr pa;
    };

    // Save the current PC to be used later (typically as the target when
    // creating/updating branch instructions)
    Label make_label() { return {pc, pa}; }

    /*
     * Write an instruction encoding to the 'contents' of memory at the
     * supplied physical address. Do not update the 'write pointers'.
     */
    void write_instruction_at(uint32_t opcode, Label at)
    {
        contents->write(at.pa, sizeof(opcode), (uint8_t*)&opcode, true /*overwrite*/);
    }

    /*
     * Write an instruction encoding to the 'contents' of memory at the current
     * PC/PA 'write pointer' and then advance the PC and PA write pointers by 4
     * bytes.
     */
    Label write_instruction(uint32_t opcode)
    {
        auto addresses = make_label();
        write_instruction_at(opcode, addresses);

        pc += 4;
        pa += 4;

        return addresses;
    }

    int write_data_end_of_page(unsigned regno, ampere::archx::pred_reg_t value)
    {
        // returns the byte offset of the predicate relative to end of page
        const unsigned pred_reg_per_64b = sizeof(std::uint64_t) / sizeof(ampere::archx::pred_reg_t);
        assert(pred_reg_per_64b > 0); // good until VL=512
        typedef union
        {
            std::uint64_t u64;
            ampere::archx::pred_reg_t p[pred_reg_per_64b];
        } pred_union_t;

        //  LSB       MSB
        //  01 23 45 67   < dword
        //  p0 p1 p2 p3   < predicates within dword
        //  p4 p5 p6 p7
        // ...

        std::uint64_t dword_offset = (regno / pred_reg_per_64b); // which dword
        std::uint64_t dword_pos = (regno % pred_reg_per_64b);    // position in dword

        // pa_end is start of NEXT page, so we subtract one more uint64_t
        uint64_t offset = (dword_offset + 1) * sizeof(std::uint64_t);
        PhysAddr pa_data = pa_end - offset;
        if (pa_data.value() % sizeof(std::uint64_t)) {
            throw std::runtime_error(fmt::format("Address not 64b aligned {:016x}", pa_data.value()));
        }

        pred_union_t pu;
        pu.u64 = 0;
        pu.p[dword_pos] = value;

        log(fmt::format("Logging p{}={:04x} to {:016x}\n", regno, value, pa_data.value()).c_str());
        contents->write(pa_data, sizeof(pu.u64), (uint8_t*)&pu.u64);

        int base_reg_offset = -offset + (dword_pos * sizeof(ampere::archx::pred_reg_t));
        return base_reg_offset;
    }

    Label nop() { return write_instruction(0xd503201f); }

    enum MOV_OP : uint8_t {
        //          NOT  = 0b00, //MOVN not implemented
        //          ORR  = 0b01, //ORR (immediate) not implemented
        ZERO = 0b10,
        KEEP = 0b11
    };

    void mov_immediate(MOV_OP op, uint8_t regno, uint16_t value, size_t shift = 0)
    {
        assert(shift % 16 == 0);

        uint32_t encoding = (uint32_t)0b100100101 << 23 | (uint32_t)op << 29 | (uint32_t)(shift >> 4) << 21
                            | // divide shifted amount by 16 first
                            (uint32_t)value << 5 | regno;
        write_instruction(encoding);
    }

    void movk(uint8_t regno, uint16_t value, size_t shift = 0) { mov_immediate(MOV_OP::KEEP, regno, value, shift); }
    void movz(uint8_t regno, uint16_t value, size_t shift = 0) { mov_immediate(MOV_OP::ZERO, regno, value, shift); }

    unsigned construct_predicate_base_register()
    {
        unsigned basereg = 0; // picked arbitrarily
        construct_register(basereg, pc_end.value());
        return basereg;
    }

    void construct_predicate_register(unsigned regno, ampere::archx::pred_reg_t value, unsigned basereg)
    {
        // store predicate value at end of code page insert "LDR
        // pr{regno}, [ data_in_code_page+[end-regno*2] ]" We need a
        // base register with address near end of page. We have a 9b
        // signed imm scaled by size of predicate reg in bytes. Since
        // we are using GPRs, we must load predicates before
        // synthesizing GPR values.
        uint32_t encoding = 0b1000'0101'1000'0000'0000'0000'0000'0000; // LDR (predicate)
        //                                xx xxxx    x xxbb bbb  pppp
        // x= imm9 (split 6b and 3b) (shift left 10 an 16)
        // b= base 5b gpr (shift left 5)
        // p= predicate 4b

        // figure out how much to subtract from the basereg which
        // points to start of next page.
        int imm9 = write_data_end_of_page(regno, value);
        int imm9lo = imm9 & 0x7;
        int imm9hi = (imm9 >> 3) & 0x3F;
        encoding |= (basereg & 0x1F) << 5;
        encoding |= regno & 0xF;
        encoding |= imm9lo << 10;
        encoding |= imm9hi << 16;
        write_instruction(encoding);
    }

    void construct_register(uint8_t regno, uint64_t value)
    {
        movz(regno, value & 0xffff);
        movk(regno, (value >> 16) & 0xffff, 16);
        movk(regno, (value >> 32) & 0xffff, 32);
        movk(regno, (value >> 48) & 0xffff, 48);
    }

    /* GPR to scalar */
    void ins(uint8_t vfp_regno, uint8_t gp_regno, bool upper_half = false)
    {
        uint32_t encoding = (uint32_t)0b0100111000001000000111 << 10 | (uint32_t)(upper_half ? 0b1 : 0b0) << 20
                            | (uint32_t)gp_regno << 5 | (uint32_t)vfp_regno;
        write_instruction(encoding);
    }

    void construct_vfp_register(uint8_t vfp_regno, uint8_t gp_regno, std::array<uint8_t, ampere::archx::MAXVL> value)
    {
        construct_register(gp_regno, *(uint64_t*)value.data());
        ins(vfp_regno, gp_regno, false /* lower bits */);
        construct_register(gp_regno, *(uint64_t*)&value.data()[8]);
        ins(vfp_regno, gp_regno, true /* upper bits */);
    }

    void mrs(SysReg sysreg, uint8_t regno)
    {
        uint32_t encoding = MRS | SYSREG_ENCODING(sysreg) | regno;
        write_instruction(encoding);
    }

    void msr(SysReg sysreg, uint8_t regno)
    {
        uint32_t encoding = MSR | SYSREG_ENCODING(sysreg) | regno;
        write_instruction(encoding);
    }

    void construct_msr(SysReg sysreg, uint64_t value, uint8_t tmp_reg = 29)
    {
        construct_register(tmp_reg, value);
        msr(sysreg, tmp_reg);
    }

    void eret()
    {
        write_instruction(0xd69f03e0); // ERET
    }

    void add_i(uint8_t rn, uint8_t rd, uint16_t imm)
    {
        uint32_t encoding = 0b1001000100u << 22 | (imm & 0xfff) << 10 | (rn & 0x1f) << 5 | (rd & 0x1f);
        write_instruction(encoding);
    }

    void sub(uint8_t rm, uint8_t rn, uint8_t rd)
    {
        // 64-bit variant
        // rd = rn - rm
        uint8_t option = 0b111;
        uint8_t imm3 = 0b000;
        uint32_t encoding = 0b11001011001u << 21 | (rm & 0x1f) << 16 | (option & 0x7) << 13 | (imm3 & 0x7) << 10
                            | (rn & 0x1f) << 5 | (rd & 0x1f);
        write_instruction(encoding);
    }

    void sub_i(uint8_t rn, uint8_t rd, uint16_t imm)
    {
        uint32_t encoding = 0b1101000100u << 22 | (imm & 0xfff) << 10 | (rn & 0x1f) << 5 | (rd & 0x1f);
        write_instruction(encoding);
    }

    Label b(int32_t relative_target, std::optional<Label> at = std::nullopt)
    {
        uint32_t encoding = (uint32_t)0b000101 << 26 | ((relative_target >> 2) & 0x3ffffff);
        if (!at)
            at = make_label();
        write_instruction_at(encoding, *at);
        return *at;
    }
    Label b(Label target, std::optional<Label> at = std::nullopt)
    {
        if (!at)
            at = make_label();
        return b(target.pc - at->pc, at);
    }

    Label b_cond(int32_t relative_target, Condition cond, std::optional<Label> at = std::nullopt)
    {
        uint32_t encoding = (uint32_t)0b01010100 << 24 | ((relative_target >> 2) & 0x7ffff) << 5 | cond;
        if (!at)
            at = make_label();
        write_instruction_at(encoding, *at);
        return *at;
    }
    Label b_cond(Label target, Condition cond, std::optional<Label> at = std::nullopt)
    {
        if (!at)
            at = make_label();
        return b_cond(target.pc - at->pc, cond, at);
    }

    Label cb(int32_t relative_target, uint8_t regno, bool z = false, std::optional<Label> at = std::nullopt)
    {
        uint32_t encoding =
            (uint32_t)0b1011010 << 25 | (z ? 0 : 1) << 24 | ((relative_target >> 2) & 0x7ffff) << 5 | regno;
        if (!at)
            at = make_label();
        write_instruction_at(encoding, *at);
        return *at;
    }
    Label cb(Label target, uint8_t regno, bool z = false, std::optional<Label> at = std::nullopt)
    {
        if (!at)
            at = make_label();
        return cb(target.pc - at->pc, regno, z, at);
    }
    Label cbnz(int32_t relative_target, uint8_t regno, std::optional<Label> at = std::nullopt)
    {
        return cb(relative_target, regno, false, at);
    }
    Label cbz(int32_t relative_target, uint8_t regno, std::optional<Label> at = std::nullopt)
    {
        return cb(relative_target, regno, true, at);
    }
    Label cbnz(Label target, uint8_t regno, std::optional<Label> at = std::nullopt)
    {
        return cb(target, regno, false, at);
    }
    Label cbz(Label target, uint8_t regno, std::optional<Label> at = std::nullopt)
    {
        return cb(target, regno, true, at);
    }

    void ubfm(uint8_t rn, uint8_t rd, uint8_t immr, uint8_t imms)
    {
        uint32_t encoding =
            (uint32_t)0b1101001101 << 22 | (immr & 0x3f) << 16 | (imms & 0x3f) << 10 | (rn & 0x1f) << 5 | (rd & 0x1f);
        write_instruction(encoding);
    }

    void orr(uint8_t rn, uint8_t rd, uint8_t rm)
    {
        uint32_t encoding = (uint32_t)0b10101010000 << 21 | (rm & 0x1f) << 16 | (rn & 0x1f) << 5 | (rm & 0x1f);
        write_instruction(encoding);
    }

    void dsb_sy() { write_instruction(0b11010101000000110011111110011111); }

    void isb() { write_instruction(0b11010101000000110011111111011111); }

    ProgramCounter next_pc() const { return pc; }
    ProgramCounter next_pa() const { return ProgramCounter(pa); }

private:
    ProgramCounter pc;
    PhysAddr pa;
    ProgramCounter pc_end;
    PhysAddr pa_end;
    PageContents contents;
};

struct SysRegAccess
{
    constexpr SysRegAccess(uint64_t committed_instruction, SysReg sysreg, uint8_t gpr, uint64_t value, bool read = true)
        : committed_instruction{committed_instruction}, sysreg{sysreg}, gpr{gpr}, value{value}, read{read}
    {
    }

    const uint64_t committed_instruction;
    const SysReg sysreg;
    const uint8_t gpr;
    const uint64_t value;
    const bool read;
};

class ELFTracer : public Tracer
{
public:
    ELFTracer() = delete;
    ELFTracer(const std::string filename,
              const std::string iss_term_filename,
              const std::string rtl_sysreg_filename,
              const gpr_map_t gprs,
              const vec_map_t vfp_regs,
              const pred_map_t predicates,
              const std::map<archx::SysReg, uint64_t> sysregs,
              const archx::EL el,
              const uint32_t pstate,
              const uint32_t fpsr,
              StateAccessor* stateaccessor,
              bool flat_map,
              uint64_t elf_entry_point,
              const uint64_t arg_section_span = 1024,
              bool debug = false)
        : elf_filename{filename},
          iss_term_filename{iss_term_filename},
          rtl_sysreg_filename{rtl_sysreg_filename},
          flat_map{flat_map},
          entry_point{elf_entry_point},
          registers{gprs},
          vfp_regs{vfp_regs},
          predicates(predicates),
          initial_sysregs{sysregs},
          sysregs{sysregs},
          initial_el{el},
          pstate{pstate},
          fpsr{fpsr},
          stateaccessor{stateaccessor},
          section_span{arg_section_span},
          debug_mode{debug}
    {
        log("Creating ELF tracer\n");
        if (!el_change(el, ContextUpdateType::TRACING))
            throw std::runtime_error(
                "ELFTracer::el_change() in constructor was unsuccessful, see other log messages\n");
    }
    virtual ~ELFTracer() {}

    virtual bool start_measurement() { return true; }
    virtual bool stop_measurement() { return true; }

    // Take all the information gathered and output it into an ELF file, along
    // with a short preamble to initialize the MMU and any other necessary
    // system state
    virtual bool stop_tracing(bool sve_observed)
    {
        if (flat_map) {
            // Ensure WXN is not set so that the write permission does not
            // imply execute-never
            initial_sysregs[SCTLR_EL1] = initial_sysregs[SCTLR_EL1] & ~(1ULL << 19);

            // Make the output physical address 48 bits. This is an attempt to
            // ensure that we can map as many virtual addresses as possible
            // inside the physical address range (so that stack accesses can
            // fit, too)
            initial_sysregs[TCR_EL1] = (initial_sysregs[TCR_EL1] & ~0x700000000) | 0x500000000;

            if (!flat_map_all_pages()) {
                log("Error flat mapping all pages!\n");
                return false;
            }
        }

        auto [entry, preamble_succeeded] = make_preamble(sve_observed);
        if (!preamble_succeeded) {
            log("Error creating preamble\n");
            return false;
        }

        ELFIO::elfio writer;

        // Initialize ELF headers
        writer.create(ELFCLASS64, ELFDATA2LSB); // 64 bit, little-endian
        writer.set_os_abi(ELFOSABI_NONE);       // no ABI specified
        writer.set_type(ET_EXEC);               // executable
        writer.set_machine(EM_AARCH64);         // target ISA is AArch64

        ElfSection section = ElfSection(&writer);
        std::uint64_t sections = 1;
        for (auto& pagecontents : memory) {
            for (auto pa = pagecontents.first.start; pa <= pagecontents.first.end(); pa += 8 /*64 bits*/) {
                const uint64_t value = pagecontents.second->read(pa);

                // Omit zero values; unspecified values are assumed to be zero
                // so let's save space!
                if (value == 0)
                    continue;

                // If the next nonzero address is more than section_span bytes past the
                // previous one, start a new section
                if (section.initialized() && pa > (section.end() + section_span)) {
                    section.finalize();
                    section = ElfSection(&writer);
                    sections++;
                    if (sections >= 65536) {
                        // if we hit this assertion change the
                        // elf-section-span parameter to 1M or larger
                        // to generate fewer but larger sections
                        throw std::runtime_error(
                            "ELFTracer exceeded 64K sections, the ELF section limit. Re-run, increasing "
                            "elf-section-span");
                    }
                }
                section.add_data(pa, value);
            }
        }

        // ensure the last section is properly finalized
        section.finalize();

        writer.set_entry(entry.value());
        writer.save(elf_filename);

        // Write a script to drive the AISLE "iss_terminal" plugin to correct
        // CNTVCT_EL0 values and another file to allow RTL/cosim to do the same
        if (sysreg_reads.size()) {
            std::fstream iss_term_file, rtl_sysreg_file;
            iss_term_file.open(iss_term_filename, std::ios::out);
            rtl_sysreg_file.open(rtl_sysreg_filename, std::ios::out);
            if (!iss_term_file) {
                log("ELFTracer failed to open %s\n", iss_term_filename.c_str());
                return false;
            }
            if (!rtl_sysreg_file) {
                log("ELFTracer failed to open %s\n", rtl_sysreg_filename.c_str());
                return false;
            }

            // Setup the initial breakpoint and resume execution
            iss_term_file << "brkp " << sysreg_reads.at(0).committed_instruction << std::endl;
            iss_term_file << "rsm" << std::endl;

            for (size_t i = 0; i < sysreg_reads.size(); i++) {
                auto read = sysreg_reads.at(i);
                // Move the desired value to the general purpose register the
                // sysreg was read into. Set a breakpoint for the next sysreg
                // read we want to replace, if there is one, and then resume.
                iss_term_file << "mov x" << uint32_t(read.gpr) << ", 0x" << std::hex << read.value << std::dec
                              << std::endl;
                if (i + 1 < sysreg_reads.size())
                    iss_term_file << "brkp " << sysreg_reads.at(i + 1).committed_instruction << std::endl;
                iss_term_file << "rsm" << std::endl;

                if (!(read.sysreg == CNTVCT_EL0)) {
                    log("ELFTracer attempting to write access to unexpected sysreg to RTL cosim injection file %s\n",
                        rtl_sysreg_filename.c_str());
                    return false;
                }
                rtl_sysreg_file << read.committed_instruction << ",CNTVCT_EL0,0x" << std::hex << read.value << std::dec
                                << std::endl;
            }

            iss_term_file.close();
            rtl_sysreg_file.close();
        }

        return true;
    }

    virtual void abort_tracing()
    {
        /* no implementation needed since nothing is written to disk before
         * stop_tracing() */
    }

    // Called when a sysreg we potentially need is seen
    bool sysreg_seen(const archx::SysReg sysreg, uint64_t value, bool is_write)
    {
        // If we have not previously seen the value of this sysreg, write it to
        // the map of sysregs we will use to initialize the ELF state
        auto [it, inserted] = initial_sysregs.emplace(sysreg, value);

        // Unconditionally update the value of our main sysregs map, since we
        // will use that as we trace (i.e. for recording PTEs for translations)
        sysregs[sysreg] = value;

        // If we read a sysreg for the first time this trace, and it has not
        // yet been written during the trace, write the value read to
        // initial_sysregs. Doing so ensures we have the most accurate values
        // for system registers that are not written by software (ELR_EL1, for
        // example), and which are therefore may not be reflected in the system
        // registers passed to ELFTracer's constructor.
        if (is_write) {
            seen_sysregs.insert(sysreg);
        } else if (!seen_sysregs.count(sysreg) && !inserted) {
            seen_sysregs.insert(sysreg);
            initial_sysregs[sysreg] = value;
            inserted = true;
        }

        /*
         * If generating a flat-map, we need to ensure that the
         * translation-related sysregs do not change over the course of our run
         */
        if (flat_map && !inserted && it->second != value) {
            switch (sysreg.addr()) {
            case SCTLR_EL1.addr():
            case TCR_EL1.addr():
            case TTBR0_EL1.addr():
            case TTBR1_EL1.addr():
#if defined(NO_CONSISTENCY_CHECKS)
                log("Warning: Saw multiple values for the same sysreg when attempting to flat-map the address range\n");
#else
                log("Failed to build a flat-mapped ELF because we saw multiple values for the same sysreg\n");
                return false;
#endif
            }
        }

        return true;
    }

    // Called when a sysreg we potentially need is written
    virtual bool sysreg_write(const archx::SysReg sysreg, uint64_t value)
    {
        return sysreg_seen(sysreg, value, true /*write*/);
    }

    // Called when a sysreg we potentially need is read
    virtual bool sysreg_read(const archx::SysReg sysreg, uint64_t value)
    {
        return sysreg_seen(sysreg, value, false /*read*/);
    }

    virtual bool el_change(const archx::EL el, ContextUpdateType update_type)
    {
#if !defined(NO_CONSISTENCY_CHECKS)
        if (el > EL1) {
            log("ELFTracer can only handle EL0 and EL1 code\n");
            return false;
        }
        if (update_type == ContextUpdateType::ASYNC_EXC) {
            log("ELFTracer can only handle synchronous exceptions (not IRQ/FIQ/SError)\n");
            return false;
        }
#endif
        return true;
    }

    virtual bool instruction_exec(const Instruction& instruction, const ProgramCounter next_pc)
    {
        instructions_seen++;
        if (!first_instruction)
            first_instruction = instruction.pc;

        Translation tr = Translation(
            instruction.pc, Bitfield(sysregs[TCR_EL1]), Bitfield(sysregs[TTBR0_EL1]), Bitfield(sysregs[TTBR1_EL1]));
        if (!memory_access(tr,
                           !instruction.pa.has_value(),
                           instruction.pa ? *instruction.pa : 0x0_pa,
                           4 /*4 bytes*/,
                           (const uint8_t*)&instruction.opcode))
            return false;

        // If the instruction being executed is a semihosting HLT instruction,
        // replace it with a no-op in the saved memory image so it does not
        // fault on AISLE/RTL. However, only do the replacement if the contents
        // of the memory image match the HLT encoding (just in case we would
        // see it in self-modifying code, or in a physical page remapped during
        // the snippet).
        if (!tr.faulted()
            && (IS_HLT(instruction.opcode, PERF_SEMIHOSTING_NUM) || IS_HLT(instruction.opcode, TRACE_SEMIHOSTING_NUM)))
        {
            overwrite_if_match(tr, instruction.opcode, 0xd503201f /*nop*/);
        }

        for (const auto& memop : instruction.memops) {
            if (memop.is_io) {
                log("ELF tracer saw IO (non-RAM) memory access to VA 0x%" PRIx64 ", aborting.\n", memop.va.value());
#if !defined(NO_CONSISTENCY_CHECKS)
                return false;
#endif
            }
            Translation tr = Translation(
                memop.va, Bitfield(sysregs[TCR_EL1]), Bitfield(sysregs[TTBR0_EL1]), Bitfield(sysregs[TTBR1_EL1]));
            if (!memory_access(tr, !memop.pa.has_value(), memop.pa ? *memop.pa : 0x0_pa, memop.bytes, memop.value))
                return false;
        }

        // Record PTEs for `DC ZVA` and prefetch instructions. They don't have
        // memops since we record the register values instead of addresses
        // associated with memory accesses, so they will not automatically get
        // their PTEs recorded 'for free' with a call to `memory_access()`
        // above.
        const auto adis_info = adecode(instruction.opcode);
        auto c_maint_type = cache_maint_type(adis_info);
        if (is_zva(c_maint_type)) {
            VirtAddr va = VirtAddr(instruction.source_regs.at(MSR_REG(instruction.opcode)));
            Translation tr =
                Translation(va, Bitfield(sysregs[TCR_EL1]), Bitfield(sysregs[TTBR0_EL1]), Bitfield(sysregs[TTBR1_EL1]));
            record_translation(tr);
        } else if (adis::prefetch(adis_info)) {
            const auto& [ok, va_opt] = prefetch_va("ELFTracer", instruction, adis_info);
            if (!ok) {
                return false; // encountered an error
            }
            if (va_opt.has_value()) {
                Translation tr = Translation(va_opt.value(),
                                             Bitfield(sysregs[TCR_EL1]),
                                             Bitfield(sysregs[TTBR0_EL1]),
                                             Bitfield(sysregs[TTBR1_EL1]));
                record_translation(tr);
            }
        }

        // Because the counter timer can progress at different rates on
        // different platforms relative to instruction execution (which can
        // cause different program behavior for programs which branch based on
        // the value of CNTVCT_EL0), and we need to ensure we are running the
        // same dynamic instruction stream on all platforms for performance
        // validation, record the values read for CNTVCT_EL0 and the committed
        // instruction count at which they were read. We'll use this later to
        // generate collateral for AISLE and RTL cosim.
        if (IS_MRS_REG(instruction.opcode, CNTVCT_EL0)) {
            auto gp_registers = stateaccessor->read_gp_registers();
            uint8_t mrs_reg = MRS_REG(instruction.opcode);
            if (!gp_registers) {
                log("ELFTracer was unable to read general purpose registers to populate sysreg value\n");
                return false;
            }
            sysreg_reads.push_back(SysRegAccess(instructions_seen, CNTVCT_EL0, mrs_reg, gp_registers->at(mrs_reg)));
        }

        // If we encountered previously un-accessed physical pages during the
        // execution of this instruction, fill out the remainder of those pages
        // based on the current state of QEMU's memory.
        if (!newpages.empty()) {
            for (auto& new_page_address : newpages) {
                auto newpage = memory.find(new_page_address);
                populate_physical_page(new_page_address, newpage->second);
            }
            newpages.clear();
        }

        return true;
    }

private:
    // Populate the entire physical page from QEMU's current memory state
    void populate_physical_page(const PhysPage& page_addr, PageContents contents)
    {
        uint8_t buffer[256];
        for (PhysAddr pa = page_addr.start; pa < page_addr.start + page_addr.size; pa += sizeof(buffer)) {
            bool success = stateaccessor->read_memory(pa.value(), buffer, sizeof(buffer));
            if (!success)
                throw std::runtime_error("Failed to read QEMU's memory in ELFTracer::populate_physical_page");

            contents->write(pa, sizeof(buffer), buffer);
        }
    }

    void overwrite_if_match(const archx::Translation& tr, uint32_t match, uint32_t replacement)
    {
        auto [pa, page_size] = tr.result();
        if (flat_map)
            pa = PhysAddr(tr.va.value());
        else
            pa = tr.result_address(); // result_address() needed because result() does not preserve untranslated bits

        assert(!(pa.value() & 0x3)); // logic below assumes aligned accesses (bits 0:1 are not set)

        // Read aligned to 64-bits to ensure we don't read past the end of the
        // page (since our API always reads 64 bits)
        auto aligned_pa = PhysAddr(pa.value() & 0xfffffffffffffff8);

        auto page_address = PhysPage(pa, page_size);
        auto page = memory.find(page_address);
        assert(page != memory.end());

        uint64_t value = page->second->read(aligned_pa);
        if (pa.value() & 0x4) {
            value = value >> 32;
        }
        value = value & 0xffffffff;

        if (value == match)
            page->second->write(pa, 4, (uint8_t*)&replacement, true /*overwrite*/);
    }

    // Save the data associated with this memory access if it is the first to
    // this address.
    void physical_memory_access(
        VirtAddr va, PhysAddr pa, PageSize page_size, size_t bytes, const uint8_t* value, bool force_write = false)
    {
        if (flat_map)
            pa = PhysAddr(va.value());

        // If we have accessed this physical page in any prior instruction, we
        // have already written its contents in full, so we do not need to
        // record the current write. If, however, this *is* the first time we
        // have seen this physical page, write the accesses that we see as we
        // see them, and fill in the rest of the page's data at the end of this
        // instruction. We wait instead of filling the page immediately because
        // for CAS instructions, for example, we do not want to get only the
        // post-execution contents of memory (we want for our memory image to
        // contain the initial contents of memory, that if the CAS succeeded
        // that would not), but because we don't get notified of these accesses
        // until after the instruction is completed executing from QEMU's
        // perspective, we need to first save all of the memory contents
        // recorded with the instruction itself (rather than pulling the whole
        // page from QEMU's representation of memory), or we risk getting the
        // wrong values.

        auto page_address = PhysPage(pa, page_size);
        auto page = memory.find(page_address);
        if (page == memory.end()) {
            // If this is the first time we have seen an access to this
            // physical page, create a new page, write this access to it, add
            // the page to our map of memory, and finally insert it into the
            // list of new pages seen this instruction.
            auto newpage = make_page_contents(page_address);
            newpage->write(pa, bytes, value);
            memory.emplace(page_address, newpage);
            newpages.insert(page_address);
        } else if (force_write) {
            // force_write==true is reserved for cases where we really need to
            // overwrite the existing values here (for example, adding page
            // table entries for the initialization preamble after the
            // execution is finished.
            page->second->write(pa, bytes, value, true /*overwrite*/);
        } else if (newpages.count(page_address)) {
            // If this page is in the list of 'new' physical pages this
            // instruction, write non-overlapping accesses to the page
            page->second->write(pa, bytes, value, false /*do not overwrite*/);
        }
    }

    void record_translation(archx::Translation& translation)
    {
        while (!translation.done()) {
            uint64_t pte;
            PhysAddr pte_addr = translation.next_pte();
            assert(!(pte_addr.value() & 0x7));
            stateaccessor->read_memory(pte_addr.value(), (uint8_t*)(&pte), 8);
            translation.pte_fetched(pte);

            // Don't record page tables if we're going to force a flat mapping
            // anyway, because their addresses in the physical address space can
            // collide with other things in the virtual address space
            if (!flat_map)
                write_descriptor(pte_addr, pte, PageSize(translation.granule_size));
        }
    }

    bool check_translation(archx::Translation& translation, bool faulted, const PhysAddr answer)
    {
        // Check translation for correctness
        if (faulted != translation.faulted()) {
            log("ELF tracer's translation of %s at 0x%" PRIx64 " is%s valid while QEMU's is%s valid\n",
                translation.is_instr ? "instruction" : "data",
                translation.va.value(),
                translation.faulted() ? " not" : "",
                faulted ? " not" : "");
            return false;
        } else if (!faulted) {
            auto pa = translation.result_address();
            if (pa != answer) {
                log("ELF tracer's translation of %s 0x%" PRIx64 " (0x%" PRIx64 ") does not match QEMU's (0x%" PRIx64
                    ")\n",
                    translation.is_instr ? "instruction" : "data",
                    translation.va.value(),
                    pa.value(),
                    answer.value());
                return false;
            }
        }
        return true;
    }

    bool check_record_translation(archx::Translation& translation, bool faulted, const PhysAddr answer)
    {
        record_translation(translation);
#if defined(NO_CONSISTENCY_CHECKS)
        check_translation(translation, faulted, answer);
        return true;
#else
        return check_translation(translation, faulted, answer);
#endif
    }

    bool memory_access(archx::Translation& tr, bool faulted, const PhysAddr answer, size_t bytes, const uint8_t* value)
    {
        if (!check_record_translation(tr, faulted, answer))
            return false;
        if (!tr.faulted()) {
            auto [pa, page_size] = tr.result();
            pa = tr.result_address(); // result_address() needed because result() does not preserve untranslated bits

            PhysPage phys_page = PhysPage(pa, page_size);
            if (pa + bytes - 1 > phys_page.end()) {
                /* We don't know what to do if the upper portion of this access
                 * is not to the same virtual page since we don't have the
                 * virtual-to-physical mapping, so bail. */
                return false;
            }

            physical_memory_access(tr.va, pa, page_size, bytes, value);
        }
        return true;
    }

    void debug_instr_translation(ProgramCounter pc)
    {
        auto tr =
            Translation(pc, Bitfield(sysregs[TCR_EL1]), Bitfield(sysregs[TTBR0_EL1]), Bitfield(sysregs[TTBR1_EL1]));
        log("Translating PC 0x%" PRIx64 "\n", pc.value());

        while (!tr.done()) {
            PhysAddr pte_addr = tr.next_pte();
            log("Looking up level %" PRIu8 " at 0x%" PRIx64 "\n", tr.current_level(), pte_addr.value());
            auto pte = page_descriptors.find(pte_addr);
            if (pte == page_descriptors.end()) {
                log("Could not find descriptor at 0x%" PRIx64 "\n", pte_addr.value());

                return;
            }
            log("Found descriptor 0x%" PRIx64 "\n", pte->second);
            tr.pte_fetched(pte->second);
        }
        return;
    }

    // Always writes a descriptor, even if doing a flat mapping
    void write_descriptor(PhysAddr pa, uint64_t value, PageSize ps, bool force_write = false)
    {
        if (force_write) {
            auto old = page_descriptors.find(pa);
            if (old != page_descriptors.end()) {
                page_descriptors.erase(old);
            }
        }
        auto [it, inserted] = page_descriptors.emplace(pa, value);

        // Add to physical memory, too
        physical_memory_access(VirtAddr(pa.value()), pa, ps, sizeof(value), (uint8_t*)&value, force_write);
#if defined(NO_CONSISTENCY_CHECKS)
        (void)it;
        (void)inserted;
#else
        // Ensure that duplicates are identical if we are going to later create
        // our own flat virtual-to-physical page mapping. Non-identical
        // duplicates implies the OS has changed a VA->PA mapping out on us,
        // and our flat-mapping logic requires that each VA have one and only
        // one PA for the duration of the trace.
        if (flat_map && !inserted && it->second != value) {
            logf("Failed to build ELF because we saw multiple values for the same descriptor");
            exit(0);
        }
#endif
    }

    bool phys_page_unallocated(PhysPage page)
    {
        // Find the first physical page whose beginning address is not
        // before 'pa'
        auto it = memory.lower_bound(page);

        // the first page with a starting address after ours overlaps
        if (it != memory.end() && it->first.overlaps(page)) {
            return false;
        }

        // the page with a starting address before this one overlaps
        auto prev_it = std::prev(it);
        if (prev_it != memory.end() && prev_it->first.overlaps(page)) {
            return false;
        }
        return true;
    }

    std::optional<PhysAddr> unallocated_phys_page(PhysAddr begin, PhysAddr end, PageSize ps)
    {
        PhysAddr pa = begin.page_address(ps);
        if (pa < begin)
            pa += ps;

        while (pa + ps < end) {
            auto page = PhysPage{pa, ps};

            // Find the first physical page whose beginning address is not
            // before 'pa'
            auto it = memory.lower_bound(page);

            // If the first page with a starting address after ours overlaps,
            // keep searching
            if (it != memory.end() && it->first.overlaps(page)) {
                pa = it->first.start + it->first.size;
                continue;
            }

            // If the page with a starting address before this one overlaps,
            // keep searching
            auto prev_it = std::prev(it);
            if (prev_it != memory.end() && prev_it->first.overlaps(page)) {
                pa = prev_it->first.start + prev_it->first.size;
                continue;
            }

            return std::optional{pa};
        }

        return std::nullopt;
    }

    /*
     * The following `add_translation` methods attempt to create translation
     * table entries for a new page using the values of the TCR/TTBR registers
     * at the beginning of the trace. This is primarily useful to create a page
     * on which to place initialization code and data, and is also used when
     * flat-mapping everything.
     */
    bool add_translation(ProgramCounter pc, PhysAddr pa, AccessPermissions ap)
    {
        // Use 'initial_sysregs' because we want to match the translation
        // regime/setup of the code we're returning to
        auto tr = Translation(pc,
                              Bitfield(initial_sysregs[TCR_EL1]),
                              Bitfield(initial_sysregs[TTBR0_EL1]),
                              Bitfield(initial_sysregs[TTBR1_EL1]));
        return add_translation(tr, pa, ap);
    }
    bool add_translation(VirtAddr va, PhysAddr pa, AccessPermissions ap)
    {
        // Use 'initial_sysregs' because we want to match the translation
        // regime/setup of the code we're returning to
        auto tr = Translation(va,
                              Bitfield(initial_sysregs[TCR_EL1]),
                              Bitfield(initial_sysregs[TTBR0_EL1]),
                              Bitfield(initial_sysregs[TTBR1_EL1]));
        return add_translation(tr, pa, ap);
    }

    /*
     * The check_add_translation_impl() method can EITHER check whether it is
     * possible to add a given VA->PA translation to the current page tables
     * without modifying the page tables, or attempt to add such a translation.
     *
     * The reason we have both modes is that attempting to add a translation
     * modifies state, but we might not know until later in the translation
     * walk that we are unable to create a translation for that mapping, at
     * which point it is difficult to properly roll back any previous state
     * changes we have made. It is easier, therefore, to do it in two passes:
     * first check whether adding a given translation would work, and then
     * later actually add it.
     *
     * Note: The translation object must have clean state!
     */
    bool add_translation(Translation& tr, PhysAddr pa, AccessPermissions ap)
    {
        return check_add_translation_impl(tr, pa, ap, true /*really add translations check*/);
    }
    bool can_add_translation(ProgramCounter& pc, PhysAddr pa, AccessPermissions ap)
    {
        auto tr = Translation(pc,
                              Bitfield(initial_sysregs[TCR_EL1]),
                              Bitfield(initial_sysregs[TTBR0_EL1]),
                              Bitfield(initial_sysregs[TTBR1_EL1]));
        return check_add_translation_impl(tr, pa, ap, false /*check*/);
    }
    bool check_add_translation_impl(Translation& tr, PhysAddr pa, AccessPermissions ap, bool add_trans = true)
    {
        auto begin = PhysAddr{RAM_BEGIN};
        auto end = PhysAddr{RAM_END};
        auto page_size = PageSize(tr.granule_size);
        std::list<WalkInfo> walk_info; // list of (pte addr,pte content) for clearing PXN bits

        while (!tr.done()) {
            PhysAddr pte_addr = tr.next_pte();
            auto pte = page_descriptors.find(pte_addr);

            if (pte == page_descriptors.end()) { // not found
                if (tr.current_level() == 3) {
                    auto pd = PageDescriptor(pa,
                                             page_size.page_bits(),
                                             false, /* not execute-never */
                                             false, /* not privileged execute-never */
                                             false, /* not contiguous */
                                             false, /* DBM */
                                             true,  /* not global */
                                             true,  /* set access flag */
                                             INNER_SHAREABLE,
                                             ap,
                                             true /* non-secure */);
                    if (add_trans)
                        write_descriptor(pte_addr, pd.value(), page_size, true /*force write*/);
                    tr.pte_fetched(pd.value());
                } else {
                    auto table_pg_addr = unallocated_phys_page(begin, end, page_size);
                    // If we failed to find any empty page for the page
                    // table, we don't have much of a remedy
                    if (!table_pg_addr) {
                        if (debug_mode)
                            log("Error: Failed to find an unallocated physical page upon which to allocate a new page "
                                "table\n");
                        return false;
                    }

                    auto page_address = PhysPage(*table_pg_addr, page_size);
                    if (add_trans) {
                        auto newpage = make_page_contents(page_address);
                        memory.emplace(page_address, newpage);
                    }

                    auto td = TableDescriptor(*table_pg_addr,
                                              page_size.page_bits(),
                                              false,     /* NSTable */
                                              NO_EFFECT, /* no table access permissions */
                                              false,     /* no execute-never table */
                                              false /* no priviledged execute-never table */);
                    if (add_trans)
                        write_descriptor(pte_addr, td.value(), page_size, true /*force write*/);
                    tr.pte_fetched(td.value());
                }
            } else { // descriptor exists
                assert(tr.current_level() != 3);
                auto td = TableDescriptor(page_size.page_bits(), pte->second);
                // If we found a descriptor during this page table
                // traversal which isn't a valid table descriptor, one of
                // our previous assumptions was incorrect (i.e. perhaps this is
                // a block or page allocation, which would mean we're trying to
                // create a mapping for a VA range which already had one.
                if (!td.valid()) {
                    if (debug_mode)
                        log(fmt::format("Error: Found an invalid table descriptor when we expected one during a page "
                                        "table traversal to add a new translation (PA: {:#x}, value: {:#x})\n",
                                        pte_addr.value(),
                                        pte->second)
                                .c_str());
                    return false;
                }

                // Ensure table descriptor is not execute-never, or other
                // funny permissions/attributes that will prohibit a page
                // mapped downstream from this table descriptor from being
                // an executable instruction page
                const uint64_t limit_xn_error_print = 10;
                if (td.XNTable()) {
                    uxn_errors++;
                    // limit printing of gigabytes of errors when 4K pages are used
                    if (debug_mode && uxn_errors <= limit_xn_error_print) {
                        logf(
                            "Error: Found an UXN (user execute-never) table descriptor"
                            "during traversal for VA=PA {:#x} to create new translation"
                            "(PA: {:#x}, value: {:#x}",
                            pa.value(),
                            pte_addr.value(),
                            pte->second);
                        if (uxn_errors == limit_xn_error_print) {
                            logf("Silencing future UXN errors. See final count below.");
                        }
                    }
                    return false;
                }
                if (td.PXNTable()) {
                    pxn_errors++;
                    if (add_trans) {
                        walk_info.emplace_back(pte_addr, pte->second);
                    }
                }

                tr.pte_fetched(pte->second);
            }

            // Ensure the PTE was inserted successfully
            assert(page_descriptors.find(pte_addr) != page_descriptors.end() || !add_trans);
        } // while

        if (walk_info.size()) {
            // walk list of (pte addr,table descriptor content) and
            // rewrite them, clearing the PXN bits so they can execute
            // at EL1.
            for (auto& rec : walk_info) {
                archx::TableDescriptorAttributes tda(rec.pte_content);
                tda.s.PXNTable = 0;
                auto new_td = TableDescriptor(page_size.page_bits(), tda.value());
                write_descriptor(rec.pte_addr, new_td.value(), page_size, true /* force write */);
            }
        }
        return true;
    }

    // Attempt to new physical instruction page before creating the page/table
    // descriptors to point to it (because that process searches for empty
    // pages, and creating an empty page signifies that this page is used)
    bool make_empty_instruction_page(PhysPage& page_address)
    {
        ProgramCounter pc(page_address.start.value());
        if (!can_add_translation(pc, page_address.start, RO_EL1)) {
            return false;
        }
        auto newpage = make_page_contents(page_address);
        auto [it, successful] = memory.emplace(page_address, newpage);
        if (!successful)
            return false;

        // Allocate page tables to flat-map this page
        if (!add_translation(pc, page_address.start, RO_EL1)) {
            // Delete the physical page we inserted if there was an error
            // setting up the page tables correctly for that location
            memory.erase(page_address);
            return false;
        }
        return true;
    }

    bool test_empty_instruction_page(const PhysPage& page)
    {
        ProgramCounter pc(page.start.value());
        if (can_add_translation(pc, page.start, RO_EL1)) {
            if (memory.count(page) == 0) {
                return true;
            }
        }
        return false;
    }

    std::optional<PhysPage> find_empty_instruction_page(PhysAddr start, PhysAddr end, PageSize ps)
    {
        PhysAddr current_address = start;
        PhysPage page = PhysPage(current_address, ps);
        while (current_address < end) {
            if (test_empty_instruction_page(page)) {
                return page;
            }
            current_address += ps;
            page = PhysPage(current_address, ps);
        }
        return std::nullopt;
    }

    bool flat_map_all_pages()
    {
        for (auto& physpage : memory) {
            if (!add_translation(VirtAddr(physpage.first.start.value()), physpage.first.start, RW_EL01)) {
                return false;
            }
        }
        return true;
    }

    std::pair<PhysAddr, bool> make_preamble(bool sve_observed)
    {
        const uint64_t cptr_el2_fpen = 3ULL << 20; // disable AdvSIMD and SVE trapping
        const uint64_t cptr_el3_ez = 1ULL << 8;    // disable SVE trapping

        if (!first_instruction) {
            log("Error: Didn't find entry point for ELF\n");
            return {0x0_pa, false};
        }
        auto page_size = PageSize(Translation(0x0_pc,
                                              Bitfield(initial_sysregs[TCR_EL1]),
                                              Bitfield(initial_sysregs[TTBR0_EL1]),
                                              Bitfield(initial_sysregs[TTBR1_EL1]))
                                      .granule_size);
        auto page = find_empty_instruction_page(entry_point, entry_point + entry_point_range, page_size);
        if (page.has_value()) {
            bool ok = make_empty_instruction_page(page.value());
            if (ok) {
                logf("preamble page physical address: {:#x}", page->start.value());
            } else {
                logf("preamble search / create failed");
                return {0x0_pa, false};
            }
        } else {
            logf("Error: Couldn't flat-map empty instruction page starting at PC/PA {:#x}", entry_point.value());
            if (uxn_errors || pxn_errors) {
                logf("UXN errors {}, PXN errors {}", uxn_errors, pxn_errors);
            }
            return {0x0_pa, false};
        }

        auto writer = InstructionWriter(ProgramCounter(page->start.value()), *page, memory.at(*page));

        /*
         * Initial MMU setup
         */
        writer.construct_msr(CONTEXTIDR_EL1, initial_sysregs[CONTEXTIDR_EL1]);
        writer.construct_msr(TTBR0_EL1, initial_sysregs[TTBR0_EL1]);
        writer.construct_msr(TTBR1_EL1, initial_sysregs[TTBR1_EL1]);
        writer.construct_msr(TCR_EL1, initial_sysregs[TCR_EL1]);

        // Make all memory attributes as Normal Memory with:
        // * Outer Write-Back Non-transient, Read-Allocate, Write-Allocate
        // * Inner Write-Back Non-transient, Read-Allocate, Write-Allocate
        writer.construct_msr(MAIR_EL1, 0xffffffffffffffff);

        // Enable the MMU at EL0/1
        writer.construct_msr(SCTLR_EL1, initial_sysregs[SCTLR_EL1]);

        // Data and instruction barriers after enabling
        writer.dsb_sy();
        writer.isb();

        /*
         * EL1-accessible sysreg setup
         */

        // Exception introspection info
        writer.construct_msr(FAR_EL1, initial_sysregs[FAR_EL1]);
        writer.construct_msr(ESR_EL1, initial_sysregs[ESR_EL1]);

        // Vector base address register
        writer.construct_msr(VBAR_EL1, initial_sysregs[VBAR_EL1]);

        // Initialize TPIDR and friends, sometimes read by EL0/EL1 code and
        // used as a base pointer
        writer.construct_msr(TPIDR_EL0, initial_sysregs[TPIDR_EL0]);
        writer.construct_msr(TPIDR_EL1, initial_sysregs[TPIDR_EL1]);
        writer.construct_msr(TPIDRRO_EL0, initial_sysregs[TPIDRRO_EL0]);

        // Set CPACR, which controls whether FP is enabled
        writer.construct_msr(CPACR_EL1, initial_sysregs[CPACR_EL1]);

        // Initialize CNTKCTL_EL1 to ensure accesses to CNTVCT_EL0 are treated
        // the same as they were for original run
        writer.construct_msr(CNTKCTL_EL1, initial_sysregs[CNTKCTL_EL1]);

        // Get the right values of the stack pointers - start with the SP_ELx
        // sysreg values, and overlay with GPR value. If SPSel == 1, the
        // current `sp` held in registers[31] refers to the EL's own SP_ELx; if
        // it is 0 it always holds SP_EL0.
        uint64_t sp_el0 = initial_sysregs[SP_EL0];
        uint64_t sp_el1 = initial_sysregs[SP_EL1];
        if (!(pstate & 0b1) || initial_el == EL0) {
            sp_el0 = registers[31];
        } else if (initial_el == EL1) {
            sp_el1 = registers[31];
#if !defined(NO_CONSISTENCY_CHECKS)
        } else {
            throw std::runtime_error("ELFTracer can only handle EL0 and EL1");
#endif
        }

        // Ensure we are using SP_EL0 (regardless of our current EL) and then
        // write it. Note: we initialize SP_EL1 later (it needs to be done
        // differently if we are initializing from EL1 vs. EL2/3).
        writer.construct_msr(SPSel, 0);
        writer.construct_register(0, sp_el0);
        writer.add_i(0, 31, 0); // mov sp, x0 (alias for the add immediate version)

        // Initialize Pointer Authentication keys
        writer.construct_msr(APDAKeyHi_EL1, initial_sysregs[APDAKeyHi_EL1]);
        writer.construct_msr(APDAKeyLo_EL1, initial_sysregs[APDAKeyLo_EL1]);
        writer.construct_msr(APDBKeyHi_EL1, initial_sysregs[APDBKeyHi_EL1]);
        writer.construct_msr(APDBKeyLo_EL1, initial_sysregs[APDBKeyLo_EL1]);
        writer.construct_msr(APGBKeyHi_EL1, initial_sysregs[APGBKeyHi_EL1]);
        writer.construct_msr(APGBKeyLo_EL1, initial_sysregs[APGBKeyLo_EL1]);
        writer.construct_msr(APIAKeyHi_EL1, initial_sysregs[APIAKeyHi_EL1]);
        writer.construct_msr(APIAKeyLo_EL1, initial_sysregs[APIAKeyLo_EL1]);
        writer.construct_msr(APIBKeyHi_EL1, initial_sysregs[APIBKeyHi_EL1]);
        writer.construct_msr(APIBKeyLo_EL1, initial_sysregs[APIBKeyLo_EL1]);

        /*
         * If we are above EL1 (i.e. EL2 or EL3), initialize some additional
         * registers which may be necessary but which cannot be initialized if
         * we are entering from EL1.
         */
        writer.mrs(CurrentEL, 0);           // x0 = CurrentEl
        writer.ubfm(0, 0, 2, 3);            // x0 = (x0 >> 2) & 0x3;
        writer.sub_i(0, 0, 1);              // x0 -= 1
        auto pre_el_sysregs = writer.nop(); // label: pre_el_sysregs
                                            // if x0 == 0, goto el1_sysreg_init (replaced later)

        // Initialize SP_EL1 by writing the sysreg when running at EL2 or EL3
        writer.construct_msr(SP_EL1, sp_el1);

        // (Mostly) initialize HCR_EL2 to 0 to disable stage 2 translation and
        // other EL2 features (i.e. trapping to it). A few bits need to be set
        // to 1:
        //
        // EnSCXT - don't trap SCXTNUM_ELx accesses
        // FIEN - don't trap fault injection registers
        // API - don't trap pointer authentication instructions
        // APK - don't trap accesses to pointer authentication sysregs
        writer.construct_msr(
            HCR_EL2, 0x0 | (1UL << 53) /*EnSCXT*/ | (1UL << 47) /*FIEN*/ | (1UL << 41) /*API*/ | (1UL << 40) /*APK*/);

        // Initialize CNTHCTL_EL2 to ensure accesses to CNTVCT_EL0 and
        // CNTV_CTL_EL0, & co. are not trapped
        writer.construct_msr(CNTHCTL_EL2,
                             0x0 | (1UL << 11) /*EL1PTEN*/ | (1UL << 10) /*EL1PCTEN*/ | (1UL << 9) /*EL0PTEN*/
                                 | (1UL << 8) /*EL0VTEN*/ | (1UL << 1) /*EL0VCTEN*/ | (1UL << 0) /*EL0PCTEN*/);

        // Setup CNTVOFF_EL2 to be the difference between the current value of
        // CNTPCT_EL0 and the desired value of CNTVCT_EL0. This is an attempt
        // at making the virtual timer on AISLE/RTL match what it was on QEMU.
        writer.construct_register(1, initial_sysregs[CNTVCT_EL0]); // x1 = desired CNTVCT_EL0 value
        writer.mrs(CNTPCT_EL0, 2);                                 // x2 = current CNTPCT_EL0 value
        writer.sub(1, 2, 1);                                       // x1 = x2 - x1
        writer.msr(CNTVOFF_EL2, 1);                                // CNTVOFF_EL2 = x1

        auto post_el23_sysregs = writer.nop(); // label: post_el23_sysregs
                                               // goto post_el_sysregs

        /*
         * Do setup that only needs doing when entering at EL1 here
         */
        auto el1_sysreg_init = writer.make_label(); // label: el1_sysreg_init

        // Initialize SP_EL1 by writing directly to 'sp' when running at EL1
        writer.construct_msr(SPSel, 1);
        writer.construct_register(0, sp_el1);
        writer.add_i(0, 31, 0); // mov sp, x0 (alias for the add immediate version)

        auto post_el_sysregs = writer.make_label();     // label: post_el_sysregs
        writer.cbz(el1_sysreg_init, 0, pre_el_sysregs); // insert `cbz x0, el1_sysreg_init` at label pre_el_sysregs
        writer.b(post_el_sysregs, post_el23_sysregs);   // insert `cbz x0, post_el_sysregs` at label post_el23_sysregs

        /*
         * Setup ELR to point to the first instruction we want to ERET to, and
         * SPSR to the saved PSTATE. Restore ELR_ELx and SPSR_ELx for ELs lower
         * than the current one. Branch around the ELs we're not running in.
         */
        writer.construct_register(1, first_instruction->value()); // x1 = entry address
        writer.construct_register(2, pstate);                     // x2 = SPSR

        writer.mrs(CurrentEL, 0); // x0 = CurrentEl
        writer.ubfm(0, 0, 2, 3);  // x0 = (x0 >> 2) & 0x3;

        writer.sub_i(0, 0, 1);             // x0 -= 1
        auto skip_el1_cbnz = writer.nop(); // label: skip_el1_cbnz
                                           // if x0 != 0, goto post_el1_init (replaced later)
        writer.msr(ELR_EL1, 1);            // ELR_EL1 = x1
        writer.msr(SPSR_EL1, 2);           // SPSR_EL1 = x2
        if (sve_observed) {
            writer.construct_msr(ZCR_EL1, initial_sysregs[ZCR_EL1]);
        }
        auto post_el1_init = writer.make_label(); // label: post_el1_init
        writer.sub_i(0, 0, 1);                    // x0 -= 1
        auto skip_el2_cbnz = writer.nop();        // label: skip_el2_cbnz
                                                  // if x0 != 0, goto post_el2_init (replaced later)
        writer.msr(ELR_EL2, 1);                   // ELR_EL2 = x1
        writer.msr(SPSR_EL2, 2);                  // SPSR_EL2 = x2
        writer.construct_msr(ELR_EL1, initial_sysregs[ELR_EL1]);
        writer.construct_msr(SPSR_EL1, initial_sysregs[SPSR_EL1]);

        if (sve_observed) {
            writer.construct_msr(CPTR_EL2, initial_sysregs[CPTR_EL2] | cptr_el2_fpen);
            writer.isb(); // required after modifying CPTR_EL2

            writer.construct_msr(ZCR_EL1, initial_sysregs[ZCR_EL1]);
            writer.construct_msr(ZCR_EL2, initial_sysregs[ZCR_EL1]); // sic tracing at EL0, RTL preamble running at EL3
        }

        auto post_el2_init = writer.make_label(); // label: post_el2_init
        writer.sub_i(0, 0, 1);                    // x0 -= 1
        auto skip_el3_cbnz = writer.nop();        // label: skip_el3_cbnz
                                                  // if x0 != 0, goto post_el3_init (replaced later)
        writer.msr(ELR_EL3, 1);                   // ELR_EL3 = x1
        writer.msr(SPSR_EL3, 2);                  // SPSR_EL3 = x2
        // Initialize SCR_EL3, ensuring we are in nonsecure (NS) mode and not
        // trapping things unnecessarily. Initialize to 0 except for the
        // following:
        //
        // ADEn - don't trap accesses to ACCDATA_EL1
        // EnAS0 - don't trap execution of ST64BV0
        // ATA - don't trap accesses to Allocation Tags
        // EnSCXT - don't trap SCZTNUM_ELx accesses
        // FIEN - don't trap fault injection registers
        // API - don't trap pointer authentication instructions
        // APK - don't trap accesses to pointer authentication sysregs
        // RW - the next lower level (typically EL2) is AArch64
        // SMD - disable secure monitor calls (there's nothing there anyway)
        // NS - run in nonsecure mode
        writer.construct_msr(SCR_EL3,
                             0x0 | (1UL << 37) /*ADEn*/ | (1UL << 36) /*EnAS0*/ | (1UL << 26)  /*ATA*/
                                 | (1UL << 25) /*EnSCXT*/ | (1UL << 21) /*FIEN*/ | (1UL << 17) /*API*/
                                 | (1UL << 16) /*APK*/ | (1UL << 10) /*RW*/ | (1UL << 7) /*SMD*/ | (1UL << 0) /*NS*/);
        writer.construct_msr(ELR_EL2, initial_sysregs[ELR_EL2]);
        writer.construct_msr(SPSR_EL2, initial_sysregs[SPSR_EL2]);
        writer.construct_msr(ELR_EL1, initial_sysregs[ELR_EL1]);
        writer.construct_msr(SPSR_EL1, initial_sysregs[SPSR_EL1]);

        if (sve_observed) {
            writer.construct_msr(CPTR_EL2, initial_sysregs[CPTR_EL2] | cptr_el2_fpen);
            writer.construct_msr(CPTR_EL3, initial_sysregs[CPTR_EL3] | cptr_el3_ez);
            writer.isb(); // required after modifying CPTR_EL2 & CPTR_EL3
            writer.construct_msr(ZCR_EL1, initial_sysregs[ZCR_EL1]);
            writer.construct_msr(ZCR_EL2, initial_sysregs[ZCR_EL1]); // sic tracing at EL0, RTL preamble running at EL3
            writer.construct_msr(ZCR_EL3, initial_sysregs[ZCR_EL1]); // sic tracing at EL0, RTL preamble running at EL3
        }

        auto post_el3_init = writer.make_label(); // label: post_el3_init

        writer.cbnz(post_el1_init, 0, skip_el1_cbnz); // insert `cbnz x0, post_el1_init` at label skip_el1_cbnz
        writer.cbnz(post_el2_init, 0, skip_el2_cbnz); // insert `cbnz x0, post_el2_init` at label skip_el2_cbnz
        writer.cbnz(post_el3_init, 0, skip_el3_cbnz); // insert `cbnz x0, post_el3_init` at label skip_el3_cbnz

        /*
         * Initialize general purpose, floating point, and SVE predicate
         * registers
         */

        // VFP registers
        writer.construct_msr(FPSR, fpsr);
        writer.construct_msr(FPCR, initial_sysregs[FPCR]);
        for (unsigned v = 0; v < 32; v++) {
            writer.construct_vfp_register(v, 0, vfp_regs[v]);
        }

        // if SVE is being used, create the predicate registers.
        if (sve_observed) {
            // We use a basereg gpr so we must load predicates before
            // we load the gprs. Assumes available space near end of code page.
            unsigned basereg = writer.construct_predicate_base_register();
            for (unsigned r = 0; r < 16; r++) {
                writer.construct_predicate_register(r, predicates[r], basereg);
            }
        }

        // General purpose registers, including LR
        for (unsigned r = 0; r < 31; r++) {
            writer.construct_register(r, registers[r]);
        }

        /*
         * Finally, ERET!
         */

        writer.eret();

        return {page->start, true};
    }

    const std::string elf_filename;
    const std::string iss_term_filename;
    const std::string rtl_sysreg_filename;
    const bool flat_map;
    PhysAddr entry_point;
    uint64_t entry_point_range = 0x1000000000; // 64Gb
    uint64_t instructions_seen{0};
    std::optional<ProgramCounter> first_instruction = std::nullopt;
    gpr_map_t registers;
    vec_map_t vfp_regs;
    pred_map_t predicates;
    std::map<SysReg, uint64_t> initial_sysregs;
    std::set<SysReg> seen_sysregs;
    std::map<SysReg, uint64_t> sysregs;
    std::vector<SysRegAccess> sysreg_reads;
    archx::EL initial_el;
    uint32_t pstate, fpsr;
    StateAccessor* stateaccessor;
    // TODO perhaps this could be simplified so that `page_descriptors` wasn't
    // a partial duplicate of `memory`
    std::map<PhysAddr, uint64_t> page_descriptors;
    // TODO if user program uses larger pages than the current leaf
    // granule, this memory data structure will not find them. We need
    // to fracture large pages to the some 4k-like granule.
    std::map<PhysPage, PageContents> memory;
    std::set<PhysPage> newpages; // physical pages accessed for first time this instruction
    uint64_t section_span;
    bool debug_mode;
    // count errors when trying to find page for preamble
    uint64_t uxn_errors{0};
    uint64_t pxn_errors{0};
};

} // namespace tracer
} // namespace ampere

/*
 * vim: sw=4:ts=4:et:si:cindent:cino=\:0,(0,g0,t0:
 */
