//
// Copyright (c) 2018-2025 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//
#pragma once
#include <cstdint>
#include <iostream>
#include <string>
#include <format>
#include <stdexcept>
#include "ampere/util/enum.h"
#include "ampere/kestrel/aice-sim/dtype.hh"

make_ns_enum(ampere::kestrel::aice, resource_t,
             INVALID, ALU, MAR, LOADQ, STOREQ, WB, MAR_BBUS, TMEM_FSMS);
make_ns_enum(ampere::kestrel::aice, tmem_fsm_direction_t, IMR_DEST, IMR_SRC,
             AMR_SRC, AMR_SRC_BF16,
             AMR_SRC_VECTOR, AMR_SRC_VECTOR_BF16,
             IMR_SCALE_DEST);
make_ns_enum(ampere::kestrel::aice, access_type_t, LOAD, STORE);
make_ns_enum(ampere::kestrel::aice, accumulate_option_t, NONE, FP32, INT32);
make_ns_enum(ampere::kestrel::aice, register_t,
             mar0, mar1,
             amr0, amr1, amr2, amr3, amr4, amr5, amr6, amr7, amr8, amr9, amr10, amr11, amr12, amr13, amr14, amr15, amr16,
             amr17, amr18, amr19, amr20, amr21, amr22, amr23, amr24, amr25, amr26, amr27, amr28, amr29, amr30, amr31, amr32,
             imr0, imr1, imr2, imr3, imr4, imr5, imr6, imr7, imr8, imr9, imr10, imr11, imr12, imr13, imr14, imr15, imr16,
             scale0, scale1, scale2, scale3, scale4, scale5, scale6, scale7, scale8,
             scale9, scale10, scale11, scale12, scale13, scale14, scale15, scale16,
             r0, r1, r2, r3, r4, r5, r6, r7, r8, r9, r10, r11, r12, r13, r14, r15, r16,
             r17, r18, r19, r20, r21, r22, r23, r24, r25, r26, r27, r28, r29, r30, r31, r32,
             r33, r34, r35, r36, r37, r38, r39, r40, r41, r42, r43, r44, r45, r46, r47, r48,
             r49, r50, r51, r52, r53, r54, r55, r56, r57, r58, r59, r60, r61, r62, r63);

namespace ampere::kestrel::aice {

static inline bool is_mx(aice_emu::dtype_t dtype)
{
    const std::array<bool, static_cast<size_t>(aice_emu::dtype_t::LAST_ITEM)> mx_tbl = {false,
                                                                                        false,
                                                                                        false,
                                                                                        false,
                                                                                        false,
                                                                                        false, // non mx types
                                                                                        true,
                                                                                        true,
                                                                                        true,
                                                                                        true,
                                                                                        true,
                                                                                        true,
                                                                                        true};
    return mx_tbl.at(static_cast<size_t>(dtype));
}

using regidx_t = std::uint32_t;
using imm_t = std::int32_t;
using addr_t = std::uint32_t;
constexpr size_t imr_row_bytes = 128;
constexpr size_t amr_row_bytes = 256;
constexpr size_t amr_rows = 64;
constexpr size_t imr_rows = 64;
constexpr size_t imr_size_bytes = imr_rows * imr_row_bytes;
constexpr size_t amr_size_bytes = amr_rows * amr_row_bytes;
constexpr size_t imr_max_idx = 16;
constexpr size_t amr_max_idx = 32;
constexpr size_t load_transfer_bytes = 128;
constexpr size_t store_transfer_bytes = 128;

extern int dbg;
#define DBGI(format_str, ...) std::cerr << std::format("{}: " format_str "\n", get_cycle() __VA_OPT__(, ) __VA_ARGS__)
#define DBG(format_str, ...)                             \
    do {                                                 \
        if (dbg & 1) {                                   \
            DBGI(format_str __VA_OPT__(, ) __VA_ARGS__); \
        }                                                \
    } while (0)
#define DBGM(mask, format_str, ...)                      \
    do {                                                 \
        if (dbg & mask) {                                \
            DBGI(format_str __VA_OPT__(, ) __VA_ARGS__); \
        }                                                \
    } while (0)
// no cycle number
#define DBGN(mask, format_str, ...)                                               \
    do {                                                                          \
        if (dbg & mask) {                                                         \
            std::cerr << std::format(format_str "\n" __VA_OPT__(, ) __VA_ARGS__); \
        }                                                                         \
    } while (0)

#define THROW(format_str, ...)                                \
    throw std::runtime_error(std::format("{}:{}:{}: {}\n",    \
                                         __FILE__,            \
                                         __LINE__,            \
                                         __PRETTY_FUNCTION__, \
                                         std::format(format_str __VA_OPT__(, ) __VA_ARGS__)))

} // namespace ampere::kestrel::aice
