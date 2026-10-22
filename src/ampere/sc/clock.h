//
// Copyright (c) 2018-2019 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//

#ifndef PANTHERA_CLOCK_H
#define PANTHERA_CLOCK_H

#include <cstdint>
#include <map>
#include <queue>
#include <set>
#include <systemc>
#include <vector>
#include <list>
#include <memory>

#include <fmt/format.h>

#include "ampere/sc/cycle.h"
#include "ampere/sc/runnable.h"

#if !defined(NDEBUG) && !defined(AMPERE_TRAP_ON_CYCLE)
#define AMPERE_TRAP_ON_CYCLE
#endif

namespace ampere {
namespace sc {

class Clock : public sc_core::sc_behavior
{
public:
    Clock(const sc_core::sc_module_name& name, sc_core::sc_time period);
    virtual Cycle get_cycle() const { return current_cycle; }
    virtual void schedule(Runnable* r, CycleDelta cycles_from_now);
    virtual void add_runnable(Runnable* r);
    virtual void add_zdelay_connection(Runnable* from, Runnable* to);

    sc_core::sc_time get_period() const { return period; }

    static Clock* get_root();
    static void set_root(std::unique_ptr<Clock> clock);

#if defined(AMPERE_TRAP_ON_CYCLE)
    void trap_cycle(Cycle c)
    {
        trigger_trap = true;
        trigger_cycle = c;
    }
#endif
    void run_always(bool b) { always = b; }

    SC_HAS_PROCESS(Clock);

protected:
    explicit Clock(const sc_core::sc_module_name& name);
    void tick();
    void initialize();

private:
    Cycle current_cycle{0};
    sc_core::sc_time period;
    sc_core::sc_event tick_event;
    int current_phase{0};
    bool initialized{false};
    int max_assigned_phase{0};

    struct ScheduleEntry
    {
        Runnable* runnable;
        Cycle cycle;

        ScheduleEntry(Runnable* runnable, Cycle cycle) : runnable(runnable), cycle(cycle) {}
    };

    struct CompareScheduleEntries
    {
        bool operator()(const ScheduleEntry& a, const ScheduleEntry& b) const
        {
            if (a.cycle == b.cycle) {
                if (a.runnable == b.runnable)
                    return false;
                else if (a.runnable->phase == b.runnable->phase)
                    return a.runnable < b.runnable; // arbitrary tie breaker
                else
                    return a.runnable->phase < b.runnable->phase;
            } else {
                return a.cycle < b.cycle;
            }
        }
    };

    using ScheduleList = std::list<Runnable*>;
    using ScheduleVec = std::vector<ScheduleList>;

    std::set<ScheduleEntry, CompareScheduleEntries> schedule_queue;
    ScheduleVec vec0, vec1;
    ScheduleVec* this_cycle_runnables{&vec0};
    ScheduleVec* next_cycle_runnables{&vec1};

    void sc_tick();
    void sc_initialize();
    void run_runnable(Runnable* runnable);

    bool always{false};
#if defined(AMPERE_TRAP_ON_CYCLE)
    bool trigger_trap{false};
    Cycle trigger_cycle{0};
#endif

    static std::unique_ptr<Clock> root_clock;

    struct ZDelayNode
    {
        Runnable* runnable;
        std::vector<ZDelayNode*> dependencies;
        bool visiting{false};
        bool visited{false};

        ZDelayNode(Runnable* r) : runnable{r} {}
    };

    std::queue<Runnable*> runnables;
    std::map<Runnable*, ZDelayNode> zdelay_nodes;
    void assign_phases();
    std::pair<int, ZDelayNode*> assign_phase(ZDelayNode* node);
    void schedule_runnables();
    void report_loop_node(ZDelayNode* node, ZDelayNode* dep);
};

} // namespace sc
} // namespace ampere

#endif // PANTHERA_CLOCK_H

/*
 * vim: sw=4:ts=4:et:si:cindent:cino=\:0,(0,g0,t0:
 */
