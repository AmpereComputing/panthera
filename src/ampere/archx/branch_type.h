//
// Copyright (c) 2019, 2021-2022 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//

#pragma once

#include "ampere/branch_stream/branch_stream.pb.h"
#include "ampere/util/enum.h"

using namespace ampere;

/*
 * Note that the BranchType values are synchronized against RTL's
 * BTB_AllBranchType_e struct defined in rtl/Subsystems/Cpu/CpuPkg.sv, and they
 * should not be changed without considering it because it is helpful for them
 * to coincide with RTL's values.
 *
 * branchtype_names in branch_type.cc should also be updated if/when this is
 * changed.
 */
make_ns_enum_without_class(ampere::archx, BranchType,
    NONE = 0,
    CONDITIONAL_DIRECT = 1,
    UNCONDITIONAL_DIRECT = 2,
    UNCONDITIONAL_INDIRECT = 3,
    CALL_DIRECT = 4,
    CALL_INDIRECT = 5,
    RETURN = 6,
    UNSPECIFIED = 7, // Only used with perfect branch predictor
    ILLEGAL = 8
);

namespace ampere::archx {

static inline char branch_type_char(BranchType br_type)
{
    static const char types[] = {'.', 'N', 'D', 'I', 'C', 'V', 'R', '?'};
    static_assert(util::enum_size<BranchType>() - 1 == sizeof(types), "Must be kept in-sync with BranchType");
    return types[(int)br_type];
}

inline BranchType protobuf_to_branchtype(bs::Branch::Type type)
{
    switch (type) {
    case bs::Branch_Type_CONDITIONAL_DIRECT:
        return BranchType::CONDITIONAL_DIRECT;
        break;
    case bs::Branch_Type_UNCONDITIONAL_DIRECT:
        return BranchType::UNCONDITIONAL_DIRECT;
        break;
    case bs::Branch_Type_UNCONDITIONAL_INDIRECT:
        return BranchType::UNCONDITIONAL_INDIRECT;
        break;
    case bs::Branch_Type_CALL_DIRECT:
        return BranchType::CALL_DIRECT;
        break;
    case bs::Branch_Type_CALL_INDIRECT:
        return BranchType::CALL_INDIRECT;
        break;
    case bs::Branch_Type_RETURN:
        return BranchType::RETURN;
        break;
    case bs::Branch_Type_NONE:
        return BranchType::NONE;
        break;
    default:
        throw std::runtime_error("cannot convert incompatible protobuf bs::Branch::Type to archx::BranchType");
    }
    return BranchType::NONE;
}

inline bs::Branch::Type branchtype_to_protobuf(BranchType type)
{
    switch (type) {
    case BranchType::CONDITIONAL_DIRECT:
        return bs::Branch_Type_CONDITIONAL_DIRECT;
        break;
    case BranchType::UNCONDITIONAL_DIRECT:
        return bs::Branch_Type_UNCONDITIONAL_DIRECT;
        break;
    case BranchType::UNCONDITIONAL_INDIRECT:
        return bs::Branch_Type_UNCONDITIONAL_INDIRECT;
        break;
    case BranchType::CALL_DIRECT:
        return bs::Branch_Type_CALL_DIRECT;
        break;
    case BranchType::CALL_INDIRECT:
        return bs::Branch_Type_CALL_INDIRECT;
        break;
    case BranchType::RETURN:
        return bs::Branch_Type_RETURN;
        break;
    case BranchType::NONE:
        return bs::Branch_Type_NONE;
        break;
    default:
        throw std::runtime_error("cannot convert incompatible archx::BranchType to bs::Branch:Type");
    }
    return bs::Branch_Type_NONE;
}

inline bool is_branch(BranchType b)
{
    return b != BranchType::NONE;
}

inline bool is_conditional(BranchType b)
{
    return b == BranchType::CONDITIONAL_DIRECT;
}

inline bool is_unconditional(BranchType b)
{
    return (b == BranchType::UNCONDITIONAL_DIRECT || b == BranchType::UNCONDITIONAL_INDIRECT
            || b == BranchType::CALL_DIRECT || b == BranchType::CALL_INDIRECT || b == BranchType::RETURN);
}

inline bool is_direct(BranchType b)
{
    return b == BranchType::CONDITIONAL_DIRECT || b == BranchType::UNCONDITIONAL_DIRECT || b == BranchType::CALL_DIRECT;
}

inline bool is_indirect(BranchType b)
{
    return b == BranchType::UNCONDITIONAL_INDIRECT || b == BranchType::CALL_INDIRECT || b == BranchType::RETURN;
}

inline bool is_call(BranchType b)
{
    return b == BranchType::CALL_INDIRECT || b == BranchType::CALL_DIRECT;
}

inline bool is_return(BranchType b)
{
    return b == BranchType::RETURN;
}

inline bool is_unspecified_branch(BranchType b)
{
    return b == BranchType::UNSPECIFIED;
}

inline bool could_mispredict(BranchType b)
{
    return is_conditional(b) || is_indirect(b);
}

} // namespace ampere::archx
