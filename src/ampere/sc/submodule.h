//
// Copyright (c) 2018-2019 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//

#pragma once

#include <memory>
#include <ranges>
#include <string>
#include <systemc>
#include <boost/core/demangle.hpp>
#include <typeindex>

#include "ampere/sc/ancestor.h"
#include "ampere/sc/module.h"
#include "ampere/sc/param.h"
#include "ampere/sc/param_registry.h"
#include "ampere/sc/poly.h"

namespace ampere::sc {

struct SubmoduleSettingsHelper
{
    const char *name;
    Module *parent_module = nullptr;
    ParameterRegistry *registry = nullptr;

    explicit SubmoduleSettingsHelper(const char *name) : name(name), parent_module(get_nearest<Module>())
    {
        if (!parent_module)
            throw std::runtime_error{std::string{"Submodule "} + name + " used outside of module"};

        registry = &parent_module->get_environment()->parameters;
    }

    template<typename T, typename... Args>
    auto &add_param(const char *param, Args &&...args)
    {
        return registry->add_parameter_with_type<T>(
            std::string{parent_module->name()} + "." + std::string{this->name} + "." + std::string{param},
            std::forward<Args>(args)...);
    }
};

template<typename T, typename Default = T>
class Submodule : public std::unique_ptr<T>
{
public:
    template<typename... Args>
    explicit Submodule(const char *name, Args &&...args)
    {
        auto settings = SubmoduleSettingsHelper(name);
        auto args_type = poly_constructor<Args...>::type();
        auto &type =
            settings.add_param<Submodule<T, Default>>("type",
                                                      "module type",
                                                      class_name<Default>(),
                                                      "default",
                                                      parameter_specifications::Type<Submodule<T, Default>>(args_type));

        auto submod_maker = find_registered_module<Args...>(type.value());
        if (!submod_maker)
            throw_find_module_error<Args...>(type.value());

        auto *p = dynamic_cast<T *>(submod_maker.template make<T>(name, std::forward<Args>(args)...));
        if (!p)
            throw std::runtime_error{std::string{"invalid type for Submodule "} + name + "; not a subclass?"};

        this->reset(p);
    }
};

template<typename T>
class EnabledSubmodule : public std::unique_ptr<T>
{
public:
    template<typename... Args>
    explicit EnabledSubmodule(const char *name, bool enabled = true, Args &&...args)
    {
        auto settings = SubmoduleSettingsHelper(name);
        if (auto &enable = settings.add_param<bool>("enable", "module enabled", enabled, "default");
            enable.value().get<bool>())
        {
            auto p = std::make_unique<T>(name, std::forward<Args>(args)...);
            this->swap(p);
        }
    }

    bool enabled() const { return static_cast<bool>(*this); }
};

namespace parameter_specifications {

template<typename T, typename U>
class Type<Submodule<T, U>, void>
{
    std::optional<std::type_index> type;

public:
    Type() = default;
    explicit Type(const std::type_index &type) : type(type) {}

    std::string name() const { return class_name<T>(); }
    std::string specification() const
    {
        if (!type)
            return fmt::format("{}", fmt::join(find_registered_modules<T>() | std::views::values, ", "));
        return fmt::format(
            "{}", fmt::join(find_registered_modules_for_constructor_type<T>(*type) | std::views::values, ", "));
    }
    static void value_to_event_stream(EventStream &event_stream,
                                      const std::string &name,
                                      const std::string &description,
                                      const json &value)
    {
        value_to_event_stream_json(event_stream, name, description, value);
    }
};

} // namespace parameter_specifications

} // namespace ampere::sc

/*
 * vim: sw=4:ts=4:et:si:cindent:cino=\:0,(0,g0,t0:
 */
