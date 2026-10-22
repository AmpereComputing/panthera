//
// Copyright (c) 2023 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//

#include <cstdint>
#include <array>
#include <bitset>
#include "tlbi.h"
namespace ampere::archx {

static constexpr std::array<adis::sys_alias_t, 24> by_va_all_level = {
    adis::sys_alias_t::TLBI_VAAE1,    adis::sys_alias_t::TLBI_VAAE1IS, adis::sys_alias_t::TLBI_VAAE1ISNXS,
    adis::sys_alias_t::TLBI_VAAE1NXS, adis::sys_alias_t::TLBI_VAAE1OS, adis::sys_alias_t::TLBI_VAAE1OSNXS,
    adis::sys_alias_t::TLBI_VAE1,     adis::sys_alias_t::TLBI_VAE1IS,  adis::sys_alias_t::TLBI_VAE1ISNXS,
    adis::sys_alias_t::TLBI_VAE1NXS,  adis::sys_alias_t::TLBI_VAE1OS,  adis::sys_alias_t::TLBI_VAE1OSNXS,
    adis::sys_alias_t::TLBI_VAE2,     adis::sys_alias_t::TLBI_VAE2IS,  adis::sys_alias_t::TLBI_VAE2ISNXS,
    adis::sys_alias_t::TLBI_VAE2NXS,  adis::sys_alias_t::TLBI_VAE2OS,  adis::sys_alias_t::TLBI_VAE2OSNXS,
    adis::sys_alias_t::TLBI_VAE3,     adis::sys_alias_t::TLBI_VAE3IS,  adis::sys_alias_t::TLBI_VAE3ISNXS,
    adis::sys_alias_t::TLBI_VAE3NXS,  adis::sys_alias_t::TLBI_VAE3OS,  adis::sys_alias_t::TLBI_VAE3OSNXS};

static constexpr std::array<adis::sys_alias_t, 24> by_va_last_level = {
    adis::sys_alias_t::TLBI_VAALE1,    adis::sys_alias_t::TLBI_VAALE1IS, adis::sys_alias_t::TLBI_VAALE1ISNXS,
    adis::sys_alias_t::TLBI_VAALE1NXS, adis::sys_alias_t::TLBI_VAALE1OS, adis::sys_alias_t::TLBI_VAALE1OSNXS,
    adis::sys_alias_t::TLBI_VALE1,     adis::sys_alias_t::TLBI_VALE1IS,  adis::sys_alias_t::TLBI_VALE1ISNXS,
    adis::sys_alias_t::TLBI_VALE1NXS,  adis::sys_alias_t::TLBI_VALE1OS,  adis::sys_alias_t::TLBI_VALE1OSNXS,
    adis::sys_alias_t::TLBI_VALE2,     adis::sys_alias_t::TLBI_VALE2IS,  adis::sys_alias_t::TLBI_VALE2ISNXS,
    adis::sys_alias_t::TLBI_VALE2NXS,  adis::sys_alias_t::TLBI_VALE2OS,  adis::sys_alias_t::TLBI_VALE2OSNXS,
    adis::sys_alias_t::TLBI_VALE3,     adis::sys_alias_t::TLBI_VALE3IS,  adis::sys_alias_t::TLBI_VALE3ISNXS,
    adis::sys_alias_t::TLBI_VALE3NXS,  adis::sys_alias_t::TLBI_VALE3OS,  adis::sys_alias_t::TLBI_VALE3OSNXS};

static constexpr std::array<adis::sys_alias_t, 48> by_range = {
    adis::sys_alias_t::TLBI_RVAAE1,     adis::sys_alias_t::TLBI_RVAAE1IS,  adis::sys_alias_t::TLBI_RVAAE1ISNXS,
    adis::sys_alias_t::TLBI_RVAAE1NXS,  adis::sys_alias_t::TLBI_RVAAE1OS,  adis::sys_alias_t::TLBI_RVAAE1OSNXS,
    adis::sys_alias_t::TLBI_RVAALE1,    adis::sys_alias_t::TLBI_RVAALE1IS, adis::sys_alias_t::TLBI_RVAALE1ISNXS,
    adis::sys_alias_t::TLBI_RVAALE1NXS, adis::sys_alias_t::TLBI_RVAALE1OS, adis::sys_alias_t::TLBI_RVAALE1OSNXS,
    adis::sys_alias_t::TLBI_RVAE1,      adis::sys_alias_t::TLBI_RVAE1IS,   adis::sys_alias_t::TLBI_RVAE1ISNXS,
    adis::sys_alias_t::TLBI_RVAE1NXS,   adis::sys_alias_t::TLBI_RVAE1OS,   adis::sys_alias_t::TLBI_RVAE1OSNXS,
    adis::sys_alias_t::TLBI_RVAE2,      adis::sys_alias_t::TLBI_RVAE2IS,   adis::sys_alias_t::TLBI_RVAE2ISNXS,
    adis::sys_alias_t::TLBI_RVAE2NXS,   adis::sys_alias_t::TLBI_RVAE2OS,   adis::sys_alias_t::TLBI_RVAE2OSNXS,
    adis::sys_alias_t::TLBI_RVAE3,      adis::sys_alias_t::TLBI_RVAE3IS,   adis::sys_alias_t::TLBI_RVAE3ISNXS,
    adis::sys_alias_t::TLBI_RVAE3NXS,   adis::sys_alias_t::TLBI_RVAE3OS,   adis::sys_alias_t::TLBI_RVAE3OSNXS,
    adis::sys_alias_t::TLBI_RVALE1,     adis::sys_alias_t::TLBI_RVALE1IS,  adis::sys_alias_t::TLBI_RVALE1ISNXS,
    adis::sys_alias_t::TLBI_RVALE1NXS,  adis::sys_alias_t::TLBI_RVALE1OS,  adis::sys_alias_t::TLBI_RVALE1OSNXS,
    adis::sys_alias_t::TLBI_RVALE2,     adis::sys_alias_t::TLBI_RVALE2IS,  adis::sys_alias_t::TLBI_RVALE2ISNXS,
    adis::sys_alias_t::TLBI_RVALE2NXS,  adis::sys_alias_t::TLBI_RVALE2OS,  adis::sys_alias_t::TLBI_RVALE2OSNXS,
    adis::sys_alias_t::TLBI_RVALE3,     adis::sys_alias_t::TLBI_RVALE3IS,  adis::sys_alias_t::TLBI_RVALE3ISNXS,
    adis::sys_alias_t::TLBI_RVALE3NXS,  adis::sys_alias_t::TLBI_RVALE3OS,  adis::sys_alias_t::TLBI_RVALE3OSNXS};

using sa_arr_t = std::bitset<static_cast<size_t>(adis::sys_alias_t::LAST)>;

static const sa_arr_t const_by_va_all_level_init()
{
    // The goal is to create a bitset (a table) indexed by
    // adis::sys_alias_t values which represent the various TLBI
    // qualfiers (among other things).

    sa_arr_t arr{};
    for (auto t : by_va_all_level) {
        arr[static_cast<size_t>(t)] = true;
    }
    return arr;
}

static const sa_arr_t const_by_va_last_level_init()
{
    // The goal is to create a bitset (a table) indexed by
    // adis::sys_alias_t values which represent the various TLBI
    // qualfiers (among other things).

    sa_arr_t arr{};
    for (auto t : by_va_last_level) {
        arr[static_cast<size_t>(t)] = true;
    }
    return arr;
}

// intialized once at program startup
static const sa_arr_t tlbi_by_va_all_level_table = const_by_va_all_level_init();
static const sa_arr_t tlbi_by_va_last_level_table = const_by_va_last_level_init();

bool tlbi_by_va_all_level(adis::sys_alias_t c)
{
    return tlbi_by_va_all_level_table[static_cast<size_t>(c)];
}

bool tlbi_by_va_last_level(adis::sys_alias_t c)
{
    return tlbi_by_va_last_level_table[static_cast<size_t>(c)];
}

} // namespace ampere::archx
