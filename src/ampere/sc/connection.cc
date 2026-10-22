//
// Copyright (c) 2019 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//
#include "ampere/sc/connection.h"

namespace ampere::sc {

Connection::Connection(Clock& clock) : clock(clock) {}

Connection::Connection(const char* name, Clock& clock) : sc_core::sc_object(name), clock(clock) {}

void Connection::add_target(Runnable* to, Runnable* from)
{
    targets.push_back(to);
}

} // namespace ampere::sc

