//
// Copyright (c) 2018-2024 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//

#pragma once

#include "ampere/archx/driver.h"
#include "ampere/archx/flush_type.h"
#include "ampere/core/coherency_validator.h"
#include "ampere/core/fetch.h"
#include "ampere/core/flush.h"
#include "ampere/core/ifb.h"
#include "ampere/core/ifb_victim_cache.h"
#include "ampere/core/instruction.h"
#include "ampere/core/l1tlb.h"
#include "ampere/core/loop_restart.h"
#include "ampere/core/mmu/ack.h"
#include "ampere/core/mmu/req.h"
#include "ampere/core/mmu/resp.h"
#include "ampere/core/prediction.h"
#include "ampere/core/tlb.h"
#include "ampere/core/tlbireq.h"
#include "ampere/sc/sc.h"
#include "ampere/uarch_common/cache.h"
#include "ampere/uarch_common/indexer.h"
#include "ampere/uarch_common/memreq.h"
#include "ampere/uarch_common/memresp.h"

namespace ampere::core {

using namespace ampere::sc;
using namespace ampere::archx;
using namespace ampere::uarch_common;

class L0Candidates : public Cache<PhysAddr, size_t>
{
public:
    L0Candidates(const sc_core::sc_module_name& name) : Cache(name) {}

    Param<size_t> required_lookups{
        "required_lookups",
        "required number of previous lookups to be considered a candidate for installing in the L0",
        0};

    /*
     * Record an IL0/IL1 access in the "L0 candidates" table, returning true if
     * that line should be installed into L0 (if not already present) and false
     * otherwise.
     */
    bool check_candidate(Txn transaction, PhysAddr pa)
    {
        auto res = lookup(transaction, pa);
        unsigned previous_lookups = 0;
        if (res) {
            previous_lookups = *res;
            if (previous_lookups < required_lookups)
                update_value(transaction, pa, previous_lookups + 1);
        } else {
            refill(transaction, pa, 1);
        }
        return previous_lookups >= required_lookups;
    }
};

class ICF : public Module, public CacheLookupInterface<PhysAddr>
{
public:
    explicit ICF(const sc_core::sc_module_name& name);
    bool run() override;
    void end_of_elaboration() override;
    bool address_resident(PhysAddr addr) const override;
    void dump(std::ostream& os) const override;

    DriverPort driver_port;
    In<Fetch> fetch_port{"fsd"};
    In<Fetch> bp_clear_port{"bp_clear"};
    In<FetchId> ittage_stall_port{"ittage_stall"};
    Out<Instruction> iq_write_port{"iq_write_port"};
    Out<MMUReq> mmu_request{"mmu_request"};
    In<MMUAck> mmu_ack{"mmu_ack"};
    In<MMUResp> mmu_response{"mmu_response"};
    In<TLBIReq> tlbi_request{"tlbi_request"};
    Out<uarch_common::Request::Fill> miss_request{"miss_request"};
    In<MemReq> miss_request_scheduled{"miss_request_scheduled"};
    In<MemReq> snoop_port{"snoop_port"};
    In<MemResp> miss_response{"miss_response"};
    Indexer<PhysAddr> l2_indexer{"l2_indexer"};

    CoherencyValidator<PhysAddr> coherency_validator{"coherency_validator"};
    Read<Flush> flush_port{"flush"};
    In<Credit> instruction_credit_return{"instruction_credit_return"};
    Out<Flush> br_check_flush_port{"br_check_flush_port"};
    Out<Fetch> loop_fetch_port{
        "loop_fetch"}; // send fake fetches created by ICF as a way to keep BPU history updated while looping
    Out<LoopRestart> loop_restart{"loop_restart"};
    Out<bool> pmu_idr_icache_miss{"pmu_idr_icache_miss"};
    Out<bool> pmu_idr_tlb_miss{"pmu_idr_tlb_miss"};
    Read<PhysAddr> pmu_l2_miss_in_icf{"pmu_l2_miss_in_icf"};
    Out<bool> pmu_idr_l2_miss{"pmu_idr_l2_miss"};
    Out<bool> pmu_idr_feq_empty{"pmu_idr_feq_empty"};
    Out<bool> pmu_idr_mem_stall{"pmu_idr_mem_stall"};
    Out<bool> pmu_idr_btc_clear{"pmu_idr_btc_clear"};
    Out<bool> pmu_idr_btb_clear{"pmu_idr_btb_clear"};
    Out<uint> icache_misses{"icache_misses"};

private:
    struct CacheLine
    {
        CacheLine() : addr(0), demand(false), prefetch(false) {}
        CacheLine(PhysAddr addr, bool demand, bool prefetch) : addr{addr}, demand{demand}, prefetch{prefetch} {}
        PhysAddr addr;
        bool demand;
        bool prefetch;
    };

    struct PrefetchLine
    {
        PrefetchLine() : fetch(), complete(false), lookup(false), dealloc_txid(0) {}
        PrefetchLine(Fetch fetch) : fetch{fetch}, complete{false}, lookup{false}, dealloc_txid{0}
        {
            fetch->fetch_id = 0; // Prefetch fetches must have a fetch_id of 0 unless it was upgraded to a demand fetch.
        }
        Fetch fetch;
        bool complete;
        bool lookup;
        std::uint64_t dealloc_txid;
    };

    using FEQ = std::map<FetchId, Fetch>;

    using ITLBAckBuffer = std::map<std::uint64_t, Fetch>;
    using ITLBMissBuffer = std::multimap<std::uint64_t, Fetch>;
    using PrefetchRequestQueue = std::list<PrefetchLine>;

    ZeroDelayOutIn<bool> resume_fetch_after_flush{"resume_fetch_after_flush"};

    Param<bool> half_cacheline_fetch{"half_cacheline_fetch", "ICF fetches half cache lines", true};
    Param<bool> split_fetches_across_cycles{
        "split_fetches_across_cycles", "Split writing fetches into IQ across two cycles", true};
    Param<uint64_t> iq_write_max_instrs{
        "iq_write_max_instrs", "The maximum number of instructions written to IQ per cycle", 8};
    Param<bool> enable_l0_cache{"enable_l0_cache", "Enable small l0 cache to save power", false};
    Param<bool> use_l0_candidates_table{
        "use_l0_candidates_table", "Require filtering fills to L0 through candidates table", false};
    Param<bool> clear_l0_on_flush{"clear_l0_on_flush", "Clear IL0 on every flush", false};
    Param<bool> enable_coherency_validation{
        "enable_coherency_validation", "Send ICF hits to the L2 to validate L2 inclusivity is maintained", false};
    Param<bool> trinity_repipeline{
        "trinity_repipeline",
        "Move ID pipeline one cycle later relative to IT, but move IT pipeline one cycle earlier relative to "
        "IP/prediction upon GPC flush (where it stays until the first frontend clear)",
        false};
    Param<uint64_t> stall_cycles_after_flush{
        "stall_cycles_after_flush", "ICF fetch delay (in cycles) post GPC flush", 0};

    Param<size_t> tlbi_stall_latency{"tlbi_stall_latency", "number of cycles for which to stall fetch on TLBI", 16};
    Param<size_t> cache_fill_lookup_stall{
        "cache_fill_lookup_stall", "number of cycles for which to stall new lookups on icache fill", 1};
    Param<bool> ignore_itlb_pa{
        "ignore_itlb_pa", "Ignore the PA returned by the ITLB (useful with perfect TLBs)", false};

    Param<size_t> fifo_free_watermark{
        "fifo_free_watermark", "Stall ICF until this many entries in IQ are free (0 disables)", 0};
    Param<size_t> loop_start_lockout{"loop_start_lockout", "number of cycles to stall for when entering loop mode", 0};
    Param<bool> fake_evict_at_miss{
        "fake_evict_at_miss", "Approximate evict-at-miss by filling and then invalidating cleared IFBs", true};
    Param<bool> use_ifb_victim_cache{
        "use_ifb_victim_cache",
        "Keep a victim cache of cleared L2C responses in case they become useful again soon",
        false};
    Param<size_t> icache_bank_bits{
        "icache_bank_bits",
        "Number of bits of the VA to use to 'bank' the icache (0 disables banking, 1 means 2 banks, 2 means 4 banks)",
        0};
    Param<bool> enable_per_cycle_pmu{"enable_per_cycle_pmu", "Output the PERCYC events (slows down panthera)", false};
    Param<bool> l1i_access_pmus_only_once{
        "l1i_access_pmus_only_once", "Only count the L1I_CACHE and L1I_CACHE_RD events once, even on miss", false};
    Param<uint64_t> min_credits_for_iq_write{
        "min_credits_for_iq_write", "Minimum number of credits needed to write any instructions into IQ", 8};

    Event feq_read_event{"feq_read", "Front End Queue read"};
    Event iq_write_event{"iq_write", "Instruction queue write"};
    Event fetch_cleared_event{"fetch_cleared", "Fetch cleared by branch clear or branch check"};
    Event btb_clear_event{"btb_clear", "Branch clear from BPU to ICF"};
    EventData<ClearType> btb_clear_type{"clear_type", "BTB clear type"};
    Event il1_miss_latency{"il1_miss_latency", "Latency (cycles) of IL1 misses"};
    Event branch_check_event{"btc_clear", "Branch check failed"};
    EventData<uint64_t> ev_offset{"offset", "Instruction offset in line"};
    Event itlb_faulted_event{"itlb_faulted", "ITLB faulted"};
    Event ifb_hit_event{"ifb_hit", "Cache miss hit an existing IFB entry"};
    EventData<bool> ev_cleared{"cleared", "IFB entry was cleared by a flush"};
    Event ifb_victim_cache_hit_event{"ifb_victim_cache_hit", "Cache miss hit an existing IFB victim cache entry"};
    Event iq_watermark_stall_event{"iq_watermark_stall",
                                   "IQ is not full but ICF is still stalled waiting for more free entries"};
    Event ev_no_clear_l1_bits{"no_clear_l1_bits",
                              "Two L2 requests to same L2 set, do not clear L1I-present bits in L2"};

    Event loop_collect_event{"loop_collect", "Collected instruction into IQ loop buffer"};
    Event loop_abort_event{"loop_abort", "Abort collecting instructions into IQ loop buffer"};
    Event loop_replay_event{"loop_replay", "Fetching out of IQ loop buffer this cycle instead of from FEQ"};
    Event loop_replay_predicted_event{"loop_replay_predicted",
                                      "Fetching out of IQ loop buffer this cycle instead of from FEQ"};
    EventData<size_t> ev_loop_iterations{"loop_predicted_iterations", "Number of total iterations in this loop"};
    EventData<size_t> ev_restart_iterations{"loop_start_iterations",
                                            "Number of iterations in this loop before we should restart the BPU"};

    Event ev_ifb_hit_cleared{"ifb_hit_cleared",
                             "Fetch hit an in-progress IFB that had been cleared by an earlier flush"};
    Event ev_ifb_stall{"ifb_stall", "Fetch is stalled waiting for a cleared IFB to complete"};

    Event pmu_event{"pmu", "A PMU event has occurred"};
    EventData<PMU> pmu_num_data{"event", "PMU event number"};

    EventData<FlushType> flush_type_data{"flush_type", "Branch check flush type"};
    EventData<ProgramCounter> br_target{"br_target", "Branch target"};
    EventData<ProgramCounter> br_pc{"br_pc", "Branch PC"};
    EventData<BranchType> br_type{"br_type", "Branch type"};

    Event snoop_event{"snoop", "ICF got a snoop from L2C"};
    Event snoop_restart_event{"snoop_restart", "ICF restarted upon a snoop from L2C"};
    Event snoop_inval_event{"snoop_inval", "L1I line invalidated due to snoop"};
    EventData<uint64_t> ev_phys_line{"phys_line", "Physical line address"};

    EventData<uint32_t> inst_bytes{"inst_bytes", "raw instruction bytes"};
    EventData<archx::inum_t> inst_id_data{"instruction_id", "Instruction ID"};

    EventData<CycleDelta> miss_latency{"miss_latency", "cache miss latency in cycles"};
    Event ev_wrong_path{"wrong_path", "Wrote wrong path instruction to IQ this cycle"};

    void translate_fetch_pc(Fetch& fetch, PhysAddr phys_addr, PageSize page_size);
    bool tlb_lookup(Fetch& fetch);
    void tlb_miss(Fetch& fetch);
    void tlb_fill(Fetch& fetch, MMUResp mmuresp);
    void accept_tlb_miss_acks();
    void accept_tlb_miss_returns();
    void accept_tlb_invalidations();
    void invalidate_tlb(TLBIReq tlbi);

    void accept_cache_miss_returns();
    void accept_cache_snoops();
    void cache_snoop(MemReq snoop_req);
    void cache_refill(Fetch& fetch, bool ifb_was_snooped);
    bool cache_lookup(Fetch& fetch);
    bool ifb_track_cache_hit(Fetch& fetch);
    bool ifb_lookup_allocation(Fetch& fetch);
    void cache_miss_return();
    void write_feq();

    Instruction fetch_instruction(Fetch& fetch);
    bool calc_can_write_iq();
    void read_ittage_stall();
    uint64_t write_queue(Fetch& fetch, uint64_t max_write_instrs);
    void collect_loop_fetches(Fetch& fetch, bool branch_check_exit);
    void loop_mode_fetch();

    bool recently_snooped(PhysAddr addr);
    void ifb_scheduler();
    void pmu_events();

    void flush();
    void flush_tag_pipe();
    void partial_flush_pipe(FetchId clearing_fetch_id, bool abort_looping = true);
    void partial_restart_pipe(FetchId youngest_surviving);
    void restart_feq_from_fetch_id(FetchId youngest_surviving);
    void clear_feq_from_fetch_id(FetchId clearing_fetch_id, bool abort_looping);
    template<class MB>
    void clear_tlb_misses_from_fetch_id(MB& buffer, FetchId clearing_fetch_id);
    void remove_otb_id_from_tlb_response_queue(uint64_t otb_id);
    void clear_pipes_from_fetch_id(FetchId clearing_fetch_id, bool invalidate = false);
    void reclaim_credits();
    void generate_branch_check_flush(Fetch& fetch, Instruction& instruction, FlushType flush_type);
    bool branch_check_flush(Fetch& fetch, Instruction& instruction);
    void read_fetch_port();

    std::optional<uint64_t> get_bank(ProgramCounter pc, std::set<uint64_t>& banks);

    CreditPool instruction_fifo{"instruction_fifo"};
    bool can_write_iq{true};

    Cache<PhysAddr, CacheLine> cache{"cache"};
    Cache<PhysAddr, CacheLine> l0{"l0"};
    L0Candidates l0_candidates{"l0_candidates"};

    L1TLB<ProgramCounter, TLBType::INSTRUCTION> itlb{"itlb"};
    IFB ifb{"ifb"};
    IFBVictimCache ifb_victim_cache{"ifb_victim_cache"};
    FEQ feq;
    ITLBAckBuffer unacked_itlb_misses; // indexed by MMU request ID (same as fetch ID)
    ITLBMissBuffer itlb_misses;        // indexed by OTB ID
    std::deque<TLBIReq> tlbi_queue;
    std::deque<MMUResp> tlb_response_queue;
    std::deque<MemReq> snoop_fifo;
    bool tlb_fill_this_cycle{false};
    bool iq_write_stall_this_cycle{false};
    bool icf_inst_fifo_full_this_cycle{false};
    std::set<uint64_t> tag_banks_this_cycle;
    std::set<uint64_t> data_banks_this_cycle;
    std::optional<Fetch> ifb_younger_hit_stall;
    bool ifb_allocation_stall{false};
    std::optional<FetchId> ittage_stall;

    std::vector<Fetch> loop_fetch;
    FetchId highest_icf_fetch_id{0};
    FetchId highest_looped_fetch_id{0};
    size_t loop_fetch_index{0};
    bool loop_collect{false};
    bool loop_replay{false};
    std::optional<size_t> loop_restart_iterations;
    std::optional<size_t> loop_predicted_iterations;
    size_t loop_iterations_seen{0};
    size_t loop_lockout{0};
    Cycle itlb_lockout_end_cycle{0};
    Cycle icache_lookup_lockout_end_cycle{0};

    // whether we are actively restarting a loop. If this is true, we should
    // not write instructions from the FEQ into the IQ, even if they are ready
    bool restarting_after_loop{false};

    bool itlb_faulted{false};
    bool invalidating_tlb{false};
    std::list<std::pair<PhysAddr, Cycle>> snooped_lines;

    FetchId read_fetch{0};
    std::deque<Fetch> next_fetches;

    bool between_flush_and_first_clear{true};
    bool waiting_for_fifo_drain{false};
    size_t instructions_written_this_cycle{0};
    bool wrong_path_this_cycle{false};
    bool can_fetch{true};

    enum OpType {
        LOOKUP,
        TLB_FILL,
        CACHE_FILL,
        TLB_INVALIDATION,
        CACHE_SNOOP
    };
    struct TagOpObj : public sc::Transaction
    {
        explicit TagOpObj(Fetch fetch, uint64_t bank, bool fill = false)
            : type{fill ? CACHE_FILL : LOOKUP}, bank{bank}, fetch{fetch}
        {
        }
        explicit TagOpObj(TLBIReq tlbi) : type{TLB_INVALIDATION}, tlbi{tlbi} {}
        explicit TagOpObj(MemReq memreq, uint64_t bank)
            : type{CACHE_SNOOP}, stall_cycles_remaining{2}, bank{bank}, snoop{memreq}
        {
        }
        explicit TagOpObj(Fetch fetch, uint64_t bank, MMUResp mmuresp)
            : type{TLB_FILL}, bank{bank}, fetch{fetch}, tlb_fill{mmuresp}
        {
        }

        OpType type;
        bool itlb_miss{false};
        bool icache_miss{false};

        // Track the number of cycles of IT pipeline stalls remaining to
        // approximate snoops, which block the pipeline for a total of 3 cycles
        // (2 in addition to the cycle in which they are scheduled, hence the 2
        // in the MemReq-based constructor for TagOpObj above)
        size_t stall_cycles_remaining{0};

        std::optional<uint64_t> bank{std::nullopt};
        std::optional<Fetch> fetch{std::nullopt};
        std::optional<TLBIReq> tlbi{std::nullopt};
        std::optional<MemReq> snoop{std::nullopt};
        std::optional<MMUResp> tlb_fill{std::nullopt};

        std::string to_string()
        {
            switch (type) {
            case LOOKUP:
                return fmt::format("LOOKUP for Fetch Id {}", (*fetch)->fetch_id);
            case TLB_FILL:
                return fmt::format("TLB_FILL for Fetch Id {}", (*fetch)->fetch_id);
            case CACHE_FILL:
                return fmt::format("CACHE_FILL for Fetch Id {}", (*fetch)->fetch_id);
            case TLB_INVALIDATION:
                return "TLB_INVALIDATION";
            case CACHE_SNOOP:
                return "CACHE_SNOOP";
            default:
                AMPERE_FATAL("Unexpected OpType enum value in TagOp");
                return "Unexpected OpType enum value in TagOp";
            }
        }
    };
    using TagOp = transaction_ptr<TagOpObj>;

    bool tag_mux(Pipeline<TagOp>::Data data);
    bool tag_array_lookup(Pipeline<TagOp>::Data data);
    bool tag_hit_miss(Pipeline<TagOp>::Data data);
    bool tag_feq_write(Pipeline<TagOp>::Data data);
    void tag_clear_and_stall(TagOp& clearing_op);
    void tag_clear_and_stall(Fetch& clearing_fetch);

    PipelineGroup<TagOp> tag_pipes;

    bool data_mux(Pipeline<Fetch>::Data data);
    bool data_array_access(Pipeline<Fetch>::Data data);
    bool data_predecode(Pipeline<Fetch>::Data data);

    PipelineGroup<Fetch> data_pipes;
};

bool data_pipeline_comparator(const Pipeline<Fetch>&, const Pipeline<Fetch>&);

} // namespace ampere::core

/*
 * vim: sw=4:ts=4:et:si:cindent:cino=\:0,(0,g0,t0:
 */
