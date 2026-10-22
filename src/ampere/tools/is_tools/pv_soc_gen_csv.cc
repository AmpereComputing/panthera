//
// Copyright (c) 2024 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//

#include <string>
#include <vector>
#include <fstream>
#include <filesystem>

#include "ampere/mesh/injector/uop_source.h"
#include "ampere/mesh/injector/uop_source_prog.h"
#include "ampere/mesh/pager.h"
#include "ampere/tools/is_tools/chip-decoder-map.h"

using std::ofstream;
using std::string;
using std::vector;

using namespace ampere;
using namespace ampere::sc;
using namespace ampere::mesh;
using namespace archx;

class ProgReader : public sc::Module
{
public:
    explicit ProgReader(const sc_core::sc_module_name& name) : Module{name} {}
    Submodule<UopSource, UopSourceProg> uop_source{"source"};
};

enum MemoryAttributes {
    EarlyWriteAck = 1,
    Device = 2,
    Cacheable = 4,
    AllocateHint = 8
};

int sc_main(int argc, char** argv)
{
    auto& env = *Environment::get_root();
    env.parameters.load_from_args(argc, argv);

    Param<string> output_filename{env, "output", "CSV filename to write", "output.csv"};

    vector<string> decoder_names = is_tools::get_decoders("max");
    Instruction::set_decoders(decoder_names);
    ProgReader top{"top"};
    RandomPager pager{"pager"};

    /* SystemC print help */
    if (env.parameters.help_requested()) {
        env.parameters.dump_parameters_verbosely(std::cout);
        return EXIT_SUCCESS;
    }

    if (!env.parameters.check_all_matched())
        throw std::runtime_error{"Not all parameters were matched. Giving up."};

    top.uop_source->enable();
    top.uop_source->set_node_id(NodeId());

    /* Check to see if file exists */
    std::filesystem::path out_path{output_filename.val()};
    if (std::filesystem::exists(out_path))
        throw std::runtime_error{fmt::format("Output file ({}) already exists.\n", output_filename.val())};

    ofstream out_file{output_filename};
    fmt::print(out_file, "uop va pa memattr page_size\n");
    auto maybe_uop = top.uop_source->fetch_uop();
    while (maybe_uop) {
        if (maybe_uop) {
            auto uop = *maybe_uop;
            auto opcode = uop->archx_uop->opcode;
            auto va = uop->archx_instruction->virtual_address();
            auto pa = pager.translate(va, "default");
            MemoryAttributes mem_attr = Cacheable;
            std::string page_size = "PS_4K";

            fmt::print(out_file, "{} 0x{} 0x{} 0x{:x} {}\n", opcode, va, pa, static_cast<int>(mem_attr), page_size);
            top.uop_source->commit_uop(uop);
        }
        maybe_uop = top.uop_source->fetch_uop();
    }

    return EXIT_SUCCESS;
}
