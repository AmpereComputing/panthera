//
// Copyright (c) 2018-2021 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//
#include <boost/make_unique.hpp>
#include <iostream>
#include <memory>

#include "ampere/event_stream/event_stream_proto.h"
#include "ampere/sc/connect.h"
#include "ampere/sc/event.h"
#include "ampere/sc/module.h"
#include "ampere/sc/transaction.h"
#include "ampere/sc/credit.h"

using namespace ampere;
using namespace ampere::sc;

/*****************************************************************************
 * Instruction type
 *****************************************************************************/
class InstructionObj : public ampere::sc::Transaction
{
public:
    Credit feqid, robid, schid;
    unsigned latency{0};
    bool executing{false}, complete{false};
};
using Instruction = ampere::sc::transaction_ptr<InstructionObj>;

/*****************************************************************************
 * Instruction Cache/Fetch unit: spawns transactions
 *****************************************************************************/
class ICF : public Module
{
public:
    Param<unsigned> width{"width", "how many instructions wide", 4};

    explicit ICF(const sc_core::sc_module_name& name) : Module(name) {}

    Out<Instruction> out{"out"};
    In<Instruction> idr_return{"idr_return"};

    bool run() override
    {
        // If there is bandwidth available on the port, put the next instr
        while (out->can_put() && instruction_fifo.size() > 0) {
            Instruction inst = instruction_fifo.front();
            out->put(inst);
            ev_dispatch(inst);
            instruction_fifo.pop_front();
        }

        // Reserve some credits for the buffer in IDR, to make sure we never
        // overflow downstream. Only fetch if we have 'width' credits.
        if (credit_feq.get_count() >= width) {
            for (unsigned w = 0; w < width; w++) {
                // Make some new instructions
                Instruction inst = make<Instruction>();
                inst->feqid = credit_feq.acquire();
                instruction_fifo.push_back(inst);
                // std::cout << get_cycle() << ": ICF Fetch " << inst << std::endl;
                ev_fetch(inst);

                // assign a random latency for execution
                inst->latency = 1 + (rand() % 6);
            }
        }

        // Receive the credits back from IDR and release
        while (idr_return->can_take()) {
            Instruction inst = idr_return->take();
            release_credit(inst->feqid);
            ev_return(inst);
        }

        // should we run again next cycle?
        return true;
    }

private:
    CreditPool credit_feq{"feq"};
    std::deque<Instruction> instruction_fifo;
    Event ev_fetch{"fetch", "Fetch instruction"};
    Event ev_dispatch{"dispatch", "dispatch instruction"};
    Event ev_return{"return", "credit return"};
};

/*****************************************************************************
 * Instruction Decode/Rename unit: decode and allocate
 *
 * Has a buffer holding instructions coming from ICF. Once it is done with
 * these instructions, it can release those credits to inform ICF that it can
 * consume more.
 *
 * IDR checks if there is room in the scheduler and in the re-order buffer.
 * If so, then it can send the instruction to each of these units.
 *****************************************************************************/
class IDR : public Module
{
public:
    Param<unsigned> width{"width", "how many instructions wide", 4};

    explicit IDR(const sc_core::sc_module_name& name) : Module(name) {}
    unsigned sch_size() { return credit_sch.get_size(); }

    In<Instruction> in{"in"};
    Out<Instruction> out_ixu{"out_ixu"};
    Out<Instruction> out_gpc{"out_gpc"};
    Out<Instruction> idr_return{"idr_return"};
    In<Instruction> ixu_return{"ixu_return"};
    In<Instruction> gpc_return{"gpc_return"};

    bool run() override
    {
        // check if we can allocate into both the scheduler and the rob
        for (unsigned w = 0; w < width; w++) {
            // if we have enough credits downstream
            if (credit_sch.get_count() && credit_rob.get_count()) {
                // and we can put downstream and return credits
                if (out_ixu->can_put() && out_gpc->can_put() && idr_return->can_put() && decode_fifo.size()) {
                    Instruction inst = decode_fifo.front();
                    inst->robid = credit_rob.acquire();
                    inst->schid = credit_sch.acquire();
                    out_gpc->put(inst);
                    out_ixu->put(inst);
                    // std::cout << get_cycle() << ": IDR Decode " << inst << std::endl;
                    ev_decode(inst);
                    decode_fifo.pop_front();
                    idr_return->put(inst);
                }
            } else if (w == 0) {
                // resource stall, post event on instruction at head of line
                if (decode_fifo.size())
                    ev_stall(decode_fifo.front());

                // no need to check any more in this loop
                break;
            }
        }

        // make sure to take all the instructions coming from the ICF and
        // place them in our own private buffer
        while (in->can_take()) {
            Instruction inst = in->take();
            decode_fifo.push_back(inst);
            // std::cout << get_cycle() << ": IDR Enqueue " << inst << std::endl;
            ev_enqueue(inst);
        }

        // Receive the credits back from IXU and release
        while (ixu_return->can_take()) {
            Instruction inst = ixu_return->take();
            ev_ixu_return(inst);
            release_credit(inst->schid);
        }

        // Receive the credits back from GPC and release
        while (gpc_return->can_take()) {
            Instruction inst = gpc_return->take();
            ev_gpc_return(inst);
            release_credit(inst->robid);
        }

        // should we run again next cycle? only if there is something to do
        return decode_fifo.size();
    }

private:
    std::deque<Instruction> decode_fifo;

    CreditPool credit_sch{"sch"};
    CreditPool credit_rob{"rob"};

    Event ev_decode{"decode", "Decode instruction"};
    Event ev_enqueue{"enqueue", "Enqueue into IDR"};
    Event ev_stall{"stall", "Resource stall"};
    Event ev_ixu_return{"return_sch", "credit return"};
    Event ev_gpc_return{"return_rob", "credit return"};
};

/*****************************************************************************
 * Scheduler Entry, for IXU array
 *****************************************************************************/
struct SchedEntry
{
    SchedEntry() { reset(); }

    bool in_use{false};   // valid bit
    bool complete{false}; // complete bit
    Cycle alloc_time{0};  // timestamp
    Cycle exec_start{0};  // timestamp
    Instruction inst;     // current inst

    void reset()
    {
        in_use = complete = false;
        alloc_time = 0;
        exec_start = 0;
        inst = nullptr;
    }
};

/*****************************************************************************
 * Integer eXecution Unit: schedule and execute
 *****************************************************************************/
class IXU : public Module
{
    using Module::init;

public:
    Param<unsigned> width{"width", "how many instructions wide", 4};

    explicit IXU(const sc_core::sc_module_name& name) : Module(name) {}

    In<Instruction> in{"in"};
    Out<Instruction> ixu_return{"ixu_return"};

    bool run() override
    {
        Cycle now = get_cycle();

        // return completed instructions to IDR
        unsigned return_count(0);
        for (unsigned i = 0; i < sch_size; i++) {
            SchedEntry* e = table[i];
            if (e->in_use && e->complete && ixu_return->can_put()) {
                ixu_return->put(e->inst);
                e->reset();
                return_count++;
            }
            // limit number of returns per cycle
            if (return_count == width)
                break;
        }

        // execution model, check which instrs have completed for writeback
        for (unsigned i = 0; i < sch_size; i++) {
            SchedEntry* e = table[i];
            if (e->in_use && e->inst->executing && !e->complete && now - e->exec_start > e->inst->latency) {
                e->complete = true;
                e->inst->complete = true;
                ev_writeback(e->inst);
            }
        }

        // scheduler algorithm to figure out which instruction can execute
        for (unsigned w = 0; w < width; w++) {
            // loop through the table to find instructions that are available
            // to execute and begin their execution
            for (unsigned i = 0; i < sch_size; i++) {
                SchedEntry* e = table[i];
                if (!e->in_use || e->complete)
                    continue;
                if (e->inst->executing)
                    continue;

                e->exec_start = now;
                e->inst->executing = true;
                ev_issue(e->inst, ev_lat(e->inst->latency));
            }
        }

        // allocate new entries into scheduler table
        while (in->can_take()) {
            Instruction inst = in->take();

            // search for a free slot to put it
            for (unsigned i = 0; i < sch_size; i++) {
                SchedEntry* e = table[i];
                if (!e->in_use) {
                    e->inst = inst;
                    e->in_use = true;
                    e->alloc_time = now;
                    break;
                }
            }

            ev_alloc(inst);
            // std::cout << get_cycle() << ": IXU Alloc " << inst << std::endl;
        }

        // should we run again next cycle? yes, always.
        return true;
    }

    void init(unsigned size)
    {
        sch_size = size;
        table = new SchedEntry*[sch_size];
        for (unsigned i = 0; i < sch_size; i++)
            table[i] = new SchedEntry;
    }

private:
    unsigned sch_size{0};
    SchedEntry** table;
    Event ev_alloc{"alloc", "Allocate into scheduler"};
    Event ev_issue{"issue", "Execute instruction"};
    Event ev_writeback{"writeback", "Execution complete"};
    EventData<unsigned> ev_lat{"latency", "latency"};
};

/*****************************************************************************
 * Global Pipeline Control unit: commit pipestage
 *****************************************************************************/
class GPC : public Module
{
public:
    Param<unsigned> width{"width", "how many instructions wide", 4};

    explicit GPC(const sc_core::sc_module_name& name) : Module(name) {}

    In<Instruction> in{"in"};
    Out<Instruction> gpc_return{"gpc_return"};

    bool run() override
    {
        // consume all instructions allocated by the IDR
        while (in->can_take()) {
            Instruction inst = in->take();
            rob.push_back(inst);
            // std::cout << get_cycle() << ": GPC ROB Write " << inst << std::endl;
            ev_alloc(inst);
        }

        // commit instructions which are complete and return credits
        for (unsigned w = 0; w < width && rob.size(); w++) {
            Instruction inst = rob.front();
            if (inst->complete && gpc_return->can_put()) {
                gpc_return->put(inst);
                // std::cout << get_cycle() << ": GPC Commit " << inst << std::endl;
                ev_commit(inst);
                rob.pop_front();
                if (!instrs_committed)
                    first_commit_time = get_cycle();
                instrs_committed++;
            }
        }

        // should we run again next cycle? only if there is something to do
        return rob.size();
    }

    // callback at end of sim to dump out perf
    void end_of_simulation() override
    {
        Module::end_of_simulation();
        Cycle cycles = get_cycle() - first_commit_time;
        fmt::print(std::cout,
                   "\nCompleted: {} cycles, {} instructions, IPC = {:.3f}\n\n",
                   get_cycle(),
                   instrs_committed,
                   instrs_committed / (float)cycles);
    }

private:
    std::deque<Instruction> rob;
    Event ev_commit{"commit", "Commit instruction"};
    Event ev_alloc{"alloc", "Allocate into ROB"};

    unsigned instrs_committed{0};
    Cycle first_commit_time{0};
};

/*****************************************************************************
 * Top module: glues everything together
 *****************************************************************************/
class Top : public Module
{
public:
    Top(const sc_core::sc_module_name& name, Clock* clock, Environment* env) : Module(name, clock, env)
    {
        connect(icf.out, idr.in).name("icf_to_idr");
        connect(idr.out_ixu, ixu.in).name("idr_to_ixu");
        connect(idr.out_gpc, gpc.in).name("idr_to_gpc");
        connect(idr.idr_return, icf.idr_return).name("idr_return");
        connect(ixu.ixu_return, idr.ixu_return).name("ixu_return");
        connect(gpc.gpc_return, idr.gpc_return).name("gpc_return");
        ixu.init(idr.sch_size());
    }

private:
    GPC gpc{"gpc"};
    ICF icf{"icf"};
    IDR idr{"idr"};
    IXU ixu{"ixu"};
};

/*****************************************************************************
 * SystemC mainline, kicks off the simulation
 *****************************************************************************/
int sc_main(int argc, char** argv)
{
    Clock clock("main", {1, sc_core::SC_NS});

    Environment env{"system"};
    env.parameters.load_from_args(argc, argv);
    env.event_stream = boost::make_unique<event_stream::EventStreamProto>("events.es");

    Top top("top", &clock, &env);

    // dump knobs with -help
    if (env.parameters.help_requested()) {
        env.parameters.dump_parameters_verbosely(std::cout);
        exit(1);
    }

    // run for while and then quit
    sc_core::sc_start({1000, sc_core::SC_NS});

    if (sc_core::sc_get_status() != sc_core::SC_STOPPED)
        sc_core::sc_stop();

    return 0;
}

/*
 * vim: sw=4:ts=4:et:si:cindent:cino=\:0,(0,g0,t0:
 */
