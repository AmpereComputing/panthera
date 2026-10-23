//
// Copyright (c) 2020 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//
#include <cstdint>
#include <fcntl.h>
#include <iostream>
#include <sys/types.h>
#include <unistd.h>
#include <deque>

#include "ampere/archx/instruction_is.h"
#include "ampere/archx/translation.h"
#include "ampere/archx/sysreg.h"
#include "ampere/instruction_stream/instruction_stream.h"
#include "chip-decoder-map.h"

typedef uint_fast32_t uint_t;

// #define USING_CAPSTONE
#if defined(USING_CAPSTONE)
#include <capstone/capstone.h>
#include <capstone/platform.h>
static csh disasm_handle;
#else
#define USING_ADIS
#include "ampere/adis/src/adis.h"
#endif

using namespace ampere;
using namespace ampere::is;

void disasm_init()
{
#if defined(USING_CAPSTONE)
    // set up capstone for disassembly
    cs_err err = cs_open(CS_ARCH_ARM64, CS_MODE_ARM, &disasm_handle);
    if (err) {
        printf("Failed on cs_open() with error returned: %u\n", err);
        exit(1);
    }
    cs_option(disasm_handle, CS_OPT_DETAIL, CS_OPT_ON);
#endif
}

std::string disasm(std::uint32_t bytes, std::uint64_t pc)
{
#if defined(USING_CAPSTONE)
    size_t count(0);
    cs_insn* insn;
    char inst_dis[256];

    // place the instruction bytes into the format required by capstone
    uint8_t byte_array[4];
    byte_array[0] = bytes;
    byte_array[1] = bytes >> 8;
    byte_array[2] = bytes >> 16;
    byte_array[3] = bytes >> 24;

    count = cs_disasm(disasm_handle, byte_array, 4, pc, 0, &insn);

    if (count) {
        // format the disasm and write it to the record
        sprintf(inst_dis, "%-5s %s", insn[0].mnemonic, insn[0].op_str);

        // free memory allocated by cs_disasm()
        cs_free(insn, count);

    } else {
        sprintf(inst_dis, "[undefined opcode: %x]", bytes);
    }

    return std::string(inst_dis);
#else
    auto id = adis::decode(bytes);
    return adis::disasm(bytes, id, pc);
#endif
}

void usage(char* progn)
{
    std::cout << progn << "[-q] instruction-stream-name\n";
    std::cout << "  -q = quiet output\n";
    exit(EXIT_FAILURE);
}
int main(int argc, char** argv)
{
    std::string filename;
    bool verbose = true;
    std::deque<std::string> cmdline(argv + 1, argv + argc);
    while (!cmdline.empty()) {
        auto arg = cmdline.front();
        cmdline.pop_front();
        if (arg == "--help" || arg == "-h") {
            usage(argv[0]);
        } else if (arg == "-q") {
            verbose = false;
        } else if (filename.empty()) {
            filename = arg;
        } else {
            usage(argv[0]);
        }
    }

    std::vector<std::string> decoder_names = is_tools::get_decoders("max");
    archx::Instruction::set_decoders(decoder_names);

    disasm_init();

    uint64_t instruction_count{0};
    uint64_t loads_missing_addresses{0};
    uint64_t stores_missing_addresses{0};
    uint64_t bad_translations{0};
    uint64_t prefetches_missing_registers{0};
    uint64_t cache_maint_missing_registers{0};
    uint64_t tlbi_missing_registers{0};
    uint64_t div_sqrt_missing_registers{0};

    archx::ArchState state;
    InstructionStreamReader reader(filename);
    Event event;

    while (reader.read(event)) {
        if (event.has_instruction()) {
            instruction_count++;
            Instruction instr = event.instruction();
            archx::InstructionIS is_instr(instr, instruction_count, state);

            if ((is_instr.is_load() || is_instr.is_store()) && !is_instr.is_barrier() && !is_instr.is_clrex()) {
                bool bad_addresses = instr.memop_size() < 1;
                for (int i = 0; !bad_addresses && i < instr.memop_size(); i++) {
                    if (instr.memop(i).va() == 0 || instr.memop(i).pa() == 0) {
                        bad_addresses = true;
                    }
                }
                if (bad_addresses) {
                    // We "allow" some types of instructions to have invalid
                    // address information, as long as they contain register
                    // information which we can use to calculate what we need
                    // to know about them. This applies to software prefetches,
                    // TLBIs and cache maintenance instructions.
                    if (is_instr.is_prefetch()) {
                        if (!instr.register__size())
                            prefetches_missing_registers++;
                    } else if (is_instr.is_tlbi()) {
                        if (!instr.register__size())
                            tlbi_missing_registers++;
                    } else if (is_instr.is_cache_maint()) {
                        auto cache_maint_type = is_instr.cache_maint_type();
                        if (!instr.register__size() && cache_maint_type != adis::sys_alias_t::IC_IALLU
                            && cache_maint_type != adis::sys_alias_t::IC_IALLUIS)
                            cache_maint_missing_registers++;
                    } else if (!instr.has_exception() && !IS_MRS_REG(instr.opcode(), archx::RNDR)
                               && !IS_MRS_REG(instr.opcode(), archx::RNDRRS))
                    {
                        // If the instruction with bad address information was
                        // none of the special cases, is not an exception, and
                        // isn't a special random "load", this is an error that
                        // we need to report.
                        if (verbose)
                            std::cout << "Error: Instruction " << instruction_count << " (encoding: 0x" << std::hex
                                      << instr.opcode() << std::dec
                                      << " dis: " << disasm(instr.opcode(), instr.program_counter().va()) << ") is a "
                                      << (is_instr.is_load() ? "load" : "store") << " but does not have any addresses"
                                      << std::endl;
                        if (is_instr.is_load())
                            loads_missing_addresses++;
                        else
                            stores_missing_addresses++;
                    }
                }

                bool bad_translation = false;
                // Check the address translation for the PC
                auto tr = archx::Translation(is_instr.program_counter(),
                                             archx::Bitfield(state.read(archx::TCR_EL1)),
                                             archx::Bitfield(state.read(archx::TTBR0_EL1)),
                                             archx::Bitfield(state.read(archx::TTBR1_EL1)));
                while (!tr.done()) {
                    archx::PhysAddr pte_addr = tr.next_pte();
                    assert(!(pte_addr.value() & 0x7));
                    uint64_t pte_contents = state.read(pte_addr);
                    tr.pte_fetched(pte_contents);
                }
                if (tr.faulted() && is_instr.program_counter_pa().value() != 0x0) {
                    if (verbose)
                        std::cout << "Error: Instruction " << instruction_count << " (encoding: 0x" << std::hex
                                  << instr.opcode() << " dis: " << disasm(instr.opcode(), instr.program_counter().va())
                                  << ") faulted when we tried to perform a translation for PC 0x"
                                  << is_instr.program_counter().value() << ", but has PA 0x"
                                  << is_instr.program_counter_pa().value() << std::dec << " in the instruction stream"
                                  << std::endl;
                    bad_translation = true;
                } else if (!tr.faulted() && is_instr.program_counter_pa() != tr.result_address()) {
                    if (verbose)
                        std::cout << "Error: Instruction " << instruction_count << " (encoding: 0x" << std::hex
                                  << instr.opcode() << " dis: " << disasm(instr.opcode(), instr.program_counter().va())
                                  << ") claims PC 0x" << is_instr.program_counter().value() << " translates to PA 0x"
                                  << is_instr.program_counter_pa().value()
                                  << " in the instruction stream, but our translation returned PA 0x"
                                  << tr.result_address().value() << std::dec << std::endl;
                    bad_translation = true;
                }
                // Check the address translation for any memory accesses
                for (int i = 0; i < instr.memop_size(); i++) {
                    auto va = archx::VirtAddr(instr.memop(i).va());
                    auto pa = archx::PhysAddr(instr.memop(i).pa());
                    // Assume that zeroes were handled by `bad_addresses` logic above
                    if (va.value() == 0 || pa.value() == 0)
                        continue;

                    auto tr = archx::Translation(va,
                                                 archx::Bitfield(state.read(archx::TCR_EL1)),
                                                 archx::Bitfield(state.read(archx::TTBR0_EL1)),
                                                 archx::Bitfield(state.read(archx::TTBR1_EL1)));
                    while (!tr.done()) {
                        archx::PhysAddr pte_addr = tr.next_pte();
                        assert(!(pte_addr.value() & 0x7));
                        uint64_t pte_contents = state.read(pte_addr);
                        tr.pte_fetched(pte_contents);
                    }
                    if (tr.faulted()) {
                        // Because we skip all pa==0x0 addresses before we get
                        // here, if we fault here we know QEMU had a valid PA
                        // for this memory access
                        if (verbose)
                            std::cout << "Error: Instruction " << instruction_count << " (encoding: 0x" << std::hex
                                      << instr.opcode()
                                      << " dis: " << disasm(instr.opcode(), instr.program_counter().va())
                                      << ") faulted when we tried to perform a translation for VA 0x" << va.value()
                                      << ", but has PA 0x" << pa.value() << std::dec << " in the instruction stream"
                                      << std::endl;
                        bad_translation = true;
                    } else if (tr.result_address() != pa) {
                        if (verbose)
                            std::cout << "Error: Instruction " << instruction_count << " (encoding: 0x" << std::hex
                                      << instr.opcode()
                                      << " dis: " << disasm(instr.opcode(), instr.program_counter().va())
                                      << ") claims VA 0x" << va.value() << " translates to PA 0x" << pa.value()
                                      << " in the instruction stream, but our translation returned PA 0x"
                                      << tr.result_address().value() << std::dec << std::endl;
                        bad_translation = true;
                    }
                }
                if (bad_translation)
                    bad_translations++;

            } else if (is_instr.is_divide() && !instr.register__size() && !instr.vfp_register_size()) {
                div_sqrt_missing_registers++;
            }
        } else if (event.has_memory()) {
            const is::Memory& mem = event.memory();
            archx::PhysAddr pa = archx::PhysAddr(mem.address().pa());
            state = std::move(state).write(pa, (uint8_t*)mem.value().data(), mem.value().size());
        } else if (event.has_sysreg()) {
            const is::SysReg& s = event.sysreg();
            archx::SysReg sysreg = archx::SysReg(s.op0(), s.op1(), s.crn(), s.crm(), s.op2());
            state = std::move(state).write(sysreg, s.value());
        } else if (event.has_context()) {
            const is::Context& c = event.context();
            if (c.type() == is::Context::EL) {
                state = std::move(state).set_exception_level(static_cast<archx::EL>(c.value()));
            }
        }
        event.Clear();
    }

    if (prefetches_missing_registers > 0)
        std::cout << "Warning: " << prefetches_missing_registers << " prefetch instructions found missing registers"
                  << std::endl;
    if (cache_maint_missing_registers > 0)
        std::cout << "Warning: " << cache_maint_missing_registers
                  << " cache maintenance instructions found missing registers" << std::endl;
    if (tlbi_missing_registers > 0)
        std::cout << "Warning: " << tlbi_missing_registers << " TLBI instructions found missing registers" << std::endl;
    if (div_sqrt_missing_registers > 0)
        std::cout << "Warning: " << div_sqrt_missing_registers << " divide or sqrt instructions found missing registers"
                  << std::endl;
    if (loads_missing_addresses || stores_missing_addresses)
        std::cout << "Warning: " << loads_missing_addresses << " loads and " << stores_missing_addresses
                  << " stores missing addresses" << std::endl;
    if (bad_translations)
        std::cout << "Warning: " << bad_translations << " bad VA->PA translations" << std::endl;

    bool error = prefetches_missing_registers || cache_maint_missing_registers || tlbi_missing_registers
                 || div_sqrt_missing_registers || loads_missing_addresses || stores_missing_addresses
                 || bad_translations;

    if (error) {
        std::cout << "Errors detected processing: " << filename << "\n";
    }
    return error;
}

/*
 * vim: sw=4:ts=4:et:si:cindent:cino=\:0,(0,g0,t0:
 */
