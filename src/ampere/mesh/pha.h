//
// Copyright (c) 2022 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//

#pragma once

#include <map>
#include <set>

#include "ampere/archx/addr.h"
#include "ampere/mesh/chi/types.h"
#include "ampere/mesh/mesh_device.h"
#include "ampere/mesh/mesh_module.h"
#include "ampere/mesh/mesh_port.h"
#include "ampere/mesh/pha_phase.h"
#include "ampere/mesh/pha_retry.h"
#include "ampere/mesh/pha_tracker.h"
#include "ampere/mesh/retry.h"
#include "ampere/mesh/slc.h"
#include "ampere/mesh/snoop_filter.h"
#include "ampere/mesh/sam.h"
#include "ampere/mesh/traffic_buffer.h"
#include "ampere/sc/pipeline.h"
#include "ampere/sc/poly_constructors.h"
#include "ampere/sc/sc.h"
#include "ampere/sc/submodule.h"
#include "ampere/uarch_common/cache.h"
#include "ampere/mesh/matrixcbusy.h"

namespace ampere::soc {
class Warmup;
}

make_enum_without_class(PipelineRejectReason,
    TxREQFull,
    TxRSP0Full,
    TxRSP1Full,
    TxSNPFull,
    TxDATFull
);

namespace ampere::mesh {

using namespace ampere::sc;

class PHA : public MeshModule,
            public implements<PHA, MeshDevice, poly_constructor_default, poly_constructor_clock>,
            public MeshEvents
{
public:
    explicit PHA(const sc_core::sc_module_name& name) : PHA{name, nullptr} {}
    PHA(const sc_core::sc_module_name& name, sc::Clock* clock);

    MeshPort mesh_port{"port", 1, 2, 1, 1};
    MeshPort& get_mesh_port(int i = 0) override { return mesh_port; }
    NodeId get_node_id(int i = 0) const override { return node_id; }

    MeshPort l3_port{"l3_port"};

    Out<PHATrackerEntry*> phase_command{"phase_command"};
    In<MeshSnp> phase_snoop_ret{"phase_snoop_ret"};

    bool is_pmha(void) { return pha_type == pha_fsm::PHA_t::Pmha; }
    bool is_pxha(void) { return pha_type == pha_fsm::PHA_t::Pxha; }

    // Testability
    std::optional<const SnoopStatus*> get_snoop_status(PhysAddr address)
    {
        auto [set, way] = snoop_filter.lookup(Txn{}, address, false);
        if (way == -1)
            return {};
        else
            return &snoop_filter.get_data(set, way);
    }

    std::set<uint64_t> trace_by_addr;

    void dump(std::ostream& = std::cout) const override;
    void dump_tracker(std::ostream& os) const { tracker.dump(os); }
    void dump_phase(std::ostream& os) const { phase.dump(os); }
    void dump_sf(std::ostream& os) const { snoop_filter.dump(os); }
    void dump_buffers(std::ostream& os) const;

protected:
    Param<chi::NodeId> node_id{"node_id", "Mesh Node identifier (for routing)", chi::NodeId{0}};
    Param<bool> enable_prefetch_target{
        "enable_prefetch_target", "Generate Prefetch Target Requests to the MCUs", false};
    Param<std::unordered_map<chi::ResponseOpcode, int>> chi_rsp_overrides{
        "chi_rsp_overrides", "Overrides which channel a response message will traverse", "[]"_json};

    Param<size_t> num_iron_entries{"num_iron_entries", "Number of iron entries in the tracker", 89};
    Param<size_t> num_gold_entries{"num_gold_entries", "Number of gold entries in the tracker", 4};
    Param<size_t> num_platinum_entries{"num_platinum_entries", "Number of platinum entries in the tracker", 1};
    Param<size_t> num_sfv_entries{"num_sfv_entries", "Number of snoop victim reserved entries in the tracker", 1};
    Param<size_t> num_snp_entries{"num_snp_entries", "Number of snoop request reserved entries in the tracker", 1};

    Param<bool> enable_silent_evictions{"enable_silent_evictions", "Do not generate Evict for clean lines", false};
    Param<bool> send_evictions_on_snoop_channel{
        "send_evictions_on_snoop_channel", "Transmit Evict FLITs on the snoop channel instead of request", false};
    Param<bool> use_fixed_mcu{"use_fixed_mcu", "Route all requests to a specific MCU (PmHA only)", false};
    Param<NodeId> fixed_mcu{"fixed_mcu", "Send requests to this MCU iff 'use_fixed_mcu' is set", NodeId{}};

    Param<bool> trace_all{"trace_all", "Print debug messages for all tracker entries", false};
    Param<json> trace_by_addr_param{
        "trace_by_addr", "Print debug messages for tracker entries matching an address", {}};
    Param<json> trace_by_txid_param{"trace_by_txid", "Print debug messages for tracker entries matching a txid", {}};

    Param<bool> local_l3{"local_l3", "Implement an L3 locally rather than stashing", false};

    Param<bool> stash_with_hashing{
        "stash_with_hashing", "Hash an address to determine the stash target rather than round-robin", false};

    Param<size_t> txreq_max_size{"txreq_max_size", "Maximum size of the TxReq FIFO", 4};
    Param<size_t> txretry_max_size{"txretry_max_size", "Maximum size of the TxReq Retry FIFO", 4};
    Param<size_t> txrsp0_max_size{"txrsp0_max_size", "Maximum size of the TxRsp0 FIFO", 4};
    Param<size_t> txrsp1_max_size{"txrsp1_max_size", "Maximum size of the TxRsp1 FIFO", 4};
    Param<size_t> txdat_max_size{"txdat_max_size", "Maximum size of the TxDat FIFO", 4};

    Param<bool> retries_outstanding{
        "retries_outstanding", "Retries remain outstanding (RetryAck does not decerment outstanding count)", false};

    Param<std::vector<uint8_t>> stash_target_table{"stash_target_table", "64 entry stash target table", "[]"_json};
    Param<std::map<uint8_t, std::vector<uint8_t>>> core_group_to_agent_id_map{
        "core_group_to_agent_id_map", "map of core group id to agent id", "[]"_json};

    std::set<uint64_t> trace_by_txid;

    Event ev_schedule{"schedule", "Schedule a tracker entry to the pipeline"};
    Event ev_access{"access", "Access the cache and snoop filter"};
    Event ev_alloc{"allocate", "Allocate a tracker entry"};
    Event ev_dealloc{"deallocate", "Deallocate a tracker entry"};
    Event ev_process_event{"process_event", "The PHA FSM processes an event"};
    Event ev_sf_write{"sf_write", "Write event in snoop filter"};
    Event ev_process_state_event{"process_state_event", "The PHA CacheState FSM processed (main pipeline)"};
    Event ev_process_state_event_rsp{"process_state_event_rsp", "The PHA CacheState FSM processed for a received rsp"};
    Event ev_process_state_event_dat{"process_state_event_dat", "The PHA CacheState FSM processed for a recieved dat"};
    Event ev_alloc_request{"allocate_request", "Allocate outgoing request"};
    Event ev_dealloc_request{"deallocate_request", "Deallocate outgoing request"};
    Event ev_pipeline_reject{"pipeline_reject", "PHA pipeline pass is rejected"};

    EventData<unsigned> evd_old_state{"old_state", "The PHA FSM's state before processing"};
    EventData<unsigned> evd_new_state{"new_state", "The PHA FSM's state after processing"};
    EventData<unsigned> evd_fsm_row{"fsm_row", "A PHA FSM row"};
    EventData<PipelineRejectReason> evd_reject_reason{"reject_reason", "Reason a pipeline pass was rejected"};

    const size_t total_tracker_entries()
    {
        return num_iron_entries + num_gold_entries + num_platinum_entries + num_sfv_entries + num_snp_entries;
    }

    PHATracker tracker{"tracker", total_tracker_entries(), this};
    pha_fsm::PHA_t pha_type{pha_fsm::PHA_t::Pxha};

    std::list<NodeId> stash_nodes;                     // List of stash nodes (round-robin mode)
    std::list<NodeId>::iterator stash_node;            // Current node selected for stash
    std::vector<std::vector<NodeId>> stash_node_table; // Table of stash nodes (stash-with-hash mode)

    std::set<NodeId> cbusy_responders;        // Nodes from which PxHAs should update cbusy state upon respones
    std::set<chi::ResponseOpcode> cbusy_rsps; // Response types which PxHAs should update cbusy state upon receiving
    std::set<chi::DataOpcode> cbusy_dats;     // Data return types which PxHAs should update cbusy state upon receiving

    PHARetry retry{"retry"};
    PHAPhase phase{"phase", node_id};
    RetryRequester retry_requester{"retry_requester"};
    size_t outstanding_requests{0};

    TrafficBuffer<MeshReq> txreq{"txreq"};
    TrafficBuffer<MeshReq> txretry{"txretry"};
    TrafficBuffer<MeshRsp> txrsp0{"txrsp0"};
    TrafficBuffer<MeshRsp> txrsp1{"txrsp1"};
    TrafficBuffer<MeshSnp> txsnp{"txsnp"};
    TrafficBuffer<MeshDat> txdat{"txdat"};

    TrafficBuffer<MeshSnp> l3snp{"l3snp"};
    TrafficBuffer<MeshRsp> l3rsp{"l3rsp"};
    TrafficBuffer<MeshDat> l3dat{"l3dat"};

    SnoopFilter snoop_filter{"snoop_filter"};
    Submodule<SAM> sam{"sam"};
    Submodule<SAM> rsam{"rsam"};
    MatrixCBusy cbusy{"cbusy"};

    bool run() override;
    void end_of_elaboration() override;
    void receive_requests();
    void receive_snoops();
    void receive_phase_snps();
    void receive_responses(int port);
    void receive_data();

    void transmit();
    template<typename T, typename CallEvent>
    void transmit_channel(TrafficBuffer<T>& txq, OutOpt<T>& port, CallEvent ev);
    template<typename T, typename CallCondition, typename CallEvent>
    void transmit_channel_if(TrafficBuffer<T>& txq, OutOpt<T>& port, CallCondition cond, CallEvent ev);

    std::pair<bool, bool> access_sf(PHATrackerEntry& entry);
    void write_sf(PHATrackerEntry& entry);
    void update_snoop_status(PHATrackerEntry& entry);

    using PipePayload = std::pair<PHATrackerEntry*, PHATrackerEntrySave>;

    bool arbitrate(Pipeline<PipePayload>::Data req);
    bool access(Pipeline<PipePayload>::Data req);
    bool update(Pipeline<PipePayload>::Data req);

    Pipeline<PipePayload> main_pipe{"main_pipe",
                                    {
                                        AMPERE_PIPE_STAGE_M(PHA, arbitrate),
                                        AMPERE_PIPE_STAGE_M(PHA, access),
                                        AMPERE_PIPE_STAGE_M(PHA, update),
                                    }};

    chi::FinalState calc_wakeid_pdb_state(PHATrackerEntry& entry);
    void restore_intr_req_fields(PHATrackerEntry* interrupted_req, PHATrackerEntry& interrupting_snp);
    bool pass_is_rejected(const PHATrackerEntry& entry);
    void generate_request(PHATrackerEntry& entry);
    void generate_response(PHATrackerEntry& entry, int channel);
    void generate_snoops(PHATrackerEntry& entry);
    void generate_data(PHATrackerEntry& entry);
    virtual void generate_prefetch_target(PHATrackerEntry& entry)
    {
        AMPERE_FATAL("calling generate_prefetch_target from non-PxHA");
    }

    NodeId choose_stash_node(NodeId src, PhysAddr addr);
    unsigned int get_tracker_occupancy();

    chi::ResponseStatus data_resp_override_for_po(const PHATrackerEntry& entry, chi::FinalState state) const;
    chi::ResponseStatus snoop_resp_override_for_po(const PHATrackerEntry& entry, chi::FinalState state) const;
    chi::ResponseStatus compdata_resp(const PHATrackerEntry& entry) const;

    int get_response_channel(chi::ResponseOpcode opcode, int default_chan);

    friend PHATracker;
    friend class soc::Warmup;
};

class PxHA : public PHA, poly<PxHA, poly_constructor_default, poly_constructor_clock>, public Stasher
{
public:
    explicit PxHA(const sc_core::sc_module_name& name) : PxHA{name, nullptr} {}
    PxHA(const sc_core::sc_module_name& name, sc::Clock* clock) : PHA{name, clock} { pha_type = pha_fsm::PHA_t::Pxha; }

    Submodule<SAM, PMHA_SAM> prefetch_tgt_sam{"prefetch_tgt_sam"};

    void generate_prefetch_target(PHATrackerEntry& entry) override;
    void stash_preload(PhysAddr addr,
                       FinalState state,
                       NodeIdToStasher node_id_to_stasher,
                       chi::NodeId requester_node) override;
    void stash_invalidate(PhysAddr addr, NodeId) override;
};

class PmHA : public PHA, poly<PmHA, poly_constructor_default, poly_constructor_clock>
{
public:
    explicit PmHA(const sc_core::sc_module_name& name) : PmHA{name, nullptr} {}
    explicit PmHA(const sc_core::sc_module_name& name, sc::Clock* clock) : PHA{name, clock}
    {
        pha_type = pha_fsm::PHA_t::Pmha;
    }
};

} // namespace ampere::mesh

/*
 * vim: sw=4:ts=4:et:si:cindent:cino=\:0,(0,g0,t0:
 */
