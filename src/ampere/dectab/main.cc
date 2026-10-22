//
// Copyright (c) 2019 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//
#include <fstream>

#include "builder.h"
#include "dfa.h"
#include "parser.h"
#include "table.h"

using namespace ampere::dectab;

struct Options
{
    bool dump_dfa{false};
    std::string function{"decode"};
    std::string input_file;
};

Options parse_args(int argc, const char** argv)
{
    Options opts;

    std::deque<std::string> args(argv + 1, argv + argc);

    while (!args.empty()) {
        auto arg = args.front();
        args.pop_front();

        if (arg == "--dump-dfa") {
            opts.dump_dfa = true;
        } else if (arg == "-f" || arg == "--function") {
            if (args.empty())
                throw std::runtime_error{"option --function takes an argument"};
            opts.function = args.front();
            args.pop_front();
        } else {
            opts.input_file = arg;
        }
    }

    return opts;
}

int main(int argc, const char** argv)
{
    Options opts = parse_args(argc, argv);
    std::ifstream input(opts.input_file);

    if (!input) {
        std::cerr << "could not open input file " << opts.input_file << std::endl;
        exit(-1);
    }

    Builder builder;
    Parser parser{builder};
    DFA dfa{builder};
    Table table;

    try {
        auto re = parser.parse(input);
        if (re)
            dfa.build(re);
    } catch (Parser::Error& err) {
        std::cerr << opts.input_file << ':' << err.lineno << '.' << err.column << ": " << err.what() << std::endl;
        exit(-1);
    }

    if (opts.dump_dfa) {
        dfa.dump(std::cout);
        return 0;
    }

    if (dfa.get_start()) {
        table.build(dfa.get_start());
        table.print_decoder(opts.function);
    } else {
        std::cout << "int " << opts.function << "(const unsigned char* bytes [[maybe_unused]])\n{\n    return 0; }"
                  << std::endl;
    }

    return 0;
}

/*
 * vim: sw=4:ts=4:et:si:cindent:cino=\:0,(0,g0,t0:
 */
