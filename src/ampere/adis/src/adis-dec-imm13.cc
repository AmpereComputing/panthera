//
// Copyright (c) 2022-2024 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//

//#define DEBUG_IMM13
#if defined(DEBUG_IMM13)
# define FMT_HEADER_ONLY
# include "fmt/core.h"
# define DBG(format_str, ...) fmt::print(format_str "\n", ##__VA_ARGS__)
#else
# define DBG(format_str, ...) do {} while(0)
#endif

#include <limits>
#include <optional>
#include <utility>
#include "adis-dec-imm13.h"

namespace adis {
typedef std::uint_fast32_t uint_t;

static uint_t hibit7(uint_t x) {
    if (x & 0b100'0000) return 6; // 64b pattern
    if (x & 0b010'0000) return 5; // 32b
    if (x & 0b001'0000) return 4; // 16b
    if (x & 0b000'1000) return 3; // 8b
    if (x & 0b000'0100) return 2; // 4b
    if (x & 0b000'0010) return 1; // 2b pattern
    return 0;
}
static std::uint32_t mask_32(uint_t x) {
    if (x == 32)
        return std::numeric_limits<std::uint32_t>::max();
    return (1UL<<x) - 1;
}
static std::uint64_t mask_64(uint64_t x) {
    if (x == 64)
        return std::numeric_limits<std::uint64_t>::max();
    return (1ULL<<x) - 1;
}
static std::uint32_t ror_32(uint_t x, uint_t r, uint_t width) {
   if (width == 32 && r == 0)
        return x;
    return mask_32(width)&((x>>r)|(x<<(width-r)));
}
static std::uint64_t ror_64(uint64_t x, std::uint64_t r, std::uint64_t width) {
    if (width == 64 && r == 0) // caught by -fsanitize=undefined
        return x;
    return mask_64(width)&((x>>r)|(x<<(width-r)));
}
static std::uint32_t replicate_fast_32(std::uint32_t x, uint_t ibits) {
    const uint_t obits = 32;
    uint_t i=0;
    std::uint32_t o=0;
    while(i < obits) {
        o |= x<<i;
        i += ibits;
    }
    return o;
}
static std::uint64_t replicate_fast_64(uint64_t x, uint_t ibits) {
    const uint_t obits = 64;
    uint_t i=0;
    std::uint64_t o=0;
    while(i < obits) {
        o |= x<<i;
        i += ibits;
    }
    return o;
}


u32optpair_t dec_imm13_32(std::uint32_t imm13, bool logical) {
    const uint_t imms = (imm13>>6) & 0x3F;
    const uint_t immr = imm13 & 0x3F;
    const uint_t n = (imm13 >> 12) & 1;
    const uint_t harg = (n << 6) | ((~imms)&0x3F);
    const uint_t length = hibit7(harg);
    if (length < 1) {
        return std::nullopt;
    }
    const uint_t esize = 1<<length;
    const uint_t levels = mask_32(length);
    // all 1s for length-of-pattern-minus-1 'x' bits set is illegal for logical immediates
    if (logical && (imms & levels) == levels) {
        DBG("fail imms & levels == levels, imms={} levels={}", imms, levels);
        return std::nullopt;
    }
    DBG("32b imm13={:04x} n={} immr={:06b}/{} imms={:06b}/{} action={}",
        imm13, n, immr, immr, imms, imms, (imms>=immr?"move to low":"copy up"));
    DBG("\t length={} levels={:032b} esize={}", length, levels, esize);
    const uint_t s = imms & levels;
    const uint_t r = immr & levels;
    const std::int32_t diff = s - r;
    const uint_t d = diff & levels;
    DBG("\t s={} r={} diff={:08x}/{} d={:08x}/{}", s, r, static_cast<std::uint32_t>(diff), diff, d, d);

    const uint_t welem = mask_32(s+1);
    DBG("\t welem={:08x}", welem);
    const uint_t telem = mask_32(d+1);
    DBG("\t telem={:08x}", telem);
    const uint_t vtemplate = ror_32(welem, r, esize);
    DBG("\t vtemplate={:08x}", vtemplate);
    const uint_t wmask = replicate_fast_32(vtemplate, esize);
    DBG("\t wmask={:08x} {:032b}", wmask, wmask);
    const uint_t tmask = replicate_fast_32(telem, esize);
    DBG("\t tmask={:08x} {:032b}", tmask, tmask);
    return std::make_pair(wmask,tmask);
}

u64optpair_t dec_imm13_64(std::uint32_t imm13, bool logical) {
    const uint_t imms = (imm13 >> 6) & 0x3F;
    const uint_t immr = imm13 & 0x3F;
    const uint_t n = (imm13 >> 12) & 1;
    const uint_t harg = (n << 6) | ((~imms)&0x3F);
    const uint_t length = hibit7(harg);  // 1..6, log power of 2 length of pattern
    if (length < 1) {
        return std::nullopt;
    }
    // convert log value to actual length 2,4,8,16,32,64
    const uint_t esize = 1 << length;
    // mask of length 1-bits
    const uint_t levels = mask_32(length);
     // all 1s for length-of-pattern-minus-1 'x' bits set is illegal for logical immediates
    if (logical && (imms & levels) == levels) {
        DBG("fail imms & levels == levels, imms={} levels={}", imms, levels);
        return std::nullopt;
    }

    DBG("64b imm13={:04x} n={} immr={:06b}/{} imms={:06b}/{} action={}",
        imm13, n, immr, immr, imms, imms, (imms>=immr?"move to low":"copy up"));
    DBG("\t length={} levels={:032b} esize={}", length, levels, esize);

    const uint_t s = imms & levels;  // 0...63
    const uint_t r = immr & levels;
    const std::int32_t diff = s - r;
    const uint_t d = diff & levels;
    DBG("\t s={} r={} diff={:08x}/{} d={:08x}/{}", s, r, static_cast<std::uint32_t>(diff), diff, d, d);
    const std::uint64_t welem = mask_64(s+1);  // up to 64 1s
    DBG("\t welem={:016x}", welem);
    const std::uint64_t telem = mask_64(d+1);
    DBG("\t telem={:016x}", telem);
    const std::uint64_t vtemplate = ror_64(welem,r,esize);
    DBG("\t vtemplate={:016x}", vtemplate);
    const std::uint64_t wmask = replicate_fast_64(vtemplate, esize);
    DBG("\t wmask={:016x} {:064b}", wmask, wmask);
    const std::uint64_t tmask = replicate_fast_64(telem, esize);
    DBG("\t tmask={:016x} {:064b}", tmask, tmask);
    return std::make_pair(wmask,tmask);
}
}
