//
// Copyright (c) 2021 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//

#ifndef AMPERE_ARCHX_DIVIDE_TYPE_H
#define AMPERE_ARCHX_DIVIDE_TYPE_H

namespace ampere::archx {

enum class DivideType {
    NONE = 0,
    INT_DIVIDE = 1,
    FP_SCALAR_DIVIDE = 2,
    FP_SCALAR_SQRT = 3,
    FP_VECTOR_DIVIDE = 4,
    FP_VECTOR_SQRT = 5
};

inline bool is_divide(DivideType d)
{
    return d != DivideType::NONE;
}

inline bool is_sqrt(DivideType d)
{
    return d == DivideType::FP_SCALAR_SQRT || d == DivideType::FP_VECTOR_SQRT;
}

inline bool is_integer(DivideType d)
{
    return d == DivideType::INT_DIVIDE;
}

inline bool is_scalar(DivideType d)
{
    return d == DivideType::FP_SCALAR_DIVIDE || d == DivideType::FP_SCALAR_SQRT;
}

inline bool is_vector(DivideType d)
{
    return d == DivideType::FP_VECTOR_DIVIDE || d == DivideType::FP_VECTOR_SQRT;
}

} // namespace ampere::archx

#endif // AMPERE_ARCHX_DIVIDE_TYPE_H
