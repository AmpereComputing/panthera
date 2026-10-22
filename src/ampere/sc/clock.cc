//
// Copyright (c) 2018-2019 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//

#include <functional>
#include <memory>

#include "ampere/sc/clock.h"
#include "ampere/sc/module.h"
#include "ampere/sc/report.h"

#if defined(AMPERE_TRAP_ON_CYCLE)
#include <sys/types.h>
#include <signal.h>
#endif

namespace ampere {
namespace sc {

Clock::Clock(const sc_core::sc_module_name& name) : sc_core::sc_behavior(name) {}

Clock::Clock(const sc_core::sc_module_name& name, sc_core::sc_time period) : sc_core::sc_behavior(name), period(period)
{
    SC_METHOD(sc_tick);
    sensitive << tick_event;
    dont_initialize();
    tick_event.notify(period);
    SC_METHOD(sc_initialize);
}

void Clock::add_runnable(Runnable* r)
{
    if (r->is_channel())
        r->phase = UPDATE_PHASE;

    runnables.push(r);
}

void Clock::schedule(Runnable* r, CycleDelta cycles_from_now)
{
    AMPERE_ASSERT(cycles_from_now > 0 || r->phase > current_phase, "cannot schedule in the past");

    if (cycles_from_now == 0)
        (*this_cycle_runnables)[r->phase].push_back(r);
    else if (cycles_from_now == 1)
        (*next_cycle_runnables)[r->phase].push_back(r);
    else
        schedule_queue.emplace(r, current_cycle + cycles_from_now);
}

void Clock::tick()
{
    // Run everything scheduled for this cycle, one phase at a time
    for (auto& phase_list : *this_cycle_runnables) {
        while (!phase_list.empty()) {
            auto runnable = phase_list.front();
            phase_list.pop_front();
            run_runnable(runnable);
        }
    }

    // If things were scheduled in the future and are now about to happen, move them into the lists
    // of runnables for the next cycle.
    while (!schedule_queue.empty()) {
        if (schedule_queue.begin()->cycle == current_cycle + 1) {
            ScheduleEntry entry = *schedule_queue.begin();
            schedule_queue.erase(schedule_queue.begin());
            (*next_cycle_runnables)[entry.runnable->phase].push_back(entry.runnable);
        } else {
            break;
        }
    }

    ++current_cycle;
    current_phase = 0;

    // Move the next cycle's work to this cycle
    std::swap(this_cycle_runnables, next_cycle_runnables);

#if defined(AMPERE_TRAP_ON_CYCLE) && defined(_POSIX_C_SOURCE)
    if (trigger_trap && current_cycle == trigger_cycle) {
        kill(getpid(), SIGTRAP);
    }
#endif
}

void Clock::initialize()
{
    assign_phases();

    // Now that we know how many phases there are, we can initialize the vectors of this/next-cycle lists
    for (int i = 0; i <= max_assigned_phase + 1; i++) {
        (*this_cycle_runnables).emplace_back();
        (*next_cycle_runnables).emplace_back();
    }

    schedule_runnables();

    for (auto& phase_list : *this_cycle_runnables)
        for (auto& runnable : phase_list)
            runnable->init();

    initialized = true;
}

void Clock::sc_tick()
{
    tick();
    tick_event.notify(period);
}

void Clock::sc_initialize()
{
    initialize();
}

void Clock::run_runnable(Runnable* runnable)
{
    if (runnable->ran_in_cycle == current_cycle)
        return;

    bool run_next_cycle = runnable->run();

    runnable->ran_in_cycle = current_cycle;
    current_phase = runnable->phase;

    if (run_next_cycle || always)
        schedule(runnable, 1);
}

std::unique_ptr<Clock> Clock::root_clock = nullptr;

Clock* Clock::get_root()
{
    if (!root_clock) {
        root_clock =
            std::make_unique<Clock>(sc_core::sc_module_name("root_clock"), sc_core::sc_time(1, sc_core::SC_NS));
    }

    return root_clock.get();
}

void Clock::set_root(std::unique_ptr<Clock> clock)
{
    root_clock = std::move(clock);
}

void Clock::add_zdelay_connection(Runnable* from, Runnable* to)
{
    auto node_to = zdelay_nodes.emplace(to, to).first;
    auto node_from = zdelay_nodes.emplace(from, from).first;
    AMPERE_ASSERT(node_to->second.runnable, "target zdelay node must not be null");
    AMPERE_ASSERT(node_from->second.runnable, "source zdelay node must not be null");

    node_to->second.dependencies.push_back(&node_from->second);
}

void Clock::assign_phases()
{
    for (auto& it : zdelay_nodes) {
        ZDelayNode& node = it.second;
        assign_phase(&node);
    }

    zdelay_nodes.clear();
}

std::pair<int, Clock::ZDelayNode*> Clock::assign_phase(ZDelayNode* node)
{
    if (!node->visited) {
        if (node->visiting) {
            std::cerr << "Zero delay loop detected! Participating nodes -> their dependency:\n";
            return {-1, node};
        }

        node->visiting = true;

        for (auto& dep : node->dependencies) {
            auto dep_phase = assign_phase(dep);

            if (dep_phase.first == -1) {
                report_loop_node(node, dep);

                if (dep_phase.second != node)
                    return dep_phase;
                else
                    throw std::runtime_error{"zero delay loop; giving up"};
            }

            if (dep_phase.first >= node->runnable->phase) {
                node->runnable->phase = dep_phase.first + 1;
                if (node->runnable->phase > max_assigned_phase)
                    max_assigned_phase = node->runnable->phase;
            }
        }

        node->visiting = false;
        node->visited = true;
    }

    return {node->runnable->phase, nullptr};
}

void Clock::schedule_runnables()
{
    while (!runnables.empty()) {
        auto runnable = runnables.front();

        // Renumber the final phase to be right after the other phases so they are nicely packed
        if (runnable->phase == UPDATE_PHASE)
            runnable->phase = max_assigned_phase + 1;

        (*this_cycle_runnables)[runnable->phase].push_back(runnable);
        runnables.pop();
    }
}

void Clock::report_loop_node(ZDelayNode* node, ZDelayNode* dep)
{
    auto obj = dynamic_cast<sc_core::sc_object*>(node->runnable);
    if (obj)
        std::cerr << '\t' << obj->name();
    else
        std::cerr << "\t?????";

    auto dep_obj = dynamic_cast<sc_core::sc_object*>(dep->runnable);
    if (dep_obj)
        std::cerr << " -> " << dep_obj->name() << '\n';
    else
        std::cerr << " -> ?????\n";
}

} // namespace sc
} // namespace ampere

/*
 * vim: sw=4:ts=4:et:si:cindent:cino=\:0,(0,g0,t0:
 */
