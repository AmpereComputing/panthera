//
// Copyright (c) 2019 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//

#ifndef AMPERE_CORE_BOB_ENTRY_H
#define AMPERE_CORE_BOB_ENTRY_H

#include <boost/pointer_cast.hpp>
#include "ampere/archx/addr.h"
#include "ampere/core/branch_context.h"
#include "ampere/core/branch_info.h"
#include "ampere/core/uop.h"
#include "ampere/sc/credit.h"

namespace ampere::core {

class BOBEntry
{
public:
    sc::Credit extract_bobid() { return std::move(uop->bobid); }
    std::uint64_t alloc_id() const { return uop->alloc_id; }
    std::uint64_t fetch_id() const { return br_info.fetch->fetch_id; }
    Fetch fetch() const { return br_info.fetch; }
    archx::ProgramCounter pc() const { return br_info.pc; };
    sc::Txn transaction() { return boost::static_pointer_cast<sc::Transaction>(uop); }
    const Prediction& prediction() const { return br_info.fetch->get_prediction(); }
    const LinePrediction& line_prediction() const { return br_info.fetch->line_prediction; }
    BranchType branch_type() const { return br_info.type; }
    uint64_t instrs_since_last_branch() const { return uop->instrs_since_last_branch; }
    bool looped() const { return uop->is_looping(); }

    explicit BOBEntry(const Uop& uop, const BranchContext& context) : BOBEntry(uop, uop->branch_instruction(), context)
    {
    }

    bool taken() const { return br_info.taken; }

private:
    BOBEntry(const Uop& uop, const Instruction& instruction, const BranchContext& context)
        : uop{uop},
          br_info{instruction->archx_instruction->branch_type(),
                  instruction->archx_instruction->program_counter(),
                  instruction->fetch,
                  uop->alloc_id},
          br_context{context}
    {
    }

    Uop uop;

public:
    BranchInfo br_info;
    BranchContext br_context;
    bool committed{false};
};

} // namespace ampere::core

#endif // AMPERE_CORE_BOB_ENTRY_H
