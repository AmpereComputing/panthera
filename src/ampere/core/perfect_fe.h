//
// Copyright (c) 2021 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//

#ifndef AMPERE_CORE_PERFECT_FE_H
#define AMPERE_CORE_PERFECT_FE_H

#include "ampere/archx/driver.h"
#include "ampere/core/flush.h"
#include "ampere/core/instruction.h"
#include "ampere/core/tlbireq.h"
#include "ampere/sc/sc.h"
#include "ampere/uarch_common/indexer.h"
#include "ampere/uarch_common/memreq.h"
#include "ampere/uarch_common/memresp.h"

namespace ampere {
namespace core {

using namespace ampere::sc;
using namespace ampere::archx;
using namespace ampere::uarch_common;

class PerfectFE : public Module, public CacheLookupInterface<PhysAddr>
{
public:
    explicit PerfectFE(const sc_core::sc_module_name& name);
    bool run() override;
    bool address_resident(PhysAddr addr) const override;
    void dump(std::ostream&) const override;

    DriverPort driver{"driver"};
    Out<Instruction> iq_write_port{"iq_write_port"};
    Out<MemReq> pte_fetch_out{"pte_fetch_out"};
    In<MemResp> pte_fetch_in{"pte_fetch_in"};
    In<TLBIReq> tlbi_request{"tlbi_request"};
    Out<uarch_common::Request::Fill> miss_request{"miss_request"};
    In<MemReq> snoop_port{"snoop_port"};
    In<MemResp> miss_response{"miss_response"};
    Read<Flush> flush_port{"flush"};
    In<Credit> instruction_credit_return{"instruction_credit_return"};
    Out<bool> pmu_idr_icache_miss{"pmu_idr_icache_miss"};
    Out<bool> pmu_idr_tlb_miss{"pmu_idr_tlb_miss"};
    Read<PhysAddr> pmu_l2_miss_in_icf{"pmu_l2_miss_in_icf"};
    Out<bool> pmu_idr_l2_miss{"pmu_idr_l2_miss"};
    Out<bool> pmu_idr_feq_empty{"pmu_idr_feq_empty"};
    Out<bool> pmu_idr_mem_stall{"pmu_idr_mem_stall"};
    Out<bool> pmu_idr_btc_clear{"pmu_idr_btc_clear"};
    Out<bool> pmu_idr_btb_clear{"pmu_idr_btb_clear"};

    In<Uop> bob_alloc{"bob_alloc"};
    Out<Credit> bob_credit_return{"bob_credit_return"};
    In<uint> branches_committed{"branches_committed"};
    In<uint> instructions_committed{"instructions_committed"};
    In<Credit> feq_credit_return{"feq_credit_return"};
    In<uint> frontend_stall{"frontend_stall"};

private:
    Param<int> fetch_alignment_bits{"fetch_alignment_bits",
                                    "only write instructions aligned to this many bits to IQ in a cycle "
                                    "(5=half-cacheline, 6=full-cacheline, default of 0 disables alignment restriction)",
                                    0};
    Param<int> taken_branches_per_cycle{
        "taken_branches_per_cycle",
        "Stop fetching instructions after seeing this many taken branches in a cycle (default of 0 disables limit)",
        0};
    Param<int> branch_commit_limit{
        "branch_commit_limit",
        "Limit the number of branches which can be committed per cycle (default of 0 disables limit)",
        0};

    Event iq_write_event{"iq_write", "Instruction queue write"};
    Event pmu_event{"pmu", "A PMU event has occurred"};
    EventData<PMU> pmu_num_data{"event", "PMU event number"};

    EventData<FlushType> flush_type_data{"flush_type", "Branch check flush type"};
    EventData<ProgramCounter> br_target{"br_target", "Branch target"};
    EventData<int> br_type{"br_type", "Branch type"};

    EventData<uint32_t> inst_bytes{"inst_bytes", "raw instruction bytes"};
    EventData<archx::inum_t> inst_id_data{"instruction_id", "Instruction ID"};

    Instruction fetch_instruction();
    void iq_stage();
    void handle_bob();
    void flush_bob_entries(std::uint64_t alloc_id);
    void handle_flush();
    void tlb_invalidation();
    void reclaim_credits();
    void cache_snoop();
    void read_throttling_counters();

    std::map<uint64_t, Uop> bob;
    CreditPool instruction_fifo{"instruction_fifo"};

    FetchId current_fetch_id{1};
    ProgramCounter last_fetched_pc{0};
    uint64_t pending_commits{0};
};

} // namespace core
} // namespace ampere

#endif

/*
 * vim: sw=4:ts=4:et:si:cindent:cino=\:0,(0,g0,t0:
 */
