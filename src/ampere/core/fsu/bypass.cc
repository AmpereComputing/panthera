//
// Copyright (c) 2018-2024 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//

#include "ampere/core/fsu/bypass.h"
#include "ampere/archx/uop_info.h"
#include "ampere/sc/report.h"
#include "ampere/util/enum.h"

#include <cstddef>
#include <cstdint>
#include <ostream>
#include <vector>
#include <fmt/ostream.h>

namespace ampere::core {

void FSUBypassDefinition::Unit::add(const json &j)
{
    bypass_array bypass_latencies{};
    for (const auto &[level, bypass] : j["levels"].items()) {
        auto l = util::Enumeration<FSUBypassLevel>(level).value();
        bypass_latencies[static_cast<size_t>(l)] = bypass.get<uint8_t>();
    }

    if (auto source = j.find("source"); source != j.end())
        sources[source->get<uint8_t>()] = bypass_latencies;
    else if (auto unit_suffix = j.find("unit_suffix"); unit_suffix != j.end())
        suffixes[unit_suffix->get<uint8_t>()] = bypass_latencies;
    else
        defaults = bypass_latencies;
}

void FSUBypassDefinition::Unit::normalize_bypass(FSUBypassDefinition::bypass_array &bypass,
                                                 const FSUBypassDefinition::bypass_array &defaults)
{
    for (size_t level = 0; level < util::enum_size<FSUBypassLevel>(); ++level) {
        auto &bypass_latency = bypass[level];
        if (bypass_latency)
            continue;

        if (const auto &default_bypass_latency = defaults[level]; default_bypass_latency) {
            bypass_latency = default_bypass_latency;
        } else if (auto default_level = bypass_level_defaults[level];
                   default_level && bypass[static_cast<size_t>(*default_level)])
        {
            bypass_latency = bypass[static_cast<size_t>(*default_level)];
        }
    }
}

void FSUBypassDefinition::Unit::finalize()
{
    for (auto &[_, bypass] : sources)
        normalize_bypass(bypass, defaults);

    for (auto &[_, bypass] : suffixes)
        normalize_bypass(bypass, defaults);

    normalize_bypass(defaults, {});
}

bool FSUBypassDefinition::Unit::level_exists(FSUBypassLevel level) const
{
    auto level_index = static_cast<size_t>(level);
    for (const auto &[_, latencies] : suffixes)
        if (latencies[level_index])
            return true;

    return defaults[level_index];
}

uint8_t FSUBypassDefinition::Unit::bypass_latency(uint8_t producer_unit_suffix,
                                                  FSUBypassLevel level,
                                                  uint8_t consumer_source) const
{
    auto level_index = static_cast<size_t>(level);
    if (!sources.empty()) {
        if (auto source_iter = sources.find(consumer_source + 1); source_iter != sources.end())
            return source_iter->second[level_index];
    }
    if (producer_unit_suffix) {
        if (auto level_iter = suffixes.find(producer_unit_suffix); level_iter != suffixes.end())
            return level_iter->second[level_index];
    }
    return defaults[level_index];
}

void FSUBypassDefinition::Unit::dump_bypass_array(std::ostream &os, const bypass_array &bypass)
{
    for (auto level : util::each_enum<FSUBypassLevel>()) {
        if (auto b = bypass[static_cast<size_t>(level)]; b)
            os << level << ": " << static_cast<unsigned>(b) << ", ";
    }
}

void FSUBypassDefinition::Unit::dump(std::ostream &os) const
{
    os << "  defaults: ";
    dump_bypass_array(os, defaults);
    os << "\n";

    if (!sources.empty()) {
        for (const auto &[s, b] : sources) {
            os << "  source " << static_cast<unsigned>(s) << ": ";
            dump_bypass_array(os, b);
            os << "\n";
        }
    }

    if (!suffixes.empty()) {
        for (const auto &[l, b] : suffixes) {
            os << "  suffix " << static_cast<unsigned>(l) << ": ";
            dump_bypass_array(os, b);
            os << "\n";
        }
    }
}

void FSUBypassDefinition::Unit::dump_matrix_header_portion(std::ostream &os, archx::FsuUnit unit) const
{
    if (defaults != bypass_array{})
        fmt::print(os, " {:>3}", unit);

    for (const auto &[suffix, _] : suffixes)
        fmt::print(os, " {:>2}{}", unit, suffix);
}

void dump_matrix_latency(std::ostream &os, uint8_t latency)
{
    if (latency)
        fmt::print(os, " {:>3}", latency);
    else
        fmt::print(os, " {:>3}", 'X');
}

void FSUBypassDefinition::Unit::dump_matrix_row_portion(std::ostream &os, FSUBypassLevel level) const
{
    if (defaults != bypass_array{})
        dump_matrix_latency(os, defaults[static_cast<size_t>(level)]);

    for (const auto &[_, latencies] : suffixes)
        dump_matrix_latency(os, latencies[static_cast<size_t>(level)]);
}

bool FSUBypassDefinition::is_local(archx::FsuUnit from, archx::FsuUnit to) const
{
    return (from == to) || locality[static_cast<size_t>(from)][static_cast<size_t>(to)];
}

FSUBypassLevel FSUBypassDefinition::bypass_level(const fsu_producer_unit &producer,
                                                 const fsu_consumer_src &consumer) const
{
    if (!is_local(producer.unit, consumer.unit) || consumer.src_type == archx::FsuSrcType::NO_LOCAL_BYP)
        return FSUBypassLevel::NORMAL;

    if (producer.dest_no_unrounded_bypass && consumer.src_type == archx::FsuSrcType::NORMAL)
        return FSUBypassLevel::UNROUNDED;

    if (consumer.src_type == archx::FsuSrcType::LATE)
        return FSUBypassLevel::LATE;

    return FSUBypassLevel::LOCAL;
}

uint8_t FSUBypassDefinition::bypass_latency(const fsu_producer_unit &producer, const fsu_consumer_src &consumer) const
{
    auto level = bypass_level(producer, consumer);
    auto bypass = units[static_cast<size_t>(producer.unit)].bypass_latency(producer.unit_suffix, level, consumer.src);
    AMPERE_ASSERT(bypass > 0, "Bypass latency not defined for producer/consumer combination");
    return bypass;
}

char level_abbreviation(FSUBypassLevel level)
{
    switch (level) {
    case FSUBypassLevel::LATE:
        return 'l';
    case FSUBypassLevel::LOCAL:
        return 'n';
    case FSUBypassLevel::NORMAL:
        return 'x';
    default:
        break;
    }
    return '?';
}

void FSUBypassDefinition::dump(std::ostream &os) const
{
    for (auto e : util::each_enum<FsuUnit>()) {
        os << e << ":\n";
        units[static_cast<size_t>(e)].dump(os);
    }
}

void FSUBypassDefinition::dump_matrix(std::ostream &os) const
{
    const std::vector<FSUBypassLevel> levels = {
        FSUBypassLevel::LOCAL,
        FSUBypassLevel::NORMAL,
        FSUBypassLevel::LATE,
    };

    os << "     ";
    for (auto p : util::each_enum<FsuUnit>())
        if (p != archx::FsuUnit::NONE && p != archx::FsuUnit::DV)
            units[static_cast<size_t>(p)].dump_matrix_header_portion(os, p);

    os << "\n";
    for (auto c : util::each_enum<FsuUnit>()) {
        if (c == archx::FsuUnit::NONE)
            continue;

        for (auto l : levels) {
            if (!units[static_cast<size_t>(c)].level_exists(l))
                continue;

            auto abbrev = level_abbreviation(l);
            fmt::print(os, "{:<2}.{}:", c, abbrev);
            for (auto p : util::each_enum<FsuUnit>()) {
                auto lp = is_local(p, c) ? l : FSUBypassLevel::NORMAL;
                if (p != archx::FsuUnit::NONE && p != archx::FsuUnit::DV)
                    units[static_cast<size_t>(p)].dump_matrix_row_portion(os, lp);
            }
            os << "\n";
        }
    }
}

void from_json(const json &j, FSUBypassDefinition &definition)
{
    if (auto latencies = j.find("latencies"); latencies != j.end()) {
        for (const auto &[_name, e] : latencies->items()) {
            auto unit = e["unit"].get<archx::FsuUnit>();
            definition.units[static_cast<size_t>(unit)].add(e);
        }
    }

    if (auto locality = j.find("locality"); locality != j.end()) {
        for (const auto &[unit_name, e] : j["locality"].items()) {
            auto unit = util::Enumeration<archx::FsuUnit>(unit_name).value();
            auto &local = definition.locality[static_cast<size_t>(unit)];
            for (const auto &u : e)
                local.set(static_cast<size_t>(u.get<archx::FsuUnit>()));
        }
    }

    for (auto &unit : definition.units)
        unit.finalize();
}

} // namespace ampere::core

/*
 * vim: sw=4:ts=4:et:si:cindent:cino=\:0,(0,g0,t0:
 */
