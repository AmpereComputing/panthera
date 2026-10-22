//
// Copyright (c) 2019 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//
#include "ampere/sc/poly.h"

#include <algorithm>
#include <map>
#include <stdexcept>
#include <string>
#include <tuple>
#include <typeindex>
#include <utility>

namespace ampere::sc {

using module_registry_t = std::map<std::pair<std::string, std::type_index>, void*>;

module_registry_t& module_registry()
{
    static module_registry_t* ptr = nullptr;

    if (!ptr) {
        static module_registry_t thing;
        ptr = &thing;
    }

    return *ptr;
}

module_implementation_registry_t& _module_implementation_registry()
{
    static module_implementation_registry_t* ptr = nullptr;

    if (!ptr) {
        static module_implementation_registry_t thing;
        ptr = &thing;
    }

    return *ptr;
}

static std::string shorten_name(const std::string& name)
{
    if (auto pos = name.rfind("::"); pos != std::string::npos)
        return name.substr(pos + 2);
    return name;
}

bool _register_module(const std::string& type_name, std::type_index constructor, void* make_module)
{
    auto& mod_registry = module_registry();

    if (mod_registry.find({type_name, constructor}) != mod_registry.end())
        throw std::runtime_error{std::string{"duplicate module type "} + type_name};

    mod_registry[{type_name, constructor}] = make_module;

    std::string short_name = type_name;

    while (true) {
        size_t pos = short_name.find("::");

        if (pos == std::string::npos)
            break;

        short_name = short_name.substr(pos + 2);

        if (mod_registry.find({short_name, constructor}) == mod_registry.end()) {
            mod_registry[{short_name, constructor}] = make_module;
        } else {
            mod_registry[{short_name, constructor}] = nullptr;
            break;
        }
    }

    return true;
}

bool _register_module_implementation(const std::string& type_name,
                                     std::type_index constructor,
                                     const std::string& implements_name,
                                     void* make_module)
{
    auto& implementation_registry = _module_implementation_registry();
    auto short_type_name = shorten_name(type_name);
    auto range = implementation_registry.equal_range({implements_name, constructor});
    for (auto i = range.first; i != range.second; ++i)
        if (i->second == short_type_name)
            throw std::runtime_error{
                fmt::format("duplicate module type {} in implementation {}", short_type_name, implements_name)};

    implementation_registry.emplace(std::piecewise_construct,
                                    std::forward_as_tuple(implements_name, constructor),
                                    std::forward_as_tuple(short_type_name));
    return _register_module(type_name, constructor, make_module);
}

bool _has_registered_module_name(const std::string& type_name)
{
    const auto& registry = module_registry();
    return std::find_if(registry.begin(), registry.end(), [&](const auto& e) { return e.first.first == type_name; })
           != registry.end();
}

void* _find_registered_module(const std::string& type_name, std::type_index constructor)
{
    auto it = module_registry().find({type_name, constructor});
    if (it == module_registry().end()) {
        return nullptr;
    } else {
        if (!it->second)
            throw std::runtime_error{fmt::format("ambiguous submodule name {}", type_name)};

        return it->second;
    }
}

} // namespace ampere::sc

/*
 * vim: sw=4:ts=4:et:si:cindent:cino=\:0,(0,g0,t0:
 */
