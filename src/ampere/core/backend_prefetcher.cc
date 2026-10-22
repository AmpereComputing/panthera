//
// Copyright (c) 2021-2022 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//

#include "ampere/core/backend_prefetcher.h"
#include "ampere/archx/el.h"

namespace ampere::core {

AMPERE_EVENT_DATA_DEFN(prefetch_type, "L1 Prefetcher type");

BackendMemReq::BackendMemReq(const L1PrefetchEntry& entry, PrefetchType prefetch_type)
    : transaction(entry.parent_txn),
      alloc_id(entry.trigger_uop ? entry.trigger_uop->alloc_id : 0),
      virt_addr(entry.virt_addr),
      phys_addr(entry.phys_addr),
      pc(entry.trigger_uop ? entry.trigger_uop->pc() : 0_pc),
      consumer_pc(entry.consumer_pc),
      uop(entry.trigger_uop),
      exception_level(entry.trigger_uop ? std::make_optional(entry.trigger_uop->ainst()->state().exception_level())
                                        : std::nullopt),
      memory_buffer_id(entry.trigger_uop ? entry.trigger_uop->is_load() ? uop->lob_id_val : uop->sobid->get_id() : 0),
      prefetch_type(prefetch_type)
{
}

L1PrefetchEntry BackendMemReq::as_entry() const
{
    L1PrefetchEntry entry(virt_addr, uop, consumer_pc);
    entry.parent_txn = transaction;
    entry.phys_addr = phys_addr;
    entry.l2_prefetch_type = l2_prefetch_type;
    return entry;
}

} // namespace ampere::core

/*
 * vim: sw=4:ts=4:et:si:cindent:cino=\:0,(0,g0,t0:
 */

