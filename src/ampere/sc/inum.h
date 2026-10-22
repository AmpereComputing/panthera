//
// Copyright (c) 2018-2024 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//
#pragma once
#include <cstdint>
#include "ampere/archx/inum.h"
#include "ampere/sc/event.h"

namespace ampere::archx { // NOTE: purposefully ampere::archx namespace to drive ADL

AMPERE_EVENT_DATA_FUNC(inum_t x, inum, x.id());

} // namespace ampere::archx

namespace ampere::sc {

template<>
class EventData<archx::inum_t> : public internal::EventDataBase
{
public:
    EventData(const std::string& name, const std::string& description) : internal::EventDataBase(name, description) {}
    EventArg<std::int64_t> operator()(archx::inum_t x) { return {definition, x.id()}; }
};

} // namespace ampere::sc

/*
 * vim: sw=4:ts=4:et:si:cindent:cino=\:0,(0,g0,t0:
 */
