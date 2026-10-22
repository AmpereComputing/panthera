//
// Copyright (c) 2021 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//
#include "ampere/mesh/mesh_req.h"
#include "ampere/mesh/mesh_snp.h"
#include "ampere/mesh/pha_retry.h"
#include "ampere/sc/counter.h"
#include <optional>

namespace ampere::mesh {

void PHARetry::define_credit_type(uint8_t credit_type, unsigned int capacity)
{
    if (pool.contains(credit_type))
        AMPERE_ERROR("redefinition of capacity for credit type {}", credit_type);

    pool[credit_type].capacity = capacity;
    pool[credit_type].free = capacity;
    pool[credit_type].arbiter_index = 0;

    // Credits in a given pool may be reallocated to other higher QoS pools,
    // so we must keep track of where credits are allocated
    for (auto& pool_ent : pool) {
        if (pool_ent.second.allocation.size() <= credit_type) {
            pool_ent.second.allocation.resize(credit_type + 1);
            pool_ent.second.allocation[credit_type] = 0;
        }
    }
}

unsigned int PHARetry::get_free()
{
    return pool[TRACKER_POOL_IRON].free + pool[TRACKER_POOL_GOLD].free + pool[TRACKER_POOL_PLATINUM].free;
}

static uint8_t qos_to_priority(Qos qos)
{
    switch (qos.to_level()) {
    case chi::Qos::Level::IRON:
        return TRACKER_POOL_IRON;
    case chi::Qos::Level::GOLD:
        return TRACKER_POOL_GOLD;
    case chi::Qos::Level::PLATINUM:
        return TRACKER_POOL_PLATINUM;
    }

    AMPERE_FATAL("Unknown Qos Priority Level");
    return TRACKER_POOL_IRON;
}

std::pair<bool, int> PHARetry::process_request(const MeshReq& req, bool is_internal)
{
    if (req->Opcode == chi::RequestOpcode::PCrdReturn) {
        auto& entry = pool.at(req->PCrdType);
        AMPERE_ASSERT(entry.issued, "got a PCrdReturn with no credits issued");
        --entry.issued;
        ++entry.free;
        return {true, req->PCrdType};
    } else {
        bool is_prefetch = req->Opcode == chi::RequestOpcode::PrefetchTgt;

        if (req->AllowRetry || is_prefetch) {
            AMPERE_ASSERT(req->PCrdType == 0, "PCrdType must be 0 on a request with AllowRetry");

            uint8_t type;

            if (req->Opcode == chi::RequestOpcode::CleanInvalid) {
                if (pool.at(TRACKER_POOL_SFV).free > 0)
                    type = TRACKER_POOL_SFV; // guaranteed to allocate below
                else
                    type = TRACKER_POOL_PLATINUM;
            } else {
                type = qos_to_priority(req->QoS);
            }

            return allocate_by_qos(type, !(is_prefetch || is_internal), req->SrcID, req->TxnID);
        } else {
            auto& entry = pool.at(req->PCrdType);
            AMPERE_ASSERT(entry.issued, "got request with AllowRetry==false but there are no credits issued");
            --entry.issued;
            return {true, req->PCrdType};
        }
    }
}

std::pair<bool, int> PHARetry::process_snoop(const MeshSnp& snp)
{
    uint8_t type;

    if (pool.at(TRACKER_POOL_SNP).free > 0)
        type = TRACKER_POOL_SNP; // guaranteed to allocate below
    else
        type = TRACKER_POOL_PLATINUM;

    return allocate_by_qos(type, false, snp->SrcID, snp->TxnID);
}

std::pair<bool, int> PHARetry::allocate_by_qos(int starting_type, bool reserve, NodeId src_id, uint16_t txn_id)
{
    int type = starting_type;

    SaturatingCounter uncompensated_starting_pool_retries(
        0, pool.at(starting_type).reserved, retries_consume_capacity ? pool.at(starting_type).reserved : 0);

    for (;;) {
        auto& entry = pool.at(type);

        if (entry.free > uncompensated_starting_pool_retries.get_value()) {
            --entry.free;
            entry.allocation[starting_type]++;
            return {true, starting_type};
        }

        uncompensated_starting_pool_retries -= entry.free;

        if (type == TRACKER_POOL_IRON)
            break;

        --type;
    }

    if (reserve) {
        auto& entry = pool.at(starting_type);
        ++entry.reserved;
        entry.requesters.push({src_id, txn_id});
    }

    return {false, starting_type};
}

std::optional<PHARetry::Requester> PHARetry::dealloc(uint8_t completed_credit_type)
{
    uint8_t returned_entry_type = 0;

    while ((returned_entry_type < completed_credit_type)
           && (pool.at(returned_entry_type).allocation.at(completed_credit_type) == 0))
    {
        returned_entry_type++;
    }

    auto& returned_entry = pool.at(returned_entry_type);
    returned_entry.allocation[completed_credit_type]--;

    auto granted_credit_type = returned_entry_type;

    if (returned_entry_type < credit_assignment_arbiter.val().size()) {
        auto arbiter = credit_assignment_arbiter.val().at(returned_entry_type);
        auto initial_index = returned_entry.arbiter_index;

        for (;;) {
            auto arbitrated_credit_type = arbiter.at(returned_entry.arbiter_index);

            if (pool.at(arbitrated_credit_type).reserved) {
                granted_credit_type = arbitrated_credit_type;

                break;
            }

            ++returned_entry.arbiter_index;
            if (returned_entry.arbiter_index >= arbiter.size())
                returned_entry.arbiter_index = 0;

            if (returned_entry.arbiter_index == initial_index)
                break;
        }
    }

    auto& granted_entry = pool.at(granted_credit_type);

    if (granted_entry.reserved) {
        --granted_entry.reserved;
        ++granted_entry.issued;

        AMPERE_ASSERT(!granted_entry.requesters.empty(), "reserved credits with no requesters");
        auto requester = granted_entry.requesters.front();
        requester.type = granted_credit_type;
        granted_entry.requesters.pop();
        returned_entry.allocation[granted_credit_type]++;
        return requester;
    } else {
        AMPERE_ASSERT(pool.at(returned_entry_type).free < pool.at(returned_entry_type).capacity,
                      "{} Attempt to free more credits than capacity {}",
                      this->name(),
                      pool.at(returned_entry_type).capacity);
        ++pool.at(returned_entry_type).free;
        return {};
    }
}

void PHARetry::dump(std::ostream& os) const
{
    os << "= " << this->name() << " ======================================================" << std::endl;
    os << "- PHARetry Entries -------------------------------------------" << std::endl;
    os << "| Entry | Capacity | Free | Issued | Reserved |" << std::endl;
    for (auto& it : pool) {
        auto key = it.first;
        auto entry = it.second;
        fmt::print(
            "| {:5} | {:8} | {:4} | {:6} | {:8} |\n", key, entry.capacity, entry.free, entry.issued, entry.reserved);
    }
}

} // namespace ampere::mesh
