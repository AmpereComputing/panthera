//
// Copyright (c) 2019 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//
#pragma once

#include "ampere/sc/environment.h"
#include "ampere/sc/poly.h"

namespace ampere::sc {

// Common constructors
using poly_constructor_default = poly_constructor<>;
using poly_constructor_clock = poly_constructor<Clock*>;
using poly_constructor_clock_env = poly_constructor<Clock*, Environment*>;

} // namespace ampere::sc

/*
 * vim: sw=4:ts=4:et:si:cindent:cino=\:0,(0,g0,t0:
 */
