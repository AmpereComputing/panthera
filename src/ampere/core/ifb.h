//
// Copyright (c) 2021-2023 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//

#pragma once

#include "ampere/core/fetch.h"
#include "ampere/core/flush.h"
#include "ampere/sc/sc.h"
#include "ampere/uarch_common/memreq.h"
#include "ampere/uarch_common/memresp.h"
#include "ampere/uarch_common/indexer.h"
#include "ampere/util/enum.h"

make_ns_enum(ampere::core, IFBSnoopPolicy,
    never, // Never restart
    hit_plus_one_on_ifb_hit, // Furiosa default: on IFB hit, restart on hit FEQ + 1

    // Note: All IFB snoop-restart policies above `restart_oldest_on_any_hit` are
    // functionally incorrect!
    restart_oldest_on_any_hit, // Upon snoop hit in either IFB or I$, if any IFB entries, always restart from oldest FEQ entry
    always_restart_oldest // If any IFB entries, always restart from oldest FEQ entry
);

namespace ampere::core {

using namespace ampere::sc;
using namespace ampere::archx;
using namespace ampere::uarch_common;

const size_t IFB_COLORS = 2;

class IFB : public Module
{
public:
    explicit IFB(const sc_core::sc_module_name& name);
    void dump(std::ostream& os) const override;

    Param<bool> reclaim_cleared_on_hit{"reclaim_cleared_on_hit", "A hit on a cleared IFB entry un-clears it", false};
    Param<size_t> max_tracked_hit_ways{
        "max_tracked_hit_ways", "The maximum number of tracked hit ways per IFB entry", 2};
    Param<size_t> max_active_entries{
        "max_active_entries", "The maximum allowed number of active (non-cleared) IFB entries", 0};
    Param<bool> enable_speculative_interface{
        "enable_speculative_interface", "Disable the speculative interface to the L2C", true};
    Param<bool> delay_nonspeculative_requests{
        "delay_nonspeculative_requests", "Delay non-speculative requests by 1 cycle", true};

    Param<IFBSnoopPolicy> snoop_policy{"snoop_policy", "Policy for re-starting snoops", "never"};

    Event ev_allocate{"allocate", "IFB entry allocated"};
    Event ev_deallocate{"deallocate", "IFB entry deallocated"};
    Event ev_reclaim_cleared{"reclaimed_cleared", "IFB entry reclaimed after being cleared"};
    Event ev_smart_color_switch{"smart_color_switch",
                                "IFB did not switch colors after flush because the current color has more credits"};
    EventData<bool> ev_cleared{"cleared", "IFB entry was cleared by a flush"};
    EventData<uint64_t> ev_phys_line{"phys_line", "Physical line address"};
    Event ev_snoop{"snoop", "IFB received snoop"};
    Event ev_snoop_hit{"snoop_hit", "IFB received snoop"};

    std::optional<FetchId> snoop(PhysAddr pa, bool icache_snoop_hit);
    bool requesting_other_line_same_l2_set(PhysAddr pa);
    bool was_snooped(std::uint64_t id);
    std::pair<bool, bool> lookup(Fetch& fetch, PhysAddr pa);
    bool is_complete(PhysAddr pa);
    bool is_cleared(PhysAddr pa);
    bool is_unclearable(PhysAddr pa);
    size_t num_active();
    size_t num_unsent();
    void allocate(std::uint64_t id,
                  FetchId fetch_id,
                  PhysAddr pa,
                  PageSize::Size page_size,
                  bool prefetch = false,
                  bool allocate_completed = false);
    void deallocate(std::uint64_t id);
    void flush();
    std::pair<bool, MemReq> schedule_request(); // returns is_a_prefetch and the address
    void age_requests();
    void l2c_scheduled_request(std::uint64_t id);
    std::tuple<bool, bool, bool> handle_response(std::uint64_t id);
    void clear_all();
    void clear_from_fetch_id(FetchId clearing_fetch_id);
    void erase_cleared_entries();
    bool can_allocate();
    bool upgrade_prefetch(std::uint64_t id, FetchId fetch_id);
    bool can_record_younger_hit(FetchId fetch_id, PhysAddr pa, size_t num_sets);
    void record_younger_hit(FetchId fetch_id, PhysAddr pa, size_t num_sets);

    Indexer<PhysAddr> l2_indexer{"l2_indexer"};

private:
    struct Entry
    {
        Entry(FetchId& fetch_id,
              PhysAddr pa,
              PageSize::Size page_size,
              std::optional<Credit>&& credit_0,
              std::optional<Credit>&& credit_1,
              Cycle request_cycle,
              bool prefetch = false)
            : fetch_id(fetch_id),
              pa(pa),
              page_size(page_size),
              credit{std::move(credit_0), std::move(credit_1)},
              request_cycle{request_cycle},
              prefetch{prefetch}
        {
        }
        FetchId fetch_id;
        std::optional<uint64_t> response_id;
        PhysAddr pa;
        PageSize::Size page_size;
        std::array<std::optional<Credit>, IFB_COLORS> credit;
        Credit l2c_fifo_credit;
        Cycle request_cycle;
        bool complete{false};
        bool sent{false};
        bool cleared{false};
        bool unclearable{false};
        bool prefetch{false};
        bool snoop_hit{false};
        std::map<FetchId, PhysAddr> younger_hits;
    };

    std::map<std::uint64_t, Entry> buffer;
    sc_core::sc_vector<CreditPool> credits{"credits", IFB_COLORS};
    CreditPool l2c_fifo{"l2c_fifo"};
    int color{0};
    std::optional<std::uint64_t> just_allocated;
    bool may_use_speculative_interface{false};
};

} // namespace ampere::core
