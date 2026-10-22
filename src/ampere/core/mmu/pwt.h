//
// Copyright (c) 2019 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//

#pragma once

#include <cstdint>
#include <stdexcept>

#include "ampere/archx/addr.h"

namespace ampere::core {

using archx::PhysAddr;
using archx::VirtAddr;

struct PWTEntry
{
    VirtAddr virt_addr;
    bool starting_level_fetched{false};
    bool done{false};
    uint8_t granule_size{0};   /* 4kb = 12, 16kb = 14, 64kb = 16 */
    uint8_t input_size{0};     /* Size of input address in bits */
    uint8_t output_size{0};    /* Size of output address in bits */
    uint8_t level{0};          /* walk level currently being processed */
    PhysAddr pte_addr[4] = {}; /* indexed by level */
    uint64_t pte[4] = {};      /* indexed by level */

    uint8_t stride() const
    {
        assert(granule_size);

        return granule_size - 3;
    }

    /* Determine the starting level of the walk */
    uint8_t starting_level() const
    {
        assert(granule_size && input_size);

        uint8_t level = 4 - ((input_size - granule_size) / stride());
        if (level > 4) {
            throw std::runtime_error("pwt.h starting_level() computation underflowed");
        }
        if ((input_size - granule_size) % stride()) {
            level--;
        }
        return level;
    }

    /* Calculate the index into the table from the correct bits in the VA */
    uint16_t index(uint8_t lvl) const
    {
        assert(granule_size && input_size);
        uint8_t addrselecttop, addrselectbottom;

        if (lvl < starting_level()) {
            return 0;
        } else if (lvl == starting_level()) {
            addrselecttop = input_size - 1;
        } else {
            addrselecttop = (4 - lvl) * stride() + granule_size - 1;
        }
        addrselectbottom = (3 - lvl) * stride() + granule_size;

        return virt_addr.extract_range(addrselecttop, addrselectbottom);
    }
};

} // namespace ampere::core

