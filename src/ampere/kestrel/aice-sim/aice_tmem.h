//
// Copyright (c) 2018-2025 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//
#pragma once
#include <iostream>
#include <memory>
#include <bitset>
#include <vector>
#include <deque>
#include <algorithm>
#include <map>
#include <unordered_map>
#include <random>
#include <cmath>

#include "ampere/event_stream/event_stream_proto.h"
#include "ampere/sc/connect.h"
#include "ampere/sc/credit.h"
#include "ampere/sc/event.h"
#include "ampere/sc/module.h"
#include "ampere/sc/transaction.h"

#include "ampere/kestrel/aice-sim/aice_common.h"
#include "ampere/kestrel/aice-sim/aice_mem_req.h"

using namespace ampere;

namespace ampere::kestrel::aice {

class Memory : public sc::Module
{
public:
    sc::Param<unsigned> tmem_latency{"tmem_latency", "TMEM memory latency (cycles)", 9};
    sc::Param<unsigned> tmem_store_resp_latency{
        "tmem_store_resp_latency", "TMEM memory store response latency (cycles)", 6};
    sc::Param<unsigned> random_latency_mean{"random_latency_mean", "Mean random latency (cycles), 0=disable random", 0};
    sc::Param<unsigned> random_latency_stddev{"random_latency_stddev", "Stddev random latency (cycles)", 10};
    sc::Param<unsigned> random_latency_seed{"random_latency_seed", "Seed for random latency", 1};

    explicit Memory(const sc_core::sc_module_name& name) : sc::Module(name) { setup_random_response_model(); }

    sc::In<mem_req_t> req_port{"request"};
    sc::Out<mem_resp_t> resp_port{"response"};

    bool run() override
    {
        DBG("Memory run");
        const sc::Cycle now = get_cycle();
        while (req_port->can_take()) {
            mem_req_t req = req_port->take();
            DBG("Memory taking request: id: {}", req->id);
            switch (req->access_type) {
            case access_type_t::LOAD: {
                sc::Cycle resp_time = now + compute_response_latency();
                // avoid load response collisions; can only do one per cycle
                while (mem_pipeline.find(resp_time) != mem_pipeline.end()) {
                    ++resp_time;
                }
                mem_pipeline.emplace(resp_time, req);
                // mem_load_req_event(req->fsm->inst, evd_mem_id(req->id), evd_mem_addr(req->addr),
                // evd_inum(req->fsm->inst->inum), evd_fetch_disasm(req->fsm->inst->disasm()));
                mem_load_req_event(req->fsm->inst, evd_mem_id(req->id), evd_mem_addr(req->addr));
                break;
            }
            case access_type_t::STORE: {
                sc::Cycle resp_time = now + tmem_store_resp_latency;
                mem_pipeline.emplace(resp_time, req);
                // mem_store_req_event(req->fsm-inst, evd_mem_id(req->id), evd_mem_addr(req->addr),
                // evd_inum(req->fsm->inst->inum), evd_fetch_disasm(req->fsm->inst->disasm()));
                mem_store_req_event(req->fsm->inst, evd_mem_id(req->id), evd_mem_addr(req->addr));
                break;
            }
            }
        }
        const auto range = mem_pipeline.equal_range(now);
        for (auto itr = range.first; itr != range.second; ++itr) {
            mem_req_t& req = itr->second;
            switch (req->access_type) {
            case access_type_t::LOAD: { // respond to loads
                DBG("Memory responding to request id: {}", req->id);
                auto resp = make_child<mem_resp_t>(req, req->id, req->addr, req->fsm, req->mr);
                // mem_load_resp_event(req->fsm->inst,  evd_mem_id(req->id), evd_mem_addr(req->addr),
                // evd_inum(req->fsm->inst->inum), evd_fetch_disasm(req->fsm->inst->disasm()));
                mem_load_resp_event(req->fsm->inst, evd_mem_id(req->id), evd_mem_addr(req->addr));
                resp_port->put(resp);
                break;
            }
            case access_type_t::STORE: {
                // mem_store_done_event(req->fsm->inst, evd_mem_id(req->id), evd_mem_addr(req->addr),
                // evd_inum(req->fsm->inst->inum), evd_fetch_disasm(req->fsm->inst->disasm()));
                mem_store_done_event(req->fsm->inst, evd_mem_id(req->id), evd_mem_addr(req->addr));
                auto resp = make_child<mem_resp_t>(req, req->id, req->addr, req->fsm, req->mr);
                DBG("Memory responding to store request id: {}, addr: {:x}", req->id, req->addr);
                resp_port->put(resp);
                break;
            }
            } // switch
        }
        mem_pipeline.erase(now);
        return !mem_pipeline.empty();
    }

private:
    std::multimap<sc::Cycle, mem_req_t> mem_pipeline;
    sc::Event mem_load_req_event{"mem_load_req", "Memory load request received"};
    sc::Event mem_load_resp_event{"mem_load_resp", "Memory load response sent"};
    sc::Event mem_store_req_event{"mem_store_req", "Memory store request received"};
    sc::Event mem_store_done_event{"mem_store_done", "Memory store done"};
    sc::EventData<std::string> evd_fetch_disasm{"disasm", "Disassembly"};
    sc::EventData<unsigned> evd_inum{"inum", "Instruction number"};
    sc::EventData<std::uint64_t> evd_mem_id{"mem_id", "Memory request id"};
    sc::EventData<addr_t> evd_mem_addr{"mem_addr", "Memory address"};

    std::mt19937 rand_gen;
    std::unique_ptr<std::normal_distribution<>> dist;

    void setup_random_response_model()
    {
        dist = std::make_unique<std::normal_distribution<>>(random_latency_mean, random_latency_stddev);
        rand_gen.seed(random_latency_seed);
    }

    unsigned get_random_response_latency() { return std::round((*dist)(rand_gen)); }

    unsigned compute_response_latency()
    {
        if (random_latency_mean) {
            return get_random_response_latency();
        }
        return tmem_latency;
    }
};

} // namespace ampere::kestrel::aice

