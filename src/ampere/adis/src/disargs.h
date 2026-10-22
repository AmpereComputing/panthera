//
// Copyright (c) 2022 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//

#pragma once
#include <deque>
#include <string>
#include <cstdint>

class args_t {
public:
    std::string input_file;
    bool prototype;
    bool encoding_name;
#if 0
    bool raw_fields;
#endif
    bool feature;
 args_t()
     : input_file(""), prototype(false), encoding_name(false),
#if 0
       raw_fields(false),
#endif
       feature(false)
        {}
};

void usage(char const* prog);
std::uint32_t get_int(std::deque<std::string>& cmdline);
std::string get_string(std::deque<std::string>& cmdline);
args_t parse_args(unsigned int argc, char** argv);
