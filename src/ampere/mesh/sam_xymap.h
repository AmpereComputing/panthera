//
// Copyright (c) 2024 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//
#pragma once

#include <cstdint>
#include <utility>

#include "ampere/sc/addr.h"
#include "ampere/util/bits.h"

namespace ampere::mesh {

using namespace archx;
using namespace sc;

using XY = std::pair<size_t, PhysAddr>;

class XYMap
{
public:
    XYMap(size_t N, size_t M = 0, int SNR = 1, int grouping = 1, bool disable_permutation = false);

    XY decode(PhysAddr addr) const;
    PhysAddr encode(XY xy) const;

    static constexpr int PA_BITS = 48;
    static constexpr int CL_BITS = PhysAddr::LINE_BITWIDTH;
    static constexpr int HA_BITS = PA_BITS - 1;
    static constexpr int SNR_HI = 45;
    static constexpr int SNR_LO = 44;

private:
    using addr_t = uint64_t;

    static constexpr int xor_factor = 3;
    static constexpr int xor_width = factor2(96); // chosen to match # of PmHAS, but might not be so sensitive

    size_t N;
    size_t M;

    bool snr2;
    bool snr4;
    bool two_banger;
    bool four_banger;

    // For the AtoXY mapping, we derive these factors of N and M up front:
    //
    //     N == J * 2^k
    //     M == L * 2^m
    int k;
    int m;
    int J;
    int L;

    // For the inverse AtoXY mapping, we also have these:
    //
    //     M2    == 2^m
    //     N * L == K * 2^(nl)
    size_t M2;
    int K;
    int nl;
    int I;

    bool disable_permutation;

    std::pair<size_t, addr_t> decode_internal(addr_t a) const;
    addr_t encode_internal(std::pair<size_t, addr_t> xy) const;

    auto extract_subnuma_region(addr_t a) const -> std::pair<addr_t, int>;
    auto inv_extract_subnuma_region(addr_t a, int snr) const -> addr_t;
    auto extract_cache_line(addr_t a) const -> std::pair<addr_t, int>;
    auto extract_group(addr_t a) const -> std::pair<addr_t, int>;
    auto permute(addr_t a) const -> addr_t;
    auto inv_permute(addr_t a) const -> addr_t;
    auto atoxy(addr_t a) const -> std::pair<size_t, addr_t>;
    auto inv_atoxy(std::pair<size_t, addr_t> xy) const -> addr_t;
    auto incorporate_group(addr_t y, int grp) const -> addr_t;
    auto incorporate_cache_line(addr_t y, int off) const -> addr_t;
    auto incorporate_subnuma_region(size_t x, int snr) const -> size_t;
    auto inv_incorporate_subnuma_region(addr_t y) const -> std::pair<addr_t, int>;

    friend class XYMapTest;
};

} // namespace ampere::mesh
