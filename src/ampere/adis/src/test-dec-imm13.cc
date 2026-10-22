//
// Copyright (c) 2022-2024 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//

#include <iostream>
#include <iomanip>
#include <cstdint>
#include "adis-dec-imm13.h"

typedef std::uint_fast32_t uint_t;

static const char *uint64_to_binary(std::uint64_t x) {
  static char b[65];
  unsigned i;
  for (i = 0; i < 64; i++, x <<= 1)
    b[i] = (0x8000000000000000ULL & x)? '1' : '0';
  b[64] = '\0';
  return b;
}
static const char *uint64_to_binary_nbits(std::uint64_t x, std::uint64_t nbits) {
  static char b[65];
  unsigned i;
  const std::uint64_t hibit = 1ULL<<(nbits-1);
  for (i = 0; i < nbits; i++, x <<= 1) {
      b[i] = (hibit & x)? '1' : '0';
  }
  b[nbits] = '\0';
  return b;
}

static void print_64(uint_t imm13, const adis::u64optpair_t& v) {
    std::cout << "n=" << ((imm13>>12)&1)
              << " immr=" << uint64_to_binary_nbits(imm13&0x3F,6)
              << " imms=" << uint64_to_binary_nbits((imm13>>6)&0x3F,6);
    if (v.has_value()) {
        auto [wmask, tmask] = v.value();
        std::cout << " "
                  << std::setw(16) << std::hex << std::setfill('0')
                  << " wmask= " 
                  << wmask << " " << uint64_to_binary(wmask)
                  << " ; tmask= " 
                  << tmask << " " << uint64_to_binary(tmask);
    }
    else
        std::cout << " INVALID";
    std::cout << std::endl;
}

[[maybe_unused]] static void print_32(uint_t imm13, const adis::u32optpair_t& v) {
    std::cout << "n=" << ((imm13>>12)&1)
              << " immr=" << uint64_to_binary_nbits(imm13&0x3F,6)
              << " imms=" << uint64_to_binary_nbits((imm13>>6)&0x3F,6);
    if (v.has_value()) {
        auto [wmask, tmask] = v.value();
        std::cout << " "
                  << std::setw(8) << std::hex << std::setfill('0')
                  << " wmask= "
                  << wmask << " " << uint64_to_binary_nbits(wmask,32)
                  << " ; tmask= "
                  << tmask << " " << uint64_to_binary_nbits(tmask,32);
    }
    else
        std::cout << " INVALID";
    std::cout << std::endl;
}

int main(int argc, char** argv) {
    bool logical = false;
    std::uint32_t lim = 1<<6;
    for(std::uint32_t sz = 0 ; sz < 2; sz++) {
        for (std::uint32_t n = 0 ; n < (sz+1) ; n++) {
            const std::uint32_t nsz = (sz==0) ? 32 : 64;
            //const std::uint32_t obits = (n==0) ? 32 : 64;
            for (uint_t imms = 0 ; imms < lim ; imms++) {
                for (uint_t immr = 0 ; immr < lim ; immr++) {
                    const uint_t imm13 = (n << 12) | (imms << 6) | immr;
                    if (nsz == 64) {
                        auto v = adis::dec_imm13_64(imm13, logical);
                        print_64(imm13, v);
                    }
                    else  {
                        auto v = adis::dec_imm13_32(imm13, logical);
                        print_32(imm13, v);
                    }
                }
            }
        }
    }
    (void)argc;(void)argv;
}
