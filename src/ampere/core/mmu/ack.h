//
// Copyright (c) 2018-2019 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//

#pragma once

#include "ampere/sc/sc.h"
#include "ampere/sc/transaction.h"

namespace ampere::core {

using namespace ampere::sc;
using namespace ampere::archx;

class MMUAckObj;
using MMUAck = ampere::sc::transaction_ptr<MMUAckObj>;

class MMUAckObj : public ampere::sc::Transaction
{
public:
    MMUAckObj(uint64_t id, std::optional<uint64_t> otbid) : id(id), otb_id(otbid) {}

    uint64_t id;
    std::optional<uint64_t> otb_id; // OTB ID if allocated, empty otherwise
};

} // namespace ampere::core

