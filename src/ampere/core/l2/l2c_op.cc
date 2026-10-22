//
// Copyright (c) 2021 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//

#include "ampere/core/l2/l2c_op.h"

namespace ampere::core::l2 {

void InFlightStores::update(MemOp::BarrierType barrier_type, MemOp::AtomicType atomic_type, int delta)
{
    any += delta;
    AMPERE_ASSERT(any >= 0, "in flight store count went below 0");

    switch (barrier_type) {
    case MemOp::BarrierType::BAR_NONE:
    case MemOp::BarrierType::BAR_DMB:
        break;
    case MemOp::BarrierType::BAR_STORE_RELEASE:
        release += delta;
        AMPERE_ASSERT(release >= 0, "in flight release store count went below 0");
        break;
    case MemOp::BarrierType::BAR_STORE_EXCL:
        exclusive += delta;
        AMPERE_ASSERT(exclusive >= 0, "in flight exclusive store count went below 0");
        break;
    case MemOp::BarrierType::BAR_STORE_EXCL_RELEASE:
        exclusive += delta;
        release += delta;
        AMPERE_ASSERT(exclusive >= 0, "in flight exclusive store count went below 0");
        AMPERE_ASSERT(release >= 0, "in flight release store count went below 0");
        break;
    }

    if (atomic_type != uarch_common::MemOp::ATOMIC_NONE) {
        atomic += delta;
        AMPERE_ASSERT(atomic >= 0, "in flight release store count went below 0");
    }
}

unsigned InFlightStores::store_types() const
{
    unsigned flags{0};
    if (any > 0)
        flags |= uarch_common::MemOp::StoreType::STORE_ANY;
    if (exclusive > 0)
        flags |= uarch_common::MemOp::StoreType::STORE_EXCLUSIVE;
    if (release > 0)
        flags |= uarch_common::MemOp::StoreType::STORE_RELEASE;
    if (atomic > 0)
        flags |= uarch_common::MemOp::StoreType::STORE_ATOMIC;

    return flags;
}

} // namespace ampere::core::l2
