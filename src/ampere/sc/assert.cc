//
// Copyright (c) 2024 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//

#include "ampere/sc/assert.h"

#include <string>

#include "ampere/sc/event.h"
#include "ampere/sc/transaction.h"

namespace ampere::sc {

AMPERE_EVENT_DEFN(assert, "Assert fired")
AMPERE_EVENT_DATA_DEFN(assert_message, "Assert message")

void post_assert_event(Transaction& txn, const std::string& message)
{
    if (txn.event_stream_) {
        auto eh = txn.event_stream_->open_event(AMPERE_EVENT_DEFN_NAME(assert), sc_core::sc_time_stamp().value());
        txn.event_stream_->add_uint_data(eh, AMPERE_EVENT_DATA_DEFN_NAME(txid), txn.txid().value);
        txn.event_stream_->add_string_data(eh, AMPERE_EVENT_DATA_DEFN_NAME(assert_message), message);
        txn.event_stream_->close_event(eh);
    }
}

} // namespace ampere::sc

/*
 * vim: sw=4:ts=4:et:si:cindent:cino=\:0,(0,g0,t0:
 */
