//
// Copyright (c) 2019-2020 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//

#include "ampere/uarch_common/memop.h"
#include "ampere/util/enum.h"

namespace ampere::uarch_common::MemOp {

std::string RequesterString(Requester requester)
{
    std::string reqstr;
    for (auto r : util::each_enum<RequesterName>()) {
        if (auto mask = 1U << static_cast<unsigned>(r); requester & mask) {
            if (!reqstr.empty())
                reqstr += ", ";
            reqstr += util::enum_to_string(r);
        }
    }

    return reqstr;
}

unsigned barrier_to_store_type(MemOp::BarrierType barrier_type)
{
    unsigned flags{0};

    switch (barrier_type) {
    case MemOp::BarrierType::BAR_NONE:
        break;
    case MemOp::BarrierType::BAR_DMB:
        flags |= StoreType::STORE_BARRIER;
        break;
    case MemOp::BarrierType::BAR_STORE_RELEASE:
        flags |= StoreType::STORE_RELEASE;
        break;
    case MemOp::BarrierType::BAR_STORE_EXCL:
        flags |= StoreType::STORE_EXCLUSIVE;
        break;
    case MemOp::BarrierType::BAR_STORE_EXCL_RELEASE:
        flags |= StoreType::STORE_RELEASE | StoreType::STORE_EXCLUSIVE;
        break;
    }

    return flags;
}

} // namespace ampere::uarch_common::MemOp

/*
 * vim: sw=4:ts=4:et:si:cindent:cino=\:0,(0,g0,t0:
 */
