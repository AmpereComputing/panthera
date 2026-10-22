//
// Copyright (c) 2018-2023 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//

#pragma once

#include "ampere/archx/addr.h"
#include "ampere/archx/driver.h"
#include "ampere/core/backend_prefetcher.h"
#include "ampere/core/ixu_defs.h"
#include "ampere/core/lsu/pwc.h"
#include "ampere/sc/credit.h"
#include "ampere/sc/putter.h"
#include "ampere/sc/sc.h"
#include "ampere/sc/pipeline.h"
#include "ampere/core/coherency_validator.h"
#include "ampere/core/flush.h"
#include "ampere/core/uop.h"
#include "ampere/core/l1tlb.h"
#include "ampere/core/mmu/req.h"
#include "ampere/core/mmu/ack.h"
#include "ampere/core/mmu/resp.h"
#include "ampere/core/tlbireq.h"
#include "ampere/core/lsu/sf_predictor.h"
#include "ampere/core/lsu/pob.h"
#include "ampere/core/lsu/ofb.h"
#include "ampere/core/lsu/cache_conflicts.h"
#include "ampere/core/ixu_defs.h"
#include "ampere/sc/submodule.h"
#include "ampere/uarch_common/cache.h"
#include "ampere/uarch_common/indexer.h"
#include "ampere/uarch_common/memop.h"
#include "ampere/uarch_common/memreq.h"
#include "ampere/uarch_common/memresp.h"
#include "uopcodes.h"

#include <bit>
#include <climits>
#include <cstdint>
#include <map>
#include <deque>
#include <optional>
#include <ostream>
#include <utility>
#include <nlohmann/json.hpp>
#include <fmt/core.h>

using json = nlohmann::json;

make_ns_enum(ampere::core, WakeCode,
    L1_MISS,
    TLB_MISS,
    OFB_FULL,
    OTB_ACK,
    OTB_FULL,
    ST_ADDR,
    ST_DATA,
    ST_RETIRE,
    RETRY_FAULT,
    BASES_LOADED,
    STLR,
    PWC
);

// These are translated into strings in the event stream in src/ampere/python/panthera/events/mapping.py
make_ns_enum(ampere::core, RecycleReason,
    DTLB_MISS = 0,
    LD_CONFLICT = 1,
    ST_CONFLICT = 2,
    UNKNOWN_STA = 3,
    FOUR_K_SPLIT = 4,
    STD_SLEEP = 5,
    PARTIAL_DATA = 6,
    STFWD_WALK = 7,
    FILL_CONFLICT = 8,
    FILL_CONFLICT2 = 9,
    SPLIT = 10,
    BARRIER = 11,
    SKIP_L1_MISS = 12,
    OTB_FULL = 13,
    ATOMIC_FWD = 14,
    SUS_DATA_NOT_READY = 15,
    OFB_FULL = 16,
    L1_MISS = 17,
    BASES_LOADED = 18,
    LD_CANCEL = 19,
    IN_FLIGHT_LOAD = 20,
    SPLIT_LO_TAG_CHECK_NOT_DONE = 21,
    TAG_CHECK_NOT_DONE = 22,
    L2_MISS_BANDWIDTH = 23,
    OFB_ALMOST_FULL = 24,
    SPEC_ATOMIC = 25,
    DCVA = 26,
    EXCL_FWD = 27,
    DTLB_CONFLICT = 30,  // Only on Furiosa
    UNKNOWN = 31,  // Not used in panthera, reserved for RTL
    EVICT_CONFLICT = 32,
    PWC_ORDERED = 33,
    PWC_MULTIPLE = 34
);

make_ns_enum(ampere::core, PipeOccupant,
    LOAD = 0,
    STA_LD = 1,
    PF = 2,
    REFILL = 3,     // Fill is going down the pipe to complete a load
    FILL_BLOCK = 4, // Load scheduling blocked due to DL1 write conflict with a fill
    STA = 5,
    EVICT = 6
);

// for MTE
make_ns_enum(ampere::core, PageType,
    UNKNOWN, // PageType is not known until address translation
    TAGGED,
    UNTAGGED,
    NON_ADDR // e.g., store barrier. No addr -> uop does not perform tag check
);

// Store events
make_ns_enum(ampere::core, StoreStallReason,
    DATA,
    LINE,
    PWC,
    L2
);

make_ns_enum(ampere::core, STAPrefetchPolicy,
    NEVER,
    ATTEMPT,
    REQUIRE
);

namespace ampere::core {

using namespace ampere::sc;
using namespace ampere::archx;
using namespace ampere::uarch_common;

class LSU : public Module, public CacheLookupInterface<PhysAddr>
{
public:
    Param<size_t> load_pipes{"load_pipes", "max load uops issued per cycle", 2};
    Param<size_t> store_pipes{"store_pipes", "max store uops issued per cycle", 4};
    Param<size_t> num_execution_units{
        "num_execution_units", "Number of pipes LSU could possibly receive loads or stores on", 18};
    Param<bool> store_pipes_have_tag_ports{"store_pipes_have_tag_ports", "Store pipes can check DL1 hit/miss", false};
    Param<bool> prefetch_uses_store_pipes{
        "prefetch_uses_store_pipes", "Execute prefetches on store pipes instead of load pipes", false};

    Param<size_t> raw_scrub_granularity{"raw_scrub_granularity", "byte granularity of RAW MD cam", 64};
    Param<bool> raw_scrub_4b_mask_bug{
        "raw_scrub_4b_mask_bug", "1-byte and 2-byte uops are treated as 4-byte uops for RAW scrub checking", true};
    Param<bool> gather_wake_limit{"gather_wake_limit", "true=limit gather wakes to 1 per cycle, default", true};

    Param<bool> enable_oracle_store_forwarding{"enable_oracle_store_forwarding",
                                               "loads use perfect SFP to determine behavior and only block if potential"
                                               "dependent store is not ready (known and addr complete)",
                                               false};

    Param<bool> enable_partial_data_fwd{
        "enable_partial_data_fwd",
        "Loads which get only part of their data from a store can forward instead of waiting for the store to retire",
        false};
    Param<bool> split_fwd_from_multiple{
        "split_fwd_from_multiple", "A split load can forward from multiple older stores", false};
    Param<bool> partial_fwd_early_exit{"partial_fwd_early_exit",
                                       "Exit stfwd walk when a load has accumulated all needed data from older stores",
                                       false};
    Param<bool> always_wait_on_partial_match{
        "always_wait_on_partial_match", "A load will never skip an older partial-match store", false};
    Param<bool> stack_load_wait{"stack_load_wait", "Stack loads will wait on older unknown stack stores", false};
    Param<bool> enable_ofb_update_to_older_req{
        "enable_ofb_update_to_older_req", "ofb can update s/lob_entry to an older req", true};

    Param<bool> fill_speculative_wake{
        "fill_speculative_wake", "Speculative wakes from L2 cause a load dispatch to be consumed", true};
    Param<bool> fill_speculative_wake_ixu{
        "fill_speculative_wake_ixu", "Speculative wakes from L2 cause IXU dependents to be 'woken'", true};
    Param<bool> prefetch_fills_execute{
        "prefetch_fills_execute", "Fills from a prefetch OFB consume a load execution slot", true};

    Param<bool> enable_coherency_validation{
        "enable_coherency_validation", "Send LSU hits to the L2 to validate L2 inclusivity is maintained", false};

    // Memory-tagging feature requires stores to perform loads to check memory tag
    Param<bool> enable_mte{
        "enable_mte",
        "memory-tagging feature is enabled; so, store address pipeline performs load to L1D before completion",
        false};
    Param<bool> drop_untagged_staLd_dl1_miss{
        "drop_untagged_staLd_dl1_miss", "If the page is untagged, drop the STA-load upon a DL1 miss", false};
    Param<bool> tagged_pages{
        "tagged_pages", "Simulate MTE with all tagged/untagged pages (default = all tagged)", true};

    Param<bool> enable_early_prfm{"enable_early_prfm", "complete software prefetches early", true};
    Param<bool> enable_early_prfm_tlb{"enable_early_prfm_tlb", "complete software prefetches early if miss TLB", true};
    Param<bool> separate_l1d_prfm_pmus{
        "separate_l1d_prfm_pmus", "Separate L1D access/refill PMUs based on prefetch or demand", false};
    Param<bool> enable_per_cycle_pmu{
        "enable_per_cycle_pmu", "Output the MEM_ACCESS*_PERCYC events (slows down panthera)", false};

    Param<bool> enable_splits{"enable_splits", "Enable realistic two-pass handling of split loads and stores", true};
    Param<int> mlb_entries{"mlb_entries", "Number of entries in the MLB used for splits", 4};
    Param<bool> enable_sus_recycle{"enable_sus_recycle", "SUS whose data is not ready will recycle", true};
    Param<bool> sus_skip_final_sta{
        "sus_skip_final_sta", "The final STA dispatch of the SUS can be skipped to mark complete", false};

    Param<bool> only_walk_stores_providing_new_bytes{
        "only_walk_stores_providing_new_bytes", "only walk stores that provide new bytes to the load", false};
    Param<bool> no_walk_for_false_positive_lower_va_bit_match{
        "no_walk_for_false_positive_lower_va_bit_match",
        "if the above knob is turned on, this knob will determine if a load should walk false lower va bit matches",
        false};
    Param<bool> walk_partials_only{
        "walk_partials_only", "if predictor predicts skip, don't walk all unknown stores", false};
    Param<bool> skip_miss_proceeds{
        "skip_miss_proceeds",
        "A load which skips a store and then misses the L1 can proceed as normal, instead of going to ST_ADDR sleep",
        false};
    Param<bool> sta_early_wakeup{
        "sta_early_wakeup",
        "A load waiting for an older store's address is woken up 2 cycles before the store completes (matches RTL)",
        true};
    Param<Cycle> sta_bypass_delay{"sta_bypass_delay", "Cycles after SOB write before an STA can execute", 0};
    Param<Cycle> lob_bypass_delay{"lob_bypass_delay", "Cycles after LOB write before a load can execute", 0};
    Param<Cycle> delay_after_lob_bypass{
        "delay_after_lob_bypass",
        "If a load boes not bypass into the pipe, it must wait this many cycles before it can schedule",
        2};
    Param<Cycle> delay_after_sob_bypass{
        "delay_after_sob_bypass",
        "If a store boes not bypass into the pipe, it must wait this many cycles before it can schedule",
        2};
    Param<unsigned> combine_stores{
        "combine_stores",
        "Allow combining N back-to-back stores within the same region of store_retirement_width size",
        0};
    Param<unsigned> delayed_retirement{
        "delayed_retirement", "Delay retirement of N stores to see if it might combine", 0};
    Param<unsigned> store_retirement_width{
        "store_retirement_width", "Width in bytes of the store retirement bus to the L2C", 16};
    Param<bool> enable_tlb_fill_conflict{"enable_tlb_fill_conflict", "TLB fills block TLB lookups", true};
    Param<bool> specific_st_data_wake{
        "specific_st_data_wake",
        "Store data exection only wakes necessary loads in ST_DATA sleep instead of all of them",
        false};
    Param<bool> read_conflict_can_miss{
        "read_conflict_can_miss",
        "A load with a read data chunk conflict can still miss the L1 and allocate an OFB",
        false};
    Param<bool> write_conflict_can_miss{
        "write_conflict_can_miss",
        "A load with a write data chunk conflict can still miss the L1 and allocate an OFB",
        false};
    Param<bool> fill_conflict_blocks_schedule{
        "fill_conflict_blocks_schedule",
        "Do not schedule loads which are guaranteed to have a tag conflict with a fill (based on speculative fill)",
        false};
    Param<bool> load_fill_conflicts{"load_fill_conflicts", "Fills cause conflicts with loads/prefetches", true};
    Param<bool> sta_fill_conflicts{"sta_fill_conflicts", "Fills cause conflicts with STAs/prefetches", true};
    Param<bool> slow_write_conflict_port1{"slow_write_conflict_port1",
                                          "Loads on DL1 port 1 which get a write conflict have a longer recycle time",
                                          false};
    Param<bool> fast_fill_conflict_port1{
        "fast_fill_conflict_port1",
        "Loads on DL1 port 1 which get a fill conflict have a shorter recycle time and do not send wake to IXU",
        false};
    Param<bool> real_exclusives{"real_exclusives", "Enable accurate modeling of load/store exclusive", true};
    Param<bool> atomics_in_l2{"atomics_in_l2", "Atomics are executed in the L2", true};
    Param<bool> prefetch_atomics{"prefetch_atomics", "Prefetch atomic data", false};
    Param<bool> atomic_stores_nonspec{
        "atomic_stores_nonspec", "Atomic stores wait until they are non-speculative before executing", true};
    Param<bool> stores_with_dest_nonspec{
        "stores_with_dest_nonspec",
        "Atomic loads and exclusive stores wait until they are non-speculative before executing",
        true};
    Param<bool> sta_load_writeback_can_bypass{
        "sta_load_writeback_can_bypass", "STA-loads performing a writeback can bypass into scheduler", false};
    Param<bool> atomic_load_speculative{"atomic_load_speculative", "Atomic loads execute/WB speculatively", false};
    Param<bool> atomic_load_speculative_with_multiple_dest{
        "atomic_load_speculative_with_multiple_dest",
        "Allow multiple-dest atomic loads to execute speculatively (i.e. casp-64b)",
        false};
    Param<bool> atomic_load_speculative_casp{
        "atomic_load_speculative_casp", "Allow casp atomic loads to execute speculatively", false};
    Param<bool> exclusive_load_speculative{
        "exclusive_load_speculative", "Exclusive loads execute/WB speculatively", false};
    Param<bool> atomic_load_speculative_fill{
        "atomic_load_speculative_fill", "Atomic loads execute/WB speculatively and fill L1d", false};
    Param<bool> atomic_load_speculative_abort_unknown{
        "atomic_load_speculative_abort_unknown", "Atomic loads execute/WB abort on unknowns", false};
    Param<bool> atomic_load_speculative_abort_matches{
        "atomic_load_speculative_abort_matches", "Atomic loads execute/WB abort on matches", false};
    Param<uint32_t> atomic_load_speculative_abort_every{
        "atomic_load_speculative_abort_every", "Abort every N speculative atomic loads", 0};
    Param<uint32_t> atomic_load_speculative_cooldown_after{
        "atomic_load_speculative_cooldown_after", "Abort every N speculative atomic loads", 0};
    Param<uint32_t> atomic_load_speculative_cooldown_duration{
        "atomic_load_speculative_cooldown_duration", "Abort every N speculative atomic loads", 0};
    Param<bool> acquire_can_skip{"acquire_can_skip", "Load-acquire can skip older unknown stores", false};
    Param<bool> acquire_sleep_on_all_barriers{
        "acquire_sleep_on_all_barriers", "Load-acquire waits for any older barrier, not just store-release", true};
    Param<bool> acquire_sleep_on_done_exclusive{
        "acquire_sleep_on_done_exclusive",
        "Load-acquire waits for older atomic/exclusive release even if it is already complete in L2",
        true};
    Param<bool> acquire_sleep_on_matching_release{
        "acquire_sleep_on_matching_release", "Load-acquire waits for older matching atomic/exclusive", false};
    Param<bool> acquire_wake_waits_till_older_retire_stlrs{
        "acquire_wake_waits_till_older_retire_stlrs",
        "Load-acquire STLR wake waits till no older STLRs exist in SOB",
        false};
    Param<int64_t> snoop_exclusion_window{
        "snoop_exclusion_window", "Block fills to lines which were snooped in the last N cycles", 3};
    Param<int64_t> in_flight_raw_scrub_window{
        "in_flight_raw_scrub_window",
        "Cycles in which an in-flight load might be missed by a store's raw-scrub check",
        3};
    Param<int64_t> barrier_wakeup_delay{
        "barrier_wakeup_delay", "Cycles after L2 says stores are done before a barrier can execute", 3};
    Param<bool> dmb_all_skip_final_sta{"dmb_all_skip_final_sta", "DMB ALL barriers skip their final STA", false};
    Param<bool> dsb_skip_final_sta{"dsb_skip_final_sta", "DSB barriers skip their final STA", false};
    Param<bool> ignore_unemulated_wrong_path_uops{
        "ignore_unemulated_wrong_path_uops", "Unemulated wrong-path loads and stores go straight to sleep", true};
    Param<bool> store_prefetch_nonspec{
        "store_prefetch_nonspec", "Store prefetch uops do not execute until they are the oldest uop", true};
    Param<bool> no_scrub_sleeping_load{
        "no_scrub_sleeping_load", "Don't RAW scrub a load which is sleeping, just re-execute it", false};
    Param<bool> enable_nontemporals{"enable_nontemporals", "Make use of non-temporal hint", false};
    Param<bool> l1_prefetch_is_nontemporal{
        "l1_prefetch_is_nontemporal", "L1 prefetches should behave like non-temporal loads", false};
    Param<int> snoop_match_high_half_upper_bit{
        "snoop_match_high_half_upper_bit", "Upper end of address range that snoops use to check the LOB", 11};
    Param<bool> enable_load_cancel{"enable_load_cancel", "Send cancel to IXU when load does not hit DL1", true};
    Param<bool> splits_send_cancel{"splits_send_cancel", "First half of a split sends wake/cancel to IXU", false};
    Param<bool> run_always{"run_always", "Call the LSU run() function every cycle", false};

    Param<bool> store_write_to_l2_on_miss{"store_write_to_l2_on_miss", "Stores only write to L2 on a L1d miss", false};
    Param<bool> evict_modified_via_l2_pwc{"evict_modified_via_l2_pwc", "Modified evictions go through L2 PWC", true};
    Param<bool> write_through_skip_pwc{"write_through_skip_pwc", "Write-though ops skip PWC entirely", false};
    Param<bool> drop_fill_no_eviction_port{
        "drop_fill_no_eviction_port", "Drop fill if no eviction port is available", true};
    Param<bool> incomplete_fill_can_complete{
        "incomplete_fill_can_complete",
        "Allow incomplete fill (unable to evict due to port constraints) to complete load",
        false};

    Param<STAPrefetchPolicy> prefetch_sta{"prefetch_sta", "STA prefetch policy", STAPrefetchPolicy::NEVER};
    Param<bool> write_allocate{"write_allocate", "L1d is write allocate", false};

    Param<bool> instantly_complete_uops_not_needing_sta{
        "instantly_complete_uops_not_needing_sta", "Instantly complete uops which do not need to dispatch STA", false};
    Param<bool> all_deferred_store_completion_requires_delay{
        "all_deferred_store_completion_requires_delay",
        "All deferred (lost arbitration) store completion requires delay before arbitrating again",
        true};

    Param<bool> otb_full_sleep{"otb_full_sleep", "OTB Full causes uops to sleep until OTB available", false};
    Param<bool> allocate_pob_on_ofb_full{
        "allocate_pob_on_ofb_full", "Prefetches which can't allocate an OFB going into the POB", false};

    Param<bool> l2_prefetches_for_ofb_full_demand{
        "l2_prefetches_for_ofb_full_demand", "Insert LSU demands into L2 demand/prefetch queue if OFB is full", false};
    Param<bool> l2_prefetches_for_ofb_full_prefetch{
        "l2_prefetches_for_ofb_full_prefetch", "Insert LSU prefetches into L2 prefetch queue if OFB is full", false};
    Param<bool> l2_prefetches_all_to_l2_prefetch_queue{
        "l2_prefetches_all_to_l2_prefetch_queue",
        "Insert all L2 prefetches (including demands) into L2 prefetch queue",
        false};
    Param<bool> lob_balancing{"lob_balancing", "Send info to IDR to help balance load pipe usage", false};
    Param<bool> load_exec_inval_pcp{
        "load_exec_inval_pcp", "When a load executes, invalidate any pending PCP prefetches to the same line", false};

    Param<std::optional<unsigned>> load_combining{
        "load_combining", "Allow load combining, up to N loads per pipe (includes original load)", {}};
    Param<bool> combine_always{"combine_always", "Always allow load combining", false};
    Param<bool> older_load_gets_port0{
        "older_load_gets_port0", "The older load uses port0 and the younger one (if it exists) uses port1", false};

    // External ports
    DriverPort driver_port;
    DriverPort oracle_driver_port;

    Out<Credit> credit_return{"credit_return"};
    In<uint> loads_committed_port{"loads_committed"};
    In<uint> stores_committed_port{"stores_committed"};

    Out<uarch_common::Request::Fill> l2_load_miss_request{"l2_load_miss_request"};
    In<MemResp> l2_load_miss_response{"l2_load_miss_response"};
    Out<MemReq> l2_store_miss_request{"l2_store_miss_request"};
    Out<MemReq> l2_writeback_request{"l2_writeback_request"};

    In<MemReq> l2_snoop_request{"l2_snoop_request"};
    Out<MemReq> l2_prefetch_request{"l2_prefetch_request"};

    Indexer<PhysAddr> l2_indexer{"l2_indexer"};
    CoherencyValidator<PhysAddr> coherency_validator{"coherency_validator"};

    InOpt<bool> mmu_can_accept{"mmu_can_accept"};
    Out<MMUReq> mmu_miss_request{"mmu_miss_request"};
    In<MMUAck> mmu_miss_ack{"mmu_miss_ack"};
    In<MMUResp> mmu_miss_response{"mmu_miss_response"};

    Out<TLBIReq> tlbi_request_out{"tlbi_request_out"};
    Out<TLBIReq> tlbi_immu_request_out{"tlbi_immu_request_out"};

    In<TLBIReq> tlbi_request_in{"tlbi_request_in"};

    Read<Flush> flush{"flush"};

    sc_core::sc_vector<In<Uop>> issue_mem{"issue_mem", num_execution_units};
    sc_core::sc_vector<Out<Uop>> writeback_mem{"writeback_mem", num_execution_units};
    sc_core::sc_vector<Out<std::pair<Uop, bool>>> complete_mem{"complete_mem", num_execution_units};
    Out<Uop> raw_scrub_port{"raw_scrub_port"};
    In<Uop> mem_rob_preview_port{"mem_rob_preview_port"};
    Out<Uop> load_cancel_out{"load_cancel_out"};

    // STALL_BACKEND TLB & CACHE
    Out<bool> pmu_oldest_load_tlb_out{"pmu_oldest_load_tlb_out"};
    Out<bool> pmu_oldest_load_cache_out{"pmu_oldest_load_cache_out"};
    Out<bool> pmu_oldest_load_ofb_full_out{"pmu_oldest_load_ofb_full_out"};
    Out<bool> pmu_oldest_ldst_valid_out{"pmu_oldest_ldst_valid_out"};
    Out<bool> pmu_oldest_ldst_atomic_out{"pmu_oldest_ldst_atomic_out"};
    Out<bool> pmu_oldest_load_st_wait{"pmu_oldest_load_st_wait"};

    // STALL_BACKEND_MEM
    Read<PhysAddr> pmu_l2_miss_in{"pmu_l2_miss_in"};
    Out<bool> pmu_oldest_load_mem_out{"pmu_oldest_load_mem_out"};

    In<bool> l2_no_pending_stores_in{"l2_no_pending_stores_in"};
    In<bool> l2_barrier_in{"l2_barrier_in"};
    In<bool> l2_store_release_in{"l2_store_release_in"};
    In<bool> l2_store_exclusive_in{"l2_store_exclusive_in"};
    In<uint64_t> next_to_commit_in{"barrier_next_to_commit"};
    Out<bool> lsu_has_barrier_out{"lsu_has_barrier_out"};
    Out<bool> lob_balance_out{"lob_balance_out"};

    std::vector<unsigned> loads_per_pipe;

    explicit LSU(const sc_core::sc_module_name& name);
    bool run() override;
    bool work_to_do() const;
    bool address_resident(PhysAddr addr) const override;

    unsigned sta_pipes() const { return store_pipes - MAX_FSU_PARTITIONS; }
    size_t load_schedule_bandwidth() const { return load_pipes * load_combining.value_or(1); }
    size_t l1d_load_miss_bandwidth() const
    {
        return load_schedule_bandwidth() + (store_pipes_have_tag_ports ? sta_pipes() : 0);
    }

private:
    struct SLOBEntry
    {
        Uop uop;
        bool executing{false};
        bool recycle{false};
        bool complete{false};
        bool split{false};
        bool split_4k{false};
        bool split_lo_complete{false};
        bool l2_miss{false}; // this entry was valid when the L2C informed us that it saw an L2C miss for its cacheline
        bool l2_prefetched{false};
        bool suppress_cancel{false};
        std::optional<WakeCode> wake_code;
        std::optional<uint64_t> wake_id;
        PageSize::Size page_size{PageSize::Size::INVALID};
        PageType page_type{PageType::UNKNOWN};
        std::optional<VirtAddr> base_virt_addr;
        std::optional<PhysAddr> base_phys_addr;
        unsigned int mem_datasize{0};
        Cycle issue_cycle{0};
        unsigned dl1_port{0};
        uint64_t covered_bytes{0};

        void recycle_into_scheduler();
        void go_to_sleep(WakeCode code) { wake_code = code; };
        void go_to_sleep(WakeCode code, uint64_t id)
        {
            wake_code = code;
            wake_id = id;
        };
        void wakeup()
        {
            wake_code = std::nullopt;
            wake_id = std::nullopt;
        };
        bool waiting_till_non_spec() const { return wake_code == WakeCode::STLR || wake_code == WakeCode::RETRY_FAULT; }
        bool should_wake_from_fill(PhysAddr phys_addr) const
        {
            return (wake_code == WakeCode::L1_MISS && uop->phys_addr->same_cache_line(phys_addr))
                   || wake_code == WakeCode::OFB_FULL;
        }

        bool is_asleep_on_pending_mmu_req(uint64_t otb_id) const
        {
            return wake_code && wake_code == WakeCode::TLB_MISS && wake_id.value() == otb_id;
        }

        bool tagged() const { return page_type == PageType::TAGGED; }
        bool tag_check_required() const
        {
            return (!uop || !(uop->is_store_barrier() || LSU::is_casp_hi(uop)))
                   && (page_type == PageType::UNKNOWN || tagged())
                   && (!uop->is_store() || uop->requires_memory_tag_check());
        }
        bool needs_canonical_check() const { return uop->archx_uop->opcode != archx::Uopcode::SYS_AT; }

        uint8_t pipe_mask() const { return 1U << uop->sch.lsu_scheduler_pipe(); }

        const VirtAddr& virt_addr() const { return (split && split_lo_complete) ? *base_virt_addr : *uop->virt_addr; }
        VirtAddr virt_addr_last_byte() const { return virt_addr() + uop->mem_datasize() - 1; }
        const std::optional<PhysAddr>& phys_addr() const
        {
            return (split && split_lo_complete) ? base_phys_addr : uop->phys_addr;
        }

        unsigned int split_high_half_mem_datasize() const
        {
            AMPERE_ASSERT(split, "Should only call this function for splits!");
            auto next_line = virt_addr().next_line_address();
            return (virt_addr() + uop->mem_datasize()) - next_line;
        }
        // Move to high half here
        void adjust_to_hi_half_addr()
        {
            // Save away the original VA/PA
            base_virt_addr = *uop->virt_addr;
            base_phys_addr = *uop->phys_addr;
            // Change the VA to the high half for the second pass
            // The modified size is only used for conflict checking, not forwarding
            uop->virt_addr = base_virt_addr->next_line_address();
            mem_datasize = (*base_virt_addr + uop->mem_datasize()) - *uop->virt_addr;

            split_lo_complete = true;
        }

        // Restore low half VA/PA after the high half of the split is complete
        // to keep the translation checker happy
        void restore_base_addr()
        {
            uop->virt_addr = *base_virt_addr;
            uop->phys_addr = *base_phys_addr;
            mem_datasize = uop->mem_datasize();
        }

        // Get "part" of virtual address, either the base address
        // or next-line address if split is provided
        VirtAddr virt_addr_part(bool split) const
        {
            const auto& virt_addr = this->virt_addr();
            return split ? virt_addr.next_line_address() : virt_addr;
        }

        // Extracts the request address part for the specified cacheline
        // and indicates which part (split or not)
        std::pair<bool, VirtAddr> virt_addr_of(const VirtAddr& addr) const
        {
            if (!split)
                return {false, *uop->virt_addr};
            const auto& virt_addr = this->virt_addr();
            bool part = !virt_addr.same_cache_line(addr);
            return {part, virt_addr_part(part)};
        }

        bool all_bytes_covered() const { return covered_bytes == util::mask(uop->mem_datasize()); }

        SLOBEntry() = default;
        explicit SLOBEntry(Uop u) : uop(u) {}
    };

    struct LOBEntry : SLOBEntry
    {
        bool slept_on_store{false};
        bool forwarded_from_store{false};
        bool skipped_unknown_store{false};
        bool skipped_unknown_store_this_pass{false};
        uint64_t skipped_store_id{0};
        bool ld_st_fwd_prediction{false};
        bool got_l1_data{false};
        bool has_mlb{false};
        uint64_t oldest_skipped_store{ULLONG_MAX};
        uint64_t youngest_forwarding_store{0};

        bool is_ready() const { return !complete && !executing && !wake_code && !recycle; }
        bool can_forward() const { return !uop->is_sw_prefetch(); }

        explicit LOBEntry(Uop u) : SLOBEntry(u) {}
    };

    struct SOBEntry : SLOBEntry
    {
        bool addr_complete{false};
        bool addr_will_complete{false};
        bool data_low_complete{false};
        bool data_high_complete{false};
        bool must_recycle{false};
        bool committed{false};
        bool pair{false};
        bool fat_sus{false};
        bool ordered{false};
        bool barrier{false};
        bool split_lo_retire_schedule_done{false};
        bool wrote_back{false};
        bool exclusive_complete{false};
        bool exclusive_retired{false};
        bool exclusive_second_pass{false};
        bool st_data_load{false};
        bool is_going_to_dealloc{false};

        bool sta_load_complete{false};
        bool split_lo_sta_load_complete{false};

        bool is_ready() const
        {
            return !executing && !wake_code && !recycle && (!addr_complete || must_recycle) && uop;
        }
        bool is_ready_for_load() const { return !executing && !wake_code && !recycle && !sta_load_complete; }
        bool is_ready_for_writeback() const { return addr_complete && !executing && !must_recycle; }
        bool is_execution_complete() const { return !executing && addr_complete && data_low_complete; }
        bool is_ready_for_completion() const
        {
            return !recycle && !must_recycle && !complete && wrote_back && is_execution_complete();
        }
        bool is_ready_to_retire() const { return committed && !executing; }
        bool retiring() const { return committed && executing; }

        void exclusive_receives_data(bool complete = false)
        {
            exclusive_second_pass = true;
            if (!uop || !uop->is_atomic_load())
                return;

            if (complete) {
                sta_load_complete = true;
            } else {
                must_recycle = true;
                executing = false;
            }
        }
        bool exclusive_is_ready_to_retire() const { return exclusive_complete && !exclusive_retired; }
        bool exclusive_is_retiring() const { return exclusive_complete && executing && !committed; }
        bool exclusive_is_not_retiring() const { return exclusive_complete && !executing && !exclusive_retired; }
        bool exclusive_is_ready_to_complete() const { return exclusive_second_pass; }

        // PA is valid if it is calculated, or a SUS which is about to complete
        bool pa_valid() const { return addr_complete || addr_will_complete; }

        bool is_casp_hi() const { return uop && LSU::is_casp_hi(uop); }
        bool is_following_part_of_wide_atomic_load() const { return is_next_part_of_wide_atomic_load(uop); }
        bool is_following_part_of_wide_atomic_load(const Uop& other) const
        {
            return is_next_part_of_wide_atomic_load(uop, other);
        }
        bool can_forward() const
        {
            return !uop
                   || (!is_casp_hi() && !uop->is_sw_prefetch()
                       && (!uop->is_store_special() || (uop->is_dc_zva() || uop->is_dc_va())));
        }
        bool needs_writeback() const { return !uop || uop->needs_writeback(); }
        bool needs_immediate_writeback() const
        {
            return needs_writeback() && !uop->is_exclusive() && !uop->is_atomic_load();
        }
        bool needs_sta_execute() const
        {
            return !uop
                   || (!uop->is_barrier_dmb_st() && !is_casp_hi()
                       && uop->archx_uop->opcode != archx::Uopcode::SYS_DC_SW);
        }
        bool writeback_only() const
        {
            return is_following_part_of_wide_atomic_load() && exclusive_is_ready_to_complete();
        }

        bool tag_check_complete() const
        {
            return page_type != PageType::UNKNOWN && (!tag_check_required() || sta_load_complete);
        }
        bool tag_check_lo_complete() const
        {
            return page_type != PageType::UNKNOWN && (!tag_check_required() || split_lo_sta_load_complete);
        }

        bool writes_cache() const
        {
            AMPERE_ASSERT(uop, "writes_cache() called on SOB entry with no uop");
            return !(uop->is_predicated_off() || uop->is_sw_prefetch());
        }

        SOBEntry() = default;
        explicit SOBEntry(Uop u) : SLOBEntry(u) {}
    };

    struct ForwardingMatches
    {
        struct Match
        {
            uint64_t alloc_id;
            bool stack_source{false};
            bool all_bytes{false};
            bool any_bytes{false};
            bool partial_data_match{false};
            bool forward_from_4k_split{false};
            bool true_match{false}; // magic knowledge for event reporting only
            uint64_t covered_bytes{0};

            explicit Match(uint64_t alloc_id, bool stack_source) : alloc_id(alloc_id), stack_source(stack_source) {}
            bool any_potential_overlap() const { return any_bytes || partial_data_match; }
        };

        bool forwarding_predicted{false};
        uint64_t oldest_unknown_store{0};
        unsigned num_older_unknown{0};
        unsigned num_older_partial{0};
        uint64_t accum_covered_bytes{0};
        std::vector<Match> matches;
        std::optional<WakeCode> wake;

        ForwardingMatches(uint64_t covered_bytes) : accum_covered_bytes(covered_bytes) {}

        bool any() const { return oldest_unknown_store != 0; }
        bool multiple(bool partials_only) const
        {
            if (partials_only)
                return num_older_partial > 1;
            return (num_older_partial + num_older_unknown) > 1;
        }

        void potentially_matching(uint64_t alloc_id)
        {
            oldest_unknown_store = alloc_id;
            num_older_unknown++;
        }
        void potentially_matching(uint64_t alloc_id, uint64_t covered_bytes)
        {
            oldest_unknown_store = alloc_id;
            accum_covered_bytes |= covered_bytes;
            num_older_partial++;
        }
        std::pair<bool, Match*> check(uint64_t alloc_id, const SOBEntry& sob_entry, const SLOBEntry& entry);
        Match* check_unknown(uint64_t alloc_id, bool stack_source)
        {
            potentially_matching(alloc_id);
            return &matches.emplace_back(alloc_id, stack_source);
        }
    };

    struct SleepReason
    {
        std::optional<WakeCode> wake;
        std::optional<RecycleReason> recycle_reason;

        bool needs_sleep() const { return static_cast<bool>(wake); }
        bool needs_recycle() const { return static_cast<bool>(recycle_reason); }

        void sleep_if_needed(SLOBEntry& entry)
        {
            if (needs_sleep())
                entry.go_to_sleep(*wake);
        }
    };

    struct ForwardingResult : SleepReason
    {
        enum Result {
            NONE = 0,
            SLEPT,
            WALK,
            PARTIAL,
            MULTIPLE_PARTIAL,
            FULL,
        };

        Result result{NONE};
        uint64_t store_alloc_id{0};

        ForwardingResult() = default;
        ForwardingResult(Result result, uint64_t store_alloc_id = 0) : result(result), store_alloc_id(store_alloc_id) {}
        ForwardingResult(WakeCode wake, std::optional<RecycleReason> reason = {}, uint64_t store_alloc_id = 0)
            : SleepReason{wake, reason}, result(SLEPT), store_alloc_id(store_alloc_id)
        {
        }

        explicit operator bool() const { return result; }
        bool needs_data() const { return result == NONE || result == PARTIAL; }
        bool can_complete() const { return result == FULL; }
        bool matched_store() const { return store_alloc_id > 0; }
    };

    struct CacheResult : SleepReason
    {
        bool hit{false};
        PrefetchType prefetch{PrefetchType::NONE};

        CacheResult() = default;
        explicit CacheResult(PrefetchType prefetch) : hit(true), prefetch(prefetch) {}
        CacheResult(const SleepReason& reason) : SleepReason(reason) {}

        explicit operator bool() const { return hit; }
    };

    struct StoreRetirementOps
    {
        struct Part
        {
            uint64_t alloc_id;
            bool hi_half;

            Part(const SOBEntry& store, bool hi_half = false) : alloc_id(store.uop->alloc_id), hi_half(hi_half) {}

            PhysAddr phys_addr(const Uop& uop) const;
            VirtAddr virt_addr(const Uop& uop) const;
            unsigned int datasize(const Uop& uop) const;
            uint64_t bytemask(const Uop& uop) const;
        };

        std::vector<Part> ops;
        MemOp::BarrierType barrier_type{MemOp::BarrierType::BAR_NONE};
        MemOp::AtomicType atomic_type{MemOp::AtomicType::ATOMIC_NONE};
        PageSize::Size page_size{};
        unsigned int datasize{0};
        uint64_t bytemask{0};
        bool non_temporal = false;
        bool write_through = false;

        StoreRetirementOps() = default;
        explicit StoreRetirementOps(const SOBEntry& store, bool local, bool default_write_through, bool hi_half = false)
        {
            add(store, local, default_write_through, hi_half);
        }

        bool empty() const { return ops.empty(); }
        void add(const SOBEntry& store, bool local, bool default_write_through, bool hi_half = false);
        bool write_back() const { return !write_through; }
    };

    bool write_through_required() const
    {
        return l2_pending_barrier || l2_pending_store_excl || l2_pending_store_release;
    }

    using LOB = std::map<uint64_t, LOBEntry>;
    using SOB = std::map<uint64_t, SOBEntry>;
    using MEMROB = std::map<uint64_t, Uop>;

    int lsu_port_lookup(Uop u, bool as_load = false) const;

    inline static bool is_younger(uint64_t current, uint64_t other) { return (current > other); }

    inline static bool is_older(uint64_t current, uint64_t other) { return (current < other); }

    bool speculative_sta_loads_enabled() const
    {
        return enable_mte || atomic_load_speculative || prefetch_sta != STAPrefetchPolicy::NEVER || !atomics_in_l2;
    }
    bool cacheable_store(const SOBEntry& sob_entry) const
    {
        return (!enable_nontemporals || !sob_entry.uop->is_nontemporal())
               && (!sob_entry.uop->is_atomic() || !atomics_in_l2) && !sob_entry.uop->is_store_barrier();
    }
    bool prefetchable_store(const SOBEntry& sob_entry) const
    {
        return cacheable_store(sob_entry) && !write_through_exclusive_like(sob_entry.uop);
    }
    bool sta_load_optional(const SOBEntry& sob_entry) const
    {
        return prefetch_sta == STAPrefetchPolicy::ATTEMPT && prefetchable_store(sob_entry);
    }
    bool sta_load_required(const SOBEntry& sob_entry) const
    {
        bool speculatively_load = !stores_with_dest_nonspec && speculative_atomic_load_allowed(sob_entry);
        bool fetch_atomic =
            (prefetch_atomics || is_non_speculative(sob_entry)) && !atomics_in_l2 && sob_entry.uop->is_atomic();
        bool require_tag_check = enable_mte && sob_entry.tag_check_required()
                                 && (!sob_entry.uop->is_exclusive() || is_non_speculative(sob_entry));
        bool require_prefetch = prefetch_sta == STAPrefetchPolicy::REQUIRE && prefetchable_store(sob_entry);

        return !sob_entry.uop->is_predicated_off() && !sob_entry.uop->is_store_barrier()
               && (!sob_entry.exclusive_is_ready_to_complete() || sob_entry.is_following_part_of_wide_atomic_load())
               && (require_tag_check || speculatively_load || fetch_atomic || sob_entry.writeback_only()
                   || require_prefetch || sob_entry.uop->is_sw_prefetch());
    }
    bool sta_load_allowed(const SOBEntry& sob_entry) const
    {
        return (sob_entry.uop && sob_entry.uop->is_sw_prefetch())
               || (enable_mte && (sob_entry.tagged() || !tagged_pages))
               || (sob_entry.uop && speculative_atomic_load_candidate(sob_entry.uop))
               || sob_entry.exclusive_is_ready_to_complete()
               || (prefetch_sta != STAPrefetchPolicy::NEVER && prefetchable_store(sob_entry));
    }
    bool sta_part_completed(const SOBEntry& sob_entry, bool lo_half = false) const
    {
        if ((enable_mte && sob_entry.tag_check_required()) || speculative_atomic_load_allowed(sob_entry)
            || (prefetch_sta != STAPrefetchPolicy::NEVER && prefetchable_store(sob_entry)))
            return lo_half ? sob_entry.split_lo_sta_load_complete : sob_entry.sta_load_complete;

        return true;
    }
    inline static bool is_casp_lo(const Uop& uop)
    {
        return uop
               && (uop->archx_uop->opcode == archx::Uopcode::CASP_LO_D_64
                   || uop->archx_uop->opcode == archx::Uopcode::CASPAL_LO_D_64);
    }
    inline static bool is_casp_hi(const Uop& uop)
    {
        return uop
               && (uop->archx_uop->opcode == archx::Uopcode::CASP_HI_D_64
                   || uop->archx_uop->opcode == archx::Uopcode::CASPAL_HI_D_64);
    }
    inline static bool is_casp(const Uop& uop)
    {
        if (!uop)
            return false;

        switch (uop->archx_uop->opcode) {
        case archx::Uopcode::CASPAL_HI_D_64:
        case archx::Uopcode::CASPAL_LO_D_64:
        case archx::Uopcode::CASPAL_W_32:
        case archx::Uopcode::CASP_HI_D_64:
        case archx::Uopcode::CASP_LO_D_64:
        case archx::Uopcode::CASP_W_32:
            return true;
        default:
            return false;
        }
    }
    inline static bool is_wide_atomic_load(const Uop& uop) { return is_casp_lo(uop) || is_casp_hi(uop); }
    inline static bool is_start_of_wide_atomic_load(const Uop& uop) { return is_casp_lo(uop); }
    inline static bool is_next_part_of_wide_atomic_load(const Uop& uop) { return is_casp_hi(uop); }
    inline static bool is_next_part_of_wide_atomic_load(const Uop& uop, const Uop& lo_uop)
    {
        return uop && lo_uop && uop->instructions.front() == lo_uop->instructions.front()
               && is_start_of_wide_atomic_load(lo_uop) && is_next_part_of_wide_atomic_load(uop);
    }
    bool local_atomic(const Uop& uop) const { return uop && uop->is_atomic() && !atomics_in_l2; }
    bool write_through_atomic(const Uop& uop) const { return uop && uop->is_atomic() && atomics_in_l2; }
    bool write_through_exclusive_like(const Uop& uop) const
    {
        return uop && (uop->is_exclusive() || write_through_atomic(uop));
    }
    bool speculative_atomic_load_candidate(const Uop& uop) const
    {
        return uop
               && ((atomic_load_speculative && uop->is_atomic_load() && (atomic_load_speculative_casp || !is_casp(uop))
                    && (atomic_load_speculative_with_multiple_dest || !is_wide_atomic_load(uop)))
                   || (exclusive_load_speculative && uop->is_exclusive_load()));
    }

    bool speculative_atomic_load_allowed(const SOBEntry& sob_entry) const
    {
        return speculative_atomic_load_candidate(sob_entry.uop) && !sob_entry.exclusive_is_ready_to_complete()
               && sob_entry.wake_code != WakeCode::STLR && !speculative_atomic_load_cooldown;
    }
    void atomic_load_receives_data(SOBEntry& sob_entry);

    void receive_issued_uops();
    void receive_dispatched_uops();
    void fill_out_sob_entry(SOBEntry& sob_entry);
    void fill_out_sob_entry_and_wake_dependents(SOBEntry& sob_entry);
    void send_pmu_state();
    void get_barrier_info();
    bool sob_has_older_barrier(const LOBEntry& lob_entry) const;

    // Load pipeline functions
    void load_store_pipeline();
    void load_pipeline();
    void execute_load(LOBEntry& lob_entry);
    void execute_sta_load(SOBEntry& sob_entry, bool may_writeback = false);
    bool iterate_lob_and_schedule_sta_load(SOBEntry& sob_entry);
    bool can_load_pipeline_schedule() const;
    bool can_load_combine(const LOBEntry& lob_entry);
    bool can_load_schedule(const LOBEntry& lob_entry);
    bool can_prefetch_schedule() const;
    bool can_sta_pipeline_schedule() const;
    bool can_sta_load_schedule(SOBEntry& sob_entry, bool may_writeback = false) const;

    ForwardingMatches forwarding_matches(SLOBEntry& entry, uint64_t oldest_skipped = ULLONG_MAX);
    ForwardingResult load_store_forward_checks(LOBEntry& lob_entry);
    ForwardingResult load_store_forward(LOBEntry& lob_entry);

    void recycle(SLOBEntry& entry, Out<uint64_t>& recycle_pipe);
    void recycle_load(LOBEntry& lob_entry, RecycleReason reason, bool fast = false);
    void recycle_sta_load(SOBEntry& sob_entry, RecycleReason reason, bool fast = false);
    void recycle_store(SOBEntry& sob_entry, RecycleReason reason, bool force = false);
    void do_stfwd_walk(LOBEntry& lob_entry, uint64_t st_alloc_id);
    bool acquire_mlb_or_sleep(LOBEntry& lob_entry, Out<uint64_t>& load_recycle_pipe);
    void sleep_load_on_store(LOBEntry& lob_entry, uint64_t st_alloc_id, WakeCode code, RecycleReason reason);
    void sleep_load_on_store(
        LOBEntry& lob_entry, SOBEntry& sob_entry, uint64_t st_alloc_id, WakeCode code, RecycleReason reason);
    std::optional<unsigned> choose_dl1_port(unsigned pipe) const;

    bool is_non_speculative(const SLOBEntry& entry) const
    {
        return entry.uop && entry.uop->alloc_id == non_spec_uop_id;
    }

    uint64_t prefetch_id{0};       // ID used to generate unique MMUReq IDs for prefetch-induced translation requests
    archx::EL curr_el{archx::EL0}; // to prevent PCP inserting replays into POB during non-EL0
    POB pob{"pob"};

    void load_complete_stage();
    void load_complete_uop(Uop uop, bool refill);
    void sta_load_complete_stage();
    bool sta_load_complete_uop(SOBEntry& sob_entry, bool check_split_complete = false);

    bool can_skip_sta(const SOBEntry& sob_entry) const;
    bool can_skip_sta_at_issue(const SOBEntry& sob_entry) const;
    bool sta_only_completes(const SOBEntry& sob_entry) const;
    void adjust_sta_for_split(SOBEntry& sob_entry);
    bool try_completing_sta(SOBEntry& sob_entry, bool refill, bool as_load = false);
    bool try_completing_sta_load(SOBEntry& sob_entry, bool refill)
    {
        return try_completing_sta(sob_entry, refill, true);
    }
    void try_completing_sta_at_issue(SOBEntry& sob_entry);
    void skip_sta_and_complete(SOBEntry& sob_entry, bool writeback = true, bool wakeup = false);
    void complete_sta(SOBEntry& sob_entry, bool writeback = true, bool as_load = false);

    // Store address/retirement functions
    void store_data_early_complete_stage();
    void store_data_complete_stage();
    void store_data_execute(Uop uop);
    void sta_wakeup_stage();
    void sta_pipeline(); // scan through the SOB to find schedulable uops for execution (counterpart to load_pipeline())
    void execute_sta(SOBEntry& sob_entry); // execute an individual STA uop for address resolution (counterpart to
                                           // load_pipeline_execute())
    bool can_store_pipeline_schedule() const { return stores_executed < store_pipes; }
    bool can_sta_schedule(SOBEntry& sob_entry)
        const; // return true if an STA can be scheduled to the store pipe (counterpart to can_load_schedule())
    void store_wakeup_dependent_load(LOBEntry& lob_entry, uint64_t sob_alloc_id, WakeCode code);
    void store_wakeup_dependent_loads(uint64_t sob_alloc_id, WakeCode code);
    void store_address_raw_scrub(const SOBEntry& sob_entry);
    void complete_store_recycle(In<uint64_t>& store_recycle_in, bool force = false);
    void store_complete_stage();
    void store_delayed_completion_stage();
    void complete_pending_stores();
    bool is_combinable_store(const SOBEntry& sob_entry) const;
    bool st_retire_load_exists() const;
    SOB::iterator next_store_to_retire();
    bool next_store_has_issued(uint64_t oldest_store_id) const;
    MemReq create_store_request(const StoreRetirementOps& req, Uop l2_uop, const StoreRetirementOps::Part& l2_part);
    void send_l2_store_miss_request(const StoreRetirementOps& req, Uop l2_uop, const StoreRetirementOps::Part& l2_part);
    bool store_retirement_schedule(Pipeline<StoreRetirementOps>::Data ops);
    bool store_retirement_stall(Pipeline<StoreRetirementOps>::Data ops);
    bool store_retirement_conflict(Pipeline<StoreRetirementOps>::Data ops);
    bool store_retirement_execute(Pipeline<StoreRetirementOps>::Data ops);
    SOB::iterator delete_sob_entry(SOB::iterator sob_iter, bool is_flush);
    void return_sob_credit(SOB::iterator sob_iter, bool is_flush);
    void sob_deallocate_stage();

    void store_retirement_write_cache(Txn tx, VirtAddr virt_addr, unsigned data_size);
    std::optional<StoreStallReason> check_store_retirement_stalls(VirtAddr virt_addr, bool write_through);

    bool can_evict_to_l2() const;
    void evict(MemReq mem_req);
    void send_l2_evict_request(Txn tx, const L1DCacheline& evicted);

    void gpc_store_commit_response();
    void gpc_load_commit_response();

    template<class Entry>
    CacheResult l1d_cache_access(Entry& e, bool spec_lookup);
    void l1d_cache_refill(MemResp mem_resp,
                          VirtAddr virt_addr,
                          bool modified,
                          bool exclusive,
                          bool hw_prefetch,
                          bool sw_prefetch,
                          PrefetchType prefetch_type);
    void l1d_cache_invalidate_aliases(Txn tx, VirtAddr virt_addr, PhysAddr phys_addr, bool exclude_self);
    bool can_send_l2_prefetch_request(const Uop& uop, PrefetchType type) const;
    void send_l2_read_request(MemReq& mem_req);
    void send_l2_prefetch_request(MemReq& mem_req, bool prefetch);
    SLOBEntry* try_release_ofb_from_entry(uint64_t alloc_id, bool spec_lookup);
    template<class Entry>
    SleepReason l1d_cache_miss(Entry& e, bool spec_lookup);
    bool l1d_cache_write_miss(const StoreRetirementOps& req, const StoreRetirementOps::Part& part, SOBEntry& e);
    uint64_t compute_l2c_set(PhysAddr pa);
    void l2c_snoop_request();
    bool refill_requires_eviction(VirtAddr virt_addr, PhysAddr phys_addr) const;
    void l2c_refill_response();
    static bool l2c_refill_pipe_consumed(const ZeroDelayOutIn<L2RefillArbitration>& l2c_refill_arbitration,
                                         uint8_t pipe_mask);
    static uint8_t l2c_refill_pipes(const ZeroDelayOutIn<L2RefillArbitration>& l2c_refill_arbitration);
    void l2c_refill_tag_conflicts();
    void l2c_refill_evict_conflicts();
    void l2c_refill_schedule(ZeroDelayOutIn<L2RefillArbitration>& l2c_refill_arbitration);
    void l2c_refill_evict_schedule();
    void l2c_refill_complete();
    void l2c_refill_wakeup();
    void dmmu_refill_wakeup();
    void l2c_refill_prefetch_complete(MemResp& mem_resp, OFBEntry& ofb_entry, bool hw_prefetch);
    void l2c_refill_demand_complete(MemResp& mem_resp, OFBEntry& ofb_entry);
    void pwc_schedule();

    void handle_flush();
    void clear_flush();
    void prefetch_schedule();
    void prefetch_issue(L1PrefetchEntry next_pf, PrefetchType type);
    void prefetch_execute(L1PrefetchEntry next_pf, PrefetchType type);

    bool can_send_dmmu_request(uint8_t channel_mask) const;
    void send_dmmu_request(MMUReq req, unsigned channel);

    template<class Entry>
    bool dl1_tlb_access(Entry& e);
    template<class Entry>
    void dl1_tlb_miss(Entry& e);
    void dmmu_refill_complete();
    void dmmu_refill_response();
    void dmmu_refill_acks();
    void dmmu_wakeup_otb_full();
    void tlb_invalidations();
    void wakeup_non_speculative();

    template<typename Buffer>
    void wakeup_by_code(WakeCode code, Buffer& slob);
    template<class Entry>
    void dl1_tlb_refill_and_wakeup(Entry& e, PhysAddr phys_addr);
    void wakeup_l2c_dependents(PhysAddr phys_addr);
    template<typename Buffer>
    void wakeup_mmu_dependents(uint64_t otb_id, Buffer& slob);
    template<typename Buffer>
    void sleep_mmu_dependents(uint64_t otb_id, WakeCode wake_code, Buffer& slob);
    template<typename Buffer>
    void dmmu_refill_sleep_or_wake(MMUAck mmu_ack, Buffer& slob);
    void wakeup_mlb_dependents();
    void release_mlb_entries();
    void wakeup_stret_dependents();
    void complete_load_recycle(In<uint64_t>& load_recycle_in);
    void sleep_sob_mmu_dependents(uint64_t otb_id, WakeCode wake_code);
    void deallocate_lob_entries(uint loads_committed);
    void clear_invalid_entries(uint64_t flush_alloc_id);

    // Stat collection functions
    void measure_load_execute_disorder(const Uop& uop);
    void measure_load_miss_disorder(const Uop& uop);

    bool can_writeback_uop(const Uop& uop, bool as_load = false) const;
    bool can_complete_uop(const Uop& uop) const;

    void writeback_load(LOBEntry& lob_entry);

    bool early_store_completion_allowed() const;
    bool sta_only_needs_completion(const SOBEntry& sob_entry) const;
    bool sta_needs_schedule_for_completion(const SOBEntry& sob_entry) const;
    bool can_complete_store(const SOBEntry& sob_entry) const;
    bool can_complete_exclusive_store(const SOBEntry& sob_entry) const;
    bool can_complete_non_exclusive_store(const SOBEntry& sob_entry) const;
    bool can_complete_barrier(const SOBEntry& sob_entry) const;
    void writeback_store(SOBEntry& sob_entry, const Uop& uop, bool as_load = false);
    void writeback_sta_load(SOBEntry& sob_entry, const Uop& uop) { writeback_store(sob_entry, uop, true); }
    void complete_store(SOBEntry& sob_entry, const Uop& uop, bool writeback = false, bool as_load = false);
    void try_completing_store(SOBEntry& sob_entry, const Uop& uop, bool as_load = false);

    template<class Entry>
    SleepReason ofb_full_recycle(Entry& entry, bool force = false);
    Uop uop_for_ofb(const OFBEntry& entry) const;

    // Utility functions
    void print_lob(std::ostream& os) const;
    void print_sob(std::ostream& os) const;
    void dump(std::ostream&) const override;

    size_t load_pipes_used{0};
    uint8_t loads_sched_pipe{0};
    size_t stores_executed{0};
    uint8_t stores_sched_pipe{0};
    size_t mmu_miss_request_channels{0};
    std::vector<VirtAddr> l2c_refill_inflight_addresses;
    std::map<uint64_t, bool> pending_store_completions;
    uint64_t block_on_dl1tlb_refill{0};
    std::optional<uint64_t> flush_id{std::nullopt};
    bool next_store_is_ordered{false};
    bool can_commit_barrier{true};
    unsigned retire_pending_store_releases{0};
    bool l2_pending_barrier{false};
    bool l2_pending_store_release{false};
    bool l2_pending_store_excl{false};
    uint64_t non_spec_uop_id{0};
    int sob_barrier_count{0};
    int mlb_in_use{0};
    unsigned delayed_stores{0};
    std::list<std::pair<PhysAddr, Cycle>> snooped_lines;
    std::list<std::pair<VirtAddr, Cycle>> loads_in_raw_scrub_window;
    Cycle barrier_can_execute_cycle{0};
    unsigned store_recycle_latency = 1;
    uint deferred_lob_dealloc{0};
    uint64_t used_dl1_ports{0};

    std::deque<uint64_t> inflight_store_data_complete;
    std::deque<MemReq> evictions;

    ZeroDelayOutIn<L2RefillArbitration> l2c_refill_load_arbitration{"l2c_refill_load_arbitration"};
    ZeroDelayOutIn<L2RefillArbitration> l2c_refill_store_arbitration{"l2c_refill_store_arbitration"};
    ZeroDelayOutInOpt<L2RefillArbitration> l2c_refill_evict_arbitration{"l2c_refill_evict_arbitration"};

    // LSU structures
    // "Reverse" TLB to aid in enforcing non-synonym condition of physical tagging
    L1DCache l1d_cache{"l1d_cache"};
    LSUCacheConflicts l1d_cache_conflicts{l1d_cache, load_pipes};
    L1TLB<VirtAddr, TLBType::DATA> dl1tlb{"dl1tlb"};

public:
    // Should ideally be private, but used in core.cc for L2 prefetcher connection
    Submodule<BackendPrefetcher<BackendMemReq>> prefetcher{"prefetcher"};

private:
    In<BackendMemReq> prefetch_request{"prefetch_request_in"};

    LOB lob;
    MEMROB lub;
    SOB sob;
    OFB ofb{"ofb", IXU_PARTITIONS};
    // Keeps LSU uops from dispatch to commit (loads) or retire (stores)
    MEMROB preview_rob;
    CreditPool pfb{"pfb"};
    SFPredictor sf_predictor{"sf_predictor"};
    unsigned raw_scrub_lower_address_bit{6};

    EnabledSubmodule<LSUPWC> pwc{"pwc", false};
    InOpt<LSUPWC::Request> pwc_request{"pwc_request"};
    OutOpt<LSUPWC::Response> pwc_response{"pwc_response"};
    InOpt<std::pair<uint64_t, PhysAddr>> pwc_deallocate{"pwc_deallocate"};

    // count of how many gathers we try to return to IXU each cycle
    std::uint64_t gather_wake_sent{0};

    std::uint64_t last_lob_id_executed{0};
    std::uint64_t last_lob_id_miss_request{0};
    std::uint64_t youngest_lob_id_miss_request{0};

    std::uint64_t atomic_loads_wrong = 0;
    std::uint64_t atomic_loads_speculated = 0;
    std::uint64_t speculative_atomic_load_cooldown = 0;

    // Internal ports
    OutIn<MMUResp> dmmu_refill_complete_pipe{"dmmu_refill_complete_pipe"};
    OutIn<MMUResp> dmmu_refill_wakeup_pipe{"dmmu_refill_wakeup_pipe"};

    // Load-refill conflicts
    OutIn<MemResp> l2c_refill_complete_pipe{"l2c_refill_complete_pipe"};
    ZeroDelayOutIn<MemResp> l2c_refill_wakeup_pipe{"l2c_refill_wakeup_pipe"};
    Out<L2RefillArbitration> l2c_refill_tag_conflict_out{"l2c_refill_tag_conflict_out"};
    Read<L2RefillArbitration> l2c_refill_tag_conflict_in{"l2c_refill_tag_conflict_in"};

    OutIn<uint64_t> ld_recycle_complete{"ld_recycle_complete"};
    OutIn<uint64_t> ld_fast_recycle_complete{"ld_fast_recycle_complete"};
    OutIn<uint64_t> ld_slow_recycle_complete{"ld_slow_recycle_complete"};
    OutIn<uint64_t> ld_fill_recycle_complete{"ld_fill_recycle_complete"};
    OutIn<uint64_t> ld_split_recycle_complete{"ld_split_recycle_complete"};
    OutIn<uint64_t> st_recycle_complete{"st_recycle_complete"};
    OutIn<uint64_t> st_exec_recycle_complete{"st_exec_recycle_complete"};
    ZeroDelayOutIn<uint64_t> exclusive_return_delay{"exclusive_return_delay"};
    OutIn<uint64_t> stlr_wakeup_delay{"stlr_wakeup_delay"};

    OutIn<Uop> load_complete{"load_complete"};
    OutIn<uint64_t> store_complete{"store_complete"};
    OutIn<uint64_t> store_delayed_completion{"store_delayed_completion"};
    OutIn<Uop> store_data_complete{"store_data_complete"};

    OutIn<std::pair<uint64_t, bool>> sta_wakeup{"sta_wakeup"};
    OutIn<uint64_t> store_data_early_complete{"store_data_early_complete_pipe"};
    OutIn<uint64_t> mlb_entry_release{"mlb_entry_release"};
    OutIn<bool> mlb_wakeup{"mlb_wakeup"};
    OutIn<uint64_t> stret_wakeup{"stret_wakeup"};
    OutIn<SOB::iterator> sob_dealloc{"sob_dealloc"}; // LR06

    PipelineGroup<StoreRetirementOps> store_retirement_pipe{
        "store_retirement_pipe",
        {
            AMPERE_PIPE_STAGE_M(LSU, store_retirement_schedule), // LR02
            AMPERE_PIPE_STAGE_M(LSU, store_retirement_stall),    // LR03
            AMPERE_PIPE_STAGE_M(LSU, store_retirement_conflict), // LR04
            AMPERE_PIPE_STAGE_M(LSU, store_retirement_execute),  // LR05
        },
        true,
        1,
        [](const StoreRetirementOps& a, const StoreRetirementOps& b) {
            const auto& ap = a.ops.front();
            const auto& bp = b.ops.front();
            return ap.alloc_id == bp.alloc_id ? !ap.hi_half : ap.alloc_id < bp.alloc_id;
        }};

    // Load events
    Event load_pipe_occupancy_event{"load_pipe_occupancy", "LD pipe and partition occupied (LD01)"};
    Event store_pipe_occupancy_event{"store_pipe_occupancy", "Store pipe occupied"};
    Event ev_load_bypass_from_ixu{"load_bypass_from_ixu", "Load executed as fast as possible after arriving from IXU"};
    Event ev_store_bypass_from_ixu{"store_bypass_from_ixu",
                                   "Store executed as fast as possible after arriving from IXU"};
    EventData<uint8_t> pipe_partition{"pipe_partition", "partition being scheduled"};
    EventData<unsigned> load_dl1_port{"dl1_port", "DL1 port used by this load"};
    EventData<PipeOccupant> pipe_occupant{"pipe_occupant", "occupant being scheduled: ld, sta_ld, pf, refill"};
    Event uop_ld_execute_event{"uop_ld_execute", "Uop LD execute (LD01)"};
    Event uop_ld_combining_event{"uop_ld_combining", "Load was able to combine with another on pipe"};
    Event uop_ld_recycle_event{"uop_ld_recycle", "Uop LD recycle"};
    EventData<RecycleReason> ld_recycle_reason{"ld_recycle_reason", "Reason for load recycle"};
    Event uop_ld_complete_event{"uop_ld_complete", "Uop LD complete"};
    Event uop_ld_st_fwd_event{"uop_ld_st_fwd", "Uop LD store-forward"};
    Event uop_ld_pwc_fwd_event{"uop_ld_pwc_fwd", "Uop LD forwarded data from PWC"};
    Event uop_read_read_chunk_sch_conflict_event{"uop_read_read_chunk_sch_conflict",
                                                 "Uop read-read chunk conflict at LD01"};
    Event uop_write_read_chunk_bank_sch_conflict_event{"uop_write_read_chunk_bank_sch_conflict",
                                                       "Uop write-read chunk & bank conflict at LD01"};
    Event uop_write_read_fill_load_sch_conflict_event{"uop_write_read_fill_load_sch_conflict",
                                                      "Uop write-read fill/load conflict at LD01"};
    Event uop_ld_pa_tag_miss_event{"uop_ld_pa_tag_miss", "Load miss due to VA tag hit with PA tag miss"};

    Event lob_write_event{"lob_write", "LOB write"};
    EventData<unsigned> evd_lobid{"lobid", "lob id of the uop written into LOB"};
    EventData<archx::inum_t> evd_lob_entry_inum{"inum", "inum of the uop written into LOB"};
    Event slob_sleep_on_existing_req_event{"slob_sleep_on_existing_req", "L1D miss sleep on existing L2 request"};
    Event slob_ofb_update_take_event{"slob_ofb_update_take", "S/LOB entry took an OFB record from younger read req"};
    Event slob_ofb_update_give_event{"slob_ofb_update_give", "S/LOB entry gave an OFB record to older read req"};

    Event block_load_pipe_dl1tlb_refill_event{"block_load_pipe_dl1tlb_refill", "DL1TLB refill blocks load pipe"};
    Event canonical_fault{"canonical_fault", "Canonical check failed in DTLB"};

    Event uop_ld_raw_scrub_detected_event{"uop_ld_raw_scrub_detected",
                                          "STA identified younger load that should have forwarded from store"};
    Event ev_exclusive_duplicate_ofb{"exclusive_duplicate_ofb",
                                     "Exclusive load waited for a duplicate OFB to complete"};
    Event ev_raw_wakeup{"raw_wakeup", "Load with RAW error woke up and re-executed instead of getting scrubbed"};
    Event ev_raw_scrub_false_hi{"raw_scrub_false_hi", "Raw scrub matched PA[27:0] but not the full PA"};
    Event ev_raw_scrub_false_lo{"raw_scrub_false_lo",
                                "Raw scrub which would not have happened with finer granularity check"};
    Event uop_ld_st_prediction_event{"uop_ld_st_prediction", "Load store forward prediction was used"};
    Event uop_ld_wait_error_event{"uop_ld_wait_error", "Load store forward prediction was to wait but mispredicted"};
    EventData<bool> ld_st_prediction_data{"ld_st_prediction", "Counter value for store forward prediction"};

    Event uop_ld_dependent_on_sta_wakeup_event{"uop_ld_dependent_on_sta_wakeup",
                                               "Load waiting on unknown store address is woken by address resolution"};
    Event uop_ld_dependent_on_std_wakeup_event{"uop_ld_dependent_on_std_wakeup",
                                               "Load waiting to forward on store data is woken by data complete"};
    Event uop_ld_dependent_on_stret_wakeup_event{"uop_ld_dependent_on_stret_wakeup",
                                                 "Load waiting for store retirement is woken"};
    Event uop_ld_cacheline_split_event{"uop_ld_cacheline_split", "Load spans multiple cachelines"};
    Event uop_st_cacheline_split_event{"uop_st_cacheline_split", "Store spans multiple cachelines"};
    Event uop_ld_mlb_wakeup_event{"uop_ld_mlb_wakeup", "Load woke up when an MLB entry became available"};
    Event uop_ld_stlr_sleep_event{"uop_ld_stlr_sleep", "Load-acquire went to STLR sleep"};
    Event uop_ld_stlr_wakeup_event{"uop_ld_stlr_wakeup",
                                   "Load-acquire woke up due to L2 signal saying older releases are complete"};
    Event uop_ld_stlr_nonspec_wakeup_event{"uop_ld_stlr_nonspec_wakeup",
                                           "Load-acquire woke up because it was non-spec"};
    Event uop_ldar_stret_sleep{"uop_ldar_stret_sleep", "Load-acquire went to ST_RETIRE sleep on older unknown store"};
    Event uop_ldar_raw_scrub{"uop_ldar_raw_scrub", "Load-acquire was raw-scrubbed by execution of older store-release"};
    Event uop_ld_st_fwd_walk_event{"uop_ld_st_fwd_walk", "Uop LD store-forward walk"};
    Event ev_partial_data_forward{"partial_data_forward", "Load forwarded partial data from a store"};
    Event ev_partial_data_early_exit{
        "partial_data_early_exit",
        "Load got all its remaining bytes from a partial-data store and ended its SF walk early"};
    Event ev_no_clear_l1_bits{"no_clear_l1_bits",
                              "Two L2 requests to same L2 set, do not clear L1D-present bits in L2"};
    Event ev_load_inval_pcp{"load_inval_pcp", "A pending PCP prefetch was invalidated by the execution of a load"};

    Event ev_load_predict_wait{"load_predict_wait", "Load waited on an unknown store based on SFP prediction"};
    Event ev_load_src_match_wait{"load_src_match_wait",
                                 "Load waited on an unknown store based on source register matching"};
    EventData<bool> evd_unknown{"unknown", "Store the load waited on had an unknown VA"};
    EventData<bool> evd_correct{"correct", "Store the load waited on is a true address match"};

    Event uop_st_exclusive_release_event{"uop_st_exclusive_release", "Exclusive/atomic store saw release"};
    Event uop_st_speculative_exclusive_event{"uop_st_speculative_exclusive", "Speculative exclusive"};
    Event uop_st_atomic_load_complete_event{"uop_st_atomic_load_complete", "Atomic load completed"};
    Event uop_st_speculative_atomic_event{"uop_st_speculative_atomic", "Speculative atomic load"};
    Event uop_st_speculative_atomic_abort_event{"uop_st_speculative_atomic_abort", "Speculative atomic load aborted"};

    Event l2_resp_drop_event{"l2_resp_drop", "L2 response was dropped"};

    EventData<uint64_t> ld_st_fwd_entry_data{"ld_st_fwd_entry", "LD store-forward entry"};
    EventData<uint64_t> ld_raw_entry_data{"ld_raw_entry", "LD RAW error entry"};
    EventData<uint64_t> sta_txid{"sta_txid", "STA that detected RAW error entry"};
    EventData<uint64_t> sta_raw_entry_data{"sta_raw_entry", "STA that detected RAW error entry"};
    EventData<unsigned> sta_raw_type_data{"sta_raw_type", "STA that detected RAW error store type"};
    EventData<uint64_t> sta_wakeup_ld_data{"sta_wakeup_ld", "STA that wakes up load"};
    EventData<uint64_t> std_wakeup_ld_data{"std_wakeup_ld", "STD that wakes up load (associated STA)"};
    EventData<unsigned> sfp_index_data{"sfp_index", "Store forward predictor index"};
    EventData<uint64_t> ld_address_data{"ld_address", "Load address"};
    EventData<uint64_t> st_address_data{"st_address", "Store address"};
    EventData<unsigned> ld_datasize_data{"ld_datasize", "Size (bytes) of the load"};
    EventData<unsigned> st_datasize_data{"st_datasize", "Size (bytes) of the store"};

    Event l2_prefetch_event{"l2_prefetch", "LSU sends an L2 prefetch"};

    Event uop_std_execute_event{"uop_std_execute", "Uop STD execute (LW01)"};
    Event uop_sta_execute_event{"uop_sta_execute", "Uop STA execute (LS01)"};
    Event uop_st_recycle_event{"uop_st_recycle", "Uop ST recycle"};
    EventData<RecycleReason> st_recycle_reason{"st_recycle_reason", "Reason for store address recycle"};
    Event uop_sta_complete_event{"uop_sta_complete", "Uop STA complete"};
    Event block_sta_pipe_dl1tlb_refill_event{"block_sta_pipe_dl1tlb_refill", "DL1TLB refill blocks STA pipe"};

    Event uop_sta_early_complete_event{"uop_sta_early_complete", "Early STA completion"};
    Event uop_sta_delayed_complete_event{"uop_sta_delayed_complete", "Delayed STA completion"};

    Event uop_st_retire_schedule_event{"uop_st_retire_schedule", "Uop ST retire schedule (GC02/LR02)"};
    Event uop_st_retire_stall_event{"uop_st_retire_stall", "Uop ST retire stall"};
    Event uop_st_retire_complete_event{"uop_st_retire_complete", "Uop ST retire complete"};
    Event uop_st_retire_combine_event{"uop_st_retire_combine", "Two stores combined in the retirement pipeline"};
    Event uop_st_retire_delay_event{"uop_st_retire_delay", "Retirement delayed in order to combine"};

    Event pwc_schedule_stall_event{"pwc_schedule_stall", "PWC schedule was stalled"};

    EventData<StoreStallReason> st_retire_stall_cause_data{"st_retire_stall_cause",
                                                           "Cause of the store retirement stall"};

    Event sob_write_event{"sob_write", "SOB write"};
    Event sob_update_event{"sob_update", "SOB update from STA/STD"};
    Event sob_deallocate_event{"sob_deallocate", "Deallocated SOB entry"};

    // STA-load events
    Event uop_sta_load_execute_event{"uop_sta_ld_execute", "Uop STA-load execute (LD01)"};
    Event uop_sta_load_complete_event{"uop_sta_ld_complete", "Uop STA-load complete"};
    Event uop_sta_load_split_lo_complete_event{"uop_sta_ld_split_lo_complete", "Uop STA-load low split complete"};

    // Other events
    Event ev_snoop{"snoop", "LSU got a snoop from L2C"};
    Event ev_snoop_inval{"snoop_inval", "Line invalidated due to snoop"};
    Event ev_snoop_not_present{"snoop_not_present",
                               "Line was not in cache but snoop was sent anyways (L2 thought it was)"};
    Event ev_snoop_flush{"snoop_flush", "Flush due to LOB snoop hit"};
    Event ev_snoop_flush_false_hit{"snoop_flush_false_hit",
                                   "Flush due to LOB snoop hit which matched PA[27:6] but not the full PA"};
    Event ev_snoop_flush_false_hi_hit{
        "snoop_flush_false_hi_hit",
        "Flush due to LOB snoop hit which matched PA[11:6] of the high half of a split but not the full PA"};

    Event ev_cache_require_unique{"cache_require_unique", "Line is required to be in unique for update"};
    Event ev_cache_write_modified{"cache_write_modified", "Write to modified data in L1d"};
    Event ev_cache_write_miss{"cache_write_miss", "Cache write miss"};

    Event ev_snoop_inval_modified{"snoop_inval_modified", "Modified line invalidated due to snoop"};
    Event ev_inval_modified_refill{"refill_inval_modified", "Modified line invalidated due to refill"};
    Event ev_inval_modified_alias{"alias_inval_modified", "Modified line invalidated due to alias"};
    Event ev_inval_modified{"inval_modified", "Modified line invalidated"};
    Event ev_inval_modified_no_l2_write{"inval_modified_no_l2_write", "Modified line invalidated but no L2 write"};

    Event ev_refill_dropped{"refill_dropped", "Fill dropped, either due to eviction unable to schedule or atomics"};
    Event ev_refill_dropped_from_evict{"refill_dropped_from_evict", "Fill dropped due to eviction unable to schedule"};
    Event ev_refill_completion_dropped{"refill_completion_dropped", "Unable to complete uop due to fill being dropped"};
    EventData<UnableToFillReason> ev_refill_drop_reason{"refill_drop_reason", "Refill drop reason"};

    // PMU
    Event pmu_event{"pmu", "A PMU event has occurred"};
    EventData<PMU> pmu_num_data{"event", "PMU event number"};

    Event ev_load_execute_disorder{"load_execute_disorder",
                                   "A load uop was disordered with respect to program order of last executed load"};
    Event ev_load_miss_disorder{"load_miss_disorder",
                                "A load uop made a fill request out of program order with respect to older requests"};
    Event ev_load_miss_order_restore{
        "load_miss_order_restore",
        "After a disordered miss a new miss occurs that is younger than the previously youngest miss"};
    EventData<unsigned> evd_load_disorder_distance{
        "distance", "Magnitude of difference between last load and current load in program order"};

    // PFB events
    Event ev_pfb_allocate_event{"pfb_allocate", "PFB allocate"};
    Event ev_pfb_deallocate_event{"pfb_deallocate", "PFB deallocate"};
    Event ev_pfb_full_event{"pfb_full", "PFB full"};
};

} // namespace ampere::core

/*
 * vim: sw=4:ts=4:et:si:cindent:cino=\:0,(0,g0,t0:
 */
