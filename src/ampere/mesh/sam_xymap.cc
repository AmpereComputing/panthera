//
// Copyright (c) 2023 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//
#include "ampere/mesh/sam_xymap.h"

namespace ampere::mesh {

static uint64_t mod(int64_t a, uint64_t n);

XYMap::XYMap(size_t N, size_t M, int SNR, int grouping, bool disable_permutation)
    : N{N}, M{M}, disable_permutation{disable_permutation}
{
    k = factor2(N);
    J = N >> k;

    if (M == 0) {
        m = HA_BITS - CL_BITS - log2(N);
        M = 1ull << m;
        L = 1;
    } else {
        m = factor2(M);
        L = M >> m;
    }

    nl = factor2(N * L);
    K = (N * L) >> nl;
    M2 = 1ull << m;
    I = 0;

    for (int i = 1; i < K; i++) {
        if (((i * M2) % K) == 1) {
            I = i;
            break;
        }
    }

    snr2 = SNR == 2;
    snr4 = SNR == 4;

    two_banger = grouping == 2;
    four_banger = grouping == 4;
}

XY XYMap::decode(PhysAddr addr) const
{
    auto [x, y] = decode_internal(addr.value());
    return {x, PhysAddr{y}};
}

PhysAddr XYMap::encode(XY xy) const
{
    return PhysAddr{encode_internal({xy.first, xy.second.value()})};
}

auto XYMap::decode_internal(addr_t a) const -> std::pair<size_t, addr_t>
{
    auto [b, snr] = extract_subnuma_region(a);
    auto [c, off] = extract_cache_line(b);
    auto [d, grp] = extract_group(c);
    addr_t p = permute(d);
    auto [x, y] = atoxy(p);
    y = incorporate_group(y, grp);
    y = incorporate_cache_line(y, off);
    x = incorporate_subnuma_region(x, snr);

    return {x, y};
}

auto XYMap::encode_internal(std::pair<size_t, addr_t> xy) const -> addr_t
{
    auto [x, y] = xy;
    auto [u, snr] = inv_incorporate_subnuma_region(x);
    auto [v, off] = extract_cache_line(y);
    auto [w, grp] = extract_group(v);
    addr_t p = inv_atoxy({u, w});
    addr_t d = inv_permute(p);
    addr_t c = incorporate_group(d, grp);
    addr_t b = incorporate_cache_line(c, off);
    addr_t a = inv_extract_subnuma_region(b, snr);

    return a;
}

auto XYMap::extract_subnuma_region(addr_t a) const -> std::pair<addr_t, int>
{
    int snr = 0;

    if (snr2) {
        snr = extract(a, SNR_HI, SNR_HI);
        a &= ~mask(SNR_HI, SNR_HI);
    } else if (snr4) {
        snr = extract(a, SNR_HI, SNR_LO);
        a &= ~mask(SNR_HI, SNR_LO);
    }

    return {a, snr};
}

auto XYMap::inv_extract_subnuma_region(addr_t a, int snr) const -> addr_t
{
    if (snr2)
        return a | (static_cast<addr_t>(snr) << SNR_HI);
    else if (snr4)
        return a | (static_cast<addr_t>(snr) << SNR_LO);
    else
        return a;
}

auto XYMap::extract_cache_line(addr_t a) const -> std::pair<addr_t, int>
{
    return {a >> CL_BITS, a & mask(CL_BITS)};
}

auto XYMap::extract_group(addr_t a) const -> std::pair<addr_t, int>
{
    if (two_banger)
        return {a >> 1, a & mask(1)};
    else if (four_banger)
        return {a >> 2, a & mask(2)};
    else
        return {a, 0};
}

auto XYMap::permute(addr_t a) const -> addr_t
{
    if (disable_permutation)
        return a;

    static constexpr uint64_t m = mask(xor_width);
    addr_t aa = a;

    for (int i = 1; i <= xor_factor; i++)
        aa ^= (a >> i) & m;

    return aa;
}

auto XYMap::inv_permute(addr_t a) const -> addr_t
{
    if (disable_permutation)
        return a;

    addr_t m = mask(xor_width);
    addr_t w = a & (-m - 1);

    for (int i = 1; i <= xor_factor; i++)
        a ^= (w >> i) & m;

    w = a & m;

    for (int i = xor_factor; i < xor_width; i += xor_factor)
        w ^= w >> i;

    a ^= w >> 1;
    return a;
}

auto XYMap::atoxy(addr_t a) const -> std::pair<size_t, addr_t>
{
    auto w = (a >> k) & ((1ull << m) - 1);
    auto z = ((a >> k) % (J * L)) / J;

    auto x = a % N;
    auto y = (z << m) + w;

    return {x, y};
}

auto XYMap::inv_atoxy(std::pair<size_t, addr_t> xy) const -> addr_t
{
    auto [x, y] = xy;
    addr_t z = y >> m;
    y &= mask(m);
    addr_t xz = z * N + x;

    auto phi_inv = [this](addr_t x, addr_t y) {
        return y + mod(I * (x - y), K) * M2;
    };

    return (phi_inv(xz >> nl, y) << nl) + (xz & mask(nl));
}

auto XYMap::incorporate_group(size_t x, int grp) const -> size_t
{
    if (two_banger)
        return (x << 1) | grp;
    else if (four_banger)
        return (x << 2) | grp;
    else
        return x;
}

auto XYMap::incorporate_cache_line(addr_t y, int off) const -> addr_t
{
    return (y << CL_BITS) + off;
}

auto XYMap::incorporate_subnuma_region(addr_t y, int snr) const -> addr_t
{
    if (snr2)
        return (y << 1) | snr;
    else if (snr4)
        return (y << 2) | snr;
    else
        return y;
}

auto XYMap::inv_incorporate_subnuma_region(addr_t y) const -> std::pair<addr_t, int>
{
    if (snr2)
        return {y >> 1, y & mask(1)};
    else if (snr4)
        return {y >> 2, y & mask(2)};
    else
        return {y, 0};
}

uint64_t mod(int64_t a, uint64_t n)
{
    // the C operator % is the remainder
    // it is different from the modulo when applied on negative number
    return (a >= 0) ? a % n : (n - ((-a) % n)) % n;
}

} // namespace ampere::mesh
