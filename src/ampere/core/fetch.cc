//
// Copyright (c) 2019-2024 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//

#include "ampere/core/fetch.h"

namespace ampere::core {

AMPERE_EVENT_DATA_DEFN(fetch_id, "Fetch Id");

ProgramCounter FetchObj::next_program_counter() const
{
    if (prediction.is_taken())
        return prediction.target;
    else
        return m_exit_pc + 4;
}

ProgramCounter FetchObj::instruction_pc(size_t instruction_index) const
{
    return program_counter + instruction_index * 4;
}

size_t FetchObj::num_instructions() const
{
    return (m_exit_pc - program_counter) / 4 + 1;
}

size_t FetchObj::instructions_fetched() const
{
    return (next_fetch_pc - program_counter) / 4;
}

void FetchObj::fetch_instruction(ProgramCounter inst_pc)
{
    AMPERE_ASSERT(inst_pc == next_fetch_pc,
                  "Fetching unexpected PC 0x{:x} (expected 0x{:x})",
                  inst_pc.value(),
                  next_fetch_pc.value());
    next_fetch_pc += 4;
}

/*
 * This is only used by return-related calculations (i.e. the RSB). So, we
 * update the target, but also know that we can't predict nextline at the
 * target of a return.
 */
void FetchObj::update_prediction_target(ProgramCounter target)
{
    prediction.target = target;
    prediction.target_nextline = false;
}

void FetchObj::update_prediction(const Prediction& new_prediction, bool truncate)
{
    ProgramCounter new_exit_point =
        new_prediction.is_taken() ? new_prediction.branch_pc : program_counter.next_line_address() - 4;

    if (truncate && new_exit_point > m_exit_pc) {
        prediction = Prediction{}; // defaults to nextline prediction
    } else {
        prediction = new_prediction;
        m_exit_pc = new_exit_point;
    }
}

void FetchObj::update_prediction_keep_exit_pc(const Prediction& new_prediction)
{
    prediction = new_prediction;
}

} // namespace ampere::core
