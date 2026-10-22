//
// Copyright (c) 2018-2023 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//

#pragma once

#include <algorithm>
#include <bit>
#include <climits>
#include <cstdint>
#include <optional>
#include <ranges>
#include <tuple>
#include <type_traits>
#include <utility>

#include "ampere/archx/addr.h"
#include "ampere/core/pwc.h"
#include "ampere/sc/event.h"
#include "ampere/sc/param.h"
#include "ampere/sc/putter.h"
#include "ampere/sc/taker.h"
#include "ampere/sc/transaction.h"
#include "ampere/uarch_common/memop.h"
#include "ampere/uarch_common/memreq.h"
#include "ampere/util/concepts.h"
#include "ampere/util/enum.h"

make_ns_enum(ampere::core, LSUPWCResponseAction,
    NO_BANDWIDTH,
    NO_OFB,
    MISSED,
    PREFETCHED,
    STORED,
    FILLED,
    FILLED_NOT_UNIQUE,
    L2
);

make_ns_enum(ampere::core, LSUPWCBlockReason,
    NONE = 0,
    ORDERED,
    MULTIPLE,
    MULTIPLE_PARTIAL
);

make_ns_enum(ampere::core, LSUPWCWakeupMethod,
    NONE = 0,
    ANY,
    PRECISE
);

namespace ampere::core {

struct LSUPWCEntry : PWCEntry<MemReq>
{
    bool release{false};
    bool write_through{false};
    bool has_ofb_entry{false};
    bool prefetched{false};

    bool can_prefetch(bool write_allocate) const { return (!write_through || write_allocate) && !prefetched; }
    void retry()
    {
        this->draining.reset();
        has_ofb_entry = false;
    }
    void miss() { has_ofb_entry = true; }

    bool fills() const { return !write_through; }
    bool filling() const { return this->draining || has_ofb_entry; }

    auto virt_addr() const { return memreq()->virt_addr.line_address(); }
    auto phys_addr() const { return memreq()->phys_addr.line_address(); }

    bool line_matches(VirtAddr addr) const { return virt_addr().same_cache_line(addr); }
    bool line_matches(PhysAddr addr) const { return phys_addr().same_cache_line(addr); }

    unsigned size() const
    {
        return ((sizeof(byte_enables) * CHAR_BIT) - std::countl_zero(byte_enables) - std::countr_zero(byte_enables));
    }

    LSUPWCEntry(uint64_t id, MemReq req, uint64_t byte_enables, bool ordered, bool waiting_for_combine = false)
        : PWCEntry<MemReq>(id, req, byte_enables, ordered, waiting_for_combine),
          release(req->barrier_type == MemOp::BAR_STORE_RELEASE || req->barrier_type == MemOp::BAR_STORE_EXCL_RELEASE)
    {
    }

    void schedule(PWCDrainReason reason)
    {
        if (this->req->atomic_type == MemOp::ATOMIC_LOAD_WIDE && !waiting_for_combine)
            this->req->atomic_type = MemOp::ATOMIC_LOAD;

        this->req->bytemask = this->byte_enables;
        PWCEntry<MemReq>::schedule(reason);
    }
};

class LSUPWC : public core::PWC<LSUPWCEntry>
{
private:
    using Base = core::PWC<LSUPWCEntry>;

    Param<bool> prefetch_misses{"prefetch_misses", "Prefetch misses into cache", false};
    Param<bool> wait_for_load_ofb{"wait_for_load_ofb", "Wait for load OFB if present", false};
    Param<unsigned> schedule_depth{"schedule_depth", "Depth to schedule PWC entries to LSU/Cache", 0};
    Param<bool> write_allocate{"write_allocate", "Allocate misses in L1d", false};
    Param<LSUPWCWakeupMethod> wakeups{"wakeups", "PWC can wakeup slept loads", LSUPWCWakeupMethod::NONE};
    Param<bool> allow_forwarding{"allow_forwarding", "Allow forwarding from PWC", true};
    Param<bool> forward_across_ordered{"forward_across_ordered", "Forward across ordered entries", false};
    Param<bool> forward_from_multiple{
        "forward_from_multiple",
        "If there's multiple matches, forward from the youngest only if no new bytes are provided by other entries",
        false};
    Param<bool> forward_from_multiple_partial{
        "forward_from_multiple_partial", "Allow for partial forwarding from multiple entries", false};

    struct
    {
        Event schedule{"schedule", "Schedule PWC entry"};
        Event response{"response", "LSU response"};
        Event fwd_blocked{"fwd_blocked", "Forwarding to load blocked"};
        EventData<bool> write_through{"write_through", "PWC entry will write through to L2"};
        EventData<bool> prefetch_only{"prefetch_only", "Prefetch miss"};
        EventData<LSUPWCResponseAction> action{"action", "Response action"};
        EventData<LSUPWCBlockReason> block_reason{"block_reason", "Block reason"};
    } event;

public:
    using ResponseAction = LSUPWCResponseAction;
    using BlockReason = LSUPWCBlockReason;
    using pwc_iterator = Base::pwc_iterator;

    explicit LSUPWC(const sc_core::sc_module_name &name) : Base{name} {}

    struct Request
    {
        uint64_t id;
        MemReq req;
        bool write_through{false};
        bool write_allocate{false};
        bool prefetch_only{false};
        uint64_t byte_enables;
        unsigned size;

        Request(const LSUPWCEntry &entry, bool write_allocate, bool prefetch_only)
            : id(entry.id),
              req(entry.req),
              write_through(entry.write_through),
              write_allocate(write_allocate),
              prefetch_only(prefetch_only),
              byte_enables(entry.byte_enables),
              size(entry.size())
        {
        }

        bool write_through_after_hit(bool hit) const { return write_through || (!hit && !write_allocate); }
    };

    struct Response
    {
        uint64_t id;
        ResponseAction action;

        bool done() const
        {
            return action == ResponseAction::FILLED || action == ResponseAction::STORED || action == ResponseAction::L2;
        }
    };

    Out<Request> store_request{"store_request"};
    In<Response> store_response{"store_response"};
    Out<std::pair<uint64_t, PhysAddr>> store_deallocate{"store_deallocate"};

    pwc_iterator find(uint64_t id)
    {
        return std::find_if(entries.begin(), entries.end(), [=](const auto &e) { return e.id == id; });
    }

    template<util::either<VirtAddr, PhysAddr> A>
    auto find_oldest(A addr)
    {
        return std::find_if(entries.begin(), entries.end(), [=](const auto &e) { return e.line_matches(addr); });
    }

    template<util::either<VirtAddr, PhysAddr> A>
    auto find_youngest(A addr)
    {
        return std::find_if(entries.rbegin(), entries.rend(), [=](const auto &e) { return e.line_matches(addr); });
    }

    bool try_combine(MemReq req, bool write_through)
    {
        if (auto entry = find_matching_entry(req, req->ordered());
            entry != entries.rend() && write_through == entry->write_through)
        {
            combine_with(entry, req);
            return true;
        }
        return false;
    }
    const LSUPWCEntry &allocate(MemReq req, bool write_through, bool requires_combine = false);

    bool pending_store_release() const
    {
        return std::any_of(entries.begin(), entries.end(), [](const auto &e) { return e.release; });
    }

    bool can_forward() const { return allow_forwarding; }
    bool can_wakeup() const { return wakeups != LSUPWCWakeupMethod::NONE; }
    bool can_wakeup_precise() const { return wakeups == LSUPWCWakeupMethod::PRECISE; }

    template<util::either<VirtAddr, PhysAddr> A>
    std::tuple<BlockReason, uint64_t, uint64_t> matches(A addr, unsigned size) const
    {
        BlockReason blocked = BlockReason::NONE;
        bool ordered = false;
        std::optional<uint64_t> entry_id;
        uint64_t matching_mask = 0;

        for (const auto &entry : std::views::reverse(entries)) {
            auto entry_addr = [&] {
                if constexpr (std::is_same_v<A, PhysAddr>)
                    return entry.phys_addr();
                else
                    return entry.virt_addr();
            }();
            if (address_range_a_intersects_b(addr, size, entry_addr, A::LINE_SIZE, 1)) {
                if (auto mask = (entry.byte_enables & addr.line_bytemask(size)) >> addr.line_offset(); mask) {
                    bool new_bytes = (mask | matching_mask) != matching_mask;
                    if (blocked == BlockReason::NONE) {
                        if (!forward_across_ordered && ordered && new_bytes)
                            blocked = BlockReason::ORDERED;
                        else if (!forward_from_multiple_partial && new_bytes && entry_id)
                            blocked = BlockReason::MULTIPLE_PARTIAL;
                        else if (!forward_from_multiple && entry_id)
                            blocked = BlockReason::MULTIPLE;
                    }
                    entry_id = entry.id;
                    matching_mask |= mask;
                }
            }
            ordered |= entry.ordered;
        }

        if (entry_id && matching_mask)
            return {blocked, *entry_id, matching_mask};
        return {BlockReason::NONE, {}, 0ULL};
    }

    template<util::either<VirtAddr, PhysAddr> A>
    std::tuple<BlockReason, uint64_t, uint64_t> check_matches(Txn transaction, A addr, unsigned size)
    {
        auto [blocked, pwc_id, mask] = matches(addr, size);
        if (blocked != BlockReason::NONE) {
            check_line_address_match(addr);
            event.fwd_blocked(
                transaction, Base::event.pwc_id(Base::age_to_pwc_id(pwc_id)), event.block_reason(blocked));
        }

        return {blocked, pwc_id, mask};
    }

    bool should_wait_for_ofb() const { return wait_for_load_ofb; }

    void deallocate(PhysAddr addr);
    void deallocate(const pwc_iterator &entry);

    bool run() override;

private:
    void handle_responses();
    void schedule_and_request();
};

} // namespace ampere::core

/*
 * vim: sw=4:ts=4:et:si:cindent:cino=\:0,(0,g0,t0:
 */
