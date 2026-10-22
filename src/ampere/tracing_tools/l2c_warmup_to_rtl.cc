//
// Copyright (c) 2020 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//
#include <iomanip>
#include <iostream>
#include <fstream>

#include "ampere/archx/addr.h"
#include "cache.h"

using namespace ampere::ms;
using namespace ampere::tracer;

int main(int argc, char **argv)
{
    if (argc < 3) {
        std::cout << argv[0] << " [l2c warmup file] [l2c pav file]" << std::endl;
        exit(1);
    }

    MemoryStreamReader reader(argv[1]);
    std::ofstream pav_file;
    Access access;

    pav_file.open(argv[2]);

    // format:
    // <inst num> <hex pa> <CODE/DATA>
    //
    // 102 00000000503c0080 CODE
    // 102 00000000503c2300 DATA
    while (reader.read(access)) {
        pav_file << std::dec << access.offset();
        pav_file << " " << std::setfill('0') << std::setw(16) << std::hex << access.pa();
        pav_file << " " << (access.source() == Access_Source_INSTRUCTION ? "CODE" : "DATA");
        // UC -> Unique Clean, UD -> Unique Dirty
        pav_file << " " << (access.type() == Access_Type_WRITE ? "UD" : "UC") << std::endl;
        access.Clear();
    }

    pav_file.close();
}

/*
 * vim: sw=4:ts=4:et:si:cindent:cino=\:0,(0,g0,t0:
 */
