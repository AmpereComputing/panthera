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

using namespace ampere;
using namespace ampere::sc;

/*********************************************************
 * Fetch pipestage (first stage), spawns transactions
 *********************************************************/
class Fetch : public Module
{
public:
    explicit Fetch(const sc_core::sc_module_name& name) : Module(name) {}

    Out<Txn> out{"out"};

    bool run() override
    {
        while (out->can_put()) {
            Txn txn = make<Txn>();
            fetch_event(txn);
            std::cout << get_cycle() << ": Fetch " << txn << std::endl;
            out->put(txn);
        }

        return true;
    }

private:
    Event fetch_event{"fetch", "Fetch instruction"};
};

/*********************************************************
 * Decode pipestage (second stage)
 *********************************************************/
class Decode : public Module
{
public:
    explicit Decode(const sc_core::sc_module_name& name) : Module(name) {}

    In<Txn> in{"in"};
    Out<Txn> out{"out"};

    bool run() override
    {
        while (in->can_take()) {
            Txn txn = in->take();
            decode_event(txn);
            std::cout << get_cycle() << ": Decode " << txn << std::endl;
            out->put(txn);
        }

        return false;
    }

private:
    Event decode_event{"decode", "Decode instruction"};
};

/*********************************************************
 * Execute pipestage (third stage)
 *********************************************************/
class Execute : public Module
{
public:
    explicit Execute(const sc_core::sc_module_name& name) : Module(name) {}

    In<Txn> in{"in"};
    Out<Txn> out{"out"};

    bool run() override
    {
        while (in->can_take()) {
            Txn txn = in->take();
            execute_event(txn);
            std::cout << get_cycle() << ": Execute " << txn << std::endl;
            out->put(txn);
        }

        return false;
    }

private:
    Event execute_event{"execute", "Execute instruction"};
};

/*********************************************************
 * Writeback pipestage (fourth stage)
 *********************************************************/
class Writeback : public Module
{
public:
    explicit Writeback(const sc_core::sc_module_name& name) : Module(name) {}

    In<Txn> in{"in"};
    Out<Txn> out{"out"};

    bool run() override
    {
        while (in->can_take()) {
            Txn txn = in->take();
            writeback_event(txn);
            std::cout << get_cycle() << ": Writeback " << txn << std::endl;
            out->put(txn);
        }

        return false;
    }

private:
    Event writeback_event{"writeback", "Writeback instruction data"};
};

/*********************************************************
 * Commit pipestage (fifth stage)
 *********************************************************/
class Commit : public Module
{
public:
    explicit Commit(const sc_core::sc_module_name& name) : Module(name) {}

    In<Txn> in{"in"};

    bool run() override
    {
        while (in->can_take()) {
            Txn txn = in->take();
            commit_event(txn);
            std::cout << get_cycle() << ": Commit " << txn << std::endl;
        }

        return false;
    }

private:
    Event commit_event{"commit", "Commit instruction"};
};

/*********************************************************
 * Top module, glues everything together
 *********************************************************/
class Top : public Module
{
public:
    Top(const sc_core::sc_module_name& name, Clock* clock, Environment* env) : Module(name, clock, env)
    {
        connect(fetch.out, decode.in).name("fetch_to_decode");
        connect(decode.out, execute.in).name("decode_to_execute");
        connect(execute.out, writeback.in).name("execute_to_writeback");
        connect(writeback.out, commit.in).name("writeback_to_commit");
    }

private:
    Fetch fetch{"fetch"};
    Decode decode{"decode"};
    Execute execute{"execute"};
    Writeback writeback{"writeback"};
    Commit commit{"commit"};
};

/*********************************************************
 * SystemC mainline, kicks off the simulation
 *********************************************************/
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

    sc_core::sc_start({100, sc_core::SC_NS});

    return 0;
}

/*
 * vim: sw=4:ts=4:et:si:cindent:cino=\:0,(0,g0,t0:
 */
