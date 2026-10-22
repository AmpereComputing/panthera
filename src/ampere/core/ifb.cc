//
// Copyright (c) 2021-2023 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//

#include "ampere/core/ifb.h"
#include "ampere/uarch_common/memreq.h"

namespace ampere::core {

using namespace uarch_common;

IFB::IFB(const sc_core::sc_module_name& name) : Module(name) {}

// Mark any outstanding IFB entries to this cacheline as having been snooped.
// This allows the ICF to check them later with was_snooped() to avoid filling
// the icache with snooped lines so we don't break inclusion with the L2C. If
// the ICF needs to re-start processing based on the contents of the IFB, send
// the fetch ID of the youngest "surviving" fetch (the youngest which does not
// have to be re-started).
std::optional<FetchId> IFB::snoop(PhysAddr pa, bool icache_snoop_hit)
{
    ev_snoop();
    if (buffer.empty())
        return std::nullopt;

    auto oldest_ifb = buffer.begin();
    std::optional<FetchId> ifb_snoop_hit;
    for (auto& it : buffer) {
        if (pa.same_cache_line(it.second.pa)) {
            it.second.snoop_hit = true;
            ifb_snoop_hit = it.second.fetch_id;
            ev_snoop_hit();
            break;
        }
    }

    if (snoop_policy == IFBSnoopPolicy::hit_plus_one_on_ifb_hit) {
        // The original Furiosa snoop policy: Restart ICF pipeline at snoop hit + 1
        // upon snoop hit in IFB (otherwise do nothing)
        if (ifb_snoop_hit)
            return ifb_snoop_hit.value();
        return std::nullopt;
    } else if (snoop_policy == IFBSnoopPolicy::restart_oldest_on_any_hit) {
        // PoR for Connor: If any IFB entries exist and there is a snoop hit in
        // either the IFB or icache, re-start from the oldest IFB + 1
        if (icache_snoop_hit || ifb_snoop_hit)
            return oldest_ifb->second.fetch_id;
        return std::nullopt;
    } else if (snoop_policy == IFBSnoopPolicy::always_restart_oldest) {
        // The "safest" IFB policy (really aggressive): If any IFB entries
        // exist, always re-start from the oldest IFB + 1 (even if there is no
        // snoop hit)
        return oldest_ifb->second.fetch_id;
    }

    AMPERE_ASSERT(
        snoop_policy == IFBSnoopPolicy::never, "IFB::snoop() found unexpected snoop policy: {}", snoop_policy);
    return std::nullopt;
}

bool IFB::requesting_other_line_same_l2_set(PhysAddr pa)
{
    auto req_index = l2_indexer->index(pa);
    for (auto& it : buffer)
        if (!pa.same_cache_line(it.second.pa) && l2_indexer->index(it.second.pa) == req_index)
            return true;

    return false;
}

// See if there is an existing IFB entry for this cacheline. Return a pair of
// booleans: 1) whether this is a hit and 2) whether this was the first hit to
// a cleared IFB entry (in which case the fetch "unclearing" it is now
// responsible for filling it into the icache)
std::pair<bool, bool> IFB::lookup(Fetch& fetch, PhysAddr pa)
{
    for (auto it = buffer.begin(); it != buffer.end(); it++) {
        if (pa.same_cache_line(it->second.pa)) {
            bool uncleared = false;
            if (reclaim_cleared_on_hit && it->second.cleared && it->second.unclearable) {
                it->second.cleared = false;
                it->second.unclearable = false;
                uncleared = true;

                // re-key the cleared and reclaimed entry to the now-oldest IL1
                // miss waiting on it, which will now be responsible for
                // filling it into the icache, deallocating it, etc.
                auto node = buffer.extract(it->first);
                uint64_t id = fetch->txid().value;
                node.key() = id;
                buffer.insert(std::move(node));
                it = buffer.find(id);
                it->second.fetch_id = fetch->fetch_id;

                // If the cleared IFB we are reclaiming was allocated in the
                // other color, we must allocate a credit for it in the current
                // color.
                if (!it->second.credit[color].has_value())
                    it->second.credit[color] = credits[color].acquire();

                ev_reclaim_cleared(TransactionId{it->first}, ev_phys_line(it->second.pa.line_address().value()));
            }
            return {true, uncleared};
        }
    }
    return {false, false};
}

bool IFB::is_complete(PhysAddr pa)
{
    for (auto& it : buffer) {
        if (pa.same_cache_line(it.second.pa) && it.second.complete)
            return true;
    }
    return false;
}

// Return true if the IFB contains an entry for this cacheline but which is no
// longer needed by any fetch due to being cleared/flushed.
bool IFB::is_cleared(PhysAddr pa)
{
    for (auto& it : buffer) {
        if (pa.same_cache_line(it.second.pa) && it.second.cleared)
            return true;
    }
    return false;
}

// Return true if the IFB contains an entry for this cacheline which has been
// cleared, but which is able to be "un-cleared" (associated with a new demand
// fetch)
bool IFB::is_unclearable(PhysAddr pa)
{
    for (auto& it : buffer) {
        if (pa.same_cache_line(it.second.pa) && it.second.cleared && it.second.unclearable)
            return true;
    }
    return false;
}

// Return the number of 'active' IFB entries - pending requests to the L2C for
// IFB entries which have either never been cleared, or have been cleared and
// un-cleared/reclaimed.
size_t IFB::num_active()
{
    size_t active = 0;
    for (auto& it : buffer) {
        if (it.second.sent && !it.second.complete && !it.second.cleared)
            active++;
    }
    return active;
}

size_t IFB::num_unsent()
{
    size_t unsent = 0;
    for (auto& it : buffer) {
        if (!it.second.sent)
            unsent++;
    }
    return unsent;
}

bool IFB::can_allocate()
{
    return credits[color].can_acquire();
}

// Allocate a new IFB entry for a new L2C request/icache miss that missed the
// IFB. 'id' is expected to be Fetch->txid().value of the fetch which will fill
// this line into the cache. If this is a 'reclaimed' previously-cleared IFB
// entry, this will be the fetch which 'uncleared' it, otherwise it will be the
// fetch which allocated the IFB entry in the first place.
void IFB::allocate(
    std::uint64_t id, FetchId fetch_id, PhysAddr pa, PageSize::Size page_size, bool prefetch, bool allocate_completed)
{
    auto [it, inserted] = buffer.try_emplace(id,
                                             fetch_id,
                                             pa,
                                             page_size,
                                             color == 0 ? std::optional(credits[0].acquire()) : std::nullopt,
                                             color == 1 ? std::optional(credits[1].acquire()) : std::nullopt,
                                             get_cycle(),
                                             prefetch);

    AMPERE_ASSERT(inserted, "attempt to allocate duplicate IFB for entry {}", id);

    ev_allocate(TransactionId{id}, ev_phys_line(pa.line_address().value()));
    it->second.sent = allocate_completed;
    it->second.complete = allocate_completed;

    // We can use the speculative interface for this request iff this is the
    // only unsent IFB entry at the time of its allocation, and it is not
    // causing the IFB to become full (note that 'full' here means the full
    // IFB, not just the max number of active entries).
    if (!allocate_completed) {
        if (!just_allocated.has_value())
            just_allocated = it->first;
        may_use_speculative_interface = (num_unsent() == 1 && credits[color].can_acquire());
    }
}

// Deallocate an IFB entry which has been filled into the icache.
// 'id' is expected to be Fetch->txid().value of the fetch which will fill this
// line into the cache. If this is a 'reclaimed' previously-cleared IFB entry,
// this will be the fetch which 'uncleared' it, otherwise it will be the fetch
// which allocated the IFB entry in the first place.
void IFB::deallocate(std::uint64_t id)
{
    auto it = buffer.find(id);
    AMPERE_ASSERT(it != buffer.end(), "attempt to deallocate IFB entry {} that doesn't exist", id);
    AMPERE_ASSERT(it->second.complete, "attempt to deallocate incomplete IFB entry {}", id);
    ev_deallocate(
        TransactionId{id}, ev_phys_line(it->second.pa.line_address().value()), ev_cleared(it->second.cleared));
    for (size_t c = 0; c < IFB_COLORS; c++)
        if (it->second.credit[c].has_value())
            release_credit(it->second.credit[c].value());
    buffer.erase(it);
}

// Check to see if an IFB entry was snooped while waiting for its response.
// 'id' is expected to be Fetch->txid().value of the fetch which will fill this
// line into the cache. If this is a 'reclaimed' previously-cleared IFB entry,
// this will be the fetch which 'uncleared' it, otherwise it will be the fetch
// which allocated the IFB entry in the first place.
bool IFB::was_snooped(std::uint64_t id)
{
    auto it = buffer.find(id);
    AMPERE_ASSERT(it != buffer.end(), "attempt to snoop check IFB entry {} that doesn't exist", id);
    return it->second.snoop_hit;
}

bool IFB::upgrade_prefetch(std::uint64_t id, FetchId fetch_id)
{
    auto it = buffer.find(id);
    AMPERE_ASSERT(it != buffer.end(), "attempt to upgrade IFB entry {} that doesn't exist", id);
    it->second.fetch_id = fetch_id;
    return it->second.complete;
}

std::pair<bool, MemReq> IFB::schedule_request()
{
    // If the L2C FIFO is full, we cannot schedule a new request this cycle
    if (!l2c_fifo.can_acquire())
        return std::make_pair(false, nullptr);

    // If we are limiting the maximum number of active IFB entries, and we are
    // currently above that limit, we do not schedule a request
    if ((max_active_entries > 0 && num_active() >= max_active_entries))
        return std::make_pair(false, nullptr);

    for (auto& it : buffer) {
        auto& entry = it.second;
        if (!(entry.complete || entry.sent || entry.cleared) && entry.fetch_id != 0) {
            // If a just-allocated entry cannot be sent on the speculative
            // interface, it must wait a cycle to be scheduled
            bool entry_just_allocated = just_allocated.has_value() && just_allocated.value() == it.first;
            bool send_just_allocated =
                !delay_nonspeculative_requests || (enable_speculative_interface && may_use_speculative_interface);
            if (entry_just_allocated && !send_just_allocated) {
                return std::make_pair(false, nullptr);
            }

            entry.l2c_fifo_credit = l2c_fifo.acquire();
            entry.sent = true;
            entry.response_id = it.first;
            return std::make_pair(false, make<MemReq>(MemReqObj::code(it.first, entry.pa, entry.page_size)));
        }
    }

    // Return prefetch, prefetches are defined when the fetch_id is === 0
    for (auto& it : buffer) {
        auto& entry = it.second;
        if (!(entry.complete || entry.sent || entry.cleared) && entry.fetch_id == 0) {
            // If a just-allocated entry cannot be sent on the speculative
            // interface, it must wait a cycle to be scheduled
            bool entry_just_allocated = just_allocated.has_value() && just_allocated.value() == it.first;
            bool send_just_allocated =
                !delay_nonspeculative_requests || (enable_speculative_interface && may_use_speculative_interface);
            if (entry_just_allocated && !send_just_allocated) {
                return std::make_pair(false, nullptr);
            }

            entry.l2c_fifo_credit = l2c_fifo.acquire();
            entry.sent = true;
            entry.response_id = it.first;
            return std::make_pair(true, make<MemReq>(MemReqObj::code(it.first, entry.pa, entry.page_size)));
        }
    }
    return std::make_pair(false, nullptr);
}

void IFB::age_requests()
{
    // The speculative interface only makes sense for allocations made this
    // cycle
    may_use_speculative_interface = false;
    just_allocated = std::nullopt;
}

// Clear the L2C FIFO credit associated with this request because the L2C has
// scheduled it
void IFB::l2c_scheduled_request(std::uint64_t id)
{
    for (auto& it : buffer) {
        auto& entry = it.second;
        if (entry.response_id && entry.response_id.value() == id) {
            // reclaim the L2C FIFO credit
            release_credit(entry.l2c_fifo_credit);
            return;
        }
    }
    AMPERE_FATAL("Failed to find IFB entry for returned response for request/response ID {}", id);
}

// Mark the entry as being 'complete' (having received a response from the
// L2C), and remove it from the IFB if it has been cleared. The 'id' here is
// expected to be MemResp->id of the L2C response.
//
// Returns a tuple of booleans, where the first bool indicates whether this IFB
// entry was cleared, the second indicates whether it can be un-cleared, and
// the third indicates whether it has been snooped
std::tuple<bool, bool, bool> IFB::handle_response(std::uint64_t id)
{
    for (auto& it : buffer) {
        auto& entry = it.second;
        if (entry.response_id && entry.response_id.value() == id) {
            // Mark the entry complete
            entry.complete = true;
            auto was_cleared = entry.cleared;
            auto unclearable = entry.unclearable;
            auto was_snooped = entry.snoop_hit;
            erase_cleared_entries();

            return {was_cleared, unclearable, was_snooped};
        }
    }
    AMPERE_FATAL("Failed to find IFB entry for returned response for request/response ID {}", id);
    return {false, false, false};
}

void IFB::flush()
{
    clear_all();
    auto color0_space = credits[0].get_count();
    auto color1_space = credits[1].get_count();
    auto old_color = color;

    // On a flush we switch to whichever color has more entries available
    if (color0_space > color1_space)
        color = 0;
    else if (color1_space > color0_space)
        color = 1;
    else
        color ^= 1;

    if (old_color == color)
        ev_smart_color_switch();
}

void IFB::clear_from_fetch_id(FetchId clearing_fetch_id)
{
    for (auto& it : buffer) {
        if (it.second.fetch_id > clearing_fetch_id) {
            it.second.cleared = true;
            it.second.unclearable = false;
        } else {
            // go through the list of younger hits and remove any younger than clearing_fetch_id
            for (auto hit_it = it.second.younger_hits.cbegin(), next_it = hit_it;
                 hit_it != it.second.younger_hits.cend();
                 hit_it = next_it)
            {
                ++next_it;
                if (hit_it->first > clearing_fetch_id) {
                    it.second.younger_hits.erase(hit_it);
                }
            }
        }
    }
    erase_cleared_entries();
}

void IFB::clear_all()
{
    for (auto& it : buffer) {
        it.second.cleared = true;
        it.second.unclearable = true;
        // remove all tracking of younger hits (they're all cleared)
        it.second.younger_hits.clear();
    }
    erase_cleared_entries();
}

void IFB::erase_cleared_entries()
{
    for (auto it = buffer.begin(); it != buffer.end();) {
        if (it->second.cleared && (it->second.complete || !it->second.sent)) {
            ev_deallocate(TransactionId{it->first},
                          ev_phys_line(it->second.pa.line_address().value()),
                          ev_cleared(it->second.cleared));
            for (size_t c = 0; c < IFB_COLORS; c++)
                if (it->second.credit[c].has_value())
                    release_credit(it->second.credit[c].value());
            it = buffer.erase(it);
        } else {
            it++;
        }
    }
}

// Search through the IFB looking for entries to the same set as the supplied
// physical address. If there are, see if either
//   1) there is already a recorded "younger hit" to the same cacheline (i.e. same way) OR
//   2) there is a free "younger hit" to record this hit into
// If neither of those is true, return false, indicating we cannot properly
// record this hit.
bool IFB::can_record_younger_hit(FetchId fetch_id, PhysAddr pa, size_t num_sets)
{
    size_t set = pa.line_number() % num_sets;
    for (const auto& entry : buffer) {
        size_t entry_set = entry.second.pa.line_number() % num_sets;
        if (set == entry_set) {
            bool found_same_line = false;
            for (const auto& [other_fetch_id, other_pa] : entry.second.younger_hits) {
                if (other_pa.same_cache_line(pa)) {
                    found_same_line = true;
                    break;
                }
            }
            if (!found_same_line && entry.second.younger_hits.size() == max_tracked_hit_ways) {
                return false;
            }
        }
    }
    return true;
}

// If there is not already a hit recorded to the same
// cacheline and there is a free slot, record this hit in it.
void IFB::record_younger_hit(FetchId fetch_id, PhysAddr pa, size_t num_sets)
{
    size_t set = pa.line_number() % num_sets;
    for (auto& entry : buffer) {
        size_t entry_set = entry.second.pa.line_number() % num_sets;
        if (set == entry_set) {
            bool found_same_line = false;
            for (const auto& [other_fetch_id, other_pa] : entry.second.younger_hits) {
                if (other_pa.same_cache_line(pa)) {
                    found_same_line = true;
                    break;
                }
            }
            if (!found_same_line) {
                AMPERE_ASSERT(entry.second.younger_hits.size() < max_tracked_hit_ways,
                              "IFB doesn't have enough younger hit entries to record this hit, call "
                              "can_record_younger_hit before calling record_younger_hit");
                entry.second.younger_hits.emplace(fetch_id, pa);
            }
        }
    }
}

void IFB::dump(std::ostream& os) const
{
    if (buffer.empty()) {
        os << "IFB entries: None\n";
    } else {
        os << "IFB entries:\n";
        os << "|    TXID    |  Fetch Id  |   Physical Address   | PgSz | Req Cycle|Complete| Sent  |Cleared|\n";
        for (auto& entry : buffer)
            fmt::print("| {:>10} | {:>10} | {:^#020x} | {:4} | {:8} | {:6} | {:5} | {:5} |\n",
                       entry.first,
                       entry.second.fetch_id,
                       entry.second.pa.value(),
                       std::string(PageSize(entry.second.page_size)),
                       entry.second.request_cycle,
                       entry.second.complete ? "True" : "False",
                       entry.second.sent ? "True" : "False",
                       entry.second.cleared ? "True" : "False");
    }

    fmt::print("ifb_color: {}\n", color);
}

} // namespace ampere::core
