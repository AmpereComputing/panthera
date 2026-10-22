//
// Copyright (c) 2018-2020 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//

#pragma once

#include "ampere/archx/driver.h"
#include "ampere/archx/sysreg.h"
#include "ampere/archx/bitfield.h"
#include "ampere/archx/pmu.h"
#include "ampere/core/mmu/mru.h"
#include "ampere/core/mmu/req.h"
#include "ampere/core/mmu/ack.h"
#include "ampere/core/mmu/resp.h"
#include "ampere/core/mmu/otb.h"
#include "ampere/core/mmu/op.h"
#include "ampere/core/mmu/l2tlb.h"
#include "ampere/core/tlbireq.h"
#include "ampere/core/mmu/tpc.h"
#include "ampere/core/flush.h"
#include "ampere/sc/sc.h"
#include "ampere/uarch_common/cache.h"
#include "ampere/uarch_common/memreq.h"
#include "ampere/uarch_common/memresp.h"

namespace ampere::core {

using namespace ampere::sc;
using namespace ampere::archx;
using namespace ampere::uarch_common;

class MMU : public Module
{
    Param<size_t> lookup_pipe_latency{"lookup_pipe_latency", "length of TLB lookup pipeline", 3};
    Param<size_t> s1tpc_pipe_latency{
        "s1tpc_pipe_latency", "length of stage 1 translation path cache lookup pipeline", 3};
    Param<size_t> walk_pipe_latency{"walk_pipe_latency", "length of table walk pipeline, counting WalkFSM", 6};
    Param<bool> force_granule_pages{
        "force_granule_pages", "Force granule-sized pages, even if pages are larger", false};
    Param<bool> separate_mru{
        "separate_mru", "Use different PSMRUs for instructions and data, only has effect with unified_mmu", true};
    Param<bool> enable_invalidation_block{
        "enable_invalidation_block", "Multi-set invalidations walk the TLB one set per cycle, blocking lookups", true};
    Param<bool> enable_per_cycle_pmu{"enable_per_cycle_pmu", "Output the PERCYC events (slows down panthera)", false};
    Param<bool> enable_contiguous_bit{
        "enable_contiguous_bit",
        "Last-level PTEs with the contiguous bit set will be cached with a larger page size",
        false};
    Param<bool> allocation_fairness{
        "allocation_fairness", "Process requests from sources recently allocated last", false};
    Param<uint64_t> phyaddroffset{"phyaddroffset", "Offset physical address after translation", 0};

    CreditPool walk_fsms{"walk_fsms"};

public:
    explicit MMU(const sc_core::sc_module_name &name);
    MMU(const sc_core::sc_module_name &name, unsigned load_pipes, unsigned sta_pipes);
    bool run() override;

    // external ports
    DriverPort driver_port;
    Out<MemReq> pte_fetch_out{"pte_fetch_out"};
    In<MemResp> pte_fetch_in{"pte_fetch_in"};
    In<TLBIReq> tlbi_request_in{"tlbi_request_in"};
    Read<Flush> flush_port{"flush_port"};
    OutOpt<bool> mmu_can_accept{"mmu_can_accept"};

    // First interface to ICF
    InOpt<MMUReq> inst_mmu_request{"inst_mmu_request"};
    OutOpt<MMUAck> inst_mmu_ack{"inst_mmu_ack"};
    OutOpt<MMUResp> inst_mmu_response{"inst_mmu_response"};
    OutOpt<TLBIReq> inst_tlbi_request_out{"inst_tlbi_request_out"};

    // Second interface to LSU
    InOpt<MMUReq> data_mmu_request{"data_mmu_request"};
    OutOpt<MMUAck> data_mmu_ack{"data_mmu_ack"};
    OutOpt<MMUResp> data_mmu_response{"data_mmu_response"};
    OutOpt<TLBIReq> data_tlbi_request_out{"data_tlbi_request_out"};

private:
    using MRU = MMUMRU;
    using MRUCopy = MMUMRUCopy;

    Event tlb_access_event{"tlb_access", "TLB lookup initiated"};
    Event tlb_invalidate_event{"tlb_invalidate", "TLB invalidation received"};
    EventData<bool> hit_data{"hit", "TLB hit"};

    Event table_walk_init_event{"table_walk_init", "TLB lookup was a miss, beginning walk"};
    Event table_walk_complete_event{"table_walk_complete", "TLB lookup completed"};
    Event contiguous_fill_event{"contiguous_fill", "Page walk resulted in page with contiguous bit set"};

    Event pte_fetch_event{"pte_fetch", "Fetching PTE"};
    Event pte_fetched_event{"pte_fetched", "PTE fetch returned"};
    EventData<unsigned> ev_level{"level", "Current level of table walk"};

    Event flush_event{"flush", "MMU received a NO_RESTART flush"};
    Event fault_event{"memory_fault", "MMU fault"};
    Event mmu_busy_event{"mmu_busy", "MMU is not idle"};

    Event ev_pmu{"pmu", "A PMU event has occurred"};
    EventData<PMU> ev_num{"event", "PMU event number"};

    // internal ports
    OutIn<uint64_t> tlb_lookup{"tlb_lookup"};
    OutIn<uint64_t> s1tpc_lookup{"s1tpc_lookup"};
    OutIn<uint64_t> table_walk{"table_walk"};

    MRU dmru, imru;
    OTB otb;
    L2TLB tlb{"tlb"};
    TPC s1tpc{"s1tpc"};
    std::deque<uint64_t> inst_response_queue;
    std::deque<uint64_t> data_response_queue;

    bool invalidation_executing{false};
    Cycle invalidation_block{0};
    std::queue<TLBIReq> tlb_invalidations;

    bool unified_mmu{false};

    OTB::ScheduleInfo lookup_candidate;
    OTB::ScheduleInfo walk_candidate;

    std::vector<MMUReq> requests;
    std::vector<unsigned> allocation_priority;

    /* Page walk handling */
    void init_walk_fsm(OTBEntry &entry);
    bool check_walk_done(OTBEntry &entry);
    bool do_walk_stage(OTBEntry &entry);

    MRU &select_mru(bool is_instruction) { return is_instruction && separate_mru ? imru : dmru; }

    void generate_response(OTBEntry &entry);
    void do_s1tpc_lookups();
    void do_tlb_lookups();
    void do_invalidation();
    void handle_flush();
    void do_walks();

    void accept_requests(InOpt<MMUReq> &mmu_request, OutOpt<MMUAck> &mmu_ack);
    void process_requests();
    bool process_request(const MMUReq &mmureq, OutOpt<MMUAck> &mmu_ack);
    void accept_invalidate_requests();
    void accept_pte_responses();

    bool schedule_tlb_lookup(OTBEntry &otbentry);
    void schedule_tlb_lookups();
    void schedule_tlb_invalidations();
    bool schedule_walk(OTBEntry &otbentry);
    void schedule_walks();

    void send_pte_fetches();
    void send_responses(std::deque<uint64_t> &response_queue, OutOpt<MMUResp> &mmu_response);

    void collect_pmu_data();

    void dump(std::ostream &) const override;

    void end_of_elaboration() override;

    TranslationContext context(const MMUReq &req) const;
    Translation translation(const OTBEntry &otb_entry) const;
};

} // namespace ampere::core

/*
 * vim: sw=4:ts=4:et:si:cindent:cino=\:0,(0,g0,t0:
 */
