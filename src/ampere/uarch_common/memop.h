//
// Copyright (c) 2018-2020 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//

#pragma once

#include <ostream>
#include <string>

#include "ampere/util/enum.h"

// Prefetch type, ordered by highest to lowest priority
// (priority used in prefetch queue insertion policy)
make_ns_enum(ampere::uarch_common::MemOp, PrefetchType,
    NONE,
    BOP_BEST_OFFSET,
    BOP_SECOND_OFFSET,
    NEXTLINE,
    SPATIAL
);

make_ns_enum_without_class(ampere::uarch_common::MemOp, Type,
    READ,        // Plain read, may get shared or unique
    READUNIQUE,  // Read and get ownership
    WRITE,       // WriteBackFull -- write back modified data
    SNOOP,
    EVICT,       // Evict -- just drop the line, data-less
    WRITEEVICT,  // WriteEvictFull -- write back clean data
    MAKEUNIQUE,  // Get ownership without reading data
    SLC_FILL,    // Used only between mesh and SLC to fill without response
    NO_TYPE
);

make_ns_enum(ampere::uarch_common::MemOp, RequesterName,
    ICF,
    LSU,
    LSU_PREFETCH,
    IMMU,
    DMMU,
    PREFETCH,
    L2C,
    MEMORY
);

namespace ampere::uarch_common::MemOp {

// NOTE: update RequesterName as well
enum Requester {
    NONE = 0,
    ICF = 1 << static_cast<unsigned>(RequesterName::ICF),
    LSU = 1 << static_cast<unsigned>(RequesterName::LSU),
    LSU_PREFETCH = 1 << static_cast<unsigned>(RequesterName::LSU_PREFETCH),
    IMMU = 1 << static_cast<unsigned>(RequesterName::IMMU),
    DMMU = 1 << static_cast<unsigned>(RequesterName::DMMU),
    PREFETCH = 1 << static_cast<unsigned>(RequesterName::PREFETCH),
    L2C = 1 << static_cast<unsigned>(RequesterName::L2C),
    MEMORY = 1 << static_cast<unsigned>(RequesterName::MEMORY),
    ANY = (1 << util::enum_size<RequesterName>()) - 1
};

enum BarrierType {
    BAR_NONE,
    BAR_DMB,
    BAR_STORE_RELEASE,
    BAR_STORE_EXCL,
    BAR_STORE_EXCL_RELEASE
};

enum AtomicType {
    ATOMIC_NONE,
    ATOMIC_LOAD,
    ATOMIC_LOAD_WIDE,
    ATOMIC_STORE
};

enum SnoopType {
    SNOOP_NONE,
    SNOOP_SYNONYM,
    SNOOP_IN_L1,
    SNOOP_OTHER
};

// Should be in increasing order of "importance" or complexity,
// which helps with combining rules
enum StoreType {
    STORE_ANY = 1U,
    STORE_BARRIER = 1U << 1,
    STORE_EXCLUSIVE = 1U << 2,
    STORE_RELEASE = 1U << 3,
    STORE_ATOMIC = 1U << 4,
};

std::string RequesterString(Requester requester);

extern unsigned barrier_to_store_type(MemOp::BarrierType barrier_type);

inline std::ostream& operator<<(std::ostream& os, Requester requester)
{
    os << RequesterString(requester);
    return os;
}

} // namespace ampere::uarch_common::MemOp

/*
 * vim: sw=4:ts=4:et:si:cindent:cino=\:0,(0,g0,t0:
 */
