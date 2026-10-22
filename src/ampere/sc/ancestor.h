//
// Copyright (c) 2018-2019 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//

#pragma once

#include <systemc>

namespace ampere::sc {

template<typename T>
T* get_nearest(sc_core::sc_object* obj = nullptr)
{
    if (!obj)
        obj = sc_core::sc_get_current_object();

    while (obj) {
        if (auto* as_T = dynamic_cast<T*>(obj); as_T)
            return as_T;

        obj = obj->get_parent_object();
    }

    return nullptr;
}

template<typename T>
T* get_nearest_ancestor(sc_core::sc_object* obj)
{
    if (auto* parent = obj->get_parent_object(); parent)
        return get_nearest<T>(parent);

    return nullptr;
}

template<typename T>
const T* get_nearest_ancestor(const sc_core::sc_object* obj)
{
    auto* const parent = obj->get_parent_object();
    if (!parent)
        return nullptr;
    return get_nearest<T>(parent);
}

template<typename T>
T* get_sibling(sc_core::sc_object* obj = nullptr)
{
    if (!obj)
        obj = sc_core::sc_get_current_object();

    if (auto* parent = obj->get_parent_object(); parent) {
        for (auto* child : parent->get_child_objects()) {
            if (child != obj) {
                if (auto* as_T = dynamic_cast<T*>(child); as_T)
                    return as_T;
            }
        }
    }

    return nullptr;
}

template<typename T>
std::pair<T*, bool> get_descendant(sc_core::sc_object* obj, unsigned level = 0)
{
    if (!level)
        return {dynamic_cast<T*>(obj), !obj->get_child_objects().empty()};

    bool any_has_more = false;
    for (auto* child : obj->get_child_objects()) {
        auto [matching, has_more] = get_descendant<T>(child, level - 1);
        if (matching)
            return {matching, false};
        any_has_more |= has_more;
    }

    return {nullptr, any_has_more};
}

template<typename T>
T* get_nearest_descendant(sc_core::sc_object* obj = nullptr)
{
    if (!obj)
        obj = sc_core::sc_get_current_object();

    bool has_more = true;
    for (unsigned level = 0; has_more; ++level) {
        auto [matching, more] = get_descendant<T>(obj, level);
        if (matching)
            return matching;
        has_more = more;
    }

    return nullptr;
}

template<typename T>
T* get_nearest_relative(sc_core::sc_object* obj = nullptr)
{
    if (!obj)
        obj = sc_core::sc_get_current_object();

    while (obj) {
        if (auto* as_T = dynamic_cast<T*>(obj); as_T)
            return as_T;
        if (auto* relative = get_sibling<T>(obj))
            return relative;

        obj = obj->get_parent_object();
    }

    return nullptr;
}

} // namespace ampere::sc

/*
 * vim: sw=4:ts=4:et:si:cindent:cino=\:0,(0,g0,t0:
 */
