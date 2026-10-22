//
// Copyright (c) 2020 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//

#ifndef AMPERE_ARCHX_EXCEPTION_TYPE_H
#define AMPERE_ARCHX_EXCEPTION_TYPE_H

namespace ampere::archx {

enum class ExceptionType {
    NONE,
    BRK,
    HLT,
    HVC,
    SMC,
    SVC,
    ERET,
    ISB,
    UNSPECIFIED
};

} // namespace ampere::archx

#endif // AMPERE_ARCHX_EXCEPTION_TYPE_H
