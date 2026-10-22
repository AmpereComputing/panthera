//
// Copyright (c) 2018-2023 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//

#pragma once

#include <map>
#include <optional>
#include <cstdint>

#include "ampere/sc/sc.h"
#include "ampere/archx/addr.h"
#include "ampere/uarch_common/memop.h"
#include "ampere/core/backend_prefetcher.h"

make_ns_enum(ampere::core, POBState,
    WAIT_OTB_ACK,
    WAIT_TRANSLATION,
    OTB_FULL,
    FAULT,
    WAIT_OFB_FULL,
    READY
);

namespace ampere::core {

using namespace ampere::sc;
using namespace ampere::archx;
using namespace ampere::uarch_common;

class POB : public sc::Module
{
    struct Entry
    {
        VirtAddr virt_addr{0}; // prefetch candidate address -> MANDATORY FIELD
        Uop trigger_uop; // book-keeping to keep track of the trigger uop later when we replay the prefetch candidate
        sc::Txn parent_txn;
        uint64_t mmu_req_id{0};        // tracks entry between allocation and MMU ACK
        ProgramCounter consumer_pc{0}; // Not actually present in RTL -- for use in L2 replacement predictors
        std::optional<POBState>
            status; // status of POB entry in terms of where in the page fetch pipeline it is -> optional field
        Cycle alloc_cycle{0};           // cycle at which this Entry was allocated -> optional, only for Panthera debug
        std::optional<unsigned> otb_id; // MANDATORY FIELD: set to be equal to the returning OTB ID when ACK is received
                                        // from MMU. Multiple prefetch vaddrs may have same otb id because OTB combines
                                        // page requests for same page addresses.
        PrefetchType type{PrefetchType::NONE}; // prefetch addr is from Gretch or PCP
        MemOp::PrefetchType l2_prefetch_type{MemOp::PrefetchType::NONE};

        Entry() = default;
        Entry(VirtAddr addr,
              Uop u,
              sc::Txn(parent),
              uint64_t mmu_req_id,
              ProgramCounter pc,
              POBState state,
              Cycle c,
              PrefetchType type,
              MemOp::PrefetchType l2_type)
            : virt_addr(addr),
              trigger_uop(u),
              parent_txn(parent),
              mmu_req_id(mmu_req_id),
              consumer_pc(pc),
              status(state),
              alloc_cycle(c),
              type{type},
              l2_prefetch_type{l2_type}
        {
        }

        bool can_replay(archx::EL current_el) const
        {
            return status == POBState::READY && (current_el == archx::EL0 || type != PrefetchType::GRETCH);
        }
    };

    std::deque<Entry> entries;

public:
    explicit POB(const sc_core::sc_module_name& name);

    const POB::Entry* replay_ready(archx::EL current_el) const;
    std::optional<POB::Entry> replay(archx::EL current_el);

    bool can_allocate() const { return entries.size() < size; }
    void allocate(L1PrefetchEntry prefetch, PrefetchType type, uint64_t mmu_req_id, POBState state);
    bool try_allocate(L1PrefetchEntry prefetch, PrefetchType type, uint64_t mmu_req_id, POBState state);
    bool empty() const { return entries.empty(); }
    void flush() { entries.clear(); }
    void el_change(archx::EL old_el, archx::EL new_el);
    void ofb_dealloc();

    void handle_mmu_response(uint64_t id, bool fault);
    void handle_mmu_ack(uint64_t req_id, std::optional<uint64_t> otb_id);

    void dump(std::ostream& os) const override;

private:
    Param<size_t> size{"size", "number of entries in prefetch outstanding buffer", 12};
    Param<bool> flush_on_el_change{"flush_on_el_change", "Flush all entries whenever EL changes", false};
    Param<bool> allocate_on_el0_only{"allocate_on_el0_only", "Only allocate POB entries in EL0", false};

    struct
    {
        Event insert{
            "insert",
            "new POB entry allocated or prefetch vaddr is combined with an existing POB entry during deduplication"};
        Event dropped_full{"dropped_full", "prefetch candidate is dropped because of no space in POB"};
        Event dropped_mmu_noack{"dropped_mmu_noack", "POB entry is dropped because OTB full ACK is received from MMU"};
        Event dropped_fault{"dropped_fault", "POB entry is dropped because MMUResponse faults"};
        Event dropped_duplicate{"dropped_duplicate",
                                "Prefetch was dropped because there is already a POB entry for this line"};
        Event dropped_el{"dropped_el", "Prefetch was dropped due to EL1+"};
        Event promote_replay{"promote_replay", "prefetch candidate in POB ready for replay"};
        Event replay{"replay", "Prefetch is replayed out of the POB"};
    } event;
};

} // namespace ampere::core

/*
 * vim: sw=4:ts=4:et:si:cindent:cino=\:0,(0,g0,t0:
 */
