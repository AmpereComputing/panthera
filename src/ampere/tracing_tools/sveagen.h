//
// Copyright (c) 2022 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//
#pragma once
#include <optional>
#include "tracer.h"
#include "ampere/archx/pred_reg.h"
#include "ampere/archx/vector.h"
#include "ampere/adis/src/adis.h"

namespace ampere::sveagen {

struct predicate_info_t
{
    unsigned reg_id;
    ampere::archx::preg_t reg_val;
    predicate_info_t(unsigned _reg_id, const ampere::archx::preg_t& _reg_val) : reg_id(_reg_id), reg_val(_reg_val) {}
};

void handle_sve_memops(tracer::QEMUStateAccessor* state_accessor, tracer::Instruction& instruction);

std::optional<unsigned> governing_predicate(uint32_t encoding, const adis::encoding_enum_t adis_info);

} // namespace ampere::sveagen

