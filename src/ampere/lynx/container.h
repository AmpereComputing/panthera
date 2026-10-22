/*
 * Copyright (c) 2024 Ampere Computing. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#pragma once

#include <unordered_map>
#include <vector>

#include "components.h"
#include "error.h"
#include "location.h"

namespace ampere::lynx {

class Container
{
public:
    // Find a generic component by location
    //
    Component* find_component(Location loc);
    const Component* find_component(Location loc) const;

    // Find a router by location. This is slightly different than other
    // finds in that it strips off the port/didx from the location so
    // that you can easily look up a co-located router from another
    // comonent's location.
    //
    RTR* find_rtr(Location loc)
    {
        if (m_rtr_index.empty())
            build_rtr_index();

        return m_rtr_index.at(loc_to_rtr_index(loc));
    }

    const RTR* find_rtr(Location loc) const
    {
        if (m_rtr_index.empty())
            build_rtr_index();

        return m_rtr_index.at(loc_to_rtr_index(loc));
    }

    // Find a particular type of component. This will return nullptr
    // if either there is no component at the location, or if it is
    // the wrong type.
    //
    template<typename T>
    T* find(Location loc);
    template<typename T>
    const T* find(Location loc) const;

    const std::vector<const Component*>& components_by_type(ComponentType dtype) const
    {
        return m_index_by_type.at(static_cast<int>(dtype));
    }

    const std::vector<const Component*> all_components() const
    {
        std::vector<const Component*> all;
        for (const auto& c : m_index_by_loc)
            all.push_back(c.second);
        return all;
    }

protected:
    std::unordered_map<Location, Component*> m_index_by_loc;
    std::vector<std::vector<const Component*>> m_index_by_type{util::enum_size<ComponentType>()};

private:
    void build_rtr_index() const;
    int loc_to_rtr_index(Location loc) const
    {
        int idx = loc.die_id;
        idx = (idx << m_rtr_idx_x_bits) + loc.x;
        idx = (idx << m_rtr_idx_y_bits) + loc.y;
        return idx;
    }

    mutable std::vector<RTR*> m_rtr_index{};
    mutable int m_rtr_idx_x_bits;
    mutable int m_rtr_idx_y_bits;
};

template<typename T>
auto Container::find(Location loc) -> T*
{
    auto it = m_index_by_loc.find(loc);

    if (it == m_index_by_loc.end() || it->second->dtype() != T::TYPE)
        throw NoComponent("no component of type {} at {}", T::TYPE, loc);
    else
        return static_cast<T*>(it->second);
}

template<typename T>
auto Container::find(Location loc) const -> const T*
{
    auto it = m_index_by_loc.find(loc);

    if (it == m_index_by_loc.end() || it->second->dtype() != T::TYPE)
        throw NoComponent("no component of type {} at {}", T::TYPE, loc);
    else
        return static_cast<const T*>(it->second);
}

} // namespace ampere::lynx
