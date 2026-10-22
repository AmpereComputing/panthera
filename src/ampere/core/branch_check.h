//
// Copyright (c) 2019-2024 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//

#pragma once

#include "ampere/archx/branch_type.h"
#include "ampere/archx/addr.h"
#include "ampere/core/fetch.h"
#include "ampere/core/instruction.h"
#include "ampere/core/prediction.h"

namespace ampere::core {

std::optional<FlushType> branch_check(Fetch& fetch, Instruction& instruction);

}
