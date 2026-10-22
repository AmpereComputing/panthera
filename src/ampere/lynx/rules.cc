/*
 * Copyright (c) 2024-2025 Ampere Computing. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "rules.h"

#include "error.h"

namespace ampere::lynx {

const PathList Rules::EmptyPathList;

const std::map<Opcode, Channel> Rules::m_default_channel_map = {
    {Opcode::ReadShared, Channel::REQ},     {Opcode::ReadUnique, Channel::REQ},
    {Opcode::ReadOnce, Channel::REQ},       {Opcode::MakeUnique, Channel::REQ},
    {Opcode::Evict, Channel::REQ},          {Opcode::WriteEvictFull, Channel::REQ},
    {Opcode::WriteBackFull, Channel::REQ},  {Opcode::ReadNoSnpSep, Channel::REQ},
    {Opcode::ReadNoSnp, Channel::REQ},      {Opcode::WriteNoSnpFull, Channel::REQ},
    {Opcode::WriteNoSnpPtl, Channel::REQ},  {Opcode::WriteUniquePtl, Channel::REQ},

    {Opcode::SnpSharedFwd, Channel::SNP},   {Opcode::SnpUniqueFwd, Channel::SNP},
    {Opcode::SnpStashShared, Channel::SNP}, {Opcode::SnpStashUnique, Channel::SNP},

    {Opcode::RespSepData, Channel::RSP0},   {Opcode::CompDBID, Channel::RSP0},
    {Opcode::Comp, Channel::RSP0},

    {Opcode::CompAck, Channel::RSP1},       {Opcode::ReadReceipt, Channel::RSP1},
    {Opcode::SnpResp, Channel::RSP1},       {Opcode::SnpRespFwd, Channel::RSP1},
    {Opcode::DBIDResp, Channel::RSP1},

    {Opcode::DataSepResp, Channel::DAT},    {Opcode::CompData, Channel::DAT},
    {Opcode::CopyBackWrData, Channel::DAT}, {Opcode::NonCopyBackWrData, Channel::DAT},
};

Rules::Rules()
{
    m_allow_all.fill(true);
    m_disallow_all.fill(false);
    m_channel_map = Rules::m_default_channel_map;
    m_targets_by_channel_and_type = Rules::m_default_targets_by_channel_and_type;
}

bool Rules::allowed(Channel channel, const Component* src, const Component* tgt) const
{
    if (src->die_id() != tgt->die_id() && !allowed_cross_die(channel, src->type(), tgt->type()))
        return false;

    if (m_allow_all[std::to_underlying(channel)])
        return !m_disallowed.contains({channel, src, tgt});
    else if (m_disallow_all[std::to_underlying(channel)])
        return m_allowed.contains({channel, src, tgt});
    return true; // should be unreachable
}

const PathList& Rules::cross_die_routes(u8 from_die_id, u8 to_die_id) const
{
    auto it = m_cross_die_routes.find({from_die_id, to_die_id});

    if (it == m_cross_die_routes.end())
        return EmptyPathList;
    else
        return it->second;
}

const PathList& Rules::cross_die_routes_src_tgt(Channel channel, const Component* src, const Component* tgt) const
{
    auto it = m_cross_die_routes_src_tgt.find({channel, src, tgt});

    if (it == m_cross_die_routes_src_tgt.end())
        return EmptyPathList;
    else
        return it->second;
}

void Rules::allow_all(Channel channel)
{
    m_allow_all[std::to_underlying(channel)] = true;
    m_disallow_all[std::to_underlying(channel)] = false;
}

void Rules::disallow_all(Channel channel)
{
    m_allow_all[std::to_underlying(channel)] = false;
    m_disallow_all[std::to_underlying(channel)] = true;
}

void Rules::allow_all()
{
    for (size_t ch = std::to_underlying(Channel{0}); ch < util::enum_size<Channel>(); ++ch)
        allow_all(static_cast<Channel>(ch));
}

void Rules::disallow_all()
{
    for (size_t ch = std::to_underlying(Channel{0}); ch < util::enum_size<Channel>(); ++ch)
        disallow_all(static_cast<Channel>(ch));
}

void Rules::allow(Channel channel, const Component* src, const Component* tgt)
{
    if (m_allow_all[std::to_underlying(channel)])
        throw RuleError{"allow has no effect is allow_all is set"};

    m_allowed.insert({channel, src, tgt});
}

void Rules::disallow(Channel channel, const Component* src, const Component* tgt)
{
    if (m_disallow_all[std::to_underlying(channel)])
        throw RuleError{"disallow has no effect is disallow_all is set"};

    m_disallowed.insert({channel, src, tgt});
}

void Rules::allow(const Component* src, const Component* tgt)
{
    for (size_t ch = std::to_underlying(Channel{0}); ch < util::enum_size<Channel>(); ++ch)
        allow(static_cast<Channel>(ch), src, tgt);
}

void Rules::disallow(const Component* src, const Component* tgt)
{
    for (size_t ch = std::to_underlying(Channel{0}); ch < util::enum_size<Channel>(); ++ch)
        disallow(static_cast<Channel>(ch), src, tgt);
}

static void check_valid_cross_die_route(const Path& p)
{
    for (const auto& comp : p) {
        if (comp->type() != ComponentType::ADI)
            throw BadConnectivity{"cross die route has non-ADI component: {}", p};
    }
}

void Rules::add_cross_die_routes(u8 from_die_id, u8 to_die_id, const PathList& pl)
{
    for (const auto& p : pl)
        check_valid_cross_die_route(p);

    auto& full_pl = m_cross_die_routes[{from_die_id, to_die_id}];
    full_pl.splice(full_pl.end(), PathList{pl});
}

void Rules::add_cross_die_route(u8 from_die_id, u8 to_die_id, const Path& p)
{
    check_valid_cross_die_route(p);
    m_cross_die_routes[{from_die_id, to_die_id}].push_back(p);
}

void Rules::add_cross_die_route_src_tgt(Channel channel, const Component* src, const Component* tgt, const Path& p)
{
    check_valid_cross_die_route(p);
    m_cross_die_routes_src_tgt[{channel, src, tgt}].push_back(p);
}

bool Rules::allowed_cross_die(Channel channel, ComponentType from_type, ComponentType to_type) const
{
    if (!m_allowed_cross_die_valid)
        const_cast<Rules*>(this)->build_allowed_cross_die();

    return m_allowed_cross_die_.contains({channel, from_type, to_type});
}

void Rules::build_allowed_cross_die()
{
    m_allowed_cross_die_.clear();

    for (const auto& [chan, comp_map] : m_targets_by_channel_and_type) {
        for (const auto& [from_type, targets] : comp_map) {
            for (const auto& [to_type, scope] : targets) {
                if (scope == Scope::CrossDie)
                    m_allowed_cross_die_.insert({chan, from_type, to_type});
            }
        }
    }

    m_allowed_cross_die_valid = true;
}

const std::vector<std::pair<ComponentType, Scope>>& Rules::get_targets_by_channel_and_type(
    Channel channel, ComponentType comp_type) const
{
    static std::vector<std::pair<ComponentType, Scope>> empty_targets;

    const auto& per_chan = m_targets_by_channel_and_type.at(channel);
    auto it = per_chan.find(comp_type);

    if (it == per_chan.end())
        return empty_targets;
    else
        return it->second;
}

void Rules::set_targets_by_channel_and_type(Channel channel,
                                            ComponentType comp_type,
                                            const std::vector<std::pair<ComponentType, Scope>>& targets)
{
    m_targets_by_channel_and_type[channel][comp_type] = targets;
    m_allowed_cross_die_valid = false;
}

bool Rules::dmt_allowed(const Component* src, const Component* tgt) const
{
    return m_dmt_allowed_between_die.contains({src->die_id(), tgt->die_id()});
}

void Rules::dmt_allow(u8 from_die, u8 to_die)
{
    m_dmt_allowed_between_die.insert({from_die, to_die});
}

void Rules::dmt_disallow(u8 from_die, u8 to_die)
{
    auto it = m_dmt_allowed_between_die.find({from_die, to_die});
    if (it != m_dmt_allowed_between_die.end())
        m_dmt_allowed_between_die.erase(it);
}

void Rules::dump_cross_die_routes(std::ostream& os)
{
    for (const auto& [dies, routes] : m_cross_die_routes) {
        auto [from_die_id, to_die_id] = dies;
        fmt::print(os, "from {} to {}:\n", from_die_id, to_die_id);
        for (const auto& p : routes)
            fmt::print(os, "    {}\n", p);
    }

    for (const auto& [chan_src_tgt, routes] : m_cross_die_routes_src_tgt) {
        auto [chan, src, tgt] = chan_src_tgt;
        fmt::print(os, "from {} to {} on channel {}:\n", *src, *tgt, chan);
        for (const auto& p : routes)
            fmt::print(os, "    {}\n", p);
    }
}

void to_json(json& j, const Rules& rules)
{
    j["allow_all"] = rules.m_allow_all;
    j["disallow_all"] = rules.m_disallow_all;

    for (const auto& [chan, src, tgt] : rules.m_allowed)
        j["allowed"].push_back(json{{"channel", chan}, {"src", *src}, {"tgt", *tgt}});

    for (const auto& [chan, src, tgt] : rules.m_disallowed)
        j["disallowed"].push_back(json{{"channel", chan}, {"src", *src}, {"tgt", *tgt}});

    j["yfirst"] = rules.m_yfirst;

    for (const auto& [rtr, tgt] : rules.m_xy_override)
        j["xy_override"].push_back(json{{"rtr", *rtr}, {"tgt", *tgt}});

    auto& cross_die_routes = j["cross_die_routes"];

    for (const auto& [die_pair, path_list] : rules.m_cross_die_routes) {
        json cross_die_route;

        auto [from_die_id, to_die_id] = die_pair;
        cross_die_route["from_die"] = from_die_id;
        cross_die_route["to_die"] = to_die_id;

        for (const auto& path : path_list) {
            json json_path;

            for (const auto& comp : path)
                json_path.push_back(*comp);

            cross_die_route["paths"].push_back(json_path);
        }

        cross_die_routes.push_back(cross_die_route);
    }

    auto& cross_die_routes_src_tgt = j["cross_die_routes_src_tgt"];

    for (const auto& [chan_src_tgt, path_list] : rules.m_cross_die_routes_src_tgt) {
        auto [chan, src, tgt] = chan_src_tgt;
        json cross_die_route{{"channel", chan}, {"src", src->loc()}, {"tgt", tgt->loc()}};

        for (const auto& path : path_list) {
            json json_path;

            for (const auto& comp : path)
                json_path.push_back(*comp);

            cross_die_route["paths"].push_back(json_path);
        }

        cross_die_routes_src_tgt.push_back(cross_die_route);
    }

    auto& targets_by_channel_and_type = j["targets_by_channel_and_type"];

    for (const auto& [chan, tgts_by_type] : rules.m_targets_by_channel_and_type) {
        for (const auto& [from_type, targets] : tgts_by_type) {
            json json_targets = json::array();

            for (const auto& [to_type, scope] : targets) {
                json_targets.push_back({{"to_type", to_type}, {"scope", scope}});
            }

            targets_by_channel_and_type.push_back(
                json{{"channel", chan}, {"from_type", from_type}, {"targets", json_targets}});
        }
    }
}

void from_json(const json& j, Rules& rules, const ModelBase& model)
{
    j["allow_all"].get_to(rules.m_allow_all);
    j["allow_all"].get_to(rules.m_disallow_all);

    if (j.contains("allowed")) {
        for (const json& allowed : j["allowed"]) {
            const auto src = model.find_component(allowed["src"].get<Location>());
            const auto tgt = model.find_component(allowed["tgt"].get<Location>());

            rules.m_allowed.insert({allowed["channel"].get<Channel>(), src, tgt});
        }
    }

    if (j.contains("disallowed")) {
        for (const json& disallowed : j["disallowed"]) {
            const auto src = model.find_component(disallowed["src"].get<Location>());
            const auto tgt = model.find_component(disallowed["tgt"].get<Location>());

            rules.m_disallowed.insert({disallowed["channel"].get<Channel>(), src, tgt});
        }
    }

    j["yfirst"].get_to(rules.m_yfirst);

    if (j.contains("xy_override")) {
        for (const json& xy_override : j["xy_override"]) {
            const auto rtr = model.find_component(xy_override["rtr"].get<Location>());
            const auto tgt = model.find_component(xy_override["tgt"].get<Location>());

            rules.m_xy_override.insert({rtr, tgt});
        }
    }

    if (j.contains("cross_die_routes")) {
        const auto& cross_die_routes = j["cross_die_routes"];

        for (const auto& route : cross_die_routes) {
            u8 from_die;
            u8 to_die;

            route["from_die"].get_to(from_die);
            route["to_die"].get_to(to_die);

            PathList path_list;

            for (const auto& json_path : route["paths"]) {
                Path path;

                for (const auto& loc : json_path) {
                    const auto comp = model.find_component(loc.get<Location>());
                    path.push_back(comp);
                }

                path_list.push_back(path);
            }

            rules.m_cross_die_routes.insert({{from_die, to_die}, path_list});
        }
    }

    if (j.contains("cross_die_routes_src_tgt")) {
        const auto& cross_die_routes = j["cross_die_routes_src_tgt"];

        for (const auto& route : cross_die_routes) {
            Channel chan = route["channel"].get<Channel>();
            const Component* src = model.find_component(route["src"].get<Location>());
            const Component* tgt = model.find_component(route["tgt"].get<Location>());

            PathList path_list;

            for (const auto& json_path : route["paths"]) {
                Path path;

                for (const auto& loc : json_path) {
                    const auto comp = model.find_component(loc.get<Location>());
                    path.push_back(comp);
                }

                path_list.push_back(path);
            }

            rules.m_cross_die_routes_src_tgt.insert({{chan, src, tgt}, path_list});
        }
    }

    if (j.contains("targets_by_channel_and_type")) {
        for (const auto& entry : j["targets_by_channel_and_type"]) {
            Channel chan = entry["channel"].get<Channel>();
            ComponentType from_type = entry["from_type"].get<ComponentType>();
            std::vector<std::pair<ComponentType, Scope>> targets;

            for (const auto& tgt : entry["targets"]) {
                ComponentType to_type = tgt["to_type"].get<ComponentType>();
                Scope scope = tgt["scope"].get<Scope>();

                targets.emplace_back(to_type, scope);
            }

            rules.set_targets_by_channel_and_type(chan, from_type, targets);
        }
    }
}

} // namespace ampere::lynx
