//
// Copyright (c) 2021 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//

#include <capstone.h>
#include <platform.h>
#include <boost/circular_buffer.hpp>
#include <iterator>
#include <iostream>
#include <set>
#include "ampere/instruction_stream/instruction_stream.h"

static csh disasm_handle;

using namespace ampere::is;

struct InstrOpcode
{
    unsigned int id;
    std::string mnemonic;

    std::vector<uint16_t> src_regs;
    std::vector<uint16_t> dst_regs;
};

struct InstrOpcode get_opcode(std::uint32_t bytes, std::uint64_t pc)
{
    size_t count(0);
    cs_insn* insn;
    struct InstrOpcode rc;

    // place the instruction bytes into the format required by capstone
    uint8_t byte_array[4];
    byte_array[0] = bytes;
    byte_array[1] = bytes >> 8;
    byte_array[2] = bytes >> 16;
    byte_array[3] = bytes >> 24;

    count = cs_disasm(disasm_handle, byte_array, 4, pc, 0, &insn);

    if (count) {
        rc.id = insn[0].id;
        rc.mnemonic = insn[0].mnemonic;

        cs_regs read;
        cs_regs write;
        uint8_t read_cnt;
        uint8_t write_cnt;
        cs_regs_access(disasm_handle, insn, read, &read_cnt, write, &write_cnt);
        for (uint8_t i = 0; i < read_cnt; i++)
            rc.src_regs.push_back(read[i]);
        for (uint8_t i = 0; i < write_cnt; i++)
            rc.dst_regs.push_back(write[i]);
    } else {
        rc.id = 0;
        rc.mnemonic = "invalid opcode";
    }
    cs_free(insn, count);
    return rc;
    ;
}

struct InstrKey
{
    unsigned int opcode_id{0};
    std::string opcode;
    bool dst_reg_same{false};     // Destination regisers of opcode sequence is the same
    bool src_reg_same{false};     // Src registers of opcode sequence is the same
    bool dst_src_reg_same{false}; // dst register of first opcode is src of second opcode

    bool operator==(const InstrKey& rhs) const
    {
        return (this->opcode == rhs.opcode) && (this->dst_reg_same == rhs.dst_reg_same)
               && (this->src_reg_same == rhs.src_reg_same) && (this->dst_src_reg_same == rhs.dst_src_reg_same);
    }
};

struct InstrKeyHasher
{
    std::size_t operator()(const InstrKey& k) const
    {
        return (std::hash<unsigned int>()(k.opcode_id) << 3) ^ (std::hash<bool>()(k.dst_reg_same) << 2)
               ^ (std::hash<bool>()(k.src_reg_same) << 1) ^ std::hash<bool>()(k.dst_src_reg_same);
    }
};

struct InstrNode
{
    uint64_t count;
    std::unordered_map<InstrKey, std::shared_ptr<InstrNode>, InstrKeyHasher> nodes;
    std::shared_ptr<InstrNode> InsertNode(InstrKey key)
    {
        auto found = nodes.find(key);
        // Not found
        if (found == nodes.end()) {
            nodes[key] = std::make_shared<InstrNode>();
            auto node = nodes[key];
            node->count++;
            return node;
        } else {
            found->second->count++;
            return found->second;
        }
    }
};

void dump(std::shared_ptr<InstrNode> node, std::string idiom, bool dst_same = false, bool src_dst_same = false)
{
    std::cout << node->count << ": " << idiom << std::endl;

    for (auto i : node->nodes) {
        if ((dst_same && i.first.dst_reg_same) || (src_dst_same && i.first.dst_src_reg_same)
            || (!dst_same && !src_dst_same))
            dump(i.second, idiom + i.first.opcode + ", ", dst_same, src_dst_same);
    }
}

int main(int argc, char* argv[])
{
    int max_length = 2;
    int option;
    bool quiet = false;
    bool dst_dep = false;
    bool src_dst_dep = false;
    while ((option = getopt(argc, argv, ":dl:hsq")) != -1) {
        switch (option) {
        case 's':
            src_dst_dep = true;
            break;
        case 'd':
            dst_dep = true;
            break;
        case 'l':
            max_length = atoi(optarg);
            break;
        case 'h':
            std::cout << "Usage: " << argv[0] << " {-lh} <instruction_stream>" << std::endl;
            std::cout << "  -d    dst of instructions sequence must match (possible fusion match)" << std::endl;
            std::cout << "  -h    displays this help and exit" << std::endl;
            std::cout << "  -l    maximium lenght of idioms to search for repeated patterns" << std::endl;
            std::cout << "  -s    dst of instruction must be a src for the following instruction in chain (posible "
                         "fusion match)"
                      << std::endl;
            std::cout << "  -q    quiet, only print out data" << std::endl;
            return 0;
            break;
        case 'q':
            quiet = true;
            break;
        case '?':
            std::cout << "unknown option: " << optopt << std::endl;
            return -1;
            break;
        };
    }

    if (optind == argc) {
        std::cout << "instruction stream required." << std::endl;
        return 0;
    }
    InstructionStreamReader reader(argv[optind]);
    Event event;
    boost::circular_buffer<InstrOpcode> instr_subset(max_length);
    uint64_t instr_count = 0;
    std::shared_ptr<InstrNode> root_node = std::make_shared<InstrNode>();

    // set up capstone for disassembly
    cs_err err = cs_open(CS_ARCH_ARM64, CS_MODE_ARM, &disasm_handle);
    if (err) {
        std::cout << "Failed on cs_open() with error returned: " << err << std::endl;
        assert(0);
    }
    cs_option(disasm_handle, CS_OPT_DETAIL, CS_OPT_ON);

    bool measurement_started = reader.version() < 2;
    if (!measurement_started && !quiet)
        std::cout << "Ignoring instructions before START_MEASUREMENT event" << std::endl;

    // Iterate through instructions
    while (reader.read(event)) {
        if (event.has_instruction()) {
            std::vector<uint16_t> prev_instr_dst_regs;  // use for same dst,dst patterns
            std::vector<uint16_t> prev_instr_dst_regs2; // use for same dst->src patterns

            instr_count++;
            if (instr_count % 1'000'000 == 0 && !quiet) {
                if (measurement_started)
                    std::cout << "Completed " << instr_count << "instructions..." << std::endl;
                else
                    std::cout << "Skipping warmup" << instr_count << "instructions..." << std::endl;
            }
            if (measurement_started) {
                Instruction instruction = event.instruction();
                InstrOpcode instr = get_opcode(instruction.opcode(), instruction.program_counter().va());
                instr_subset.push_back(instr);
                std::shared_ptr<InstrNode> node = root_node;

                // TODO Handle instructions at the end when we dont' have a full
                auto itr = instr_subset.begin();

                while (itr != instr_subset.end()) {
                    InstrKey key;
                    key.opcode_id = itr->id;
                    key.opcode = itr->mnemonic;
                    // Is dst register the same as the previous instruction?
                    if (dst_dep) {
                        key.dst_reg_same = false;
                        std::vector<uint16_t> cur_instr_dst_regs;
                        if (itr == instr_subset.begin()) { // first instruction save dst regs off
                            for (auto c_instr_reg : itr->dst_regs) {
                                key.dst_reg_same = true;
                                cur_instr_dst_regs.push_back(c_instr_reg);
                            }
                        } else {
                            for (auto c_instr_reg : itr->dst_regs) {
                                for (auto p_instr_reg : prev_instr_dst_regs) {
                                    if (c_instr_reg == p_instr_reg) {
                                        key.dst_reg_same = true;
                                        cur_instr_dst_regs.push_back(c_instr_reg);
                                    }
                                }
                            }
                        }
                        prev_instr_dst_regs = cur_instr_dst_regs;
                    } else if (src_dst_dep) {
                        key.dst_src_reg_same = false;
                        std::vector<uint16_t> cur_instr_dst_regs;
                        if (itr == instr_subset.begin()) { // first instruction save dst regs off
                            for (auto c_instr_reg : itr->dst_regs) {
                                key.dst_src_reg_same = true;
                                cur_instr_dst_regs.push_back(c_instr_reg);
                            }
                        } else {
                            for (auto c_instr_reg : itr->src_regs) {
                                for (auto p_instr_reg : prev_instr_dst_regs2) {
                                    if (c_instr_reg == p_instr_reg) {
                                        key.dst_src_reg_same = true;
                                        cur_instr_dst_regs.push_back(c_instr_reg);
                                    }
                                }
                            }
                        }
                        prev_instr_dst_regs2 =
                            cur_instr_dst_regs; // TODO: This is not correct beyond 2 instruction sequences. Should I
                                                // still stith th esame dst from first dst, or use new dst in third
                                                // instructions dst?  Right now I stil with the first instructions dst,
                                                // but it must be a src in the rest
                    }

                    node = node->InsertNode(key);

                    /*
                    // Debug Printing
                    // std::cout << itr->mnemonic << std::endl;
                    std::cout << itr->mnemonic << "(src:";
                    for (auto i : itr->src_regs) {
                        std::cout << cs_reg_name(disasm_handle, i) << ",";
                    }
                    std::cout << "), (dst:";
                    for (auto i : itr->dst_regs) {
                        std::cout << cs_reg_name(disasm_handle, i) << ",";
                    }
                    std::cout << "), ";
                    */

                    itr++;
                }
                // std::cout << std::endl;
            }
        } else if (event.has_control()) {
            Control control = event.control();
            if (control.type() == Control::START_MEASUREMENT)
                measurement_started = true;
        }
        event.Clear();
    }

    // TODO: Look at remaining instructions poping off until cqueue is empty

    dump(root_node, "", dst_dep, src_dst_dep);

    return 0;
}

/*
 * vim: sw=4:ts=4:et:si:cindent:cino=\:0,(0,g0,t0:
 */
