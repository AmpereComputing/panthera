/*
 * Copyright (c) 2024 Ampere Computing. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#pragma once

#include <nlohmann/json.hpp>
#include <map>
#include <vector>

#include "ampere/util/enum.h"
#include "component.h"
#include "location.h"
#include "lynx.enum.h"

namespace ampere::lynx {

using json = nlohmann::json;

class ModelBase
{
public:
    ModelBase() = default;
    virtual ~ModelBase() = default;

    const Component* find_component(Location loc) const;
    const std::vector<const Component*>& components_by_type(ComponentType type) const;
    const Component* find_rtr(Location loc) const;
    const Component* find_by_tag(u8 die_id, const std::string_view& tag) const;
    const std::vector<const Component*>& all_components() const { return m_components; }

    const Component* follow_connection(const Component* comp) const { return m_connection.at(comp); }
    void connect_components(const Component* a, const Component* b);
    void connect_components_one_way(const Component* from, const Component* to);

    const std::vector<std::pair<const Component*, const Component*>> all_connections() const;

    friend void to_json(json& j, const ModelBase& model);

protected:
    void add_component(const Component* comp);

private:
    std::vector<const Component*> m_components;

    bool reindex{true};

    std::unordered_map<Location, const Component*> m_index_by_loc;
    std::vector<std::vector<const Component*>> m_index_by_type{util::enum_size<ComponentType>()};
    std::map<std::pair<u8, const std::string_view>, const Component*> m_index_by_tag;
    std::vector<const Component*> m_rtr_index{};
    std::unordered_map<const Component*, const Component*> m_connection;

    void build_index();
    void build_rtr_index();

    int loc_to_rtr_index(Location loc) const
    {
        int idx = loc.die_id;
        idx = (idx << m_rtr_idx_x_bits) + loc.x;
        idx = (idx << m_rtr_idx_y_bits) + loc.y;
        return idx;
    }

    int m_rtr_idx_x_bits{0};
    int m_rtr_idx_y_bits{0};
};

class Model : public ModelBase
{
public:
    using ModelBase::ModelBase;

    void add_component(std::unique_ptr<Component> comp)
    {
        this->ModelBase::add_component(comp.get());
        m_comp_store.push_back(std::move(comp));
    }

    void add_component(Component&& comp)
    {
        auto p = std::unique_ptr<Component>{new Component(std::move(comp))};
        this->ModelBase::add_component(p.get());
        m_comp_store.push_back(std::move(p));
    }

private:
    std::vector<std::unique_ptr<Component>> m_comp_store;
};

void from_json(const json& j, Model& model);

} // namespace ampere::lynx
