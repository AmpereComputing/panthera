//
// Copyright (c) 2018-2019 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//

#include "ampere/core/testing/uop_consumer.h"

namespace ampere::core {

bool UopConsumer::can_put() const
{
    return true;
}

void UopConsumer::put(Uop uop)
{
    uops.push_back(std::move(uop));
}

const Uop& UopConsumer::get(uint64_t index) const
{
    return uops.at(index);
}

uint64_t UopConsumer::size() const
{
    return uops.size();
}

void UopConsumer::release()
{
    for (auto& u : uops) {
        if (u->robid)
            u->robid->release();
        if (u->schid)
            u->schid->release();
        if (u->lobid)
            u->lobid->release();
        if (u->lubid)
            u->lubid->release();
        if (u->sobid)
            u->sobid->release();
        if (u->prfid)
            u->prfid->release();
        if (u->prfid2)
            u->prfid2->release();
        if (u->flagsid)
            u->flagsid->release();
    }
}

} // namespace ampere::core

/*
 * vim: sw=4:ts=4:et:si:cindent:cino=\:0,(0,g0,t0:
 */
