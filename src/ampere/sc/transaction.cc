//
// Copyright (c) 2019 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//
#include "ampere/sc/transaction.h"
#include <cstdint>

namespace ampere::sc {

AMPERE_EVENT_DEFN(start_transaction, "start of transaction")
AMPERE_EVENT_DEFN(end_transaction, "end of transaction")

AMPERE_EVENT_DATA_DEFN(txid, "Transaction Identifier")
AMPERE_EVENT_DATA_DEFN(parent, "parent transaction id")

std::uint64_t Transaction::current_id = 0;

Transaction::Transaction() : _txid{++current_id} {}

Transaction::~Transaction()
{
    if (event_stream_) {
        auto eh = event_stream_->open_event(AMPERE_EVENT_DEFN_NAME(end_transaction), sc_core::sc_time_stamp().value());
        auto txdata = to_event_arg(txid());
        event_stream_->add_uint_data(eh, txdata.data_type, txdata.data_value);
        // TODO This is not creating an event and therefore will bypass the eventprocessors response to enable this
        event_stream_->close_event(eh);
    }
}

// TODO This is not creating an event and therefore will bypass the eventprocessors response to enable this
void post_start_transaction(event_stream::EventStream* stream, Transaction& t)
{
    t.event_stream_ = stream;
    auto eh = stream->open_event(AMPERE_EVENT_DEFN_NAME(start_transaction), sc_core::sc_time_stamp().value());
    stream->add_uint_data(eh, AMPERE_EVENT_DATA_DEFN_NAME(txid), t.txid().value);
    stream->close_event(eh);
}

void post_start_transaction_with_parent(event_stream::EventStream* stream, Transaction& t, Transaction& p)
{
    t.event_stream_ = stream;
    auto eh = stream->open_event(AMPERE_EVENT_DEFN_NAME(start_transaction), sc_core::sc_time_stamp().value());
    stream->add_uint_data(eh, AMPERE_EVENT_DATA_DEFN_NAME(txid), t.txid().value);
    stream->add_uint_data(eh, AMPERE_EVENT_DATA_DEFN_NAME(parent), p.txid().value);
    stream->close_event(eh);
}

} // namespace ampere::sc

/*
 * vim: sw=4:ts=4:et:si:cindent:cino=\:0,(0,g0,t0:
 */
