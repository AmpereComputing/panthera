//
// Copyright (c) 2019 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//

#ifndef AMPERE_CORE_FETCH_ID_H
#define AMPERE_CORE_FETCH_ID_H

#include <cstdint>
#include "ampere/sc/event.h"

namespace ampere::core {

using FetchId = std::uint64_t;

inline sc::EventArg<FetchId> fetch_id_data(FetchId fetch_id)
{
    extern ::ampere::event_stream::DataType _evd_fetch_id_defn;
    return {_evd_fetch_id_defn, fetch_id};
}

} // namespace ampere::core

#endif // AMPERE_CORE_FETCH_ID_H
