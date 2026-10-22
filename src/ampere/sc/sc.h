//
// Copyright (c) 2018-2019 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//
#pragma once

#include <functional>
#include <string>

// IWYU pragma: begin_exports
#include "ampere/sc/addr.h"
#include "ampere/sc/config_port.h"
#include "ampere/sc/connect.h"
#include "ampere/sc/counter.h"
#include "ampere/sc/debug.h"
#include "ampere/sc/event.h"
#include "ampere/sc/inum.h"
#include "ampere/sc/module.h"
#include "ampere/sc/param.h"
#include "ampere/sc/pipeline.h"
#include "ampere/sc/poly.h"
#include "ampere/sc/poly_constructors.h"
#include "ampere/sc/report.h"
#include "ampere/sc/submodule.h"
#include "ampere/sc/transaction.h"

#include "ampere/util/bits.h"
// IWYU pragma: end_exports

#include <fmt/core.h>
#include <fmt/ostream.h>

#include <effolkronium/random.hpp>

namespace ampere::sc {

using Random = effolkronium::random_static;

class LocalRandom : public effolkronium::random_local
{
public:
    explicit LocalRandom(size_t seed_value) { seed(seed_value); }
    explicit LocalRandom(const Module &module) : LocalRandom(std::hash<std::string>{}(module.name())) {}

    LocalRandom() : LocalRandom(0) {} // default seed is time
};

} // namespace ampere::sc

template<>
struct fmt::formatter<sc_core::sc_time> : fmt::ostream_formatter
{};

/*
 * vim: sw=4:ts=4:et:si:cindent:cino=\:0,(0,g0,t0:
 */
