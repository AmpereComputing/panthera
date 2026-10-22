//
// Copyright (c) 2021-2022 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//

#pragma once

#include "ampere/sc/sc.h"
#include "ampere/util/enum.h"
#include "ampere/core/uop.h"
#include "ampere/core/flush.h"
#include "ampere/archx/addr.h"
#include "ampere/core/backend_prefetcher.h"
#include "ampere/core/prefetchers/l1p_pcp.h"
#include "pmu.h"

#include <boost/circular_buffer.hpp>

/* Starting point: GRETCH from University of Waterloo. TACO, Feb 2021.
   https://dl.acm.org/doi/abs/10.1145/3439803 */

/* Gretch structures:
 * 1) ADT: address-data table
 * 2) RT: relationship table
 * 3) PTH: pcp transition history
 * 4) DRT: data retrieval table
 * 5) PQ: prefetch queue
 */

make_ns_enum(ampere::core, PCP_COMMUNICATION,
    HI_TO_LO,
    LO_TO_HI,
    STABLE_HI
);

make_ns_enum(ampere::core, GretchState,
    INIT,
    WAIT_TRIGGER,
    POPULATE_ADT,
    TRAIN
);

namespace ampere::core {

using namespace ampere::sc;

// Data structure to provide 2 pieces of information to Gretch's cacheline stepping logic:
//    1) trigger uop: required for PC association in RT
//    2) physical addr: required to know the data elements in the producer fill cacheline
struct FillCL
{
    BackendMemReq trigger_req;
    PhysAddr phys_addr{0};
    bool demand_refill{false};
    FillCL(BackendMemReq req, PhysAddr paddr, bool dr) : trigger_req(req), phys_addr(paddr), demand_refill(dr) {}
};

// items that need to be fed to GRETCH from the rest of the LSU (see in LSU code)
enum class GRETCHFeedItem {
    FILL,               // PCP or demand fill cacheline to Gretch's cacheline staging buffer
    COMMITTED_LOAD,     // committed load input to DRT, ADT, and RT
    PCP_STATE_CHANGE_PC // PC that exhibits our desired state change behavior (default: ACT_HI->ACT_HI)
};

class PCPTraining : public StrideTraining<BackendMemReq>
{
    bool aggressive{false};
    boost::circular_buffer<BackendMemReq> trained;
    Method listening;

public:
    PCPTraining(PCP_COMMUNICATION communication, unsigned size, bool aggressive) : aggressive(aggressive), trained(size)
    {
        if (communication == PCP_COMMUNICATION::HI_TO_LO)
            listening = Method::HI_TO_LO;
        else if (communication == PCP_COMMUNICATION::LO_TO_HI)
            listening = Method::LO_TO_HI;
        else
            listening = Method::STABLE_HI;
    }
    void train(BackendMemReq uop, Method method) override
    {
        if (method == listening)
            trained.push_back(uop);
    }
    std::optional<BackendMemReq> gather() override
    {
        if (trained.empty())
            return {};

        auto req = trained.front();
        trained.pop_front();
        return req;
    }
    bool empty() const { return trained.empty(); }

    bool should_inc_multiplier(PrefetchType prefetch_type) const override
    {
        return prefetch_type == PrefetchType::GRETCH && aggressive;
    }
};

class GRETCH : public BackendPrefetcher<BackendMemReq>
{
    static const int OFFSET_THRESHOLD = 128;
    static const unsigned MAX_ADDRESSES = 2;

public:
    Param<size_t> adt_size{"adt_size", "number of entries in address-data table (ADT)", 8};
    Param<size_t> rt_size{"rt_size", "number of entries in relationship table (RT)", 6};
    Param<unsigned> prefetch_queue_size{"prefetch_queue_size", "number of prefetches to keep in the prefetch queue", 8};
    Param<size_t> drt_size{
        "drt_size", "number of entries in DRT, i.e., the secondary storage to retrieve data at commit time", 8};
    Param<size_t> pth_size{"pth_size", "number of entries in PTH (PCP Transition History)", 4};
    Param<unsigned> commit_threshold{
        "commit_threshold", "threshold number of commit loads to stop ADT population", 110};
    Param<PCP_COMMUNICATION> pcp_communication{"pcp_communication",
                                               "PCP communicates the desired PC on hi_to_lo, lo_to_hi, or stable_hi",
                                               PCP_COMMUNICATION::STABLE_HI};
    Param<bool> page_fetch{"page_fetch", "fetch TLB on page miss", true};
    Param<int> confidence_threshold{"confidence_threshold", "confidence threshold for RT entry", 3};
    Param<bool> recycle_tlb_miss{
        "recycle_tlb_miss",
        "if prefetch candidate encounters TLB miss, hold it aside and re-issue it after miss handling is done",
        true};
    Param<unsigned> commit_bw{
        "commit_bw", "commit bandwidth (i.e., number of commit loads per cycle that can be used to populate ADT)", 2};
    Param<bool> guaranteed_training{
        "guaranteed_training", "LOB will not dellocate more than commit_bw loads per cycle", false};
    // 8 cycles default -> assuming 1 bit/cycle divider and we require 8 least significant bits of the result (since
    // offset threshold is 128)
    Param<CycleDelta> long_lat{"long_lat", "latency (in cycles) for ADT pair computations requiring divisions", 8};
    Param<CycleDelta> low_lat{
        "low_lat", "latency (in cycles) for ADT pair computations where division can be skipped", 1};
    Param<unsigned> data_bw{
        "data_bw",
        "number of data items in the PCP-brought-back cacheline that can provided to the cacheline-handling logic",
        2};
    Param<unsigned> div_bitwidth{"div_bitwidth", "Operand size of divider", 8};
    // should be set to false for Gretch unit tests
    Param<bool> use_drt{"use_drt",
                        "use the secondary data storage to retrieve data at commit time (as opposed to perfect data "
                        "retrieval from per-LOB-entry 8B data)",
                        true};
    Param<unsigned> demand_producer_runahead{
        "demand_producer_runahead",
        "how many data items away from the exact address to start for prefetch generation",
        3};
    Param<CycleDelta> on_cycles{"on_cycles", "cycles for which gretch is on and under monitoring", 16384};
    Param<CycleDelta> off_cycles{"off_cycles", "cycles for which gretch is off", 65536};
    Param<bool> dynamic_adjust_training{"dynamic_adjust_training", "dynamically turn off gretch training", true};
    Param<unsigned> num_cacheline_buffers{
        "num_cacheline_buffers", "number of cacheline buffers to store incoming cachelines for prefetch generation", 1};
    Param<bool> pq_drop_incoming{
        "pq_drop_incoming", "when prefetch queue is full, drop incoming prefetch candidate", true};
    Param<bool> secure_gretch{"secure_gretch", "pause Gretch in non-EL0 context", true};
    Param<bool> force_insert{"force_insert", "force insertion of a particular relationship into RT", false};
    Param<Cycle> force_cycle{
        "force_cycle", "cycle (relative to first instruction commit) at which the relationship is force-inserted", 0};
    Param<bool> dynamic_conf{"dynamic_conf", "dynamically adjust confidence of RT entries", true};
    Param<bool> aggressive_pcp_control{
        "aggressive_pcp_control",
        "Demand load hitting Gretch OFB directly multiplies stride_mult (overriding slow stride increase)",
        true};
    Param<bool> filter_close_base_addrs{
        "filter_close_base_addrs", "filter same PC/close base addrs at RT insertion", false};
    Param<bool> filter_close_base_addrs_pref_gen{
        "filter_close_base_addrs_pref_gen", "filter same PC/close base addrs at prefetch generation", false};
    Param<unsigned> lo_bit{"lo_bit", "low bit for deciding close base addr", 6};
    Param<unsigned> max_address_range{"max_address_range", "Maximum address MSB", 0};
    Param<bool> drop_nullptr{"drop_nullptr", "Drop prefetches for nullptr", false};

    explicit GRETCH(const sc_core::sc_module_name& name);
    struct ADTEntry;
    struct RTEntry;
    struct DRTEntry;

    struct CLDataSlice;

    PCPTraining pcp_training;

    // Port connections to provide individual cacheline data items from producer cacheline to generate GRETCH prefetch
    // candidates from RT
    OutInOpt<GRETCH::CLDataSlice> gen_pref{"gen_pref"};
    // Port connections to provide committed loads to DRT, ADT, and RT
    OutInOpt<BackendMemReq> ld_commit{"ld_commit"};

    bool run() override;

    /* "gretch_train" functions use the information provided by "gretch_feed" functions
        to populate ADT, perform ADT computations, and generate prefetches
        from RT (by calling Gretch internal functions). */
    void train_adt_populate_measure_confidence();
    void train_adt_compute();
    void train_gen_prefetch();
    void train_supply_cacheline_data();

    void issue_impl(uint64_t id, BackendMemReq req) override;
    void writeback_impl(uint64_t id, BackendMemReq req) override;
    bool commit_impl(uint64_t id, BackendMemReq req) override;
    void fill_impl(BackendMemReq req, bool fb_pref, bool was_alloc_by_pf) override;
    void flush_impl(Flush flush_info) override;
    PrefetchType prefetch_type() const override { return PrefetchType::GRETCH; }

    bool can_prefetch_page(BackendMemReq req) const override { return page_fetch; }
    bool can_replay() const override { return recycle_tlb_miss; }
    using BackendPrefetcher::can_allocate_pob;
    bool can_allocate_pob(BackendMemReq req, archx::EL current_level) const override;
    bool can_issue_prefetch() const override;

    // ................... Functions to populate ADT ...................................
    void update_pcp_state_change_pc();
    bool update_pth(
        archx::ProgramCounter pc); // collect desired-PCP-state-change PC in PTH. If the PC is inserted, return true
    std::pair<uint64_t, bool> handle_load_committed_from_lsu(
        BackendMemReq req, bool use_for_gretch_training); /* Use committing load to invalidate DRT and populate ADT.
                                                           * Return the data and data_valid which will subsequently
                                                           * be used for confidence measurement. */
    // ................... Functions for DRT .........................
    enum class DRTOpType {
        ALLOCATE_LOB_ID,
        WRITE_DATA,
        LOOKUP_DATA,
        INVALIDATE
    };
    bool manage_drt(
        DRTOpType op,
        uint64_t alloc_id,
        uint64_t lobId,
        std::optional<ProgramCounter> pc = std::nullopt,
        const Uop& uop = nullptr /*added later just for events*/); // one-stop function to perform any of the above ops

    // ................... Functions and DS for operations once ADT population epoch is done
    // ............................
    void train_on_adt_contents(BackendMemReq req); // establish data dependence relationship between various ADT pairs
    void update_rt(int index, int comp_id);        // take item from temporary RT table and insert into actual RT table
    BackendMemReq& get_adt_train_req() { return adt_content_train_req; } // just for tracking the uop in event stream
    bool inactive() const { return gated || (secure_gretch && (current_el_gpc_stage != archx::EL0)); }

    // struct to store the outcome of an ADT pair computation
    struct CompInfo
    {
        int comp_id;             // computation id
        bool high_latency;       // whether ADT pair computation is high latency
        int rt_table_temp_index; // if ADT pair computation resulted in a usable RT entry,
                                 // this is the index of that RT entry in temporary RT table.
        Cycle complete_cycle{0};

        // introduce latency but no successful RT entry. Examples: 1) performed div but offset > threshold; 2) non-zero
        // mod.
        CompInfo(int id, bool hl, Cycle cc) : comp_id(id), high_latency(hl), rt_table_temp_index(-1), complete_cycle(cc)
        {
        }

        CompInfo(int id, bool hl, int ind, Cycle cc)
            : comp_id(id), high_latency(hl), rt_table_temp_index(ind), complete_cycle(cc)
        {
        }
    };

    using CompQueue = boost::circular_buffer<CompInfo>;
    CompQueue comp_queue{
        (unsigned long)pow(2.0, adt_size)}; /* Store, in the form of CompInfo, the outcomes of ADT pair computations.
                                               Some cycles later, each CompInfo in this queue is used to retrieve RT
                                               entries from the temporary table and put into the actual RT. */

    // ............................. Functions and DS for handling incoming PCP cacheline
    // ..................................
    using CacheLine = std::vector<uint64_t>;
    boost::circular_buffer<std::pair<BackendMemReq, CacheLine>> cacheline_buffers{
        num_cacheline_buffers};      /* buffers to hold on to incoming PCP cachelines
                              while we take time to generate pref candidates.
                              Hold on to pcp_trigger_uop until
                              cacheline has been fully dealt with */
    Cycle accepted_pcp_cacheline{0}; // track that only one PCP cacheline per cycle is accepted
    void buffer_incoming_fill(BackendMemReq req, PhysAddr phys_addr, unsigned size, bool demand_refill);
    unsigned current_index_in_cacheline{0}; // index of the current data item in the current cacheline being dealt with
    uint8_t current_cacheline_num_rt_hits{
        0}; // Number of RT hit entries for the current cacheline being stepped through.
            // This is counted once just at the beginning before starting to step through the cacheline.
            // gap-ccsv-euroroad.125_35_20220318_a18474.is.xz is a snippet instruction stream where there is
            // some transient RT behavior during the stepping (another RT entry with same
            // producer PC reaches confidence threhsold during the stepping).
            // So, counting this in fine-grained fashion leads to variability.
    std::vector<RTEntry> current_cacheline_rt_hit_entries; // RT hit entries for the current cacheline are set aside in
                                                           // this staging storage this is modeled to make sure
                                                           // transient RT changes don't affect prefetch generation
    void process_and_supply_cacheline_data();
    uint8_t hit_producer_count(archx::ProgramCounter pc, bool set_aside_entries = false);
    boost::circular_buffer<CLDataSlice> supplied_cl_data{
        128}; // Storage only in Panthera for coding purposes.
              // Data slice in cacheline are handled one by one and placed here
              // to be used for prefetch address calculation.
              // Large #entries to make sure no data slice from the cacheline is lost.

    // ................... Functions and DS for building confidence in RT entries/issuing prefetches
    // ............................
    void update_confidence_at_commit(BackendMemReq req, uint64_t data_val, bool data_valid);
    void generate_prefetches(CLDataSlice& cl_data_slice);
    void handle_mmu_response(uint64_t id, bool fault);
    void handle_mmu_ack(uint64_t req_id, std::optional<uint64_t> otb_id);
    bool work_to_do() const;

    void manage_dynamic_training(); // for dynamically turning off Gretch training
    archx::EL current_el_gpc_stage{
        archx::EL0}; // Track EL id in the GPC stage, i.e., when Gretch sees a committing load.
                     // Any Gretch activity happening in the GPC stage can use this variable safely
                     // (e.g, trigger adt, populate adt, measure confidence, lookup data in drt).
                     // This variable is also used as a global notion to stop scheduling prefetches down load pipes
                     // and to register some events.
                     // Any other Gretch activity which may happen in a speculative pipeline stage additionally uses
                     // uop->archx_inst->state().exception_level (e.g., allocate LOB id in DRT, write DATA in DRT,
                     // buffer incoming producer fill)
    // PTH event
    Event ev_pth_input{"pth_input", "a new unique desired-PCP-state-change PC is collected in PTH"};

    // ADT training events
    Event ev_adt_train{"adt_train", "finished an ADT pair computation"};
    EventData<bool> evd_hl{"adt_train_high_lat", "whether ADT pair computation is high latency"};
    EventData<int> evd_comp_id{"adt_train_comp_id", "computation ID in this epoch's comp queue"};
    EventData<bool> evd_relationship_found{"relationship_found",
                                           "whether a relationship came oout of the ADT pair training"};

    EventData<unsigned> evd_lsu_pc{"lsu_pc", "9-bit PC available to LSU"};

    Event pmu_event{"pmu", "A PMU event has occurred"};
    EventData<PMU> pmu_num_data{"event", "PMU event number"};

    // print structures
    void rt_print(std::ostream& os) const;
    void prefetch_queues_print(std::ostream& os) const;

    // for Gretch tests
    GretchState get_state() const { return state; }
    unsigned get_num_addr() const { return num_addr_observed; }
    const auto& get_adt_table() const { return adt_table; }
    bool dd_exists(const ADTEntry& a, const ADTEntry& b, uint64_t& base_addr, uint64_t& offset, bool& early_exit)
    {
        return infer_array_indirect(a, b, base_addr, offset, early_exit);
    }
    void force_inject_in_rt(); // only for PV purposes

private:
    LocalRandom random;
    GretchState state{GretchState::INIT}; // GRETCH's state
    unsigned num_addr_observed{0};        // number of addresses that ADT has seen in this epoch

    std::vector<ADTEntry> adt_table;
    std::deque<GRETCH::RTEntry> rt_table;
    std::vector<DRTEntry> drt;

    BackendMemReq adt_content_train_req; // keep track of which uop starts training on ADT contents. For debug purposes
                                         // in event stream
    int num_comp_remaining{0}; // number of ADT pair computations remaining in this epoch for RT update operation
    std::vector<RTEntry> rt_table_temp; /* temporary RT table to store RT entries until right number of cycles have
                                           elapsed before the RT entries can be inserted into actual RT table.
                                           Real hardware will not have any temporary rt table. */
    int last_rt_table_temp_index{-1};   // keep track of the latest index of the temporary RT table for CompInfo

    std::vector<GRETCH::RTEntry>
        dd_pref_cand; // Just for debug, keep track of unique relationships which gave rise to prefetch candidates.

    bool force_inserted{false};
    Cycle time_of_first_commit{0};
    unsigned pref_this_cycle = 0;

    bool within_security_policy() const { return (!secure_gretch || current_el_gpc_stage == archx::EL0); }
    bool meets_security_policy(const BackendMemReq& req) const
    {
        return !secure_gretch || (secure_gretch && (req.exception_level.value_or(archx::EL0) == archx::EL0));
    }

    // ................... Helper functions to populate ADT ...................................
    bool activate_adt_populate_at_commit(BackendMemReq req,
                                         uint64_t data_val); // return true if a new round of ADT population started
    void populate_adt_at_commit(BackendMemReq req, uint64_t data_val, bool data_found);
    boost::circular_buffer<archx::ProgramCounter> pth{
        pth_size}; // PCP Transition History -> stores unique PCs which have exhibited the desired state change

    // ................... Helper functions to manage data storage for commit-time data retrieval
    // .........................
    bool allocate_drt(uint64_t alloc_id, uint64_t lob_id, const Uop& uop = nullptr);
    bool invalidate_drt(uint64_t alloc_id, uint64_t lob_id);
    bool lookup_data_in_drt(uint64_t alloc_id, uint64_t lob_id);
    bool write_data_drt(uint64_t alloc_id, uint64_t lob_id);
    unsigned hash_lob_id(uint64_t lob_id) const;

    // ................... Helper functions for operations once ADT population epoch is done
    // ............................
    void update_rt_raw(int index, int comp_id); // avoid redundancy in RT and insert the discovered relationship
    void rt_replacement(RTEntry& rt);
    bool infer_array_indirect(const ADTEntry& a,
                              const ADTEntry& b,
                              uint64_t& base_addr,
                              uint64_t& offset,
                              bool& early_exit); /* Is there array-indirect relationship between ADT entries a and b?
                                                    early_exit -> whether this computation turns out to be high latency
                                                    div (early_exit = false) */

    // ................... Helper functions for building confidence in RT entries/issuing prefetches
    // ............................
    void update_confidence(RTEntry& rt_entry, bool inc);
    void compute_prefetch_candidate(RTEntry& rt_entry, uint64_t data, BackendMemReq req);

    archx::ProgramCounter ixu_pc(archx::ProgramCounter pc); // partial PC actually available to LSU
    bool find_data_at_commit(
        archx::ProgramCounter pc,
        uint64_t alloc_id,
        uint64_t lob_id); // pc: ixu_pc, alloc_id/lob_id: used to check whther data can be retrieved
    bool adt_full();
    void flush_adt(); // invalidate all ADT entries

    // ............................. Helpers For Dynamically Turning off Gretch .......................................
    Cycle cycles_elapsed_epoch{0}; // cycles elapsed since the beginning of gretch-on or gretch-off
    Cycle previous_cycle{0};       // LSU run() function does not technically run every cycle. Need this to correctly
                                   // increment cycles elapsed this epoch.
    bool gated{false};             // on/off state of gretch
    void gate_gretch_train();
    void ungate_gretch_train();

    // Print internal structures
    void adt_print(std::ostream& os) const;
    void print_state(std::ostream& os) const;
    void comp_queue_print(std::ostream& os) const;

    void prefetch_dd_print(std::ostream& os) const;
    void drt_print(std::ostream& os) const;
    void pth_print(std::ostream& os) const;
    void cacheline_buffer_print(std::ostream& os) const;
    void dump(std::ostream&) const override;

    // ADT population events
    Event ev_adt_trigger{"adt_trigger", "ADT registers trigger from PCP (hence starts a new round of ADT population)"};
    // Event ev_adt_addr{"adt_addr", "ADT registers address"};
    // Event ev_adt_data{"adt_data", "ADT registers data"};
    Event ev_adt_epoch{"adt_epoch", "ADT population stops because ADT is full or threshold commits reached"};
    EventData<bool> evd_adt_timeout{"adt_timeout", "ADT epoch is done because commit threshold is reached"};

    // RT events
    Event ev_rt_insert{"rt_insert", "RT entry created"};
    Event ev_rt_evict{"rt_evict", "RT entry is evicted"};
    EventData<archx::ProgramCounter> evd_producer_pc{"rt_producer_pc", "RT entry's producer PC"};
    EventData<archx::ProgramCounter> evd_consumer_pc{"rt_consumer_pc", "RT entry's consumer PC"};
    EventData<uint64_t> evd_base_addr{"rt_base_addr", "RT entry's base address"};
    EventData<uint64_t> evd_offset{"rt_offset", "RT entry's offset"};

    // Events for generating original prefetch candidates
    Event ev_producer_fill{"producer_fill", "fill cacheline captured to generate Gretch prefetch candidates"};
    Event ev_pq_insert{"pq_insert", "prefetch candidate produced"};
    Event ev_pq_dropped_full{"pq_dropped_full", "dropped because PQ is full"};

    // ADT training events (only in Panthera)
    Event ev_comp_queue{"comp_queue", "computation queue created"};
    EventData<uint64_t> evd_num_comp{"num_comp", "number of ADT pair computations"};
    EventData<uint64_t> evd_num_comp_hl{"num_comp_hl",
                                        "number of high latency ADT pair computations (i.e., requiring divs)"};
    EventData<uint64_t> evd_num_comp_rt{
        "num_comp_rt", "number of ADT pair computations resulting in data-dependency relationship discovery"};

    // DRT events
    Event ev_drt_alloc{"drt_alloc", "new lob id is allocated into DRT"};
    EventData<uint64_t> evd_lobid{"lobid", "lob id used to allocate DRT"};
    EventData<int64_t> evd_drt_alloc_inum{"inum", "inum of the uop which allocates DRT"};
    Event ev_drt_data_write{"drt_data_write", "data is written into DRT"};
    Event ev_drt_data_read{"drt_data_read", "Data is read from DRT"};

    // Miscellaneous
    Event ev_same_cycle_conf_measure_dropped{
        "same_cycle_conf_measure_dropped",
        "confidence measurement dropped due to loads with producer/consumer PCs committing in the same cycle"};
    Event ev_div{"div", "Number of div operations"};
    Event ev_comp_queue_handled{"comp_queue_handled",
                                "All computations in comp_queue have been handled"}; // for Gretch integration tests
    Event ev_producer_fill_el0{
        "producer_fill_el0", "fill cacheline captured to generate Gretch prefetch candidates from an EL0 relationship"};
    Event ev_producer_fill_el1{
        "producer_fill_el1", "fill cacheline captured to generate Gretch prefetch candidates from an EL1 relationship"};
    Event ev_skipped_producer_fill{"skipped_producer_fill",
                                   "Skipped producer fill due to start PA being out-of-bounds of cacheline"};

    // count how many times we hit >1 RT entries with same producer
    Event ev_one_hit{"one_rt_hit", "hit one high-confidence RT entry"};
    Event ev_two_hit{"two_rt_hit", "hit two high-confidence RT entries with same producer"};
    Event ev_close_base_addr{"close_base_addr", "Two high confidence entries have close base addr"};
};

struct GRETCH::DRTEntry
{
    bool valid{false};
    uint64_t lobid{std::numeric_limits<uint64_t>::max()};
    uint64_t allocid{std::numeric_limits<uint64_t>::max()}; // only in panthera
    uint64_t data{0};
};

struct GRETCH::ADTEntry
{
    archx::ProgramCounter pc{0};
    archx::VirtAddr addr[2];
    uint64_t data[2] = {0, 0}; // Required only for the first ADT entry
    int num_addresses{0};
    int num_data{0};                // Meaningful only for the first ADT entry
    int16_t lobid[2] = {-1, -1};    // for debug only
    archx::inum_t inum[2] = {0, 0}; // for debug only
};

struct GRETCH::RTEntry
{
    archx::ProgramCounter producer{0}; // 9 bits: PC[10:2]
    archx::ProgramCounter consumer{0}; // 9 bits: PC[10:2]
    uint64_t base_addr{0};             // 64 bits; 53 bits in RTL
    uint64_t offset{0};                // 8 bits
    archx::VirtAddr predicted_addr{0}; // 64 bits -> for confidence measurement at commit time; 53 bits in RTL
    bool valid_prediction{false};      // bit to mark that a prediction is ready/valid
    Cycle prediction_cycle{0};         // cycle at which prediction was generated (not in RTL)
    int confidence{0};                 // saturating counter indicating confidence;
    bool gen_pref{
        false}; // Bit to mark whether this entry has been used for prefetch generation.
                // Included to protect against eviction in case none of the entries have confidence <= -1*threshold

    Cycle inserted_cycle{0};     // for debug
    Cycle prefetch_gen_cycle{0}; // for debug
    archx::EL el{archx::EL0};    // for debug/analysis

    RTEntry(archx::ProgramCounter prod, archx::ProgramCounter con, uint64_t ba, uint64_t off)
        : producer(prod), consumer(con), base_addr(ba), offset(off)
    {
    }

    bool operator==(RTEntry const& rhs) const
    {
        return ((this->producer == rhs.producer) && (this->consumer == rhs.consumer)
                && (this->base_addr == rhs.base_addr) && (this->offset == rhs.offset));
    }
};

// all the info needed to handle a single data slice in the incoming cacheline
struct GRETCH::CLDataSlice
{
    BackendMemReq req;           // uop responsible for the incoming cacheline; needed to match producer PC in RT
    uint64_t data{0};            // data value
    bool last_data_slice{false}; // whether this is the last data slice in the cacheline

    CLDataSlice(BackendMemReq req, uint64_t data, bool lds) : req(req), data(data), last_data_slice(lds) {}
};

class L1P_GRETCH : public GRETCH, implementation<L1P_GRETCH, BackendPrefetcher<BackendMemReq>>
{
    PCP* pcp;

public:
    explicit L1P_GRETCH(const sc_core::sc_module_name& name);

    void launch_impl(BackendMemReq req) override;
};

} // namespace ampere::core

/*
 * vim: sw=4:ts=4:et:si:cindent:cino=\:0,(0,g0,t0:
 */
