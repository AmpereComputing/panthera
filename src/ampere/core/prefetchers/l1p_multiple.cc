//
// Copyright (c) 2019-2021 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//

#include "ampere/core/prefetchers/l1p_multiple.h"

namespace ampere::core {

L1P_Multiple::L1P_Multiple(sc_core::sc_module_name name) : MultipleBackendPrefetchers<BackendMemReq>(name) {}

} // namespace ampere::core

/*
 * vim: sw=4:ts=4:et:si:cindent:cino=\:0,(0,g0,t0:
 */
