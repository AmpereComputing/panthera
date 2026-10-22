//
// Copyright (c) 2021 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//

#pragma once

#include <cstdint>
#include <deque>
#include <optional>

#include "ampere/archx/addr.h"
#include "ampere/sc/sc.h"
#include "ampere/uarch_common/memreq.h"
#include "ampere/uarch_common/memop.h"
#include "ampere/util/iterator.h"

make_ns_enum(ampere::core, PWCDrainReason,
    SIZE_THRESHOLD = 0,
    WAITING_TO_FILL,
    HIT_BY_REQUEST,
    TIMEOUT,
    ORDERED_REQUEST,
    BARRIER
);

namespace ampere::core {

using namespace archx;
using namespace sc;
using namespace uarch_common;

using uarch_common::MemOp::StoreType;

template<typename T>
inline static const MemReq &as_memreq(const T &req)
{
    if constexpr (std::is_same_v<T, MemReq>) {
        return req;
    } else {
        return req->req;
    }
}

template<typename T>
struct PWCEntry
{
    uint64_t id;
    T req;
    uint64_t byte_enables{0};
    bool ordered{false};
    bool read_hit{false};
    bool waiting_for_combine{false};
    std::optional<PWCDrainReason> draining;

    PWCEntry(uint64_t id, T req, uint64_t byte_enables, bool ordered, bool waiting_for_combine = false)
        : id(id),
          req(std::move(req)),
          byte_enables(byte_enables),
          ordered(ordered),
          waiting_for_combine(waiting_for_combine)
    {
    }

    const auto &memreq() { return as_memreq(req); }
    const auto &memreq() const { return as_memreq(req); }

    PhysAddr phys_addr() const { return memreq()->phys_addr; }
    bool full_line() const { return byte_enables == util::mask(PhysAddr::LINE_SIZE); }
    unsigned chunks() const;
    bool atomic() const { return memreq()->atomic(); }
    bool strictly_ordered() const { return !waiting_for_combine && atomic(); }
    bool line_matches(PhysAddr addr) const { return phys_addr().same_cache_line(addr); }
    bool line_matches(MemReq req) const { return line_matches(req->phys_addr); }
    bool combinable(MemReq req) const
    {
        return !draining && !req->atomic() && !strictly_ordered() && line_matches(req);
    }
    void schedule(PWCDrainReason reason) { draining = reason; }
};

// Generic Partial Write Combiner
template<typename E>
class PWC : public Module
{
public:
    explicit PWC(const sc_core::sc_module_name &name) : Module{name} {}

    Param<size_t> size{"size", "number of entries in the PWC", 8};
    Param<size_t> threshold{"threshold", "drain when occupied entries > threshold", 3};
    Param<uint16_t> timeout{
        "timeout", "drain automatically after timeout cycles have elapsed since the last drain", 32};
    Param<unsigned> max_age{"max_age", "maximum value of the age and priority timers", 512};
    Param<bool> drain_in_order{"drain_in_order", "Drain PWC in-order if any entry is hit", true};
    Param<bool> combining_allowed{"combining_allowed", "Combining is allowed", true};
    Param<bool> can_combine_with_oldest_during_drain{
        "can_combine_with_oldest_during_drain", "Allowed to combine with the oldest while draining", false};
    Param<bool> unordered_can_combine_with_ordered{
        "unordered_can_combine_with_ordered", "Unordered stores can combine with the youngest ordered", false};
    Param<unsigned> unordered_can_combine_with_previous_ordered{
        "unordered_can_combine_with_previous_ordered", "Unordered stores can combine with the N previous ordered", 0};
    Param<bool> ordered_can_combine_with_youngest{
        "ordered_can_combine_with_youngest",
        "Ordered stores are allowed to combine with the youngest entry (if matching)",
        false};
    Param<uint64_t> partial_address_mask{"partial_address_mask",
                                         "Partial address mask for detecting LD-ST conflicts",
                                         util::mask(31, PhysAddr::LINE_BITWIDTH)};

    bool can_combine(const MemReq &req) const;

    template<typename T>
    const E &allocate(T req, bool requires_combine = false);

    void combine(MemReq req);
    bool try_combine(MemReq req);

    bool full() const { return entries.size() == size; }
    bool empty() const { return entries.empty(); }

    bool has_address_match(PhysAddr addr) const;
    bool has_partial_address_match(PhysAddr addr) const;

    uint64_t check_partial_address_match(PhysAddr addr) { return check_address_match(addr, true); }
    uint64_t check_line_address_match(PhysAddr addr, bool ordered_only = false)
    {
        return check_address_match(addr, false, ordered_only);
    }

    unsigned store_types() const;

    void set_pending_barrier(bool value) { pending_barrier = value; }
    void set_pending_writes(bool value) { pending_writes = value; }
    void set_request_is_waiting() { request_is_waiting = true; }

    unsigned int get_priority_timer() const { return priority_timer.get_value(); }

    bool run() override;

protected:
    using DrainReason = PWCDrainReason;

    struct
    {
        Event pwc_hit{"pwc_hit", "loads that hit in the PWC"};
        Event combine{"combine", "store combined with another in the PWC"};
        Event allocate{"allocate", "store allocated a new entry in the PWC"};
        Event deallocate{"deallocate", "store(s) deallocated from the PWC"};
        EventData<bool> ordered{"ordered", "store in the PWC is ordered"};
        EventData<bool> combined_latest{"combined_latest", "latest PWC entry"};
        EventData<bool> full_line{"full_line", "all bytes present for line"};
        EventData<unsigned> pwc_id{"pwc_id", "PWC entry ID"};
        EventData<uint64_t> byte_enables{"byte_enables", "Byte enable mask"};
        EventData<uint64_t> phys_line{"phys_line", "Physical line address"};
        EventData<DrainReason> drain_reason{"drain_reason", "Drain reason"};
        EventData<unsigned> chunks{"chunks", "16B chunks to be written"};
    } event;

    std::deque<E> entries;
    using pwc_iterator = typename std::deque<E>::iterator;
    using pwc_reverse_iterator = typename std::deque<E>::reverse_iterator;

    unsigned ordered_count{0};
    unsigned read_hit_count{0};
    unsigned pending_deallocations{0};
    SaturatingCounter age_timer{max_age};
    SaturatingCounter priority_timer{max_age};
    uint64_t current_id{1};

    bool pending_writes{false};
    bool pending_barrier{false};
    bool request_is_waiting{false};

    PhysAddr partial_address(PhysAddr addr) const { return addr & partial_address_mask; }
    uint64_t check_address_match(PhysAddr addr, bool partial, bool ordered_only = false);

    pwc_reverse_iterator find_matching_entry(const MemReq &req, bool is_ordered);
    void combine_with(pwc_reverse_iterator entry, MemReq req);
    bool can_combine_with_wide_atomic(MemReq req) const;
    bool can_combine_with_youngest(MemReq req) const;
    bool should_pick_top_entry() const;
    pwc_iterator pick_hit_entry();

    std::optional<DrainReason> drain_reason() const;
    bool want_to_drain() const { return drain_reason().has_value(); }

    void deallocate(const pwc_iterator &entry, DrainReason reason);
    void deallocate_pending() { ++pending_deallocations; }
    void deallocate_no_longer_pending();

    unsigned age_to_pwc_id(uint64_t age) const { return (age - 1) % size; }
};

template<typename T>
using BasicPWC = PWC<PWCEntry<T>>;

template<typename T>
unsigned PWCEntry<T>::chunks() const
{
    constexpr unsigned CHUNK_SIZE = 16;
    unsigned chunks = 0;
    for (unsigned c = 0; c < PhysAddr::LINE_SIZE / CHUNK_SIZE; ++c)
        if (byte_enables & util::mask((c + 1) * CHUNK_SIZE - 1, c * CHUNK_SIZE))
            ++chunks;

    return chunks;
}

template<typename E>
bool PWC<E>::run()
{
    if (!entries.empty()) {
        ++age_timer;

        if (want_to_drain())
            ++priority_timer;
    }

    return true;
}

template<typename E>
template<typename T>
const E &PWC<E>::allocate(T req, bool requires_combine)
{
    const auto &memreq = as_memreq(req);
    bool is_ordered = memreq->ordered();

    auto &entry = entries.emplace_back(current_id++, req, memreq->bytemask, is_ordered, requires_combine);
    event.allocate(req, event.pwc_id(age_to_pwc_id(entry.id)), memreq->phys_addr, event.ordered(is_ordered));

    if (is_ordered)
        ++ordered_count;

    return entry;
}

template<typename E>
void PWC<E>::combine_with(pwc_reverse_iterator entry, MemReq req)
{
    AMPERE_ASSERT_ERROR(entry != entries.rend(), "No entry to combine with");

    entry->waiting_for_combine = false;
    // Upgrade entry to ordered if not already
    if (!entry->ordered && req->ordered()) {
        entry->ordered = true;
        ++ordered_count;
    }
    // Upgrade request if combining has more store-types
    if (entry->memreq()->store_types() < req->store_types()) {
        if constexpr (std::is_same_v<decltype(entry->req), MemReq>) {
            entry->req = req;
        } else {
            entry->req->req = req;
        }
    }
    entry->byte_enables |= req->bytemask;
    event.combine(
        req, req->phys_addr, event.combined_latest(entry == entries.rbegin()), event.full_line(entry->full_line()));
}

template<typename E>
void PWC<E>::combine(MemReq req)
{
    combine_with(find_matching_entry(req, req->ordered()), req);
}

template<typename E>
bool PWC<E>::try_combine(MemReq req)
{
    if (auto entry = find_matching_entry(req, req->ordered()); entry != entries.rend()) {
        combine_with(entry, req);
        return true;
    }
    return false;
}

template<typename E>
bool PWC<E>::can_combine(const MemReq &req) const
{
    auto entry = const_cast<PWC *>(this)->find_matching_entry(req, req->ordered());
    return entry != entries.rend()
           && (entry->waiting_for_combine
               || (combining_allowed
                   && (can_combine_with_oldest_during_drain || !want_to_drain()
                       || util::make_iterator(entry) != entries.begin())));
}

template<typename E>
bool PWC<E>::can_combine_with_wide_atomic(MemReq req) const
{
    return req->atomic_type == uarch_common::MemOp::ATOMIC_LOAD_WIDE && entries.back().waiting_for_combine;
}

template<typename E>
bool PWC<E>::can_combine_with_youngest(MemReq req) const
{
    return ordered_can_combine_with_youngest
           && entries.back().combinable(req)
           // can only combine if not draining entry or if it's already ordered due to complexity/timing
           && (entries.size() > 1 || !want_to_drain() || !entries.back().ordered);
}

template<typename E>
typename PWC<E>::pwc_reverse_iterator PWC<E>::find_matching_entry(const MemReq &req, bool is_ordered)
{
    if (!entries.empty()) {
        if (!is_ordered) {
            unsigned ordered_age = 0;
            for (auto entry = entries.rbegin(); entry != entries.rend() && !entry->strictly_ordered(); ++entry) {
                if ((!entry->ordered || unordered_can_combine_with_ordered) && entry->combinable(req))
                    return entry;

                if (entry->ordered) {
                    if (ordered_age >= unordered_can_combine_with_previous_ordered)
                        break;
                    ++ordered_age;
                }
            }
        } else if (can_combine_with_wide_atomic(req) || can_combine_with_youngest(req)) {
            return entries.rbegin();
        }
    }

    return entries.rend();
}

template<typename E>
std::optional<typename PWC<E>::DrainReason> PWC<E>::drain_reason() const
{
    if (!entries.empty() && entries.front().waiting_for_combine)
        return std::nullopt;
    if ((entries.size() - pending_deallocations) > threshold)
        return DrainReason::SIZE_THRESHOLD;
    if (request_is_waiting)
        return DrainReason::WAITING_TO_FILL;
    if (age_timer.get_value() >= timeout)
        return DrainReason::TIMEOUT;
    if (ordered_count)
        return DrainReason::ORDERED_REQUEST;
    if (read_hit_count)
        return DrainReason::HIT_BY_REQUEST;
    if (pending_barrier)
        return DrainReason::BARRIER;
    return std::nullopt;
}

template<typename E>
bool PWC<E>::should_pick_top_entry() const
{
    return drain_in_order || !read_hit_count || pending_barrier;
}

template<typename E>
typename PWC<E>::pwc_iterator PWC<E>::pick_hit_entry()
{
    for (auto e = entries.begin(); e != entries.end(); ++e) {
        if (e->read_hit && !e->waiting_for_combine)
            return e;
        if (e->ordered)
            break;
    }

    return entries.begin();
}

template<typename E>
void PWC<E>::deallocate(const pwc_iterator &entry, DrainReason reason)
{
    if (entry->ordered) {
        AMPERE_ASSERT(ordered_count > 0, "ordered_count dropped below 0");
        --ordered_count;
    }

    if (entry->read_hit) {
        AMPERE_ASSERT(read_hit_count > 0, "read_hit_count dropped below 0");
        --read_hit_count;
    }

    event.deallocate(entry->req,
                     event.pwc_id(age_to_pwc_id(entry->id)),
                     event.byte_enables(entry->byte_enables),
                     event.drain_reason(reason),
                     event.full_line(entry->full_line()),
                     event.chunks(entry->chunks()));

    entries.erase(entry);
    age_timer.set_value(0);
    priority_timer.set_value(0);
    request_is_waiting = false;

    if (entries.empty()) {
        AMPERE_ASSERT(ordered_count == 0, "ordered_count should be 0 when PWC is empty, but it's {}", ordered_count);
        AMPERE_ASSERT(read_hit_count == 0, "read_hit_count should be 0 when PWC is empty, but it's {}", read_hit_count);
    }
}

template<typename E>
void PWC<E>::deallocate_no_longer_pending()
{
    AMPERE_ASSERT(pending_deallocations > 0, "pending_deallocations dropped below 0");
    --pending_deallocations;
}

template<typename E>
bool PWC<E>::has_address_match(PhysAddr addr) const
{
    return std::any_of(entries.begin(), entries.end(), [addr](const auto &e) { return e.line_matches(addr); });
}

template<typename E>
bool PWC<E>::has_partial_address_match(PhysAddr addr) const
{
    auto partial_addr = partial_address(addr);
    return std::any_of(entries.begin(), entries.end(), [this, partial_addr](const auto &e) {
        return partial_address(e.phys_addr()).same_cache_line(partial_addr);
    });
}

template<typename E>
uint64_t PWC<E>::check_address_match(PhysAddr addr, bool partial, bool ordered_only)
{
    auto partial_addr = partial_address(addr);
    for (auto &entry : entries) {
        auto match_addr = partial ? partial_address(entry.phys_addr()) : entry.phys_addr();
        if (match_addr.same_cache_line(partial_addr) && (!ordered_only || entry.ordered)) {
            if (!entry.read_hit) {
                event.pwc_hit(
                    entry.req, event.pwc_id(age_to_pwc_id(entry.id)), event.phys_line(addr.line_address().value()));
                ++read_hit_count;
            }
            entry.read_hit = true;
            return entry.id;
        }
    }

    return 0;
}

template<typename E>
unsigned PWC<E>::store_types() const
{
    unsigned flags{0};
    for (const auto &entry : entries)
        flags |= entry.memreq()->store_types();

    return flags;
}

} // namespace ampere::core
