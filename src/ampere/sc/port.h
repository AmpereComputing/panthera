//
// Copyright (c) 2019-2021 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//
#pragma once

#include <sysc/communication/sc_port.h>

namespace ampere::sc {

template<typename T>
using optional_sc_port = sc_core::sc_port<T, 1, sc_core::SC_ZERO_OR_MORE_BOUND>;

} // namespace ampere::sc

/*
 * vim: sw=4:ts=4:et:si:cindent:cino=\:0,(0,g0,t0:
 */
