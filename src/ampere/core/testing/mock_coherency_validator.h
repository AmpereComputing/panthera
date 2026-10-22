//
// Copyright (c) 2018-2019 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//

#pragma once

#include "ampere/sc/sc.h"
#include "ampere/archx/addr.h"
#include "ampere/core/coherency_validator.h"
#include <gmock/gmock.h>

namespace ampere::core {
class MockCoherencyValidator : public CoherencyValidationInterface<archx::PhysAddr>
{
public:
    MOCK_METHOD(bool, line_present, (const archx::PhysAddr&, bool is_instr), (override));
};

} // namespace ampere::core

/*
 * vim: sw=4:ts=4:et:si:cindent:cino=\:0,(0,g0,t0:
 */
