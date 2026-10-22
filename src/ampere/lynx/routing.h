/*
 * Copyright (c) 2024-2025 Ampere Computing. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#pragma once

#include <iostream>
#include <list>
#include <map>
#include <utility>
#include <vector>

#include <fmt/ostream.h>

#include "component.h"
#include "lynx.enum.h"
#include "model.h"

namespace ampere::lynx {

using Path = std::list<const Component*>;
using PathList = std::list<Path>;
using Link = std::pair<const Component*, const Component*>;

class Rules;

auto path(const ModelBase& model, const Component* src, Path tgts, const Rules& rules, Channel channel) -> Path;
auto paths_to_target(const ModelBase& model,
                     const Rules& rules,
                     const Component* src,
                     const Component* tgt,
                     Channel channel) -> PathList;
auto paths_to_all_targets(const ModelBase& model, const Rules& rules, const Component* src, Channel channel)
    -> PathList;
auto paths_to_all_targets_of_type(const ModelBase& model,
                                  const Rules& rules,
                                  const Component* src,
                                  ComponentType tgt_type,
                                  Channel channel) -> PathList;
auto paths_by_target(const ModelBase& model,
                     const Rules& rules,
                     const Component* src,
                     ComponentType tgt_type,
                     Channel channel) -> std::map<const Component*, PathList>;
auto get_targets(const ModelBase& model, ComponentType comp_type, Scope scope, u8 from_die_id)
    -> std::vector<const Component*>;

inline bool operator==(const Link& a, const Link& b)
{
    return a.first == b.first && a.second == b.second;
}
inline bool operator!=(const Link& a, const Link& b)
{
    return !(a == b);
}
inline bool operator<(const Link& a, const Link& b)
{
    return a.first == b.first ? a.second < b.second : a.first < b.first;
}

std::ostream& operator<<(std::ostream& os, const Link& link);
std::ostream& operator<<(std::ostream& os, const Path& path);

template<typename T>
std::list<T> operator+(std::list<T> a, std::list<T> b)
{
    a.splice(a.end(), b);
    return a;
}

extern void validate_path(const Path& path);

} // namespace ampere::lynx

template<>
struct fmt::formatter<ampere::lynx::Link> : fmt::ostream_formatter
{};
template<>
struct fmt::formatter<ampere::lynx::Path> : fmt::ostream_formatter
{};
