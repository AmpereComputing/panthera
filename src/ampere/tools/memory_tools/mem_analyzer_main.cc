//
// Copyright (c) 2018-2019 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//

#include "ampere/tools/memory_tools/mem_analyzer.h"

int main(int argc, char const** argv)
{
    try {
        ampere::tools::memory_tools::MemAnalyzer memAnalyzer(argc, argv);

        // Trace input
        auto input_stream = new google::protobuf::io::FileInputStream(0);
        ampere::is::Instruction instruction;

        while (ampere::readDelimitedFrom(input_stream, &instruction, nullptr)) {
            memAnalyzer.ProcessInstr(instruction);
            instruction.Clear();
        }

        memAnalyzer.StatsOutput();
    } catch (const std::exception& e) {
        std::cout << "Exiting: " << e.what() << std::endl;
    }

    return 0;
}

/*
 * vim: sw=4:ts=4:et:si:cindent:cino=\:0,(0,g0,t0:
 */
