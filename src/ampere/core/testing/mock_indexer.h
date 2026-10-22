//
// Copyright (c) 2018-2019 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//

#pragma once

#include "ampere/sc/sc.h"
#include "ampere/archx/addr.h"
#include "ampere/uarch_common/indexer.h"

using namespace ampere::archx;

namespace ampere::core {

using namespace uarch_common;

class MockIndexer : public IndexInterface<PhysAddr>
{
public:
    MOCK_CONST_METHOD1(index, Index(PhysAddr));
};

class MockCacheLookup : public CacheLookupInterface<PhysAddr>
{
public:
    MOCK_CONST_METHOD1(address_resident, bool(PhysAddr));
};

} // namespace ampere::core

/*
 * vim: sw=4:ts=4:et:si:cindent:cino=\:0,(0,g0,t0:
 */
