//
// Copyright (c) 2020 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//

#ifndef AMPERE_CORE_BRANCH_HISTORY_TYPES_H
#define AMPERE_CORE_BRANCH_HISTORY_TYPES_H

#include <deque>
#include <vector>

#include "ampere/archx/addr.h"
#include "ampere/core/fetch_id.h"

namespace ampere::core {

using namespace archx;

using BPSet = unsigned int;
using BPTag = std::uint64_t;

struct BranchHistoryEntry
{
    BranchHistoryEntry(FetchId fetch_id, ProgramCounter pc, ProgramCounter target, size_t offset)
        : fetch_id{fetch_id}, pc{pc}, target{target}, offset{offset} {};

    FetchId fetch_id;
    ProgramCounter pc;
    ProgramCounter target;

    size_t offset;
};

using BranchHistoryQueue = std::deque<BranchHistoryEntry>;
using Point = typename BranchHistoryQueue::const_reverse_iterator;

class BranchHistoryBits : public std::vector<std::uint64_t>
{
public:
    BranchHistoryBits() : std::vector<std::uint64_t>(8, 0) {}
    virtual ~BranchHistoryBits() = default;

    void build(Point beg, Point end);
    std::uint64_t get(int lo, int hi) const;

protected:
    virtual std::uint64_t hash_entry(const BranchHistoryEntry& entry) const;
};

} // namespace ampere::core

#endif // AMPERE_CORE_BRANCH_HISTORY_TYPES_H
