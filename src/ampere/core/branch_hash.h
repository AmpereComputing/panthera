//
// Copyright (c) 2020 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//

#ifndef AMPERE_CORE_BRANCH_HASH_H
#define AMPERE_CORE_BRANCH_HASH_H

#include <utility>
#include <vector>

#include "ampere/core/branch_history_types.h"
#include "ampere/sc/sc.h"

namespace ampere::core {

extern unsigned int f_hash(ProgramCounter pc);
extern unsigned int g_hash(ProgramCounter pc);

class Hasher : public sc::Module
{
public:
    Hasher(const sc_core::sc_module_name& name) : sc::Module{name} {}

    virtual void start_hashing(Point beg, Point end) = 0;
    virtual std::uint64_t hash_history(unsigned depth, int nbits) const = 0;
    virtual std::pair<BPSet, BPTag> hash_set_tag(ProgramCounter pc,
                                                 unsigned depth,
                                                 int set_bits,
                                                 int tag_bits) const = 0;
};

class FNVHasher : public sc::implements<FNVHasher, Hasher>
{
public:
    explicit FNVHasher(const sc_core::sc_module_name& name) : sc::implements<FNVHasher, Hasher>{name} {}

    void start_hashing(Point beg, Point end) override;
    std::uint64_t hash_history(unsigned depth, int nbits) const override;
    std::pair<BPSet, BPTag> hash_set_tag(ProgramCounter pc, unsigned depth, int set_bits, int tag_bits) const override;

private:
    Point hash_beg;
    Point hash_end;
};

class FNV4BPB : public sc::implements<FNV4BPB, Hasher>
{
public:
    explicit FNV4BPB(const sc_core::sc_module_name& name) : sc::implements<FNV4BPB, Hasher>{name} {}

    void start_hashing(Point beg, Point end) override;
    std::uint64_t hash_history(unsigned depth, int nbits) const override;
    std::pair<BPSet, BPTag> hash_set_tag(ProgramCounter pc, unsigned depth, int set_bits, int tag_bits) const override;

private:
    std::uint64_t hash_history_internal(unsigned depth, int nbits, int ignore_initial_bits = 0) const;

    Point hash_beg;
    Point hash_end;
};

class XORHasher : public sc::implements<XORHasher, Hasher>
{
public:
    explicit XORHasher(const sc_core::sc_module_name& name) : sc::implements<XORHasher, Hasher>{name} {}

    void start_hashing(Point beg, Point end) override;
    std::uint64_t hash_history(unsigned depth, int nbits) const override;
    std::pair<BPSet, BPTag> hash_set_tag(ProgramCounter pc, unsigned depth, int set_bits, int tag_bits) const override;

    std::uint64_t hash_history_internal(
        Point beg, Point end, unsigned depth, int nbits, int ignore_initial_bits = 0) const;

private:
    Point hash_beg;
    Point hash_end;
};

class SeznacHasher : public sc::implements<SeznacHasher, Hasher>
{
public:
    explicit SeznacHasher(const sc_core::sc_module_name& name);

    void start_hashing(Point beg, Point end) override;
    std::uint64_t hash_history(unsigned depth, int nbits) const override;
    std::pair<BPSet, BPTag> hash_set_tag(ProgramCounter pc, unsigned depth, int set_bits, int tag_bits) const override;

    sc::Param<bool> two_bits_per_branch{"two_bits_per_branch", "generate 2 bits per branch into the BHR", true};
    sc::Param<bool> adjust_set_per_index{"adjust_set_per_index", "generate a different set per global", true};
    sc::Param<bool> use_second_tag_hash{"use_second_tag_hash", "generate tag from hash(N) and hash(N-1)", true};
    sc::Param<bool> use_plus_one_tag_hash{"use_plus_one_tag_hash", "generate tag from hash(N+1) only", false};
    sc::Param<bool> use_wide_tag_hash{"use_wide_tag_hash", "generate 13-bit 'wide' tag from hash", false};

private:
    std::uint64_t hash_history_internal(unsigned depth, int nbits, int ignore_initial_bits) const;
    std::unique_ptr<BranchHistoryBits> history_bits;
};

class WideTagHasher : public sc::implements<WideTagHasher, Hasher>
{
public:
    explicit WideTagHasher(const sc_core::sc_module_name& name);
    sc::Param<int> set_ignore_bits{
        "set_ignore_bits", "how many low bits to ignore when computing a global set [0,2]", 2};
    sc::Param<bool> use_2xdepth_plus2{"use_2xdepth_plus2", "use 2*depth + 2 bits when hashing history", false};

    void start_hashing(Point beg, Point end) override;
    std::uint64_t hash_history(unsigned depth, int nbits) const override;
    std::pair<BPSet, BPTag> hash_set_tag(ProgramCounter pc, unsigned depth, int set_bits, int tag_bits) const override;

private:
    std::uint64_t hash_history_internal(unsigned depth, int nbits, int ignore_initial_bits) const;

    std::unique_ptr<BranchHistoryBits> history_bits;
};

} // namespace ampere::core

#endif // AMPERE_CORE_BRANCH_HASH_H
