//
// Copyright (c) 2018-2019 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//

#include "ampere/core/xu.h"
#include <iterator>
#include "ampere/sc/report.h"

namespace ampere::core {
// #define DEBUG_XU
#if defined(DEBUG_XU)
#define DBGXU(format_str, ...) fmt::print(stderr, "{}: " format_str "\n", fmt_cycle(), ##__VA_ARGS__)
#else
#define DBGXU(format_str, ...) \
    do {                       \
    } while (0)
#endif
// #define DEBUG_BRMP
#if defined(DEBUG_BRMP) // branch mispredicts
#define DBGBRMP(format_str, ...) fmt::print(stderr, "{}: " format_str "\n", fmt_cycle(), ##__VA_ARGS__)
#else
#define DBGBRMP(format_str, ...) \
    do {                         \
    } while (0)
#endif

XU::XU(const sc_core::sc_module_name& name) : Module(name)
{
    num_resources[(int)archx::ResourceType::INT_DIVIDER] = num_int_dividers;
    num_resources[(int)archx::ResourceType::FP_DIVIDER] = num_fp_dividers;
}

bool XU::busy() const
{
    return !uop_pipeline.empty();
}

unsigned XU::latency(const ConstUop& uop) const
{
    if (force_latency)
        return force_latency;

    int latency = uop->latency();
    latency += latency_add;
    return std::max(std::min<int>(latency, MAX_UOP_LATENCY - 1), 1);
}

unsigned XU::free_resources(unsigned rtype) const
{
    // Count the number of available resources of this type
    int free_count{0};
    for (unsigned r = 0; r < num_resources[rtype]; r++)
        if (get_cycle() >= resource_free_cycle[rtype][r])
            free_count++;

    return free_count;
}

void XU::occupy()
{
    // Used when we issue load-canceled uops that will not complete. Prevent anything else from
    // issuing on this pipe in this cycle, but don't do any of the other side-effects of issue, since
    // this uop will be canceled before it writes back.
    issue_cycle = get_cycle();
}

bool XU::can_issue(const ConstUop& uop) const
{
    const auto now = get_cycle();
    if (issue_cycle == now) {
        DBGXU("     xu used  {:x} {:s} {}", uop->pc().value(), uop->get_uopcode(), uop->get_txid());
        return false;
    }

    // If this uop uses a multi-cycle resource and it's still busy executing a previous
    // uop, then we can't issue.
    //
    // get_resource_count() will cap the number of resources needed for a uop at the maximum number
    // that exist, so that we don't end up with uops that can't ever issue.
    if (uop->get_resource_type() != archx::ResourceType::NONE) {
        auto rtype = static_cast<unsigned>(uop->get_resource_type());
        if (free_resources(rtype) < uop->get_resource_count(num_resources[rtype])) {
            DBGXU("     xu resource rate limit  {:x} {:s} {}", uop->pc().value(), uop->get_uopcode(), uop->get_txid());
            return false;
        }
    }

    auto latency = this->latency(uop);
    auto ready_cycle = now + latency;
    bool ok = uop_pipeline.count(ready_cycle) < max_writebacks;
    if (!ok) {
        DBGXU("     xu not ready  {:x} {:s} {}", uop->pc().value(), uop->get_uopcode(), uop->get_txid());
    }
    return ok;
}

void XU::issue(Uop uop, unsigned latency_override)
{
    auto latency = latency_override ? latency_override : this->latency(uop);
    auto ready_cycle = get_cycle() + latency;
    uop_pipeline.emplace(ready_cycle, uop);

    issue_cycle = get_cycle();
    uop->issue_cycle = issue_cycle;

    // Keep track of when any multi-cycle resource consumed by this uop will become available.
    //
    // get_resource_count() will cap the number of resources needed for a uop at the maximum number
    // that exist, so that we don't end up with uops that can't ever issue.
    if (uop->get_resource_type() != archx::ResourceType::NONE) {
        int rtype = (int)uop->get_resource_type();
        for (unsigned n = 0; n < uop->get_resource_count(num_resources[rtype]); n++) {
            for (unsigned r = 0; r < num_resources[rtype]; r++) {
                if (get_cycle() >= resource_free_cycle[rtype][r]) {
                    resource_free_cycle[rtype][r] = get_cycle() + latency + uop->get_resource_rate();
                    break;
                }
            }
        }
    }

    // observability
    if (uop->is_branch()) {
        bool mispredict = uop->is_mispredicted_branch();
        auto instruction = uop->branch_instruction();

        auto branch_type = instruction->archx_instruction->branch_type();
        auto branch_type_event =
            mispredict ? event.branch_type_mispredict(branch_type) : event.branch_type(branch_type);

        event.uop_execute(uop,
                          event.branch(true),
                          event.mispredict(mispredict),
                          event.mispredict_target(instruction->is_mispredicted_target()),
                          event.mispredict_direction(instruction->is_mispredicted_direction()),
                          branch_type_event,
                          event.branch_pc(instruction->archx_instruction->program_counter().value()),
                          event.branch_target_pred(instruction->predicted_next_pc().value()),
                          event.branch_target_true(instruction->archx_instruction->next_program_counter().value()));
    } else {
        event.uop_execute(uop, event.latency(latency));
    }
} // issue()

std::vector<Uop> XU::execute()
{
    auto cycle = get_cycle();
    auto range = uop_pipeline.equal_range(cycle);
    std::vector<Uop> uops;

    for (auto i = range.first; i != range.second; ++i) {
        auto uop = i->second;
        // A branch which is about to get load-canceled shouldn't signal a mispredict
        if (uop->is_branch() && uop->is_mispredicted_branch() && !uop->will_cancel()) {
            auto instruction = uop->branch_instruction();
            auto branch_type = instruction->archx_instruction->branch_type();
            switch (branch_type) {
            case archx::BranchType::UNCONDITIONAL_INDIRECT:
            case archx::BranchType::CALL_INDIRECT:
                DBGBRMP("xu mispredict indirect, allocid {}, pc {}, disasm {}",
                        uop->get_alloc_id(),
                        uop->pc(),
                        uop->a64_disasm());
                mispredict_indirect_port->put(uop);
                break;
            default:
                DBGBRMP("xu mispredict direct, allocid {}, pc {}, disasm {}",
                        uop->get_alloc_id(),
                        uop->pc(),
                        uop->a64_disasm());
                mispredict_port->put(uop);
            }
        }

        uops.push_back(uop);

        event.uop_execute_done(uop);
    }
    uop_pipeline.erase(cycle);
    return uops;
}

void XU::flush(const Flush& flush_info)
{
    std::erase_if(uop_pipeline, [&](const auto& e) { return e.second->is_younger(flush_info->alloc_id); });
}

void XU::dump(std::ostream& os) const
{
    os << name() << " ";
    for (const auto& it : uop_pipeline)
        fmt::print(os, "|{:<8}, {}", it.second->alloc_id, it.first);
    os << std::endl;
}

} // namespace ampere::core

/*
 * vim: sw=4:ts=4:et:si:cindent:cino=\:0,(0,g0,t0:
 */
