//
// Copyright (c) 2023 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//

#pragma once
#include "ampere/adis/src/adis.h"
namespace ampere::archx {

bool tlbi_by_va_all_level(adis::sys_alias_t c);
bool tlbi_by_va_last_level(adis::sys_alias_t c);

} // namespace ampere::archx
