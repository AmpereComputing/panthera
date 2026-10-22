//
// Copyright (c) 2021Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//
#pragma once

#include <map>
#include <queue>

#include "ampere/mesh/mesh_req.h"
#include "ampere/mesh/mesh_snp.h"
#include "ampere/sc/sc.h"
#include <optional>

namespace ampere::mesh {

using namespace sc;

enum : uint8_t {
    TRACKER_POOL_IRON,
    TRACKER_POOL_GOLD,
    TRACKER_POOL_PLATINUM,
    TRACKER_POOL_SFV,
    TRACKER_POOL_SNP
};

class PHARetry : public Module
{
public:
    explicit PHARetry(const sc_core::sc_module_name& name) : Module{name} {}

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

        size_t arbiter_index;
        std::vector<size_t> allocation;

        std::queue<Requester> requesters;
    };

    using WeightedRoundRobin = std::vector<size_t>;
    using PoolCreditArbiter = std::vector<WeightedRoundRobin>;

    Param<PoolCreditArbiter> credit_assignment_arbiter{
        "credit_assignment_arbiter",
        "Arrays of credit assignment round robin rules for ascending pool types",
        "[[2, 1, 2, 0, 1, 2], [2, 1, 2, 1, 2]]"_json};
    Param<bool> retries_consume_capacity{"retries_consume_capacity",
                                         "Do not allow higher QoS requests to exceed capacity by back-filling free "
                                         "pool slots when older retries complete via lower QoS pool entries",
                                         true};

    void define_credit_type(uint8_t credit_type, unsigned int capacity);
    std::pair<bool, int> process_request(const MeshReq& req, bool is_internal);
    std::pair<bool, int> process_snoop(const MeshSnp& snp);
    std::optional<Requester> dealloc(uint8_t completed_credit_type);
    unsigned int get_free();
    void dump(std::ostream&) const override;

    const PoolEntry& get_pool_entry(uint8_t credit_type) const { return pool.at(credit_type); }

protected:
    std::map<uint8_t, PoolEntry> pool;

    std::pair<bool, int> allocate_by_qos(int starting_type, bool reserve, NodeId src_id, uint16_t txn_id);
};

} // namespace ampere::mesh
