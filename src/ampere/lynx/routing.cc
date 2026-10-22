/*
 * Copyright (c) 2024-2025 Ampere Computing. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "routing.h"

#include <cassert>

#include <fmt/format.h>
#include <fmt/ostream.h>

#include "error.h"
#include "rules.h"

namespace ampere::lynx {

void validate_path(const Path& path)
{
    int last_die_id = -1;

    for (const auto& comp : path) {
        if (last_die_id == -1) {
            last_die_id = comp->die_id();
        } else if (last_die_id != comp->die_id()) {
            if (comp->type() != ComponentType::ADI)
                throw BadConnectivity{"die crossing without ADI: {}", path};
            last_die_id = comp->die_id();
        }
    }
}

static const Component* traverse_x(
    const ModelBase& model, const Component* a, const Component* b, Path& path, const Rules& rules)
{
    while (a->x() < b->x()) {
        path.push_back(a);
        const auto* next = model.find_rtr(east(a->loc()));
        if (!rules.connected(a, next))
            throw NoRoute{"cannot go from {} -> {}; they are not connected", *a, *next};
        a = next;
    }
    while (a->x() > b->x()) {
        path.push_back(a);
        const auto* next = model.find_rtr(west(a->loc()));
        if (!rules.connected(a, next))
            throw NoRoute{"cannot go from {} -> {}; they are not connected", *a, *next};
        a = next;
    }
    return a;
}

static const Component* traverse_y(
    const ModelBase& model, const Component* a, const Component* b, Path& path, const Rules& rules)
{
    while (a->y() < b->y()) {
        path.push_back(a);
        const auto* next = model.find_rtr(north(a->loc()));
        if (!rules.connected(a, next))
            throw NoRoute{"cannot go from {} -> {}; they are not connected", *a, *next};
        a = next;
    }
    while (a->y() > b->y()) {
        path.push_back(a);
        const auto* next = model.find_rtr(south(a->loc()));
        if (!rules.connected(a, next))
            throw NoRoute{"cannot go from {} -> {}; they are not connected", *a, *next};
        a = next;
    }
    return a;
}

auto path(const ModelBase& model, const Component* src, Path tgts, const Rules& rules, Channel channel) -> Path
{
    Path path;

    while (!tgts.empty()) {
        const auto tgt = tgts.front();
        tgts.pop_front();

        // The only way we are allowed to cross die is though an ADI(RDI) connection
        // (i.e., via "follow_connection", below)
        assert(src->die_id() == tgt->die_id());

        path.push_back(src);

        auto a = model.find_rtr(src->loc());
        auto b = model.find_rtr(tgt->loc());

        if (rules.xfirst(a, src, tgt, channel)) {
            a = traverse_x(model, a, b, path, rules);
            a = traverse_y(model, a, b, path, rules);
        } else {
            a = traverse_y(model, a, b, path, rules);
            a = traverse_x(model, a, b, path, rules);
        }

        assert(a == b);

        path.push_back(b);
        path.push_back(tgt);

        if (!tgts.empty()) {
            try {
                src = model.follow_connection(tgt);
            } catch (const std::out_of_range&) {
                throw NoRoute{"tried to route through unconnected component {}", *tgt};
            }
        }
    }

    return path;
}

auto paths_to_target(
    const ModelBase& model, const Rules& rules, const Component* src, const Component* tgt, Channel channel) -> PathList
{
    PathList paths;

    if (rules.allowed(channel, src, tgt)) {
        if (src->die_id() != tgt->die_id()) {
            // FIXME: cross die route handling is ugly; this could be unified I think
            const PathList& routes = rules.cross_die_routes_src_tgt(channel, src, tgt);

            if (!routes.empty()) {
                for (auto cross_die_route : routes) {
                    if (rules.allowed(channel, src, cross_die_route.front())) {
                        cross_die_route.push_back(tgt);
                        paths.push_back(path(model, src, cross_die_route, rules, channel));
                    }
                }
            } else {
                const PathList& routes_by_die = rules.cross_die_routes(src->die_id(), tgt->die_id());

                for (auto cross_die_route : routes_by_die) {
                    if (rules.allowed(channel, src, cross_die_route.front())) {
                        cross_die_route.push_back(tgt);
                        paths.push_back(path(model, src, cross_die_route, rules, channel));
                    }
                }
            }
        } else {
            paths.push_back(path(model, src, {tgt}, rules, channel));
        }
    }

    return paths;
}

std::vector<const Component*> get_targets(const ModelBase& model, ComponentType comp_type, Scope scope, u8 from_die_id)
{
    std::vector<const Component*> result{};

    for (const auto& comp : model.components_by_type(comp_type)) {
        if ((scope == Scope::CrossDie || comp->die_id() == from_die_id))
            result.push_back(comp);
    }

    return result;
}

PathList paths_to_all_targets(const ModelBase& model, const Rules& rules, const Component* src, Channel channel)
{
    PathList path_list;

    for (const auto& [comp_type, scope] : rules.get_targets_by_channel_and_type(channel, src->type())) {
        for (const auto& tgt : get_targets(model, comp_type, scope, src->die_id()))
            path_list.splice(path_list.end(), paths_to_target(model, rules, src, tgt, channel));
    }

    return path_list;
}

PathList paths_to_all_targets_of_type(
    const ModelBase& model, const Rules& rules, const Component* src, ComponentType tgt_type, Channel channel)
{
    PathList path_list;

    for (const auto& [comp_type, scope] : rules.get_targets_by_channel_and_type(channel, src->type())) {
        if (comp_type == tgt_type) {
            for (const auto& tgt : get_targets(model, comp_type, scope, src->die_id()))
                path_list.splice(path_list.end(), paths_to_target(model, rules, src, tgt, channel));
        }
    }

    return path_list;
}

std::map<const Component*, PathList> paths_by_target(
    const ModelBase& model, const Rules& rules, const Component* src, ComponentType tgt_type, Channel channel)
{
    std::map<const Component*, PathList> result;

    for (const auto& [comp_type, scope] : rules.get_targets_by_channel_and_type(channel, src->type())) {
        if (comp_type == tgt_type) {
            for (const auto& tgt : get_targets(model, comp_type, scope, src->die_id())) {
                auto paths = paths_to_target(model, rules, src, tgt, channel);
                if (!paths.empty())
                    result[tgt] = std::move(paths);
            }
        }
    }

    return result;
}

std::ostream& operator<<(std::ostream& os, const Link& link)
{
    fmt::print(os, "({},{})", *link.first, *link.second);
    return os;
}

std::ostream& operator<<(std::ostream& os, const Path& path)
{
    bool first = true;

    for (const auto& elem : path) {
        if (!first)
            fmt::print(os, " -> ");
        fmt::print(os, "{}", *elem);
        first = false;
    }

    return os;
}

} // namespace ampere::lynx
