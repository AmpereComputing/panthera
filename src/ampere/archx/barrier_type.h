//
// Copyright (c) 2020 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//

#ifndef AMPERE_ARCHX_BARRIER_TYPE_H
#define AMPERE_ARCHX_BARRIER_TYPE_H

namespace ampere::archx {

enum class Barrier_Type {
    NONE,
    DSB,
    DMB,
    ISB,
    CSDB
};

inline bool is_barrier(Barrier_Type b)
{
    return b != Barrier_Type::NONE;
}

inline bool is_dsb(Barrier_Type b)
{
    return b == Barrier_Type::DSB;
}

inline bool is_dmb(Barrier_Type b)
{
    return b == Barrier_Type::DMB;
}

inline bool is_isb(Barrier_Type b)
{
    return b == Barrier_Type::ISB;
}

inline bool is_csdb(Barrier_Type b)
{
    return b == Barrier_Type::CSDB;
}

} // namespace ampere::archx

#endif // AMPERE_ARCHX_BARRIER_TYPE_H
