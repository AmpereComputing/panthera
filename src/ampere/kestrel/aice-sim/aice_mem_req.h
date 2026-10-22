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
#include "ampere/util/enum.h"

#include "ampere/kestrel/aice-sim/aice_common.h"
#include "ampere/kestrel/aice-sim/aice_instruction.h"
#include "ampere/kestrel/aice-sim/aice_mem_req.h"
#include "ampere/kestrel/aice-sim/aice_tmem_fsm.h"

namespace ampere::kestrel::aice {

class mem_req_obj_t : public ampere::sc::Transaction
{
public:
    std::uint64_t id;
    access_type_t access_type;
    addr_t addr{0};
    std::shared_ptr<tmem_fsm_t> fsm;
    regidx_t mr;
    mem_req_obj_t(std::uint64_t id, access_type_t acc_typ, addr_t addr, std::shared_ptr<tmem_fsm_t> fsm, regidx_t mr)
        : id(id), access_type(acc_typ), addr(addr), fsm(fsm), mr(mr)
    {
    }
};

class mem_resp_obj_t : public ampere::sc::Transaction
{
public:
    std::uint64_t req_id;
    addr_t addr{0};
    std::shared_ptr<tmem_fsm_t> fsm;
    regidx_t mr; // only required for responding to load requests
    mem_resp_obj_t(std::uint64_t req_id, addr_t addr, std::shared_ptr<tmem_fsm_t> fsm, regidx_t mr)
        : req_id(req_id), addr(addr), fsm(fsm), mr(mr)
    {
    }
};
using mem_req_t = ampere::sc::transaction_ptr<mem_req_obj_t>;
using const_mem_req_t = ampere::sc::const_ptr<mem_req_t>;
using mem_resp_t = ampere::sc::transaction_ptr<mem_resp_obj_t>;
using const_mem_resp_t = ampere::sc::const_ptr<mem_resp_t>;

} // namespace ampere::kestrel::aice
