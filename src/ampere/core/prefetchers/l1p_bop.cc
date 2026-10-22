//
// Copyright (c) 2023 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//

#include "ampere/core/prefetchers/l1p_bop.h"

namespace ampere::core {

const std::int64_t L1BOP::offsets0[] = {1,  2,   3,   4,   5,   6,   8,   9,   10,  12,  15,  16, 18,
                                        20, 24,  25,  27,  30,  32,  -1,  -2,  -3,  -4,  -5,  -6, -8,
                                        -9, -10, -12, -15, -16, -18, -20, -24, -25, -27, -30, -32};

// use sizeof() avoid miscounting offsets
const std::uint64_t L1BOP::num_offsets0{sizeof(L1BOP::offsets0) / sizeof(std::int64_t)};

const std::int64_t L1BOP::offsets1[] = { // values we use with L2 BOP
    1,  2,  3,  4,  5,  6,  7,  8,  9,  10, 11, 12, 13, 14,  15,  16,
    18, 20, 24, 28, 30, 32, 36, 40, -1, -2, -3, -4, -5, -12, -18, -44};

const std::uint64_t L1BOP::num_offsets1{sizeof(L1BOP::offsets1) / sizeof(std::int64_t)};

const std::int64_t L1BOP::offsets2[] = { // all values for offset searching
    -63, -62, -61, -60, -59, -58, -57, -56, -55, -54, -53, -52, -51, -50, -49, -48, -47, -46, -45, -44, -43,
    -42, -41, -40, -39, -38, -37, -36, -35, -34, -33, -32, -31, -30, -29, -28, -27, -26, -25, -24, -23, -22,
    -21, -20, -19, -18, -17, -16, -15, -14, -13, -12, -11, -10, -9,  -8,  -7,  -6,  -5,  -4,  -3,  -2,  -1,
    1,   2,   3,   4,   5,   6,   7,   8,   9,   10,  11,  12,  13,  14,  15,  16,  17,  18,  19,  20,  21,
    22,  23,  24,  25,  26,  27,  28,  29,  30,  31,  32,  33,  34,  35,  36,  37,  38,  39,  40,  41,  42,
    43,  44,  45,  46,  47,  48,  49,  50,  51,  52,  53,  54,  55,  56,  57,  58,  59,  60,  61,  62,  63};

const std::uint64_t L1BOP::num_offsets2{sizeof(L1BOP::offsets2) / sizeof(std::int64_t)};

const std::int64_t L1BOP::offsets3[] = { // from profiling
    -1, 1,  2,  3,  4,  5,  6,  7,  8,  9,  10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21,
    22, 23, 24, 25, 26, 27, 28, 29, 30, 31, 32, 33, 34, 35, 36, 37, 38, 39, 40, 41, 42, 43,
    44, 45, 46, 47, 48, 49, 50, 51, 52, 53, 54, 55, 56, 57, 58, 59, 60, 61, 62, 63};

const std::uint64_t L1BOP::num_offsets3{sizeof(L1BOP::offsets3) / sizeof(std::int64_t)};

L1P_BOP::L1P_BOP(const sc_core::sc_module_name& name) : L1BOP(name) {}

} // namespace ampere::core
