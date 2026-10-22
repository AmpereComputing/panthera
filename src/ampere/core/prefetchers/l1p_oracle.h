
//
// Copyright (c) 2019-2021 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//

#pragma once

#include "ampere/archx/addr.h"
#include "ampere/core/backend_prefetcher.h"
#include "ampere/core/prefetchers/oracle.h"

namespace ampere::core {

template<typename T>
class OracleBackendPrefetcher : public OraclePrefetcher<T, BackendPrefetcher<T>>
{
    using Base = OraclePrefetcher<T, BackendPrefetcher<T>>;
    using prefetch_type_t = typename Prefetcher<T>::prefetch_type_t;

public:
    explicit OracleBackendPrefetcher(sc_core::sc_module_name name) : Base(name) {}

    void issue_impl(uint64_t id, T req) override { this->train_impl(req, true, false, true); }
    void execute_impl(
        uint64_t index, T req, bool hit, bool prefetch, bool demand, prefetch_type_t prefetch_type) override
    {
        auto addr = Base::addr_of(req).line_address();
        if (!hit)
            Base::in_flight.try_emplace(addr, this->get_cycle());
    }
};

template<>
BackendMemReq OraclePrefetcher<BackendMemReq, BackendPrefetcher<BackendMemReq>>::make_prefetch_request(
    BackendMemReq req, VirtAddr addr, bool prefetch_into_requester);

template<>
uint64_t OraclePrefetcher<BackendMemReq, BackendPrefetcher<BackendMemReq>>::request_access_hint(
    BackendMemReq req) const;

template<>
bool OraclePrefetcher<BackendMemReq, BackendPrefetcher<BackendMemReq>>::should_train(BackendMemReq req) const;

template<>
bool OraclePrefetcher<BackendMemReq, BackendPrefetcher<BackendMemReq>>::is_code_request(BackendMemReq req) const;

class L1P_Oracle : public OracleBackendPrefetcher<BackendMemReq>,
                   public implementation<L1P_Oracle, BackendPrefetcher<BackendMemReq>>
{
    Param<bool> use_instruction_id_as_hint{
        "use_instruction_id_as_hint", "Use instruction nyumber as hint for locating memory access", true};

public:
    explicit L1P_Oracle(sc_core::sc_module_name name);

    std::optional<int64_t> instruction_access_hint(const archx::Instruction &instruction) const override
    {
        if (use_instruction_id_as_hint)
            return instruction.inum().id();
        return {};
    }
};

} // namespace ampere::core

/*
 * vim: sw=4:ts=4:et:si:cindent:cino=\:0,(0,g0,t0:
 */
