//
// Copyright (c) 2021 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//
#include "ampere/mesh/retry.h"
#include <optional>

namespace ampere::mesh {

void Retry::define_credit_type(uint8_t credit_type, unsigned int capacity)
{
    if (pool.contains(credit_type))
        AMPERE_ERROR("redefinition of capacity for credit type {}", credit_type);

    pool[credit_type].capacity = capacity;
    pool[credit_type].free = capacity;
}

std::pair<bool, int> Retry::process_request(const MeshReq& req)
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

            int type = request_to_pcrdtype(req);

            if (type < 0)
                return {true, type};

            auto& entry = pool.at(type);

            if (entry.free > entry.parked) {
                --entry.free;
                ev_alloc(req, evd_pool_type(type));
                return {true, type};
            } else {
                if (!is_prefetch) {
                    ++entry.reserved;
                    entry.requesters.push({req->SrcID, req->TxnID});
                }
                return {false, type};
            }
        } else {
            auto& entry = pool.at(req->PCrdType);
            AMPERE_ASSERT(entry.issued,
                          "got request with AllowRetry==false but there are no credits issued, txid={}",
                          req->txid().value);
            --entry.issued;
            return {true, req->PCrdType};
        }
    }
}

std::optional<Retry::Requester> Retry::dealloc(uint8_t credit_type)
{
    auto& entry = pool.at(credit_type);
    if ((entry.free >= entry.parked) && entry.reserved) {
        --entry.reserved;
        ++entry.issued;

        AMPERE_ASSERT(!entry.requesters.empty(), "reserved credits with no requesters");
        auto requester = entry.requesters.front();
        requester.type = credit_type;
        entry.requesters.pop();
        return requester;
    } else {
        AMPERE_ASSERT(pool.at(credit_type).free < pool.at(credit_type).capacity,
                      "{} Attempt to free more credits than capacity {}",
                      this->name(),
                      pool.at(credit_type).capacity);
        ++pool.at(credit_type).free;
        ev_dealloc(evd_pool_type(credit_type));
        return {};
    }
}

void Retry::park(uint8_t credit_type, unsigned int parked)
{
    auto& entry = pool.at(credit_type);

    if (parked < entry.capacity) {
        entry.parked = parked;
    } else {
        entry.parked = entry.capacity - 1;
    }
}

void Retry::dump(std::ostream& os) const
{
    os << "= " << this->name() << " ======================================================" << std::endl;
    os << "- Retry Entries -------------------------------------------" << std::endl;
    os << "| Entry | Capacity | Free | Issued | Reserved |" << std::endl;
    for (auto& it : pool) {
        auto key = it.first;
        auto entry = it.second;
        fmt::print("| {:5} | {:8} | {:4} | {:6} | {:8} |\n",
                   key,
                   entry.capacity - entry.parked,
                   entry.free,
                   entry.issued,
                   entry.reserved);
    }
}

void RetryRequester::push_retry(uint8_t credit_type, NodeId tgt_id, uint16_t txn_id)
{
    if (credit_type >= retries.size())
        retries.resize(credit_type + 1);

    retries[credit_type][tgt_id].push(txn_id);
}

uint16_t RetryRequester::pop_retry(uint8_t credit_type, NodeId tgt_id)
{
    AMPERE_ASSERT(retries.size() > credit_type && !retries[credit_type][tgt_id].empty(),
                  "no retries for credit type {}",
                  credit_type);
    auto txn_id = retries[credit_type][tgt_id].front();
    retries[credit_type][tgt_id].pop();
    return txn_id;
}

} // namespace ampere::mesh
