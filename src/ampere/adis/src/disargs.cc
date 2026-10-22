//
// Copyright (c) 2022 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//

#include "disargs.h"
#include <iostream>
#include <iomanip>
#include <cstdlib>

void usage(char const* prog) {
    std::cerr << "Usage " << prog << " [options]\n";
    char const* umsg[] = {
        "-h            (this help)",
        "-i FILENAME   (input file name, REQUIRED)",
        "-p            (also emit prototype)",
        "-e            (also emit ARM encoding name)",
#if 0
        "-r            (also emit raw field values)",
#endif
        "-f            (also emit ARM ISA feature)",
        0 };

    for(int i = 0; umsg[i]; i++)
        std::cerr << '\t' << umsg[i] << '\n';
    exit(EXIT_FAILURE);
}

std::uint32_t get_int(std::deque<std::string>& cmdline) {
    if (cmdline.empty()) {
        std::cerr << "Need an integer argument for option\n";
        exit(EXIT_FAILURE);
    }
    std::uint32_t r = std::stoi(cmdline.front());
    cmdline.pop_front();
    return r;
}

std::string get_string(std::deque<std::string>& cmdline) {
    if (cmdline.empty()) {
        std::cerr << "Need an string argument for option\n";
        exit(1);
    }
    std::string s = cmdline.front();
    cmdline.pop_front();
    return s;
}

args_t parse_args(unsigned int argc, char** argv) {
    args_t args;
    std::deque<std::string> cmdline(argv+1, argv+argc);
    while(!cmdline.empty()) {
        auto arg=cmdline.front();
        cmdline.pop_front();

        if (arg == "-h" || arg == "--help") {
            usage(argv[0]);
        }
        else if (arg == "-i") {
            args.input_file = get_string(cmdline);
        }
        else if (arg == "-p") {
            args.prototype = true;
        }
        else if (arg == "-e") {
            args.encoding_name = true;
        }
        else if (arg == "-f") {
            args.feature = true;
        }
#if 0
        else if (arg == "-r") {
            args.raw_fields = true;
        }
#endif
        else {
            std::cerr << "Error: Unrecognized option\n\n";
            usage(argv[0]);
        }
    }

    if (args.input_file.size() == 0){
        std::cerr << "Need an input file. Use: -i FILENAME\n";
        exit(EXIT_FAILURE);
    }
    return args;
}
