/*
 * Copyright (c) 2024-2025 Ampere Computing. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "link_graph.h"

#include <queue>
#include <set>

#include "component.h"
#include "error.h"
#include "routing.h"
#include "rules.h"

namespace ampere::lynx {

static u32 encode_location(Location loc);
static std::pair<u32, u32> encode_link(const Link& link);
static std::vector<u32> encode_path(const Path& path);

bool LinkGraph::has_node(const Link& link) const
{
    return node_map.contains(encode_link(link));
}

void LinkGraph::add_path_to_graph(const Path& path)
{
    if (path.size() < 3)
        return;

    int path_id = add_path(path);

    // a, b, and c will chase each other through the path list like this:
    //
    //    * --> * --> * --> * --> ...
    //          ^     ^     ^
    //          |     |     |
    //          c     b     a
    //
    auto a = ++ ++path.begin();
    auto b = ++path.begin();
    auto c = path.begin();

    for (; a != path.end(); ++a, ++b, ++c) {
        auto from_node = add_node(Link{*c, *b}, path_id);
        auto to_node = add_node(Link{*b, *a}, path_id);
        add_edge(from_node, to_node);
    }
}

LinkGraph::path_id_t LinkGraph::add_path(const Path& path)
{
    validate_path(path);

    path_id_t path_id;

    auto key = encode_path(path);
    auto pp = path_map.find(key);

    if (pp != path_map.end()) {
        path_id = pp->second;
    } else {
        path_id = static_cast<int>(paths.size());
        paths.push_back(path);
        path_map[key] = path_id;
    }

    return path_id;
}

LinkGraph::node_id_t LinkGraph::add_node(const Link& link, path_id_t path_id)
{
    node_id_t node_id;

    auto key = encode_link(link);
    auto np = node_map.find(key);

    if (np != node_map.end()) {
        node_id = np->second;
        nodes[node_id].paths.insert(path_id);
    } else {
        node_id = static_cast<int>(nodes.size());
        nodes.emplace_back(link, path_id);
        node_map[key] = node_id;
    }

    return node_id;
}

void LinkGraph::add_edge(node_id_t from, node_id_t to)
{
    if (edges.size() <= static_cast<size_t>(from))
        edges.resize(from + 1);

    edges[from].insert(to);
}

Path LinkGraph::find_cycle() const
{
    std::vector<node_id_t> cycle;

    for (node_id_t node_id = 0; static_cast<size_t>(node_id) < nodes.size(); ++node_id)
        find_cycle_bfs(node_id, cycle);

    return cycle_to_path(cycle);
}

std::list<Path> LinkGraph::find_cycle_with_contributing_paths() const
{
    std::vector<node_id_t> cycle;

    for (node_id_t node_id = 0; static_cast<size_t>(node_id) < nodes.size(); ++node_id)
        find_cycle_bfs(node_id, cycle);

    return cycle_to_contributing_paths(cycle);
}

void LinkGraph::find_cycle_bfs(node_id_t start_node, std::vector<node_id_t>& cycle) const
{
    struct entry
    {
        node_id_t node;
        int parent;
    };

    std::vector<entry> q(nodes.size());
    std::vector<int> distance(nodes.size(), -1);

    int q_beg = 0;
    int q_end = 0;

    q[q_end++] = {start_node, -1};
    distance[start_node] = 0;

    while (q_beg < q_end) {
        auto node = q[q_beg].node;

        for (auto neighbor : edges[node]) {
            if (neighbor == start_node) {
                size_t cycle_length = distance[node] + 1;

                if (cycle.empty() || cycle_length < cycle.size()) {
                    cycle.clear();
                    cycle.resize(cycle_length);

                    int j = cycle_length - 1;

                    for (int i = q_beg; q[i].node != start_node; i = q[i].parent)
                        cycle[j--] = q[i].node;

                    cycle[0] = start_node;
                }
            } else if (distance[neighbor] == -1) {
                distance[neighbor] = distance[node] + 1;
                q[q_end++] = entry{neighbor, q_beg};
            }
        }

        ++q_beg;
    }
}

Path LinkGraph::cycle_to_path(const std::vector<node_id_t>& cycle) const
{
    Path path;

    for (auto node_id : cycle)
        path.push_back(nodes[node_id].link.first);

    return path;
}

/* Concept:
 *
 * We have a cycle of nodes. Each node contains the set of path ids for
 * the paths that contributed to the existance of this node. So to find
 * an example set of contributing paths (not necessarily optimal, but
 * we'll do our best), we do this:
 *
 *    1. Build the set of uncovered edges, using the intersection of
 *       path ids for each sequential pair of nodes in the cycle.
 *    2. While this set of uncovered edges is not empty:
 *        a. Calculate a count of edges which are covered by each path id.
 *        b. Choose the path which covers the maximum number of nodes and
 *           add that to our resulting path list. As a tie-breaker, choose
 *           the shortest path.
 *        c. Remove all the edges covered by the path from (b) from the
 *           uncovered edge set.
 */
std::list<Path> LinkGraph::cycle_to_contributing_paths(const std::vector<node_id_t>& cycle) const
{
    std::list<Path> contributing_paths;
    std::list<std::set<path_id_t>> edges;

    for (size_t i = 0, j = 1; i < cycle.size(); ++i, ++j) {
        if (j == cycle.size())
            j = 0;

        auto a = cycle[i];
        auto b = cycle[j];

        std::set<path_id_t> intersect;
        std::set_intersection(nodes[a].paths.begin(),
                              nodes[a].paths.end(),
                              nodes[b].paths.begin(),
                              nodes[b].paths.end(),
                              std::inserter(intersect, intersect.begin()));

        edges.push_back(intersect);
    }

    while (!edges.empty()) {
        std::vector<int> coverage(paths.size());
        int max_coverage = 0;
        path_id_t max_coverage_path = 0;
        size_t max_coverage_path_len = 0;

        for (const auto& edge : edges) {
            for (auto path_id : edge) {
                auto cov = ++coverage[path_id];

                if ((cov > max_coverage) || (cov == max_coverage && paths[path_id].size() < max_coverage_path_len)) {
                    max_coverage = cov;
                    max_coverage_path = path_id;
                    max_coverage_path_len = paths[path_id].size();
                }
            }
        }

        contributing_paths.push_back(paths[max_coverage_path]);

        for (auto it = edges.begin(); it != edges.end();) {
            bool erased = false;
            for (auto path_id : *it) {
                if (path_id == max_coverage_path) {
                    it = edges.erase(it);
                    erased = true;
                    break;
                }
            }
            if (!erased)
                ++it;
        }
    }

    return contributing_paths;
}

bool LinkGraph::has_cycle() const
{
    return !find_cycle().empty();
}

u32 encode_location(Location loc)
{
    if (!(loc.port == -1 || loc.port < 8))
        throw UnsupportedLocation("port value {} is to large to encode; some assumptions in LinkGraph have been broken",
                                  loc.port);

    if (!(loc.didx == -1 || loc.didx < 8))
        throw UnsupportedLocation("didx value {} is to large to encode; some assumptions in LinkGraph have been broken",
                                  loc.didx);

    return ((loc.die_id) | (loc.x << 8) | (loc.y << 16) | ((loc.port & 0x7) << 24) | ((loc.didx & 0x7) << 27));
}

std::pair<u32, u32> encode_link(const Link& link)
{
    u32 a = encode_location(link.first->loc());
    u32 b = encode_location(link.second->loc());

    return {a, b};
}

std::vector<u32> encode_path(const Path& path)
{
    std::vector<u32> key;

    std::transform(path.begin(), path.end(), std::back_inserter(key), [](const Component* comp) -> u32 {
        return encode_location(comp->loc());
    });

    return key;
}

void LinkGraph::print(std::ostream& os) const
{
    for (node_id_t from_id = 0; static_cast<size_t>(from_id) < edges.size(); ++from_id) {
        for (node_id_t to_id : edges[from_id]) {
            const Node& from = nodes[from_id];
            const Node& to = nodes[to_id];
            fmt::print(os, "  {} -> {}\n", from.link, to.link);
        }
    }
}

LinkGraph build_link_graph(const ModelBase& model, const Rules& rules, Channel channel)
{
    LinkGraph g;

    for (const auto& src : model.all_components()) {
        if (src->port() >= 0) {
            for (const auto& path : paths_to_all_targets(model, rules, src, channel))
                g.add_path_to_graph(path);
        }
    }

    return g;
}

void to_json(json& j, const LinkGraph& graph)
{
    for (const auto& path : graph.paths) {
        json jpath = json::array();

        for (const auto& comp : path)
            jpath.push_back(json{comp->loc()});

        j["paths"].push_back(jpath);
    }
}

void from_json(const json& j, LinkGraph& graph, const Model& model)
{
    if (j.contains("paths")) {
        for (const auto& jpath : j["paths"]) {
            Path path;

            for (const auto& jloc : jpath) {
                Location loc = jloc.get<Location>();
                path.push_back(model.find_component(loc));
            }

            graph.add_path_to_graph(path);
        }
    }
}

} // namespace ampere::lynx
