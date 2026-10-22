//
// Copyright (c) 2018-2022 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//
#pragma once

#include <deque>
#include <mutex>
#include <string>
#include <typeindex>
#include <unordered_map>
#include <optional>
#include <variant>

#include "ampere/event_stream/event_base.h"

namespace ampere::event_stream {

using EventType = int;
using DataType = int;
using EnumType = int;
using EnumMappingType = std::unordered_map<int64_t, std::string>;

class EventHandleBase
{
public:
    virtual ~EventHandleBase() = default;
};

using EventHandle = EventHandleBase*;

class EventStream
{
public:
    EventStream() : definitions(&Definitions::instance()), enumerations(&Enumerations::instance()) {}

    virtual ~EventStream() = default;

    virtual void post_event(EventType event_type, std::uint64_t time) { close_event(open_event(event_type, time)); }
    virtual EventHandle open_event(EventType event_type, std::uint64_t time) = 0;
    virtual void close_event(EventHandle event) = 0;

    virtual void add_int_data(EventHandle event, DataType event_data_type, std::int64_t value) = 0;
    virtual void add_uint_data(EventHandle event, DataType event_data_type, std::uint64_t value) = 0;
    virtual void add_string_data(EventHandle event, DataType event_data_type, const std::string& value) = 0;

    virtual void set_bool_parameter(const std::string& name, const std::string& description, bool value) = 0;
    virtual void set_int_parameter(const std::string& name, const std::string& description, std::int64_t value) = 0;
    virtual void set_uint_parameter(const std::string& name, const std::string& description, std::uint64_t value) = 0;
    virtual void set_double_parameter(const std::string& name, const std::string& description, double value) = 0;
    virtual void set_string_parameter(const std::string& name,
                                      const std::string& description,
                                      const std::string& value) = 0;
    virtual void set_json_parameter(const std::string& name,
                                    const std::string& description,
                                    const std::string& value) = 0;
    virtual void start_simulation() {}

    EventType define_event(EventBase* event);
    EventType define_event(const std::string& name, const std::string& description);
    DataType define_data(const std::string& name, const std::string& description);
    DataType define_data(const std::string& name, const std::string& description, EnumType enumeration);

    static EventType define_global_event(EventBase* event);
    static EventType define_global_event(const std::string& name, const std::string& description);
    static DataType define_global_data(const std::string& name, const std::string& description);
    static DataType define_global_data(const std::string& name, const std::string& description, EnumType enumeration);

    EnumType define_enum(size_t id, const EnumMappingType& enumerations);
    static EnumType define_global_enum(size_t id, const EnumMappingType& enumerations);

    template<typename T>
    inline DataType define_data_with_enum(const std::string& name, const std::string& description)
    {
        auto enum_type = reserve_enum<T>();
        return define_data(name, description, enum_type);
    }
    template<typename T>
    inline static DataType define_global_data_with_enum(const std::string& name, const std::string& description)
    {
        auto enum_type = reserve_global_enum<T>();
        return define_global_data(name, description, enum_type);
    }

    template<typename T>
    inline DataType define_data_with_enum(const std::string& name,
                                          const std::string& description,
                                          const EnumMappingType& enumerations)
    {
        auto enum_type = define_enum<T>(enumerations);
        return define_data(name, description, enum_type);
    }
    template<typename T>
    inline static DataType define_global_data_with_enum(const std::string& name,
                                                        const std::string& description,
                                                        const EnumMappingType& enumerations)
    {
        auto enum_type = define_global_enum<T>(enumerations);
        return define_global_data(name, description, enum_type);
    }

    EnumType define_data_with_anonymous_enum(const std::string& name,
                                             const std::string& description,
                                             const EnumMappingType& enumerations)
    {
        auto enum_type = define_anonymous_enum(enumerations);
        return define_data(name, description, enum_type);
    }
    inline static EnumType define_global_data_with_anonymous_enum(const std::string& name,
                                                                  const std::string& description,
                                                                  const EnumMappingType& enumerations)
    {
        auto enum_type = define_anonymous_global_enum(enumerations);
        return define_global_data(name, description, enum_type);
    }

    template<typename T>
    EnumType provide_enum(const EnumMappingType& enumerations)
    {
        return provide_enum(typeid(T), enumerations);
    }
    template<typename T>
    inline static EnumType provide_global_enum(const EnumMappingType& enumerations)
    {
        return provide_global_enum(typeid(T), enumerations);
    }

    virtual void enable() = 0;
    virtual void disable() = 0;
    virtual bool is_enabled() { return true; }

    static void reset_global_definitions();  // testability hook
    static void reset_global_enumerations(); // testability hook

protected:
    EnumType define_anonymous_enum(const EnumMappingType& enumerations);
    static EnumType define_anonymous_global_enum(const EnumMappingType& enumerations);

    template<typename T>
    inline EnumType reserve_enum()
    {
        return reserve_enum(typeid(T));
    }
    template<typename T>
    inline static EnumType reserve_global_enum()
    {
        return reserve_global_enum(typeid(T));
    }
    EnumType reserve_enum(std::type_index enum_type);
    static EnumType reserve_global_enum(std::type_index enum_type);

    template<typename T>
    inline EnumType define_enum(const EnumMappingType& enumerations)
    {
        return define_enum(typeid(T), enumerations);
    }
    template<typename T>
    inline static EnumType define_global_enum(const EnumMappingType& enumerations)
    {
        return define_global_enum(typeid(T), enumerations);
    }
    EnumType define_enum(std::type_index enum_type, const EnumMappingType& enumerations);
    static EnumType define_global_enum(std::type_index enum_type, const EnumMappingType& enumerations);

    EnumType provide_enum(std::type_index enum_type, const EnumMappingType& enumerations);
    static EnumType provide_global_enum(std::type_index enum_type, const EnumMappingType& enumerations);

    void catch_up(bool complete = false);

    virtual void finalize_event_definition(EventType event_type,
                                           const std::string& name,
                                           const std::string& description) = 0;
    virtual void finalize_data_definition(DataType data_type,
                                          const std::string& name,
                                          const std::string& description) = 0;
    virtual void finalize_data_definition(DataType data_type,
                                          const std::string& name,
                                          const std::string& description,
                                          EnumType enumeration [[maybe_unused]])
    {
        finalize_data_definition(data_type, name, description);
    }
    virtual void finalize_enum_definition(EnumType enum_type [[maybe_unused]],
                                          const EnumMappingType& enumerations [[maybe_unused]])
    {
    }

    class Definitions
    {
    public:
        static Definitions& instance();

        EventType define_event(EventBase* event);
        EventType define_event(const std::string& name, const std::string& description);
        DataType define_data(const std::string& name, const std::string& description);
        DataType define_data(const std::string& name, const std::string& description, EnumType enumeration);
        EventBase* get_event(int id)
        {
            auto ev = events.find(id);
            return ev == events.end() ? nullptr : ev->second;
        }
        const auto& get_definition(int id) const { return definitions.at(id - 1); }

        bool caught_up(size_t index) const { return index >= definitions.size(); }
        size_t catch_up(EventStream& stream, size_t index);
        void clear();

        Definitions() = default;

    private:
        enum class DefType {
            EVENT,
            DATA
        };
        struct Definition
        {
            DefType type;
            std::string name;
            std::string description;
            std::optional<EnumType> enumeration;
            Definition(DefType type, const std::string& name, const std::string& description)
                : type(type), name(name), description(description)
            {
            }
            Definition(DefType type, const std::string& name, const std::string& description, EnumType enum_type)
                : type(type), name(name), description(description), enumeration(enum_type)
            {
            }
        };
        std::deque<Definition> definitions;
        std::mutex definitions_mtx;
        std::unordered_map<int, EventBase*> events;
    };

    class Enumerations
    {
    public:
        static Enumerations& instance();

        template<typename T>
        EnumType reserve_enum()
        {
            return reserve_enum(typeid(T));
        }
        EnumType reserve_enum(std::type_index enum_type);

        template<typename T>
        EnumType define_enum(const EnumMappingType& enumerations)
        {
            return define_enum(typeid(T), enumerations);
        }
        EnumType define_enum(std::type_index enum_type, const EnumMappingType& enumerations);
        EnumType define_enum(size_t id, const EnumMappingType& enumerations);

        template<typename T>
        EnumType provide_enum(const EnumMappingType& enumerations)
        {
            return provide_enum(typeid(T), enumerations);
        }
        EnumType provide_enum(std::type_index enum_type, const EnumMappingType& enumerations);

        bool caught_up(size_t index) const { return index >= enumerations.size(); }
        size_t catch_up(EventStream& stream, size_t index);
        void clear();

        Enumerations() = default;

    private:
        std::deque<EnumMappingType> enumerations;
        std::unordered_map<std::variant<std::type_index, size_t>, EnumType> enumeration_types;
        std::mutex enumerations_mtx;
    };

    Definitions* definitions{nullptr};
    size_t definition_index{0};

    Enumerations* enumerations{nullptr};
    size_t enumeration_index{0};

    friend class EventStreamBroadcast;
};

} // namespace ampere::event_stream

/*
 * vim: sw=4:ts=4:et:si:cindent:cino=\:0,(0,g0,t0:
 */
