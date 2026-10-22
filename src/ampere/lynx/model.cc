/*
 * Copyright (c) 2024-2025 Ampere Computing. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "model.h"
#include <utility>

#include "error.h"

namespace ampere::lynx {

class ADI;

void ModelBase::add_component(const Component* comp)
{
    m_components.push_back(comp);
    reindex = true;
}

const Component* ModelBase::find_component(Location loc) const
{
    if (reindex)
        const_cast<ModelBase*>(this)->build_index();

    try {
        return m_index_by_loc.at(loc);
    } catch (const std::out_of_range&) {
        throw NoComponent{"no component at {}", loc};
    }
}

const std::vector<const Component*>& ModelBase::components_by_type(ComponentType type) const
{
    if (reindex)
        const_cast<ModelBase*>(this)->build_index();

    try {
        return m_index_by_type.at(std::to_underlying(type));
    } catch (const std::out_of_range&) {
        throw NoComponent{"no components of type {}", type};
    }
}

const Component* ModelBase::find_rtr(Location loc) const
{
    if (reindex)
        const_cast<ModelBase*>(this)->build_index();

    try {
        return m_rtr_index.at(loc_to_rtr_index(loc));
    } catch (const std::out_of_range&) {
        throw NoComponent{"no router at {}", loc};
    }
}

const Component* ModelBase::find_by_tag(u8 die_id, const std::string_view& tag) const
{
    if (reindex)
        const_cast<ModelBase*>(this)->build_index();

    try {
        return m_index_by_tag.at({die_id, tag});
    } catch (const std::out_of_range&) {
        throw NoComponent{"no component with tag {} in die {}", tag, die_id};
    }
}

void ModelBase::build_index()
{
    m_index_by_loc.clear();
    m_index_by_type.clear();
    m_index_by_type.resize(util::enum_size<ComponentType>());
    m_index_by_tag.clear();

    for (const auto comp : m_components) {
        if (m_index_by_loc[comp->loc()])
            throw DuplicateLocation{
                "two components share the same location: they are {} and {}", *m_index_by_loc[comp->loc()], *comp};

        m_index_by_loc[comp->loc()] = comp;
        m_index_by_type[std::to_underlying(comp->type())].push_back(comp);

        if (!comp->tag().empty())
            m_index_by_tag[{comp->die_id(), comp->tag()}] = comp;
    }

    // This must happen after the above index builds, because we use the index of
    // components by type to find the latest set of routers.
    //
    build_rtr_index();

    reindex = false;
}

static constexpr int ispow2(uint64_t n)
{
    return n && ((n & (n - 1)) == 0);
}

static constexpr int ilog2(uint64_t n, bool up = false)
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
void ModelBase::build_rtr_index()
{
    m_rtr_index.clear();

    u8 max_die_id{0};
    u8 max_x{0};
    u8 max_y{0};

    for (const auto& rtr : m_index_by_type.at(std::to_underlying(ComponentType::RTR))) {
        max_die_id = std::max(rtr->die_id(), max_die_id);
        max_x = std::max(rtr->x(), max_x);
        max_y = std::max(rtr->y(), max_y);
    }

    m_rtr_idx_x_bits = ilog2(max_x + 1, true);
    m_rtr_idx_y_bits = ilog2(max_y + 1, true);
    auto die_bits = ilog2(max_die_id + 1, true);

    auto idx_size = 1u << (m_rtr_idx_x_bits + m_rtr_idx_y_bits + die_bits);
    m_rtr_index.resize(idx_size);

    for (const auto& rtr : m_index_by_type.at(std::to_underlying(ComponentType::RTR)))
        m_rtr_index.at(loc_to_rtr_index(rtr->loc())) = rtr;
}

void ModelBase::connect_components(const Component* a, const Component* b)
{
    connect_components_one_way(a, b);
    connect_components_one_way(b, a);
}

void ModelBase::connect_components_one_way(const Component* a, const Component* b)
{
    assert(a->type() == ComponentType::ADI);
    assert(b->type() == ComponentType::ADI);
    m_connection[a] = b;
}

const std::vector<std::pair<const Component*, const Component*>> ModelBase::all_connections() const
{
    std::vector<std::pair<const Component*, const Component*>> result;

    for (const auto& c : m_connection)
        result.emplace_back(c);

    return result;
}

void to_json(json& j, const ModelBase& model)
{
    auto& components = j["components"];
    auto& connections = j["connections"];

    for (const auto& comp : model.m_components)
        components.push_back(*comp);

    for (const auto& conn : model.m_connection)
        connections.push_back(json::array({conn.first->loc(), conn.second->loc()}));
}

void from_json(const json& j, Model& model)
{
    for (const auto& comp : j["components"])
        model.add_component(comp.get<Component>());

    for (const auto& conn : j["connections"]) {
        Location a;
        Location b;

        conn.at(0).get_to(a);
        conn.at(1).get_to(b);

        const Component* comp_a = model.find_component(a);
        const Component* comp_b = model.find_component(b);

        assert(comp_a && comp_b);

        model.connect_components(comp_a, comp_b);
    }
}

} // namespace ampere::lynx
