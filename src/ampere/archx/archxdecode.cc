//
// Copyright (c) 2019 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//
#include <iostream>

#include <boost/filesystem/path.hpp>
#include <capstone.h>
#include <arm64.h>

#include "ampere/archx/instruction_is.h"
#include "ampere/archx/inum.h"
#include "ampere/instruction_stream/instruction_stream.h"

using namespace ampere;

csh open_capstone()
{
    csh handle;
    cs_err err = cs_open(CS_ARCH_ARM64, CS_MODE_ARM, &handle);

    if (err) {
        std::cerr << "cs_open() failed with error: " << err << std::endl;
        exit(-1);
    }

    cs_option(handle, CS_OPT_DETAIL, CS_OPT_ON);
    return handle;
}

std::pair<std::set<archx::LReg>, std::set<archx::LReg>> register_usage_archx(archx::UopSet& uops)
{
    static std::set<archx::LReg> exclude = {archx::LReg::INVALID, archx::LReg::ZR, archx::LReg::s0, archx::LReg::s1};

    std::set<archx::LReg> dests;
    std::set<archx::LReg> sources;

    for (auto& uop : uops) {
        if (exclude.find(uop->dst) == exclude.end())
            dests.insert(uop->dst);

        if (exclude.find(uop->dst2) == exclude.end())
            dests.insert(uop->dst2);

        if (exclude.find(uop->src1) == exclude.end())
            sources.insert(uop->src1);

        if (exclude.find(uop->src2) == exclude.end())
            sources.insert(uop->src2);

        if (exclude.find(uop->src3) == exclude.end())
            sources.insert(uop->src3);

        if (uop->produce_flags)
            dests.insert(archx::LReg::FLAGS);
    }

    return {dests, sources};
}

archx::LReg cs_reg_to_archx_reg(csh handle, std::uint16_t regnum)
{
    const char* regname = cs_reg_name(handle, regnum);

    try {
        switch (*regname) {
        case 'x':
            if (regname[1] == 'z' && regname[2] == 'r')
                return archx::LReg::ZR;
            return archx::LReg(archx::LReg::L0 + std::stoi(regname + 1));
        case 'w':
            if (regname[1] == 'z' && regname[2] == 'r')
                return archx::LReg::ZR;
            return archx::LReg(archx::LReg::L0 + std::stoi(regname + 1));
        case 's':
            if (regname[1] == 'p')
                return archx::LReg::SP;
            goto fpsimd;
        case 'q':
        case 'd':
        case 'h':
        case 'b':
        case 'v':
        fpsimd:
            // FIXME: all need to be V0 based
            return archx::LReg(archx::LReg::L0 + std::stoi(regname + 1));
        case 'n':
            // Does this mean flag setter??
            if (!strcmp(regname, "nzcv"))
                return archx::LReg::FLAGS;
        default:
            std::cerr << "don't understand register " << regname << std::endl;
            exit(-1);
        }
    } catch (std::invalid_argument&) {
        std::cerr << "failed to convert register " << regname << std::endl;
        exit(-1);
    }
}

std::pair<std::set<archx::LReg>, std::set<archx::LReg>> register_usage_capstone(csh handle,
                                                                                archx::Instruction& instruction,
                                                                                char** mnemonic,
                                                                                bool pure_store)
{
    cs_insn* insn;

    std::uint32_t opcode = instruction.opcode();
    auto code = reinterpret_cast<const std::uint8_t*>(&opcode);

    auto count = cs_disasm(handle, code, 4, instruction.program_counter().value(), 0, &insn);
    *mnemonic = insn->mnemonic;

    if (count == 0) {
        std::cerr << "disassembly failed for opcode: " << instruction.opcode() << std::endl;
        exit(-1);
    }

    std::set<archx::LReg> dests;
    std::set<archx::LReg> sources;

    cs_regs regs_read, regs_write;
    uint8_t regs_read_count, regs_write_count;

    if (!cs_regs_access(handle, insn, regs_read, &regs_read_count, regs_write, &regs_write_count)) {
        if (regs_read_count) {
            for (int i = 0; i < regs_read_count; i++)
                sources.insert(cs_reg_to_archx_reg(handle, regs_read[i]));
        }

        if (regs_write_count && !pure_store) {
            for (int i = 0; i < regs_write_count; i++)
                dests.insert(cs_reg_to_archx_reg(handle, regs_write[i]));
        }
    }

    cs_free(insn, count);

    return {dests, sources};
}

void show_differences(const std::set<archx::LReg>& archx_regs,
                      const std::set<archx::LReg>& capstone_regs,
                      const char* type)
{
    std::set<archx::LReg> in_archx_not_in_capstone;

    std::set_difference(archx_regs.begin(),
                        archx_regs.end(),
                        capstone_regs.begin(),
                        capstone_regs.end(),
                        std::inserter(in_archx_not_in_capstone, in_archx_not_in_capstone.begin()));

    if (!in_archx_not_in_capstone.empty())
        std::cout << type << " registers in archx but not in capstone:;";

    for (auto& lreg : in_archx_not_in_capstone)
        std::cout << lreg << ';';

    std::set<archx::LReg> in_capstone_not_in_archx;

    std::set_difference(capstone_regs.begin(),
                        capstone_regs.end(),
                        archx_regs.begin(),
                        archx_regs.end(),
                        std::inserter(in_capstone_not_in_archx, in_capstone_not_in_archx.begin()));

    if (!in_capstone_not_in_archx.empty())
        std::cout << type << " registers in capstone but not in archx:;";

    for (auto& lreg : in_capstone_not_in_archx)
        std::cout << lreg << ';';
}

bool compare_registers(const std::pair<std::set<archx::LReg>, std::set<archx::LReg>>& archx_regs,
                       const std::pair<std::set<archx::LReg>, std::set<archx::LReg>>& capstone_regs,
                       archx::Instruction& instruction,
                       bool detail)
{
    auto& archx_dests = archx_regs.first;
    auto& archx_sources = archx_regs.second;
    auto& capstone_dests = capstone_regs.first;
    auto& capstone_sources = capstone_regs.second;

    bool mismatch = false;

    if (archx_dests != capstone_dests) {
        mismatch = true;
        if (detail)
            show_differences(archx_dests, capstone_dests, "destination");
    }

    if (archx_sources != capstone_sources) {
        if (detail)
            show_differences(archx_sources, capstone_sources, "source");
        mismatch = true;
    }

    return mismatch;
}

int main(int argc, char** argv)
{
    if (argc < 2) {
        std::cerr << "usage: " << argv[0] << " <instruction stream file>" << std::endl;
        exit(-1);
    }

    auto reader = new is::InstructionStreamReader(boost::filesystem::path(argv[1]));

    is::Instruction instruction;
    archx::ArchState state;

    std::uint64_t instruction_count = 0;
    std::uint64_t uop_count = 0;
    std::uint64_t mismatches = 0;

    csh handle = open_capstone();

    std::map<std::string, std::uint64_t> mismatches_by_mnemonic;
    std::map<std::uint64_t, std::uint64_t> bad_addresses_by_opcode;

    while (reader->read(instruction)) {
        archx::inum_t current_id(0);
        archx::inum_t prior_id(0);
        archx::InstructionIS archx_instruction(instruction, current_id, prior_id, state);
        auto archx_uops = archx_instruction.decode();
        char* mnemonic;

        bool pure_store = true;
        bool bad_load_store = false;

        for (auto& uop : archx_uops) {
            if (uop->info->unit != archx::Uopunit::Store)
                pure_store = false;

            if ((uop->info->unit == archx::Uopunit::Store || uop->info->unit == archx::Uopunit::Load)
                && archx_instruction.physical_address().value() == 0x0
                && archx_instruction.virtual_address().value() == 0x0)
            {
                bad_load_store = true;
            }
        }

        auto archx_regs = register_usage_archx(archx_uops);
        auto capstone_regs = register_usage_capstone(handle, archx_instruction, &mnemonic, pure_store);

        if (compare_registers(archx_regs, capstone_regs, archx_instruction, false)) {
            if (mismatches_by_mnemonic[mnemonic]++ == 0) {
                std::cout << "mismatch in " << mnemonic << " opcode " << std::hex << archx_instruction.opcode()
                          << std::dec << ';';
                compare_registers(archx_regs, capstone_regs, archx_instruction, true);
                std::cout << std::endl;
            }
            ++mismatches;
        }

        if (bad_load_store) {
            if (bad_addresses_by_opcode[archx_instruction.opcode()]++ == 0) {
                std::cout << "load/store opcode without address in " << mnemonic << " opcode " << std::hex
                          << archx_instruction.opcode() << std::dec << ';' << std::endl;
            }
        }

        ++instruction_count;
        uop_count += archx_uops.size();

        instruction.Clear();
    }

    delete reader;
    std::cout << "Decoded " << instruction_count << " instructions (" << uop_count << " uops) with " << mismatches
              << " mismatches." << std::endl;

    for (auto& mismatch : mismatches_by_mnemonic) {
        std::cout << mismatch.first << '\t' << mismatch.second << std::endl;
    }
}

/*
 * vim: sw=4:ts=4:et:si:cindent:cino=\:0,(0,g0,t0:
 */
