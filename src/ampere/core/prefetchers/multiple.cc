//
// Copyright (c) 2019-2021 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//

#include "ampere/core/prefetchers/multiple.h"
#include "ampere/uarch_common/memreq.h"

namespace ampere::core {

CP_Multiple::CP_Multiple(sc_core::sc_module_name name) : MultiplePrefetchers<Fetch>(name) {}

L2P_Multiple::L2P_Multiple(sc_core::sc_module_name name) : MultiplePrefetchers<MemReq>(name) {}

} // namespace ampere::core

/*
 * vim: sw=4:ts=4:et:si:cindent:cino=\:0,(0,g0,t0:
 */
