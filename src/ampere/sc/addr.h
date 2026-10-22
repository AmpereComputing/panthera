//
// Copyright (c) 2018-2019 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//

#ifndef AMPERE_SC_ADDR_H
#define AMPERE_SC_ADDR_H

#include <cstdint>
#include "ampere/archx/addr.h"
#include "ampere/sc/event.h"

namespace ampere::archx { // NOTE: purposefully ampere::archx namespace to drive ADL

AMPERE_EVENT_DATA_FUNC(VirtAddr x, virtual_address, x.value());
AMPERE_EVENT_DATA_FUNC(PhysAddr x, physical_address, x.value());
AMPERE_EVENT_DATA_FUNC(ProgramCounter x, program_counter, x.value());

} // namespace ampere::archx

namespace ampere::sc {

template<>
class EventData<archx::ProgramCounter> : public internal::EventDataBase
{
public:
    EventData(const std::string& name, const std::string& description) : internal::EventDataBase(name, description) {}
    EventArg<std::uint64_t> operator()(archx::ProgramCounter pc) { return {definition, pc.value()}; }
};

} // namespace ampere::sc

#endif // AMPERE_SC_ADDR_H

/*
 * vim: sw=4:ts=4:et:si:cindent:cino=\:0,(0,g0,t0:
 */
