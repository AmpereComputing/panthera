//
// Copyright (c) 2018-2020 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//

#ifndef AMPERE_UARCH_COMMON_INDEXER_H
#define AMPERE_UARCH_COMMON_INDEXER_H

#include "ampere/sc/sc.h"

namespace ampere::uarch_common {

using Index = uint64_t;

template<typename Address>
class IndexInterface : public sc_core::sc_interface
{
public:
    virtual Index index(Address addr) const = 0;
};

template<typename Address>
using Indexer = sc_core::sc_port<IndexInterface<Address>>;

template<typename Address>
class CacheLookupInterface : public sc_core::sc_interface
{
public:
    virtual bool address_resident(Address addr) const = 0;
};

template<typename Address>
using CacheLookup = sc_core::sc_port<CacheLookupInterface<Address>>;

} // namespace ampere::uarch_common

#endif // AMPERE_UARCH_COMMON_INDEXER_H

/*
 * vim: sw=4:ts=4:et:si:cindent:cino=\:0,(0,g0,t0:
 */
