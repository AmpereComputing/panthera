//
// Copyright (c) 2019 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//

#ifndef AMPERE_CORE_BRANCH_HISTORY_H
#define AMPERE_CORE_BRANCH_HISTORY_H

#include <vector>

#include "ampere/archx/addr.h"
#include "ampere/archx/branch_type.h"
#include "ampere/core/branch_hash.h"
#include "ampere/core/branch_history_types.h"
#include "ampere/core/fetch_id.h"
#include "ampere/sc/sc.h"

namespace ampere::core {

using namespace archx;

class BranchHistory : public sc::Module
{
public:
    explicit BranchHistory(const sc_core::sc_module_name& name) : sc::Module{name} {}

    void add_history_size(size_t size);

    void push(FetchId fetch_id, ProgramCounter branch_pc, ProgramCounter target = 0_pc);
    void commit(FetchId fetch_id);
    void flush(FetchId fetch_id);

    void start_hashing(FetchId fetch_id);
    std::uint64_t hash(unsigned depth, int nbits = 64) const;
    std::pair<BPSet, BPTag> hash(ProgramCounter pc, unsigned depth, int set_bits, int tag_bits) const;

    std::string history_as_string(FetchId fetch_id, unsigned depth);
    std::string compressed_history_as_string(FetchId fetch_id);

    const BranchHistoryQueue& get_contents() const { return contents; }
    bool same_contents(const BranchHistoryQueue& other, size_t max_depth) const;
    size_t uncommitted_depth() { return current_offset - oldest_uncommitted_offset; }

protected:
    BranchHistoryQueue contents;
    std::vector<size_t> depths;

    size_t current_offset{0};
    size_t oldest_uncommitted_offset{0};

    sc::Submodule<Hasher, WideTagHasher> hasher{"hash"};

    Point history_begin() const { return contents.rbegin(); }
    Point history_begin(FetchId fetch_id) const;
    Point history_end() const { return contents.rend(); }

    size_t size() const { return contents.size(); }
};

} // namespace ampere::core

#endif // AMPERE_CORE_BRANCH_HISTORY_H
