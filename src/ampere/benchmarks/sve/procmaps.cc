//
// Copyright (c) 2023 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//
#include <iostream>
#include <fstream>
#include <sstream>
// #define FMT_HEADER_ONLY
#include <fmt/core.h>

void read_proc_self_maps()
{
    std::ifstream in("/proc/self/maps");
    fmt::print(stderr, "========================\n");
#define LINESZ 1024
    if (in.is_open()) {
        char line[LINESZ];
        while (in.getline(line, LINESZ)) {
            fmt::print(stderr, "{}\n", line);
        }
    } else {
        // it appears that there is some issue reading /proc/self/maps when tracing
        fmt::print(stderr, "maps not open\n");
    }
    fmt::print(stderr, "========================\n");
}

#if defined(TESTING)
int main(int argc, char** argv)
{
    read_proc_self_maps();
}
#endif
