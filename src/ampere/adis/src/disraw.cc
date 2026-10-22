//
// Copyright (c) 2022 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//

#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <iomanip>
#include <cassert>
#include <fstream>

#include "adis.h"

static const char nl = '\n';

int main(int argc, char** argv) {
    if (argc != 2) {
        std::cout << "Need a filename argument...\n";
        exit(EXIT_FAILURE);
    }
    std::ifstream f(argv[1]);
    while(f) {
        std::uint32_t itext;
        f >> std::hex >> itext;
        adis::encoding_enum_t r = adis::decode(itext);
        if (r == adis::ENC_INVALID) {
            std::cout << "ERROR! 0x" << std::hex << std::setfill('0')
                      << std::setw(8) << itext
                      << " -> " << std::dec << r
                      << ": " << "ERROR" << nl;
        }
        else {

            std::cout << "Accepted! 0x" << std::hex << std::setfill('0')
                      << std::setw(8) << itext
                      << " -> " << adis::asm_prototype(r)
                      << "  /// ";
#if 0
            adis::adis_data_t ad = { itext,  r};
            adis::adis_field_disas(&ad, std::cout);
#endif
            adis::disasm(itext, r, 0 /*va*/);
            std::cout << nl;
        }
    }
}
