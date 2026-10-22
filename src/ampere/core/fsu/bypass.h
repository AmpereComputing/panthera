//
// Copyright (c) 2018-2024 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//

#include "ampere/util/enum.h"
#include "ampere/archx/uop_info.h"
#include "ampere/core/uop.h"

#include <array>
#include <cstdint>
#include <map>
#include <optional>

#include <nlohmann/json_fwd.hpp>

using nlohmann::json;

make_ns_enum(ampere::core, FSUBypassLevel,
    LATE,       // Late source bypass
    LOCAL,      // Local (unit) bypass
    UNROUNDED,  // No local unrounded bypass (either source specific or producer was unrounded)
    NORMAL      // Normal/cross bypass
);

namespace ampere::core {

// Bypass level latency defaults (if not provided). Used to fill in latency for levels
// which are not necessarily explicit from YAML and will be used.
inline constexpr std::array<std::optional<FSUBypassLevel>, util::enum_size<FSUBypassLevel>()> bypass_level_defaults = {
    std::nullopt,
    std::nullopt,
    FSUBypassLevel::LOCAL, // Unrounded will be chosen based upon dest, but should default to LOCAL
    std::nullopt,
};

struct fsu_producer_unit
{
    archx::FsuUnit unit;
    uint8_t unit_suffix;
    bool dest_no_unrounded_bypass;

    static fsu_producer_unit from_uop(const ConstUop &uop)
    {
        auto unit = uop->fsu_unit_producing();
        AMPERE_ASSERT(
            !uop->is_fsu() || unit != archx::FsuUnit::NONE, "No FSU unit for producer: {}", uop->uop_disasm());
        return {unit,
                static_cast<uint8_t>(uop->archx_uop->info->fsu_unit_producing_suffix),
                uop->archx_uop->info->dest_no_unr_byp};
    }
};

struct fsu_consumer_src
{
    archx::FsuUnit unit;
    uint8_t src;
    archx::FsuSrcType src_type;

    static fsu_consumer_src from_uop(const ConstUop &uop, uint8_t source)
    {
        auto unit = uop->fsu_unit();
        AMPERE_ASSERT(
            !uop->is_fsu() || unit != archx::FsuUnit::NONE, "No FSU unit for consumer: {}", uop->uop_disasm());
        return {unit, source, uop->archx_uop->info->fsu_src_type[source]};
    }
};

// Essentially defines FSU bypass matrix (from JSON param), where matrix is encoded
// as a series of potentially overlapping "rules". Each rule is defined for a producer
// unit and optional unit-suffix or source, which provides specific level latencies. If
// not all latencies are provided, they will be filled in by defaults (i.e. less specific rule).
// Rules are picked in priority order, where order is:
//
// 1. "source" provided
// 2. "unit_suffix" provided
// 3. No specifier (aka default)
//
// JSON is of format:
// {
//     "latencies": {
//         "<full human readable unit>": {  # Defines a "rule" for assigning bypass latencies
//             "unit": "<unit name>",                # Required
//             "unit_suffix": <unit suffix number>,  # Optional, rule is for specific unit-suffix
//             "source": <source number>,            # Optional, rule is for specific source
//             "levels": {                           # Defines latencies per level for rule
//                 "<level>": <latency>,
//                 ...
//             }
//         },
//         ...
//     },
//     "locality": {  # Defines unit locality (which producer units are local to consumer units)
//         "<unit name>": ["<local unit name>", ...],
//         ...
//     }
// }
class FSUBypassDefinition
{
    using bypass_array = std::array<uint8_t, util::enum_size<FSUBypassLevel>()>;

    class Unit
    {
        static void normalize_bypass(bypass_array &bypass, const bypass_array &defaults);
        static void dump_bypass_array(std::ostream &os, const bypass_array &bypass);

        bypass_array defaults{};
        std::map<uint8_t, bypass_array> sources;
        std::map<uint8_t, bypass_array> suffixes;

    public:
        void add(const json &j);
        void finalize();

        bool level_exists(FSUBypassLevel level) const;
        uint8_t bypass_latency(uint8_t producer_unit_suffix, FSUBypassLevel level, uint8_t consumer_source) const;

        void dump(std::ostream &os) const;

        void dump_matrix_header_portion(std::ostream &os, archx::FsuUnit unit) const;
        void dump_matrix_row_portion(std::ostream &os, FSUBypassLevel level) const;
    };

    std::array<Unit, util::enum_size<archx::FsuUnit>()> units;
    std::array<std::bitset<util::enum_size<archx::FsuUnit>()>, util::enum_size<archx::FsuUnit>()> locality;

    bool is_local(archx::FsuUnit from, archx::FsuUnit to) const;
    FSUBypassLevel bypass_level(const fsu_producer_unit &producer, const fsu_consumer_src &consumer) const;

public:
    uint8_t bypass_latency(const fsu_producer_unit &producer, const fsu_consumer_src &consumer) const;

    void dump(std::ostream &os) const;
    void dump_matrix(std::ostream &os) const;

    friend void from_json(const json &j, FSUBypassDefinition &definition);
};

} // namespace ampere::core

/*
 * vim: sw=4:ts=4:et:si:cindent:cino=\:0,(0,g0,t0:
 */
