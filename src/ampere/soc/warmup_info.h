/*
 * Copyright (c) 2025 Ampere Computing. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#pragma once

#include "ampere/mesh/chi/types.h"

namespace ampere::soc {

struct WarmupInfo
{
    sc::PhysAddr address;
    enum Action {
        READ,
        READ_UNIQUE,
        WRITE,
        FILL_CLEAN,
        FILL_DIRTY
    } action;
    mesh::chi::NodeId from;
};

} // namespace ampere::soc
