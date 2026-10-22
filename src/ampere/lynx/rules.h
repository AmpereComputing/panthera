/*
 * Copyright (c) 2024-2025 Ampere Computing. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#pragma once

#include <nlohmann/json.hpp>
#include <map>
#include <set>

#include "component.h"
#include "routing.h"

namespace ampere::lynx {

using json = nlohmann::json;

class Rules
{
public:
    Rules();

    bool xfirst(const Component* rtr, const Component* src, const Component* tgt, Channel channel) const
    {
        bool default_xfirst = !m_yfirst.contains(rtr->die_id());

        // There are four ways to specify XY overrides in Lynx today:
        //
        //    1) A specific src->tgt on a specific channel
        //    2) A specific src->tgt on any channel
        //    3) A rtr->tgt on a specific channel
        //    4) A rtr->tgt on any channel
        //
        // Of these, only (4) is currently implemented by the hardware.
        // The other options exist for experimentation.
        //
        if (m_xy_override_chan.contains({src, tgt, channel}) || m_xy_override.contains({src, tgt})
            || m_xy_override_chan.contains({rtr, tgt, channel}) || m_xy_override.contains({rtr, tgt}))
        {
            return !default_xfirst;
        } else {
            return default_xfirst;
        }
    }

    void add_xy_override(const Component* rtr, const Component* tgt) { m_xy_override.insert({rtr, tgt}); }

    void add_xy_override(const Component* rtr, const Component* tgt, Channel channel)
    {
        m_xy_override_chan.insert({rtr, tgt, channel});
    }

    bool allowed(Channel channel, const Component* src, const Component* tgt) const;

    const PathList& cross_die_routes(u8 from_die_id, u8 to_die_id) const;
    const PathList& cross_die_routes_src_tgt(Channel channel, const Component* src, const Component* tgt) const;

    void set_xfirst(u8 die_id) { m_yfirst.erase(die_id); }
    void set_yfirst(u8 die_id) { m_yfirst.insert(die_id); }

    void allow_all(Channel channel);
    void disallow_all(Channel channel);

    void allow_all();
    void disallow_all();

    void allow(Channel channel, const Component* src, const Component* tgt);
    void disallow(Channel channel, const Component* src, const Component* tgt);

    void allow(const Component* src, const Component* tgt);
    void disallow(const Component* src, const Component* tgt);

    void add_cross_die_routes(u8 from_die_id, u8 to_die_id, const PathList& pl);
    void add_cross_die_route(u8 from_die_id, u8 to_die_id, const Path& p);

    void add_cross_die_route_src_tgt(Channel channel, const Component* src, const Component* tgt, const Path& p);

    void dump_cross_die_routes(std::ostream& os);

    Channel channel_map(Opcode opcode) const { return m_channel_map.at(opcode); }
    static Channel default_channel_map(Opcode opcode) { return m_default_channel_map.at(opcode); }

    void set_channel_map(Opcode opcode, Channel channel) { m_channel_map.at(opcode) = channel; }

    const std::vector<std::pair<ComponentType, Scope>>& get_targets_by_channel_and_type(Channel channel,
                                                                                        ComponentType comp_type) const;
    void set_targets_by_channel_and_type(Channel channel,
                                         ComponentType comp_type,
                                         const std::vector<std::pair<ComponentType, Scope>>& targets);

    bool dmt_allowed(const Component* src, const Component* tgt) const;
    void dmt_allow(u8 from_die, u8 to_die);
    void dmt_disallow(u8 from_die, u8 to_die);

    bool connected(const Component* a, const Component* b) const { return m_disconnected.count({a, b}) == 0; }
    void disconnect(const Component* a, const Component* b) { m_disconnected.insert({a, b}); }

protected:
    std::set<std::tuple<Channel, const Component*, const Component*>> m_allowed;
    std::set<std::tuple<Channel, const Component*, const Component*>> m_disallowed;
    mutable std::set<std::tuple<Channel, ComponentType, ComponentType>> m_allowed_cross_die_;
    mutable bool m_allowed_cross_die_valid{false};

    std::array<bool, util::enum_size<Channel>()> m_allow_all;
    std::array<bool, util::enum_size<Channel>()> m_disallow_all;

    std::set<u8> m_yfirst;
    std::set<std::pair<const Component*, const Component*>> m_xy_override;
    std::set<std::tuple<const Component*, const Component*, Channel>> m_xy_override_chan;

    std::map<std::pair<u8, u8>, PathList> m_cross_die_routes;
    std::map<std::tuple<Channel, const Component*, const Component*>, PathList> m_cross_die_routes_src_tgt;

    std::map<Opcode, Channel> m_channel_map;
    std::map<Channel, std::map<ComponentType, std::vector<std::pair<ComponentType, Scope>>>>
        m_targets_by_channel_and_type;

    std::set<std::pair<u8, u8>> m_dmt_allowed_between_die;

    std::set<std::pair<const Component*, const Component*>> m_disconnected; // for disconnected routers (rare)

    bool allowed_cross_die(Channel channel, ComponentType from_type, ComponentType to_type) const;
    void build_allowed_cross_die();

    static const PathList EmptyPathList;
    static const std::map<Opcode, Channel> m_default_channel_map;
    static const std::map<Channel, std::map<ComponentType, std::vector<std::pair<ComponentType, Scope>>>>
        m_default_targets_by_channel_and_type;

    friend void to_json(json& j, const Rules& rules);
    friend void from_json(const json& j, Rules& rules, const ModelBase& model);
};

} // namespace ampere::lynx
