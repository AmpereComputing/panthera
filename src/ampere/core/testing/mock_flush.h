//
// Copyright (c) 2019 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//

#pragma once

#include "ampere/sc/sc.h"
#include "ampere/core/flush.h"
#include "ampere/sc/testing/test_interfaces.h"

namespace ampere::core {

using MockFlushReader = sc::MockReader<Flush>;
using MockFlushWriter = sc::MockPutter<Flush>;

} // namespace ampere::core

/*
 * vim: sw=4:ts=4:et:si:cindent:cino=\:0,(0,g0,t0:
 */
