/*
 * Copyright (c) 2024 Ampere Computing. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#pragma once

#include <nlohmann/json.hpp>
#include <map>
#include <set>

#include "routing.h"

namespace ampere::lynx {

using json = nlohmann::json;

class LinkGraph
{
public:
    LinkGraph() = default;
    void add_path_to_graph(const Path& path);
    bool has_cycle() const;
    Path find_cycle() const;
    std::list<Path> find_cycle_with_contributing_paths() const;

    bool has_node(const Link& link) const;

    void print(std::ostream& = std::cout) const;

private:
    using node_id_t = int; // index into `nodes`
    using path_id_t = int; // index into `paths`

    using AdjacencyList = std::vector<std::set<node_id_t>>;

    struct Node
    {
        Link link;
        std::set<path_id_t> paths;

        Node(const Link& link, path_id_t path_id) : link{link}, paths{path_id} {}
    };

    std::vector<Node> nodes;
    std::vector<Path> paths;

    std::map<std::pair<u32, u32>, node_id_t> node_map;
    std::map<std::vector<u32>, path_id_t> path_map;

    // adjacency list, indexed by node id, pointing to target node ids
    AdjacencyList edges;

    path_id_t add_path(const Path& path);
    node_id_t add_node(const Link& link, path_id_t path_id);
    void add_edge(node_id_t from, node_id_t to);

    friend void to_json(json& j, const LinkGraph& graph);
    friend void from_json(const json& j, LinkGraph& graph, const Model& model);

    void find_cycle_bfs(node_id_t start_node, std::vector<node_id_t>& cycle) const;
    Path cycle_to_path(const std::vector<node_id_t>& cycle) const;
    std::list<Path> cycle_to_contributing_paths(const std::vector<node_id_t>& cycle) const;
};

class Model;

LinkGraph build_link_graph(const ModelBase& soc, const Rules& rules, Channel channel);

} // namespace ampere::lynx
