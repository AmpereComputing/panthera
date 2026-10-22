//
// Copyright (c) 2018-2019 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//

#pragma once

#include <cstddef>
#include <cstdint>
#include <map>
#include <ostream>
#include <vector>

#include "ampere/archx/branch_type.h"
#include "ampere/sc/cycle.h"
#include "ampere/sc/sc.h"
#include "ampere/core/uop.h"
#include "ampere/core/flush.h"

namespace ampere::core {

using namespace ampere::sc;

class XU : public Module
{
    inline static const int MAX_NUM_RESOURCES = 8;
    inline static const size_t MAX_UOP_LATENCY{64};

    Param<unsigned> force_latency{"force_latency", "Force latency to value (0 disables force)", 0};
    Param<int> latency_add{"latency_add", "Add this amount to all uop latencies", 0};
    Param<unsigned> max_writebacks{"max_writebacks", "Maximum number of writebacks in a cycle", 1};
    Param<Below<unsigned, MAX_NUM_RESOURCES>> num_int_dividers{
        "num_int_dividers", "Number of integer dividers per pipe", 1};
    Param<Below<unsigned, MAX_NUM_RESOURCES>> num_fp_dividers{"num_fp_dividers", "Number of FP dividers per pipe", 1};

public:
    explicit XU(const sc_core::sc_module_name& name);

    Out<Uop> mispredict_port{"mispredict"};
    Out<Uop> mispredict_indirect_port{"mispredict_indirect"};

    unsigned latency(const ConstUop& uop) const;
    bool can_issue(const ConstUop& uop) const;
    void issue(Uop uop, unsigned latency_override = 0);
    void occupy();
    std::vector<Uop> execute();
    bool busy() const;
    void flush(const Flush& flush_info);

    void dump(std::ostream& os) const override;

private:
    struct
    {
        Event uop_execute{"uop_execute", "Uop begins execution"};
        Event uop_execute_done{"uop_execute_done", "Uop completed execution"};
        EventData<bool> branch{"branch", "Is this a branch"};
        EventData<bool> mispredict{"branch_mispredict", "Is this a mispredicted branch"};
        EventData<bool> mispredict_target{"branch_mispredict_target", "Is this a mispredicted branch target"};
        EventData<bool> mispredict_direction{"branch_mispredict_direction", "Is this a mispredicted branch direction"};
        EventData<BranchType> branch_type{"branch_type", "Type of branch"};
        EventData<BranchType> branch_type_mispredict{"branch_type_mispredict", "Type of branch"};
        EventData<unsigned> latency{"uop_latency", "Latency for the executed uop"};
        EventData<uint64_t> branch_target_pred{"branch_target_pred", "Predicted target of branch uop"};
        EventData<uint64_t> branch_target_true{"branch_target_true", "True target of branch uop"};
        EventData<uint64_t> branch_pc{"branch_pc", "Program counter of branch uop"};
    } event;

    std::multimap<Cycle, Uop> uop_pipeline;
    Cycle issue_cycle{0};
    Cycle resource_free_cycle[(unsigned)archx::ResourceType::RESOURCE_COUNT][MAX_NUM_RESOURCES]{{0}};
    unsigned num_resources[(unsigned)archx::ResourceType::RESOURCE_COUNT];

    unsigned free_resources(unsigned rtype) const;
};

} // namespace ampere::core

/*
 * vim: sw=4:ts=4:et:si:cindent:cino=\:0,(0,g0,t0:
 */
