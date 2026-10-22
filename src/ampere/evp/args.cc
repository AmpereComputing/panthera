//
// Copyright(c) 2022 Ampere Computing LLC
// SPDX-License-Identifier: BSD-3-Clause
//
#include "args.h"

namespace ampere::evp {

bool Args::pop(const char* cli_switch)
{
    if (!arg_list.empty() && matches_switch(arg_list.front(), cli_switch)) {
        arg_list.pop_front();
        return true;
    }

    return false;
}

void Args::done()
{
    if (!arg_list.empty())
        throw std::runtime_error{fmt::format("extra arguments on command line (\"{}\" ...)", arg_list.front())};
}

bool Args::matches_switch(const std::string& arg, std::string cli_switch)
{
    size_t alt;

    while ((alt = cli_switch.find('|')) != std::string::npos) {
        if (arg == cli_switch.substr(0, alt))
            return true;
        cli_switch.erase(0, alt + 1);
    }

    return arg == cli_switch;
}

} // namespace ampere::evp
