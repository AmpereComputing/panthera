//
// Copyright (c) 2021 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//
/**
 * L2C - Model of the Level 2 Cache.
 *
 * The L2C is a complex module with a lot of parts. It's been broken into a few pieces, either by component classes
 * or simply by functional area. A brief tour of the files:
 *
 *     * l2c.cc: construction, warmstart, and interface related functionality
 *     * l2c_arb.cc: methods dealing with request arbitration
 *     * l2c_bank.cc: the Bank class (representing the physical cache)
 *     * l2c_fb.cc: the FillBuffer and FillBufferEntry classes
 *     * l2c_line.cc: the Line class, representing a cache line and shared by the Bank and FillBufferEntry
 *     * l2_pipe.cc: implementation of the main L2 pipeline and all its opcodes
 *     * l2_pwc.cc: the Pre-Write Combiner class (combines writes on the way into the L2)
 */
#pragma once
#include <vector>
#include "ampere/archx/addr.h"
#include "ampere/archx/driver.h"
#include "ampere/core/coherency_validator.h"
#include "ampere/core/l2/l2c_bank.h"
#include "ampere/core/l2/l2c_fb.h"
#include "ampere/core/l2/l2c_line.h"
#include "ampere/core/l2/l2c_op.h"
#include "ampere/core/l2/l2c_opcode.enum.h"
#include "ampere/core/l2/l2c_pwc.h"
#include "ampere/core/l2/l2c_reqqueue.h"
#include "ampere/core/l2/l2c_reqqueue_prefetch.h"
#include "ampere/core/l2/crp/l2_replacement_policy.h"
#include "ampere/core/prefetcher.h"
#include "ampere/mesh/cbusy.h"
#include "ampere/mesh/chi/opcodes.enum.h"
#include "ampere/mesh/chi/types.h"
#include "ampere/mesh/mesh_device.h"
#include "ampere/mesh/mesh_module.h"
#include "ampere/mesh/mesh_port.h"
#include "ampere/mesh/retry.h"
#include "ampere/mesh/sam.h"
#include "ampere/mesh/injector/uop_source.h"
#include "ampere/mesh/injector/uop_source_is.h"
#include "ampere/mesh/injector/uop_source_prog.h"
#include "ampere/sc/sc.h"
#include "ampere/sc/submodule.h"
#include "ampere/uarch_common/memop.h"
#include "ampere/uarch_common/memreq.h"
#include "ampere/uarch_common/memresp.h"
#include "ampere/memory_stream/memory_stream.h"

namespace ampere::l2c {
class PrefetcherL2CTest;
}
namespace ampere::soc {
class Warmup;
}
namespace ampere::core {

using namespace sc;
using namespace uarch_common;
using ampere::mesh::UopSource;
using ampere::mesh::UopSourceIS;
using ampere::mesh::UopSourceProg;

class L2C : public mesh::MeshModule,
            mesh::MeshEvents,
            public IndexInterface<PhysAddr>,
            public CoherencyValidationInterface<PhysAddr>
{
public:
    explicit L2C(const sc_core::sc_module_name& name);

    using mesh::MeshModule::MeshModule;

    In<MemReq> immu_request{"immu_request"};
    Out<MemResp> immu_response{"immu_response"};

    In<uarch_common::Request::Fill> icf_request{"icf_request"};
    OutOpt<MemReq> icf_request_schedule{"icf_request_schedule"};
    Out<MemResp> icf_response{"icf_response"};
    Out<MemReq> icf_snoop_request{"icf_snoop_request"};

    In<MemReq> dmmu_request{"dmmu_request"};
    Out<MemResp> dmmu_response{"dmmu_response"};

    In<uarch_common::Request::Fill> lsu_read_request{"lsu_read_request"};
    Out<MemResp> lsu_read_response{"lsu_read_response"};
    In<MemReq> lsu_write_request{"lsu_write_request"};
    In<MemReq> lsu_evict_request{"lsu_evict_request"};
    Out<MemReq> lsu_snoop_request{"lsu_snoop_request"};
    In<bool> lsu_has_barrier_in{"lsu_has_barrier_in"};
    Out<bool> l2_no_pending_stores_out{"l2_no_pending_stores_out"};
    Out<bool> l2_barrier_out{"l2_barrier_out"};
    Out<bool> l2_store_release_out{"l2_store_release_out"};
    Out<bool> l2_store_exclusive_out{"l2_store_exclusive_out"};
    Out<PhysAddr> l2_miss_out{"l2_miss_out"};
    Out<PhysAddr> l2_miss_out_to_icf{"l2_miss_out_to_icf"};
    CacheLookup<PhysAddr> l1i_lookup{"l1i_lookup"};
    CacheLookup<PhysAddr> l1d_lookup{"l1d_lookup"};

    In<MemReq> prefetch_request{"prefetch_request"};
    In<MemReq> lsu_prefetch_request{"lsu_prefetch_request"};

    mesh::MeshPort mesh_port;
    DriverPort oracle_driver;
    DriverPort real_driver;

    Submodule<mesh::SAM, mesh::CDRN_SAM> sam{"sam"};

    std::set<PhysAddr> warmup_lines;
    uint64_t warmup_access_count{0};
    uint64_t warmup_fill_invalid{0};
    uint64_t warmup_hits{0};
    uint64_t warmup_evicts{0};
    uint64_t warmup_fill_again{0};

    void set_node_id(mesh::chi::NodeId id)
    {
        node_id = id;
        warmup_uop_source->set_node_id(id);
    }

    bool run() override;
    void end_of_elaboration() override;
    void start_of_simulation() override;

    // Coherency Valiation interface
    bool line_present(const PhysAddr& addr, bool iside) override;
    Index index(PhysAddr addr) const override;

    mesh::chi::FinalState l2_state_to_chi(l2::LineState state);
    std::pair<PhysAddr, mesh::chi::FinalState> warmstart_access(ms::Access ms_access, bool from_core);
    void ms_warmstart(std::string filename);
    void warmstart_complete();

    // Testability
    void preload(PhysAddr addr, l2::LineState state);
    void preload_l3(PhysAddr addr, l2::LineState state);
    void preload_with_inclusion_bits(PhysAddr addr, l2::LineState state, MemOp::Requester requester);

    l2::LineState peek_state(PhysAddr addr);
    bool is_l1d_set(PhysAddr addr);
    bool is_l1i_set(PhysAddr addr);

    void dump(std::ostream& os) const override;

    Submodule<Prefetcher<MemReq>> prefetcher{"prefetcher"};

    Param<bool> code_is_likely_shared{
        "code_is_likely_shared", "Set the LikelyShared attribute for requests from ICF", false};
    Param<bool> disable_make_unique{"disable_make_unique", "Disable MakeUnique", false};
    Param<std::string> fast_warmup_ms{"fast_warmup_ms", "name of the memory stream file used for (warmstart)", ""};

private:
    Param<size_t> num_banks{"num_banks", "number of cache banks", 8};
    Param<bool> prefetch_clear_on_hit{
        "prefetch_clear_on_hit", "Clear prefetch bit on a line when it is hit by a demand load", false};

    Param<std::string> memory_stream_name{
        "warmup_ms", "name of the memory stream file used for preloading L2 cache (warmstart)", ""};
    Param<bool> check_warmstart_overruns{
        "check_warmstart_overruns", "Fail if double-insertions or evictions occur on warmstart", false};
    Param<bool> reset_lru_after_warmup{
        "reset_lru_after_warmup", "Reset LRU/RRIP state after warmup to match RTL", false};
    Param<bool> enable_warmup_events{"enable_warmup_events", "Enable event stream before cache warmstart", false};
    Param<bool> warmup_stream_only{
        "warmup_stream_only", "End warmup after the memory stream (do not include cache dump)", false};
    Param<bool> warmup_prefetcher{"warmup_prefetcher", "Warmup prefetcher (fill/evict)", false};

    Param<bool> enable_perfect_mode{"enable_perfect_mode", "Force all accesses to hit", false};
    Param<bool> enable_perfect_mode_user{"enable_perfect_mode_user", "Force all user-mode accesses to hit", false};
    Param<bool> enable_antiperfect_mode{"enable_antiperfect_mode", "Force all accesses to miss", false};
    Param<bool> mmu_always_hit{"mmu_always_hit", "Force all MMU accesses to hit L2", false};
    Param<bool> never_fill{"never_fill", "Data return never fills the cache, thus no evictions", false};
    Param<bool> always_nns_memtype{
        "always_nns_memtype",
        "Assume Normal Non-Shareable memtype, so writes complete with READ_DEALLOC instead of FILL",
        false};

    // Bank scheduling latencies
    Param<uint64_t> bank_read_busy_mask{
        "bank_read_busy_mask",
        "Mask of upcoming cycles in which the bank is busy after a read; LSB is current cycle",
        0xf};
    Param<uint64_t> bank_write_busy_mask{
        "bank_write_busy_mask",
        "Mask of upcoming cycles in which the bank is busy after a write; LSB is current cycle",
        0x3f};
    Param<uint64_t> bank_atomic_busy_mask{
        "bank_atomic_busy_mask",
        "Mask of upcoming cycles in which the bank is busy after an atomic; LSB is current cycle",
        0x1ff};
    Param<uint64_t> bank_cancel_busy_mask{
        "bank_cancel_busy_mask",
        "Mask of upcoming cycles in which the bank is busy cancelling an access; LSB is current cycle",
        0x3};
    // Before Domino, READ_DEALLOC writes data into the L2 array. In Domino, READ_DEALLOC only reads data from FB (L2
    // array writing occurs in FILL).
    Param<uint64_t> bank_read_dealloc_busy_mask{
        "bank_read_dealloc_busy_mask",
        "Mask of upcoming cycles in which the bank is busy after a READ_DEALLOC op; LSB is current cycle",
        0x3f};
    Param<bool> fills_without_dest_need_read_pipe{
        "fills_without_dest_need_read_pipe",
        "All fills occupy the read pipe, even if they are not returning data to a requester",
        true};

    Param<int> prefetch_arb_depth{"prefetch_arb_depth", "Prefetches considered in each cycle by arbitration", 1};
    Param<int> immu_arb_depth{"immu_arb_depth", "IMMU reads considered in each cycle by arbitration", 1};
    Param<int> dmmu_arb_depth{"dmmu_arb_depth", "DMMU reads considered in each cycle by arbitration", 1};
    Param<int> srq_arb_depth{
        "srq_arb_depth", "System request queue entries considered in each cycle by arbitration", 1};
    Param<int> snpq_arb_depth{"snpq_arb_depth", "Snoop queue entries considered in each cycle by arbitration", 1};
    Param<unsigned int> write_priority_age_threshold{
        "write_priority_age_threshold",
        "How old (in cycles) does the store at the head of the PWC have to be to switch to write-priority",
        8};
    Param<bool> first_half_fill_blocks_read{
        "first_half_fill_blocks_read", "The first half of a fill occupies the read pipe", true};

    Param<unsigned> mmu_filter_bits{
        "mmu_filter_bits", "Number of PA bits used to look for address conflicts in MMU requests", 8};
    Param<unsigned> prefetch_capacity{"prefetch_capacity", "Number of outstanding prefetches allowed", 8};
    Param<bool> prefetch_evict_full{
        "prefetch_evict_full", "Evicted lines with prefetch bit set do WriteEvictFull", true};
    Param<bool> prefetch_all_fb_hits{
        "prefetch_all_fb_hits", "Train prefetcher on hit of any FB, even if its prefetch bit is not set", false};
    Param<bool> early_comp_ack_enable{
        "early_comp_ack_enable", "Send CompAck as soon as RespSepData or DataSepResp is received", false};
    Param<bool> evict_at_fill{
        "evict_at_fill",
        "Incoming fills are written to the cache as soon as the data arrives, instead of at eviction",
        false};
    Param<bool> sys_pipe_dealloc{
        "sys_pipe_dealloc", "FB_DEALLOC and READ_DEALLOC can schedule on a special sys-only pipe", false};
    Param<bool> dealloc_back_invalidates{
        "dealloc_back_invalidates", "_DEALLOC ops perform LSU back invalidation rather than victim-readout op", false};
    Param<bool> l2_write_back_clean{
        "l2_write_back_clean",
        "Write back data for evictions of clean lines from L2. Default true because L2 assumes an SLC/L3.",
        true};
    Param<bool> l3_write_back_clean{
        "l3_write_back_clean",
        "Write back data for evictions of clean lines from L3. Default false because L3 evictions go to DRAM",
        false};
    Param<bool> use_fill_first_flit{
        "use_fill_first_flit",
        "Use FILL_FIRSTFLIT bank opcode to receive first 32B from the system (not valid in Connor)",
        true};
    Param<bool> use_cleanunique{"use_cleanunique", "Use cleanunique to upgrade from shared to unique", false};
    Param<bool> fast_write_complete{
        "fast_write_complete", "Writes are complete 2 cycles earlier for the purposes of barrier completion", false};

    // L1 coherency parameters
    Param<unsigned> num_va_bits{"num_va_bits", "Number of virtual address bits stored for lines in L1D", 2};
    Param<bool> perfect_l1i_bits{"perfect_l1i_bits", "L2 has perfect knowledge of lines in L1I", false};
    Param<bool> perfect_l1d_bits{"perfect_l1d_bits", "L2 has perfect knowledge of lines in L1D", false};
    Param<bool> never_clear_l1i{"never_clear_l1i", "Never clear the I-cache inclusion bit", false};
    Param<bool> never_clear_l1d{"never_clear_l1d", "Never clear the D-cache inclusion bit", false};
    Param<bool> enable_synonym_snoops{
        "enable_synonym_snoops", "Snoop DL1 when it requests a line it already has", true};

    Param<bool> enable_per_cycle_pmu{"enable_per_cycle_pmu", "Output the PERCYC events (slows down panthera)", false};

    // Split FB features
    Param<bool> parallel_eviction{
        "parallel_eviction", "Initiate eviction in parallel with fills to minimize data fillbuffer lifetime", false};

    Param<uint64_t> phyaddroffset{"phyaddroffset", "Offset physical address after translation", 0};

    std::vector<std::uint64_t> fb_used; // per bank

    // Node ID for the mesh
    mesh::chi::NodeId node_id;

    mesh::CBusy cbusy{"cbusy"};

    l2::RequestQueue immu_prq{"immu_prq", l2::RequestQueueName::IMMU, immu_arb_depth};
    l2::RequestQueue icf_prq{"icf_prq", l2::RequestQueueName::ICF, 1};
    l2::RequestQueue dmmu_prq{"dmmu_prq", l2::RequestQueueName::DMMU, dmmu_arb_depth};
    l2::RequestQueue lsu_read_prq{"lsu_read_prq", l2::RequestQueueName::LSU_READ, -1};
    l2::RequestQueue lsu_evict_prq{"lsu_evict_prq", l2::RequestQueueName::LSU_EVICT, -1};
    l2::RequestQueuePrefetch prefetch_prq{
        "prefetch_prq", l2::RequestQueueName::PREFETCH, prefetch_arb_depth, prefetcher.get(), prefetch_capacity};
    l2::RequestQueue srq_fill{"srq_fill", l2::RequestQueueName::SYSTEM_FILL, srq_arb_depth};
    l2::RequestQueue srq_dealloc{"srq_dealloc", l2::RequestQueueName::SYSTEM_DEALLOC, srq_arb_depth};
    l2::RequestQueue snpq{"snpq", l2::RequestQueueName::SNOOP, snpq_arb_depth};

    using RequestFilter = std::set<uint64_t>;
    RequestFilter immu_duplicate_filter;
    RequestFilter dmmu_duplicate_filter;

    std::vector<std::vector<l2::Schedulable*>> requests;

    mesh::RetryRequester retry{"retry"};

    l2::Op read_op{nullptr};
    l2::Op write_op{nullptr};
    l2::Op sys_op{nullptr};

    std::queue<mesh::MeshReq> sys_req_queue;
    std::queue<mesh::MeshRsp> sys_rsp_queue;
    std::queue<mesh::MeshDat> sys_dat_queue;

    l2::BankSet banks{"banks", num_banks};
    l2::FillBuffer fb;
    l2::PWC pwc{"pwc"};

    bool lsu_has_barrier{false};
    Cycle last_read_dealloc_cycle{0};

    unsigned pending_store_types{0};

    void arbitrate();
    PipelineArbitration op_bus_needs(l2::Opcode opcode) const;
    PipelineArbitration can_schedule(l2::RequestQueueName queue_name, l2::Op op) const;
    uint64_t filter_address(PhysAddr addr) const;
    uint64_t get_read_busy_mask(l2::Opcode opcode) const;
    uint64_t get_write_busy_mask(l2::Opcode opcode) const;
    bool address_within_bank(PhysAddr addr) const;
    bool is_pwc_write_op(const l2::Op& op) const;

    bool schedule_read(Pipeline<l2::Op>::Data op);
    bool schedule_write(Pipeline<l2::Op>::Data op);
    bool schedule_sys(Pipeline<l2::Op>::Data op);
    bool process(Pipeline<l2::Op>::Data op);
    bool writeback(Pipeline<l2::Op>::Data op);
    bool write_complete(Pipeline<l2::Op>::Data op);
    bool system(Pipeline<l2::Op>::Data op);

    void do_blkrd(Pipeline<l2::Op>::Data pipe_op);
    void do_blkrdunique(Pipeline<l2::Op>::Data pipe_op);
    void do_subwr(Pipeline<l2::Op>::Data pipe_op);
    void do_atomic(Pipeline<l2::Op>::Data pipe_op);
    void do_fill_firstflit(Pipeline<l2::Op>::Data pipe_op);
    void do_fill(Pipeline<l2::Op>::Data pipe_op);
    void do_read_victim(Pipeline<l2::Op>::Data pipe_op);
    void do_read_dealloc(Pipeline<l2::Op>::Data pipe_op);
    void do_fb_dealloc(Pipeline<l2::Op>::Data pipe_op);
    void do_snp_inv(Pipeline<l2::Op>::Data pipe_op);
    void do_snp_fwd(Pipeline<l2::Op>::Data pipe_op);
    void do_unsupported(Pipeline<l2::Op>::Data pipe_op);
    void do_alloc_l3(Pipeline<l2::Op>::Data pipe_op);

    l2::RequestQueue* prq_for_request(const l2::Op op);
    void requeue_request(l2::Op op, uint64_t pwc_id = 0);

    static bool must_replay_for_store_conflicts(MemOp::Requester requester);
    static bool must_replay_for_store_conflicts(const l2::Op& op);
    bool ld_st_ordering_conflict(l2::Op& op);
    bool ld_st_ordering_conflict_at_fill(PhysAddr addr, FillBufferEntry& fbe);

    bool generate_eviction(const l2::Op& op, PhysAddr victim_addr, l2::LineState victim_state, bool victim_prefetch);
    void initiate_parallel_eviction(l2::Op& op);
    void swap_victim(const l2::Op& op);
    void back_invalidate_victim(const l2::Op& op);
    void replace_line(const l2::Op& op);
    unsigned in_flight_store_types() const;
    bool finalize_fb(l2::Op& op);

    Pipeline<l2::Op> read_pipe = {"read_pipe",
                                  {AMPERE_PIPE_STAGE_M(L2C, schedule_read),
                                   AMPERE_PIPE_STAGE_M(L2C, process),
                                   AMPERE_PIPE_STAGE_M(L2C, writeback),
                                   AMPERE_PIPE_STAGE_M(L2C, system)}};

    Pipeline<l2::Op> write_pipe = {"write_pipe",
                                   {AMPERE_PIPE_STAGE_M(L2C, schedule_write),
                                    AMPERE_PIPE_STAGE_M(L2C, process),
                                    AMPERE_PIPE_STAGE_M(L2C, writeback),
                                    AMPERE_PIPE_STAGE_M(L2C, write_complete),
                                    AMPERE_PIPE_STAGE_M(L2C, system)}};

    Pipeline<l2::Op> sys_pipe = {
        "sys_pipe",
        {AMPERE_PIPE_STAGE_M(L2C, schedule_sys), AMPERE_PIPE_STAGE_M(L2C, process), AMPERE_PIPE_STAGE_M(L2C, system)}};

    using process_func_t = void (L2C::*)(Pipeline<l2::Op>::Data);
    std::vector<process_func_t> dispatch;

    l2::Op memreq_to_op(MemReq req, bool include_atomics = false);
    void maybe_upgrade(l2::Op& op, l2::FillBufferEntry& fbe);

    void build_requests_by_priority();
    void build_dispatch_table();

    void receive_from_core();
    void send_to_core(MemReq req, LineState state);

    bool perfect_hit(VirtAddr addr) const;
    bool get_l1i(const l2::Line& line) const;
    bool get_l1d(const l2::Line& line) const;
    void set_va_bits(PhysAddr pa, VirtAddr va);
    void set_inclusion_bit(PhysAddr addr, bool value, bool iside);
    void update_inclusion_bits(MemReq& req, uarch_common::Request::L1Contents& l1_contents, bool iside);
    void check_inclusion_bit(PhysAddr addr, bool iside);

    void send_to_sys();
    void receive_from_sys();

    void notify_lsu_of_pending_store_types();

    mesh::MeshReq gen_sys_req(const l2::Op& op,
                              l2::FillBufferEntry& fbe,
                              mesh::RequestOpcode opcode,
                              PhysAddr addr,
                              bool likely_shared = false);
    mesh::MeshReq gen_sys_req(const l2::Op& op, mesh::RequestOpcode opcode, PhysAddr addr, bool likely_shared = false);

    l2::FillBufferEntry& fb_allocate(l2::Op& op, bool snp, bool eviction = false);
    void fb_deallocate(l2::Op& op);

    boost::optional<l2::Line&> lookup_l2(const l2::Op& op);

    void pmu_refill_events(l2::Op& op);

    std::uint8_t data_id_to_chunks(uint8_t data_id);
    mesh::RequestOpcode mesh_write_request_opcode(const l2::Op& op, bool hit, bool needs_original_data = false) const;
    mesh::chi::ResponseStatus cache_state_to_resp_status(l2::LineState cache_state);

    struct WarmupEntry
    {
        PhysAddr start_addr;
        PhysAddr stop_addr;
        std::vector<soc::WarmupInfo::Action> actions;
    };

    struct WarmupMs
    {
        WarmupMs(const std::string warmup_ms) : warmup_ms(warmup_ms)
        {
            if (!warmup_ms.empty())
                ms_reader = std::make_unique<ms::MemoryStreamReader>(warmup_ms.c_str());
        }
        std::unique_ptr<ms::MemoryStreamReader> ms_reader{nullptr};
        ms::Access ms_access;
        std::string warmup_ms;
    };

    WarmupMs ms_warmup{fast_warmup_ms.val()};
    std::optional<soc::WarmupInfo> gen_warmup();

    Submodule<UopSource, UopSourceIS> warmup_uop_source{"warmup_source"};

    struct
    {
        Event access{"access", "Cache access"};
        Event refill{"refill", "Cache refill"};
        Event refill_shared{"refill_shared", "Cache refill in shared state"};
        Event eviction{"eviction", "Cache eviction"};
        Event eviction_data{"eviction_data", "Cache eviction with data"};
        EventData<bool> hit_data{"hit", "Cache hit"};
        EventData<bool> l2{"l2", "Cache type: L2 or L3 (true = L2)"};
        Event stash_reject{"stash_reject", "stash rejected because of no fill buffer entries"};
        Event hit_l3{"hit_l3", "Cache lookup hits L3"};
        Event fb_dealloc_stash{"fb_dealloc_stash", "Stash FB entry deallocates"};
        Event fb_dealloc_req{"fb_dealloc_req", "Req FB entry deallocates"};
        Event recv_stash{"recv_stash", "Receive stash"};
        Event pwc_hit_at_refill{"pwc_hit_at_refill", "Recycle request at fill due to hitting a PWC entry"};

        Event arbitrate{"arbitrate", "L2C pipeline arbitration"};
        Event read_schedule{"read_schedule", "L2C pipeline read schedule"};
        Event write_schedule{"write_schedule", "L2C pipeline write schedule"};
        Event sys_schedule{"sys_schedule", "L2C pipeline sys schedule"};
        Event fb_full{"fb_full", "Unable to schedule an op, FB is full"};
        Event needed_banks_busy{"needed_banks_busy", "Unable to schedule an op, needed banks are busy"};
        Event process{"process", "L2C pipeline is processing an op"};
        Event writeback{"writeback", "L2C writes data back to a requester"};
        Event fb_pending_req{"fb_pending_req", " we are adding a pending request to an fb entry"};
        Event fb_hit{"fb_hit", "It's a hit in FB"};
        Event read_hit_write_fb{"read_hit_write_fb", "Read hit a fill buffer allocated by a write"};
        Event read_hit_write_nosnp{"read_hit_write_nosnp",
                                   "Read hit a fill buffer allocated by WriteNoSnp and had to replay"};
        Event lsu_snoop_clear_inclusion{"lsu_snoop_clear_inclusion", "L1D Inclusion bit is cleared"};
        Event fb_alloc{"fb_alloc", "Allocate a fill buffer"};
        Event fb_dealloc{"fb_dealloc", "Deallocate a fill buffer"};
        Event warmup_fill_again{"warmup_fill_again",
                                "During L2 warmstart a line was filled which had already been filled before"};

        std::vector<Event*> fb_used; // per bank
        EventData<std::uint64_t> occupancy{"occupancy", "Number of entries in fill buffer for this bank"};

        EventData<int> found_fbid{"found_fbid", "Found fill buffer id"};
        EventData<int> fbid{"fbid", "Fill buffer id"};
        EventData<int> bank{"bank", "L2C bank"};

        Event mesh_backup{"mesh_backup", "Can't send request since mesh is backed up"};

        Event preload_fill_invalid{"preload_fill", "L2 preload filled into an invalid line"};
        Event preload_hit{"preload_hit", "L2 preload hit"};
        Event preload_evict{"preload_evict", "L2 preload evicted a lint"};

        EventData<uint64_t> phys_line{"phys_line", "Physical line address"};

        // PMU
        Event pmu{"pmu", "A PMU event has occurred"};
        EventData<archx::PMU> pmu_num{"event", "PMU event number"};
    } event;

    static void dump_request_queue(std::ostream& os, const char* name, const l2::RequestQueue& q);
    friend class ampere::l2c::PrefetcherL2CTest;
    friend class soc::Warmup;
};

} // namespace ampere::core
