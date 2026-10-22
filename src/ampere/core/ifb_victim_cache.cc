//
// Copyright (c) 2023 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//

#include "ampere/core/ifb_victim_cache.h"

namespace ampere::core {

IFBVictimCache::IFBVictimCache(const sc_core::sc_module_name& name) : Module(name)
{
    // initialize vector of entries, invalidate them all
    buffer.resize(num_entries);
}

/*
 * If we received a snoop for a victim cache entry, attempt to invalidate it.
 * We can immediately invalidate it if it is not in use (i.e. having been hit
 * upon by a fetch which hasn't yet filled IL1), but if its in use we mark it
 * as a snoop_hit so it isn't filled into the IL1 later (ICF is responsible for
 * calling was_snooped() prior to filling).
 */
void IFBVictimCache::snoop(PhysAddr pa)
{
    for (auto& entry : buffer) {
        if (entry.valid && pa.same_cache_line(entry.pa)) {
            entry.snoop_hit = true;
            if (!entry.in_use()) {
                entry.valid = false;
            }
            return;
        }
    }
}

/*
 * Check if an entry in the IFB victim cache has been snooped since it was
 * initially hit upon.
 */
bool IFBVictimCache::was_snooped(FetchId fetch_id)
{
    for (auto& entry : buffer) {
        if (entry.valid && entry.fetch_id.has_value() && entry.fetch_id == fetch_id) {
            return entry.snoop_hit;
        }
    }
    AMPERE_FATAL("attempt to snoop check IFBVictimCache entry that wasn't hit-upon by fetch ID {} as expected",
                 fetch_id);
    return false;
}

void IFBVictimCache::flush()
{
    for (auto& entry : buffer) {
        entry.fetch_id = std::nullopt;
        if (entry.snoop_hit)
            entry.valid = false;
    }
}

void IFBVictimCache::clear_from_fetch_id(FetchId clearing_fetch_id)
{
    for (auto& entry : buffer) {
        if (entry.fetch_id.has_value() && *entry.fetch_id > clearing_fetch_id) {
            entry.fetch_id = std::nullopt;
            if (entry.snoop_hit)
                entry.valid = false;
        }
    }
}

/*
 * Check to see if a line is present in the victim cache. If it is and we are
 * the first fetch to use it, mark it as being used by the current fetch.
 * Return true upon hit and false for a miss. Throw an exception if the same
 * line has already been hit upon (since the last clear).
 */
bool IFBVictimCache::lookup(Fetch& fetch)
{
    auto pa = *fetch->phys_addr;
    ev_read(fetch);
    for (auto& entry : buffer) {
        if (entry.valid && pa.same_cache_line(entry.pa)) {
            AMPERE_ASSERT_ERROR(!entry.fetch_id.has_value(), "IFB victim cache already has an outstanding hit");
            entry.fetch_id = fetch->fetch_id;
            ev_hit(fetch);
            return true;
        }
    }
    return false;
}

/*
 * Ensure the line isn't already present (it shouldn't ever be). Starting at
 * the current write pointer, overwrite the first entry in the FIFO which is
 * not currently in use. Fail to refill if we wrap around to the previous write
 * pointer and no entry is in use.
 */
void IFBVictimCache::refill(PhysAddr pa)
{
    for (auto& entry : buffer)
        AMPERE_ASSERT(!entry.valid || !pa.same_cache_line(entry.pa),
                      "Attempting to refill already-present cacheline in IFB victim cache");

    size_t idx = write_pointer;
    do {
        auto& entry = buffer.at(idx);
        if (!entry.in_use()) {
            buffer[idx] = Entry(pa);
            write_pointer = (idx + 1) % num_entries;
            ev_fill();
            return;
        }
        idx = (idx + 1) % num_entries;
    } while (idx != write_pointer);
}

void IFBVictimCache::deallocate(FetchId fetch_id)
{
    for (auto& entry : buffer) {
        if (entry.in_use() && *entry.fetch_id == fetch_id) {
            entry.valid = false;
            entry.fetch_id = std::nullopt;
            return;
        }
    }

    AMPERE_FATAL("Failed to deallocate victim cache entry for fetch ID {} because one doesn't exist", fetch_id);
}

} // namespace ampere::core
