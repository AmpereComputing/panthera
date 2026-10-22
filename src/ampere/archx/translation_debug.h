//
// Copyright (c) 2022 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//
#ifndef AMPERE_ARCHX_TRANSLATION_DEBUG_H
#define AMPERE_ARCHX_TRANSLATION_DEBUG_H

#include "ampere/archx/addr.h"
#include "ampere/archx/driver.h"

namespace ampere::archx {

std::string debug_translation(const DriverPort& driver, VirtAddr va, bool is_instr);

} // namespace ampere::archx

#endif // AMPERE_ARCHX_TRANSLATION_DEBUG_H

/*
 * vim: sw=4:ts=4:et:si:cindent:cino=\:0,(0,g0,t0:
 */
