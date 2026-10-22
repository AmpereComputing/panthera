//
// Copyright (c) 2023 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//

#pragma once

#include "ampere/core/fetch.h"
#include "ampere/sc/sc.h"

namespace ampere::core {

using namespace ampere::sc;
using namespace ampere::archx;

class IFBVictimCache : public Module
{
public:
    explicit IFBVictimCache(const sc_core::sc_module_name& name);

    Param<size_t> num_entries{"num_entries", "The maximum number of victim cache FIFO entries", 4};

    Event ev_fill{"fill", "IFB victim cache entry filled"};
    Event ev_read{"read", "IFB victim cache entry read"};
    Event ev_hit{"hit", "IFB victim cache entry hit"};

    void snoop(PhysAddr pa);
    bool was_snooped(FetchId fetch_id);
    void flush();
    void clear_from_fetch_id(FetchId clearing_fetch_id);
    bool lookup(Fetch& fetch);
    void refill(PhysAddr pa);
    void deallocate(FetchId fetch_id);

private:
    struct Entry
    {
        Entry(PhysAddr pa) : fetch_id(std::nullopt), pa(pa), valid(true), snoop_hit(false) {}
        Entry() : fetch_id(std::nullopt), pa(0x0_pa), valid(false), snoop_hit(false) {}
        bool in_use() { return fetch_id.has_value(); }
        std::optional<FetchId> fetch_id;
        PhysAddr pa;
        bool valid{false};
        bool snoop_hit{false}; // Entry has been snooped but not cleared
    };
    std::vector<Entry> buffer;
    size_t write_pointer{0};
};

} // namespace ampere::core
