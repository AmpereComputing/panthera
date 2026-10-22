//
// Copyright (c) 2019 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//

#include "ampere/core/branch_history.h"
#include "ampere/sc/report.h"

namespace ampere::core {

void BranchHistory::add_history_size(size_t size)
{
    AMPERE_ASSERT(depths.empty() || size > depths.back(), "branch history depths must be in ascending order");
    AMPERE_ASSERT(contents.empty(), "add_history_size() must be called before any history is pushed");
    depths.push_back(size);
}

// `target` is initialized to 0 for non-indirect branches so that the branch
// target is only hashed in for indirect branches
void BranchHistory::push(FetchId fetch_id, ProgramCounter branch_pc, ProgramCounter target)
{
    if (!contents.empty() && contents.back().fetch_id >= fetch_id)
        AMPERE_ERROR("fetch ids must increase");

    AMPERE_ASSERT(!depths.empty(), "no history depths were defined");
    contents.emplace_back(fetch_id, branch_pc, target, current_offset++);
    AMPERE_ASSERT(current_offset >= oldest_uncommitted_offset,
                  "BranchHistory::push current_offset < oldest_uncommitted_offset");
}

// By committing, we are asserting that we will no longer need to examine branch history from
// the point of view of this fetch_id or anything older than it (i.e., the oldest branch in the
// machine has a younger [greater] fetch_id). This allows us to prune away entries that are no
// longer reachable. An unreachable entry is one that is further away from the oldest active
// fetch_id than the deepest history we ever need to examine.
//
// For example, let's say our maximum depth was 3 and we are asked to commit/remove fetch X.
// That means we only need to keep enough history to look backwards 3 entries from Y, which
// is now the oldest in the machine.
//
// older . . . . . . . .  . . . younger
//
//     _________
//    /         \ <-- max hist 3
//   +---+---+---+---+---+---+---+
//   |   |   |   | W | X | Y | Z | . . .
//   +---+---+---+---+---+---+---+
//
//             _________
//            /         \ <-- max hist 3
//           +---+---+---+---+---+
//           |   |   |   | Y | Z | . . .
//           +---+---+---+---+---+
//
void BranchHistory::commit(FetchId fetch_id)
{
    // Returns "Y" or end (the oldest uncommitted fetch)
    auto oldest_fetch = std::upper_bound(
        contents.begin(), contents.end(), fetch_id, [](FetchId fetch_id, const BranchHistoryEntry& entry) {
            return fetch_id < entry.fetch_id;
        });

    oldest_uncommitted_offset = oldest_fetch == contents.end() ? current_offset : oldest_fetch->offset;
    AMPERE_ASSERT(current_offset >= oldest_uncommitted_offset,
                  "BranchHistory::commit current_offset < oldest_uncommitted_offset");
    size_t depth =
        oldest_fetch == contents.end() ? contents.size() : oldest_uncommitted_offset - contents.front().offset;
    auto max_history = depths.back() + 10;

    while (depth-- > max_history)
        contents.pop_front();
}

void BranchHistory::flush(FetchId fetch_id)
{
    auto flush_point = std::lower_bound(
        contents.begin(), contents.end(), fetch_id, [](const BranchHistoryEntry& entry, FetchId fetch_id) {
            return entry.fetch_id < fetch_id;
        });
    contents.erase(flush_point, contents.end());
    current_offset = contents.empty() ? 0 : contents.back().offset + 1;
    AMPERE_ASSERT(current_offset >= oldest_uncommitted_offset,
                  "BranchHistory::flush current_offset < oldest_uncommitted_offset");
}

void BranchHistory::start_hashing(FetchId fetch_id)
{
    hasher->start_hashing(history_begin(fetch_id), history_end());
}

std::uint64_t BranchHistory::hash(unsigned depth, int nbits) const
{
    return hasher->hash_history(depth, nbits);
}

std::pair<BPSet, BPTag> BranchHistory::hash(ProgramCounter pc, unsigned depth, int set_bits, int tag_bits) const
{
    return hasher->hash_set_tag(pc, depth, set_bits, tag_bits);
}

auto BranchHistory::history_begin(FetchId fetch_id) const -> Point
{
    auto point = std::lower_bound(
        contents.begin(), contents.end(), fetch_id, [](const BranchHistoryEntry& entry, FetchId fetch_id) {
            return entry.fetch_id < fetch_id;
        });

    // make_reverse_iterator() has the property that the reversed iterator points to
    // the element preceding the one it was built from, which is exactly what we want
    // here since that represents the "history" up to but not including the fetch_id
    // we used as a lower_bound.
    //
    return std::make_reverse_iterator(point);
}

std::string BranchHistory::history_as_string(FetchId fetch_id, unsigned depth)
{
    std::stringstream ss;
    ss << std::hex;

    bool delim = false;

    for (auto point = history_begin(fetch_id); point != history_end() && depth; ++point, --depth) {
        if (delim)
            ss << ',';
        ss << "0x" << point->pc;
        if (point->target != 0_pc)
            ss << "/0x" << point->target;
        delim = true;
    }

    return ss.str();
}

std::string BranchHistory::compressed_history_as_string(FetchId fetch_id)
{
    BranchHistoryBits history_bits;
    std::stringstream ss;
    ss << std::hex;

    history_bits.build(history_begin(fetch_id), history_end());

    for (int i = history_bits.size() - 1; i >= 0; i--) {
        ss << history_bits[i];
    }

    // strip any leading 0s, but leave "0x0" if its truly empty
    auto result = ss.str();
    result.erase(0, std::min(result.find_first_not_of('0'), result.size() - 1));

    return "0x" + result;
}

bool BranchHistory::same_contents(const BranchHistoryQueue& other, size_t max_depth = 0) const
{
    size_t n = std::min(contents.size(), depths.back());
    if (max_depth != 0)
        n = std::min(n, max_depth);

    if (n <= other.size()) {
        return std::equal(contents.rbegin(),
                          contents.rbegin() + n,
                          other.rbegin(),
                          [](const BranchHistoryEntry& a, const BranchHistoryEntry& b) {
                              return a.pc == b.pc && a.target == b.target;
                          });
    } else {
        return false;
    }
}

} // namespace ampere::core
