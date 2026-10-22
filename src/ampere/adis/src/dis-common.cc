//
// Copyright (c) 2022 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//

#include <iostream>
#include <iomanip>
#include <cassert>

#include "adis.h"
#include "disargs.h"
#include "dis-common.h"

void dump_instr(const args_t& args, symbols_t& symbols, std::uint8_t* f, uint_t n, std::uint64_t va) {
    std::cout << std::hex << std::setfill('0');

    assert(n%4==0);
    for(uint_t i=0;i<n;i+=4) {
        std::uint64_t pc = va + i;
        auto nm = symbols.find(pc);
        if (nm != symbols.end()) {
            std::cout << "\n\n<" << nm->second << ">:\n";
        }
        std::uint32_t itext = 0;
        for (int j=0;j<4;j++) {
            itext |= (f[i+j] << (8*j));
        }
        std::cout << std::hex << std::setfill('0')
                  << std::setw(16) << std::right << pc << ": "
                  << std::hex << std::setfill('0')
                  << std::setw(8) << itext << ' '
                  << std::dec << std::setfill(' ')
                  << "   ";

        // we can now try to disassemble itext (at locatiion pc) ...
        adis::encoding_enum_t r = adis::decode(itext);
        if (r == adis::ENC_INVALID)
            std::cout << "Error";
        else {
            std::cout << std::setw(30) << std::left << adis::disasm(itext, r, pc);
            if (args.prototype) {
                std::cout << " /// " << std::setw(45) << std::left << adis::asm_prototype(r);
            }
            if (args.encoding_name) {
                std::cout << "  /// " << std::setw(20) << std::left << adis::encoding_name(r);
            }
#if 0
            if (args.raw_fields) {
                std::cout << "  /// ";
                adis::adis_data_t ad = { itext,  r};
                adis::adis_field_disas(&ad, std::cout);
            }
#endif
            if (args.feature) {
                std::cout << "\t /// " << adis::isa_feature(r);
            }
        }
        std::cout << '\n';
    }
    std::cout << std::dec << std::setfill(' ');
}
