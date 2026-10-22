//
// Copyright (c) 2020 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//

#include "ampere/core/branch_hash.h"

namespace ampere::core {

using ampere::util::fold_to_n_bits;

// Fowler-Noll-Vo hash function (FNV-1, see https://en.wikipedia.org/wiki/Fowler%E2%80%93Noll%E2%80%93Vo_hash_function)
//
// A hardware-unrealistic hash function that's used as a base line "high quality hash" for reference.
//
std::uint64_t fnv_hash_word(std::uint64_t data, std::uint64_t h = 14695981039346656037ull)
{
    const std::uint64_t fnv_prime{1099511628211};

    for (size_t i = 0; i < sizeof(std::uint64_t); i++) {
        h *= fnv_prime;
        h ^= (data & 0xffu);
        data >>= 8u;
    }

    return h;
}

// Utility function for hashing a branch PC to 4 bits. Performs:
//
// F(PC) [3..0] = PC[5:2] ^ PC[9:6] ^ PC[13:10] ^ PC[17:14] ^ PC[21:18] ^
//                PC[25:22] ^ PC[29:26] ^ PC[33:30] ^ PC[37:34] ^ PC[41:28] ^
//                PC[45:32] ^ PC[49:46] ^ PC[52:50]
unsigned int f_hash(ProgramCounter pc)
{
    auto x = pc.extract_range(52, 6);

    x ^= x >> 32;
    x ^= x >> 16;
    x ^= x >> 8;
    x ^= x >> 4;

    return (x ^ pc.extract_range(5, 2)) & 0b1111;
}

// Utility function for hashing a target PC to 2 bits.
//
unsigned int g_hash(ProgramCounter pc)
{
    auto x = pc.extract_range(52, 2);

    x ^= x >> 32;
    x ^= x >> 16;
    x ^= x >> 8;
    x ^= x >> 4;
    x ^= x >> 2;

    return x & 0b11;
}

void BranchHistoryBits::build(Point beg, Point end)
{
    std::fill(this->begin(), this->end(), 0);

    size_t word = 0;
    int shift = 0;
    std::uint64_t tmp = 0;

    for (auto point = beg; point != end; ++point) {
        auto& entry = *point;

        std::uint64_t h = hash_entry(entry);
        tmp ^= h << shift;

        if (shift == 62) {
            if (word >= this->size())
                this->resize(word + 1);

            (*this)[word] = tmp;
            tmp = h >> 2;
            shift = 0;
            ++word;
        } else {
            shift += 2;
        }
    }

    if (word >= this->size())
        this->resize(word + 1);

    (*this)[word] = tmp;
}

std::uint64_t BranchHistoryBits::get(int lo, int hi) const
{
    int lo_word = lo / 64;
    int hi_word = hi / 64;

    int lo_offset = lo % 64;
    int hi_offset = hi % 64;

    if (lo_word == hi_word) {
        return util::extract((*this)[lo_word], hi_offset, lo_offset);
    } else {
        std::uint64_t tmp = util::extract((*this)[lo_word], 63, lo_offset);
        tmp |= ((*this)[hi_word] & util::mask(hi_offset + 1)) << (64 - lo_offset);
        return tmp;
    }
}

std::uint64_t BranchHistoryBits::hash_entry(const BranchHistoryEntry& entry) const
{
    return f_hash(entry.pc) ^ g_hash(entry.target);
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// The FNVHasher is used as a reference hash. It is not a cryptographic hash, but is otherwise high quality in
// terms of distribution of values, mixing of bits, etc. The FNVHasher does not work from a realistic representation
// of the BHR, but instead uses the full branch PCs and target PCs in its hash. That way, we can study the loss of
// precision that results from various design decisions.
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void FNVHasher::start_hashing(Point beg, Point end)
{
    hash_beg = beg;
    hash_end = end;
}

std::pair<BPSet, BPTag> FNVHasher::hash_set_tag(ProgramCounter pc, unsigned depth, int set_bits, int tag_bits) const
{
    auto h = hash_history(depth, 64);
    h = fold_to_n_bits((h >> 2u) ^ pc.line_number(), set_bits + tag_bits);
    return {h & util::mask(set_bits), h >> set_bits};
}

std::uint64_t FNVHasher::hash_history(unsigned depth, int nbits) const
{
    std::uint64_t h = 14695981039346656037ull;

    for (auto point = hash_beg; depth && point != hash_end; ++point, --depth) {
        std::uint64_t val = point->pc.value();
        if (point->target.value())
            val ^= point->target.value() >> 2u;
        h = fnv_hash_word(val, h);
    }

    return fold_to_n_bits(h, nbits);
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// FNV4BPB (FNV "4 bits per branch") is variation of the FNVHasher that uses FNV to digest a 4-bit representation
// of branch PCs and targets. Thus it's a step more realistic than FNVHasher, since there is a lot of aliasing
// as you collapse from a full PC to 4 bits. However, it is more aligned with how the BHR actually works. Those
// 4-bit values are still combined with a high quality hash, however.
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void FNV4BPB::start_hashing(Point beg, Point end)
{
    hash_beg = beg;
    hash_end = end;
}

std::pair<BPSet, BPTag> FNV4BPB::hash_set_tag(ProgramCounter pc, unsigned depth, int set_bits, int tag_bits) const
{
    auto set_history = hash_history_internal(depth, set_bits, 2);
    auto tag_history = hash_history_internal(depth, tag_bits);

    BPSet set = set_history ^ fold_to_n_bits(pc.extract_range(set_bits * 2 + 6, 6), set_bits);
    BPTag tag = tag_history ^ fold_to_n_bits(pc.extract_range(52, 6), tag_bits);

    return {set, tag};
}

std::uint64_t FNV4BPB::hash_history(unsigned depth, int nbits) const
{
    return hash_history_internal(depth, nbits, 2);
}

std::uint64_t FNV4BPB::hash_history_internal(unsigned depth, int nbits, int ignore_initial_bits) const
{
    std::uint64_t h = 14695981039346656037ull;

    for (auto point = hash_beg; depth && point != hash_end; ++point, --depth) {
        auto val = f_hash(point->pc) ^ g_hash(point->target);
        val >>= ignore_initial_bits;
        h = fnv_hash_word(val, h);
        ignore_initial_bits = 0;
    }

    return fold_to_n_bits(h, nbits);
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// XORHasher is based on an initial POR for the Furiosa core. It combines the 4-bit branch PC/target hashes into a
// chain (the same way as the BHR, but it does this on the fly), and the folds the bits down with XORs. It seems
// to have some aliasing problems where some branch patterns are indistinguishable, and this leads to poor
// performance.
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void XORHasher::start_hashing(Point beg, Point end)
{
    hash_beg = beg;
    hash_end = end;
}

std::pair<BPSet, BPTag> XORHasher::hash_set_tag(ProgramCounter pc, unsigned depth, int set_bits, int tag_bits) const
{
    auto set_history = hash_history_internal(hash_beg, hash_end, depth, set_bits, 2);
    auto tag_history = hash_history_internal(hash_beg, hash_end, depth, tag_bits);

    BPSet set = set_history ^ fold_to_n_bits(pc.extract_range(set_bits * 2 + 6, 6), set_bits);
    BPTag tag = tag_history ^ fold_to_n_bits(pc.extract_range(52, 6), tag_bits);

    return {set, tag};
}

std::uint64_t XORHasher::hash_history(unsigned depth, int nbits) const
{
    return hash_history_internal(hash_beg, hash_end, depth, nbits);
}

std::uint64_t XORHasher::hash_history_internal(
    Point beg, Point end, unsigned depth, int nbits, int ignore_initial_bits) const
{
    int total_bits = depth * 2 + 2;

    std::uint64_t output = 0;
    std::uint64_t tmp = 0;
    int shift = -ignore_initial_bits;

    for (auto point = beg; total_bits && point != end; ++point) {
        auto h = f_hash(point->pc) ^ g_hash(point->target);

        if (shift < 0)
            tmp ^= (h >> -shift);
        else
            tmp ^= (h << shift);

        shift += 2;
        total_bits -= 2;

        if (shift >= nbits) {
            output ^= tmp & util::mask(nbits);
            tmp >>= nbits;
            shift -= nbits;
        }
    }

    if (total_bits) // ran out of history before getting all our bits
        output ^= tmp;
    else
        output ^= tmp & util::mask(shift);

    return output;
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// SeznacHasher tries to be as close as possible to Andre Seznac's TAGE hash.
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

class SeznacBranchHistoryBits : public BranchHistoryBits
{
public:
    using BranchHistoryBits::BranchHistoryBits;

protected:
    std::uint64_t hash_entry(const BranchHistoryEntry& entry) const override
    {
        auto x = entry.pc.value() >> 2;
        return (((x ^ 1) & 1) << 1) | ((x >> 1) & 1);
    }
};

SeznacHasher::SeznacHasher(const sc_core::sc_module_name& name) : sc::implements<SeznacHasher, Hasher>{name}
{
    if (two_bits_per_branch)
        history_bits = std::make_unique<SeznacBranchHistoryBits>();
    else
        history_bits = std::make_unique<BranchHistoryBits>();
}

void SeznacHasher::start_hashing(Point beg, Point end)
{
    history_bits->build(beg, end);
}

std::pair<BPSet, BPTag> SeznacHasher::hash_set_tag(ProgramCounter pc, unsigned depth, int set_bits, int tag_bits) const
{
    static std::map<unsigned, int> depth_to_index = {
        {1, 1},
        {2, 2},
        {3, 3},
        {4, 4},
        {7, 5},
        {11, 6},
        {18, 7},
        {29, 8},
        {47, 9},
        {76, 10},
        {123, 11},
        {199, 12},
    };

    auto cache_line = pc.line_number();
    unsigned adj;

    if (adjust_set_per_index)
        adj = abs(set_bits - depth_to_index.at(depth)) + 1;
    else
        adj = 2;

    auto set = (cache_line ^ (cache_line >> adj) ^ hash_history_internal(depth, set_bits, 0)) & util::mask(set_bits);

    if (use_plus_one_tag_hash) {
        auto tag = fold_to_n_bits(cache_line ^ hash_history(depth, tag_bits + 1), tag_bits);
        return {set, tag};
    } else if (use_wide_tag_hash) {
        auto tag = fold_to_n_bits(cache_line ^ hash_history_internal(depth, 13, 0), tag_bits);
        return {set, tag};
    } else {
        auto tag_h1 = hash_history(depth, tag_bits);
        auto tag_h2 = use_second_tag_hash ? hash_history(depth, tag_bits - 1) : 0;
        auto tag = (cache_line ^ tag_h1 ^ (tag_h2 << 1)) & util::mask(tag_bits);

        return {set, tag};
    }
}

std::uint64_t SeznacHasher::hash_history(unsigned depth, int nbits) const
{
    return hash_history_internal(depth, nbits, 0);
}

std::uint64_t SeznacHasher::hash_history_internal(unsigned depth, int nbits, int ignore_initial_bits) const
{
    std::uint64_t output = 0;

    int total_bits = depth * 2 + (2 - ignore_initial_bits);
    int offset = ignore_initial_bits;

    while (total_bits > 0) {
        int n = std::min(nbits, total_bits);
        output ^= history_bits->get(offset, offset + n - 1);
        offset += n;
        total_bits -= n;
    }

    return output;
}

WideTagHasher::WideTagHasher(const sc_core::sc_module_name& name) : sc::implements<WideTagHasher, Hasher>{name}
{
    history_bits = std::make_unique<BranchHistoryBits>();
}

void WideTagHasher::start_hashing(Point beg, Point end)
{
    history_bits->build(beg, end);
}

std::pair<BPSet, BPTag> WideTagHasher::hash_set_tag(ProgramCounter pc, unsigned depth, int set_bits, int tag_bits) const
{
    auto set_history = hash_history_internal(depth, set_bits, set_ignore_bits);
    auto tag_history = fold_to_n_bits(hash_history(depth, std::max(13, tag_bits)), tag_bits);

    BPSet set = set_history ^ fold_to_n_bits(pc.extract_range(set_bits * 2 + 6 - 1, 6), set_bits);
    BPTag tag = tag_history ^ fold_to_n_bits(pc.extract_range(52, 6 + set_bits), tag_bits);

    return {set, tag};
}

std::uint64_t WideTagHasher::hash_history(unsigned depth, int nbits) const
{
    return hash_history_internal(depth, nbits, 0);
}

std::uint64_t WideTagHasher::hash_history_internal(unsigned depth, int nbits, int ignore_initial_bits) const
{
    std::uint64_t output = 0;

    int total_bits = depth * 2 - ignore_initial_bits;
    int offset = ignore_initial_bits;

    if (use_2xdepth_plus2)
        total_bits += 2;

    while (total_bits > 0) {
        int n = std::min(nbits, total_bits);
        output ^= history_bits->get(offset, offset + n - 1);
        offset += n;
        total_bits -= n;
    }

    return output;
}

} // namespace ampere::core
