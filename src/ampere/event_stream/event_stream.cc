//
// Copyright (c) 2018-2022 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//
#include "ampere/event_stream/event_stream.h"
#include <stdexcept>
#include "event_stream.h"

namespace ampere::event_stream {

EventStream::Definitions& EventStream::Definitions::instance()
{
    static Definitions* ptr = nullptr;

    if (!ptr) {
        static Definitions obj{};
        ptr = &obj;
    }

    return *ptr;
}

EventStream::Enumerations& EventStream::Enumerations::instance()
{
    static Enumerations* ptr = nullptr;

    if (!ptr) {
        static Enumerations obj{};
        ptr = &obj;
    }

    return *ptr;
}

EventType EventStream::Definitions::define_event(EventBase* event)
{
    std::unique_lock<std::mutex> lock{definitions_mtx};
    definitions.emplace_back(DefType::EVENT, event->name, event->description);
    events.insert({definitions.size(), event});
    return definitions.size(); // FIXME: narrowing conversion
}

EventType EventStream::Definitions::define_event(const std::string& name, const std::string& description)
{
    std::unique_lock<std::mutex> lock{definitions_mtx};
    definitions.emplace_back(DefType::EVENT, name, description);
    return definitions.size(); // FIXME: narrowing conversion
}

DataType EventStream::Definitions::define_data(const std::string& name, const std::string& description)
{
    std::unique_lock<std::mutex> lock{definitions_mtx};
    definitions.emplace_back(DefType::DATA, name, description);
    return definitions.size(); // FIXME: narrowing conversion
}

DataType EventStream::Definitions::define_data(const std::string& name,
                                               const std::string& description,
                                               EnumType enumeration)
{
    std::unique_lock<std::mutex> lock{definitions_mtx};
    definitions.emplace_back(DefType::DATA, name, description, enumeration);
    return definitions.size(); // FIXME: narrowing conversion
}

size_t EventStream::Definitions::catch_up(EventStream& stream, size_t index)
{
    std::unique_lock<std::mutex> lock{definitions_mtx};

    while (index < definitions.size()) {
        auto& defn = definitions[index];

        // FIXME: narrowing conversions for EventType and DataType
        if (defn.type == DefType::EVENT)
            stream.finalize_event_definition(EventType(index + 1), defn.name, defn.description);
        else if (defn.enumeration)
            stream.finalize_data_definition(DataType(index + 1), defn.name, defn.description, *defn.enumeration);
        else
            stream.finalize_data_definition(DataType(index + 1), defn.name, defn.description);

        ++index;
    }

    return index;
}

void EventStream::Definitions::clear()
{
    std::unique_lock<std::mutex> lock{definitions_mtx};
    definitions.clear();
}

EnumType EventStream::Enumerations::reserve_enum(std::type_index enum_type)
{
    std::unique_lock<std::mutex> lock{enumerations_mtx};
    if (auto existing = enumeration_types.find(enum_type); existing != enumeration_types.end())
        return existing->second;

    auto enum_id = this->enumerations.size();
    this->enumerations.emplace_back();
    enumeration_types.emplace(enum_type, enum_id);
    return enum_id;
}

EnumType EventStream::Enumerations::define_enum(std::type_index enum_type, const EnumMappingType& enumerations)
{
    if (enumerations.empty())
        throw std::logic_error("Defining an empty enum");

    std::unique_lock<std::mutex> lock{enumerations_mtx};
    if (auto existing = enumeration_types.find(enum_type); existing != enumeration_types.end()) {
        if (this->enumerations[existing->second] != enumerations)
            throw std::logic_error("Enumeration was defined with different values");
        return existing->second;
    }

    auto enum_id = this->enumerations.size();
    this->enumerations.emplace_back(enumerations);
    enumeration_types.emplace(enum_type, enum_id);
    return enum_id;
}

EnumType EventStream::Enumerations::define_enum(size_t id, const EnumMappingType& enumerations)
{
    std::unique_lock<std::mutex> lock{enumerations_mtx};
    if (auto existing = enumeration_types.find(id); existing != enumeration_types.end()) {
        if (this->enumerations[existing->second] != enumerations)
            throw std::logic_error("Enumeration was redefined with different values");
        return existing->second;
    }

    auto enum_id = this->enumerations.size();
    this->enumerations.emplace_back(enumerations);
    enumeration_types.emplace(id, enum_id);
    return enum_id;
}

EnumType EventStream::Enumerations::provide_enum(std::type_index enum_type, const EnumMappingType& enumerations)
{
    std::unique_lock<std::mutex> lock{enumerations_mtx};
    auto existing = enumeration_types.find(enum_type);
    if (existing == enumeration_types.end())
        throw std::logic_error("Providing enumeration which was not already defined");

    auto enum_id = existing->second;
    auto& enum_values = this->enumerations[enum_id];
    if (enum_values.empty())
        this->enumerations[enum_id] = enumerations;
    else if (this->enumerations[existing->second] != enumerations)
        throw std::logic_error("Enumeration was defined with different values");

    return enum_id;
}

size_t EventStream::Enumerations::catch_up(EventStream& stream, size_t index)
{
    std::unique_lock<std::mutex> lock{enumerations_mtx};

    for (; index < enumerations.size(); ++index) {
        auto& enumeration = enumerations[index];
        if (enumeration.empty())
            return index;

        stream.finalize_enum_definition(index, enumeration);
    }

    return index;
}

void EventStream::Enumerations::clear()
{
    std::unique_lock<std::mutex> lock{enumerations_mtx};
    enumerations.clear();
    enumeration_types.clear();
}

static size_t hash_enum_mappings(const EnumMappingType& enumerations)
{
    size_t value_hash = 0;
    size_t name_hash = 0;
    auto value_hasher = std::hash<EnumMappingType::key_type>{};
    auto name_hasher = std::hash<EnumMappingType::mapped_type>{};
    for (const auto& [value, name] : enumerations) {
        value_hash ^= (value_hasher(value) << 1);
        name_hash ^= (name_hasher(name) << 1);
    }

    return value_hash ^ (name_hash << 1);
}

EventType EventStream::define_event(EventBase* event)
{
    return definitions->define_event(event);
}

EventType EventStream::define_event(const std::string& name, const std::string& description)
{
    return definitions->define_event(name, description);
}

DataType EventStream::define_data(const std::string& name, const std::string& description)
{
    return definitions->define_data(name, description);
}

DataType EventStream::define_data(const std::string& name, const std::string& description, EnumType enumeration)
{
    return definitions->define_data(name, description, enumeration);
}

EnumType EventStream::define_enum(std::type_index enum_type, const EnumMappingType& enumerations)
{
    return this->enumerations->define_enum(enum_type, enumerations);
}

EnumType EventStream::define_enum(size_t id, const EnumMappingType& enumerations)
{
    return this->enumerations->define_enum(id, enumerations);
}

EnumType EventStream::define_anonymous_enum(const EnumMappingType& enumerations)
{
    auto id = hash_enum_mappings(enumerations);
    return this->enumerations->define_enum(id, enumerations);
}

EnumType EventStream::reserve_enum(std::type_index enum_type)
{
    return this->enumerations->reserve_enum(enum_type);
}

EnumType EventStream::provide_enum(std::type_index enum_type, const EnumMappingType& enumerations)
{
    return this->enumerations->provide_enum(enum_type, enumerations);
}

EventType EventStream::define_global_event(EventBase* event)
{
    return Definitions::instance().define_event(event);
}

EventType EventStream::define_global_event(const std::string& name, const std::string& description)
{
    return Definitions::instance().define_event(name, description);
}

DataType EventStream::define_global_data(const std::string& name, const std::string& description)
{
    return Definitions::instance().define_data(name, description);
}

DataType EventStream::define_global_data(const std::string& name, const std::string& description, EnumType enumeration)
{
    return Definitions::instance().define_data(name, description, enumeration);
}

EnumType EventStream::define_global_enum(std::type_index enum_type, const EnumMappingType& enumerations)
{
    return Enumerations::instance().define_enum(enum_type, enumerations);
}

EnumType EventStream::define_global_enum(size_t id, const EnumMappingType& enumerations)
{
    return Enumerations::instance().define_enum(id, enumerations);
}

EnumType EventStream::define_anonymous_global_enum(const EnumMappingType& enumerations)
{
    auto id = hash_enum_mappings(enumerations);
    return Enumerations::instance().define_enum(id, enumerations);
}

EnumType EventStream::reserve_global_enum(std::type_index enum_type)
{
    return Enumerations::instance().reserve_enum(enum_type);
}

EnumType EventStream::provide_global_enum(std::type_index enum_type, const EnumMappingType& enumerations)
{
    return Enumerations::instance().provide_enum(enum_type, enumerations);
}

void EventStream::catch_up(bool complete)
{
    definition_index = definitions->catch_up(*this, definition_index);
    if (complete && !definitions->caught_up(definition_index))
        throw std::logic_error("Not all definitions finalized");

    enumeration_index = enumerations->catch_up(*this, enumeration_index);
    if (complete && !enumerations->caught_up(enumeration_index))
        throw std::logic_error("Not all enumerations finalized");
}

void EventStream::reset_global_definitions()
{
    Definitions::instance().clear();
}

void EventStream::reset_global_enumerations()
{
    Enumerations::instance().clear();
}

} // namespace ampere::event_stream

/*
 * vim: sw=4:ts=4:et:si:cindent:cino=\:0,(0,g0,t0:
 */
