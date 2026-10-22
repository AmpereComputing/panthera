//
// Copyright (c) 2021Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//
#pragma once

#include <map>
#include <queue>

#include "ampere/mesh/mesh_req.h"
#include "ampere/sc/sc.h"
#include <optional>

namespace ampere::mesh {

using namespace sc;

class Retry : public Module
{
public:
    explicit Retry(const sc_core::sc_module_name& name) : Module{name} {}

    struct Requester
    {
        NodeId src_id;
        uint16_t txn_id;
        uint8_t type;
    };

    struct PoolEntry
    {
        unsigned int capacity;
        unsigned int free;
        unsigned int reserved;
        unsigned int issued;
        unsigned int parked;

        std::queue<Requester> requesters;
    };

    void define_credit_type(uint8_t credit_type, unsigned int capacity);
    std::pair<bool, int> process_request(const MeshReq& req);
    std::optional<Requester> dealloc(uint8_t credit_type);
    void park(uint8_t credit_type, unsigned int parked);
    void dump(std::ostream&) const override;

    const PoolEntry& get_pool_entry(uint8_t credit_type) const { return pool.at(credit_type); }

protected:
    virtual int request_to_pcrdtype(const MeshReq& req) { return 0; }
    std::map<uint8_t, PoolEntry> pool;

    Event ev_alloc{"alloc", "Allocate a an entry in retry"};
    Event ev_dealloc{"dealloc", "Deallocate an entry in the retry"};
    EventData<unsigned> evd_pool_type{"pool_type", "The pool type of the allocation/deallocation"};
};

class RetryRequester : public Module
{
public:
    explicit RetryRequester(const sc_core::sc_module_name& name) : Module{name} {}
    void push_retry(uint8_t credit_type, NodeId tgt_id, uint16_t txn_id);
    uint16_t pop_retry(uint8_t credit_type, NodeId tgt_id);

protected:
    std::vector<std::map<NodeId, std::queue<uint16_t>>> retries;
};

} // namespace ampere::mesh
