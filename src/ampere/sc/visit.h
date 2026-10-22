//
// Copyright (c) 2022 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//
#pragma once

#include <functional>
#include <string>
#include <systemc>

namespace ampere::sc {

template<typename T>
void visit_object_and_children(sc_core::sc_object* obj, const std::string& kind, const std::function<void(T*)>& action)
{
    if (kind.empty() || kind == obj->kind()) {
        auto typed_obj = dynamic_cast<T*>(obj);

        if (typed_obj)
            action(typed_obj);
        else if (!kind.empty())
            throw std::runtime_error("visit_objects: invalid object type for action");
    }

    for (auto child : obj->get_child_objects())
        visit_object_and_children(child, kind, action);
}

template<typename T>
void visit_objects(const std::string& kind, const std::function<void(T*)>& action)
{
    for (auto obj : sc_core::sc_get_top_level_objects())
        visit_object_and_children(obj, kind, action);
}

template<typename T>
void visit_objects(const std::function<void(T*)>& action)
{
    for (auto obj : sc_core::sc_get_top_level_objects())
        visit_object_and_children(obj, "", action);
}

} // namespace ampere::sc
