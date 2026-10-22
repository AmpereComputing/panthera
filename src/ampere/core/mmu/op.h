
//
// Copyright (c) 2018-2020 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//

#pragma once

#include "ampere/util/enum.h"

make_ns_enum(ampere::core, MMUOp,
    TLBLkp, // TLB Lookup
    PageWalk,
    Recycle,
    TLBFill,
    DuplicateStall
);

/*
 * vim: sw=4:ts=4:et:si:cindent:cino=\:0,(0,g0,t0:
 */
