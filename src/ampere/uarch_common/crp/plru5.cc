//
// Copyright (c) 2022 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//

#include "ampere/uarch_common/crp/plru5.h"

namespace ampere::uarch_common {

void PLRU5::init(size_t _sets, size_t _ways)
{
    //    sc_core::sc_report_handler::set_verbosity_level(sc_core::SC_DEBUG);
    sets = _sets;
    ways = _ways;
    lru_node.resize(sets);
    lpu_ptr.resize(sets, 0);
    for (unsigned i = 0; i < sets; i++) {
        lru_node[i].resize(3, 0);
    }

    AMPERE_UNCATEGORIZED_DEBUG("{} init: sets {}, ways {}", name(), sets, ways);
    if (ways != 5) {
        AMPERE_ERROR("PLRU5 only supports 5-way associativity");
    }
}

// PLRU5 should be updated on usage (hit or fill)
void PLRU5::touch(size_t set, size_t way, TouchOp op, bool nontemporal)
{
    [[maybe_unused]] auto last_lpu_ptr = lpu_ptr[set];
    auto last_lru_node = lru_node[set];
    // first touch lpu_ptr
    switch (way) {
    case 0:
    case 1:
        if (lpu_ptr[set] == 0) {
            lpu_ptr[set] = 1;
        }
        break;
    case 2:
    case 3:
        if (lpu_ptr[set] == 1) {
            lpu_ptr[set] = 2;
        }
        break;
    case 4:
        if (lpu_ptr[set] == 2) {
            lpu_ptr[set] = 0;
        }
        break;
    }

    // touch on lru_node
    switch (way) {
    case 0:
        if (lru_node[set][0] == 0) {
            lru_node[set][0] = 1;
        }
        break;
    case 1:
        if (lru_node[set][0] == 1) {
            lru_node[set][0] = 0;
        }
        break;
    case 2:
        if (lru_node[set][1] == 0) {
            lru_node[set][1] = 1;
        }
        break;
    case 3:
        if (lru_node[set][1] == 1) {
            lru_node[set][1] = 0;
        }
        break;
    case 4:
        // Third group only has one way, so no lru_node is always 0
        AMPERE_ASSERT(lru_node[set][2] == 0, "PLRU5: group C pointer should always be 0");
        break;
    }
    AMPERE_UNCATEGORIZED_DEBUG("plru5  touch: set {}, way {}, lpu_ptr {} --> {}, lru_node {}{}{} --> {}{}{}",
                               set,
                               way,
                               last_lpu_ptr,
                               lpu_ptr[set],
                               last_lru_node[2],
                               last_lru_node[1],
                               last_lru_node[0],
                               lru_node[set][2],
                               lru_node[set][1],
                               lru_node[set][0]);
}

// Choose a victim for replacement
size_t PLRU5::victim(size_t set_index)
{
    size_t chosen(0);
    auto last_lpu_ptr = lpu_ptr[set_index];
    auto last_lru_node = lru_node[set_index];

    chosen = 2 * last_lpu_ptr + last_lru_node[last_lpu_ptr];
    AMPERE_ASSERT(chosen >= 0 && chosen < 5, "PLRU5 chosen way out of bounds");

    AMPERE_UNCATEGORIZED_DEBUG("plru5 victim: set {}, chosen {}, lpu_ptr {}, lru_node {}{}{}",
                               set_index,
                               chosen,
                               last_lpu_ptr,
                               last_lru_node[2],
                               last_lru_node[1],
                               last_lru_node[0]);
    return chosen;
}

void PLRU5::invalidate(size_t set, size_t way) {}

} // namespace ampere::uarch_common
