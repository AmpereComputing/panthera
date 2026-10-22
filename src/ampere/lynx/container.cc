/*
 * Copyright (c) 2024 Ampere Computing. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "container.h"
#include "error.h"

namespace ampere::lynx {

auto Container::find_component(Location loc) -> Component*
{
    auto it = m_index_by_loc.find(loc);

    if (it == m_index_by_loc.end())
        throw NoComponent("no component at {}", loc);
    else
        return it->second;
}

auto Container::find_component(Location loc) const -> const Component*
{
    auto it = m_index_by_loc.find(loc);

    if (it == m_index_by_loc.end())
        throw NoComponent("no component at {}", loc);
    else
        return it->second;
}

static constexpr int ispow2(uint64_t n)
{
    return n && ((n & (n - 1)) == 0);
}

static constexpr int ilog2(uint64_t n, bool up = 0)
{
    bool p2 = ispow2(n);
    int c = 0;

    while (n) {
        c++;
        n >>= 1;
    }

    return (p2 || !up) ? c - 1 : c;
}

/* Build a specialized, fast index for routers since it is used constantly during
 * path finding. The index will simply be a table (vector) mapping location to a
 * router. The complexity here is that we have a variable number for X and Y, so
 * we need to discover how many routers we have total in order to efficiently
 * build the table.
 */
void Container::build_rtr_index() const
{
    u8 max_die_id{0};
    u8 max_x{0};
    u8 max_y{0};

    for (const auto& rtr : components_by_type(ComponentType::RTR)) {
        max_die_id = std::max(rtr->die_id(), max_die_id);
        max_x = std::max(rtr->x(), max_x);
        max_y = std::max(rtr->y(), max_y);
    }

    m_rtr_idx_x_bits = ilog2(max_x + 1, true);
    m_rtr_idx_y_bits = ilog2(max_y + 1, true);

    auto idx_size = (max_die_id + 1) * (max_x + 1) * (max_y + 1);
    m_rtr_index.resize(idx_size);

    for (const auto& rtr : components_by_type(ComponentType::RTR))
        m_rtr_index[loc_to_rtr_index(rtr->loc())] = static_cast<RTR*>(const_cast<Component*>(rtr));
}

} // namespace ampere::lynx
