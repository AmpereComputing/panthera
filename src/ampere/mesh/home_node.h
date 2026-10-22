//
// Copyright (c) 2020 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//

#ifndef AMPERE_MESH_HOME_NODE_H
#define AMPERE_MESH_HOME_NODE_H

#include <map>
#include <set>

#include "ampere/archx/addr.h"
#include "ampere/mesh/chi/types.h"
#include "ampere/mesh/home_node_tracker.h"
#include "ampere/mesh/mesh_device.h"
#include "ampere/mesh/mesh_module.h"
#include "ampere/mesh/mesh_port.h"
#include "ampere/mesh/retry.h"
#include "ampere/mesh/snoop_filter.h"
#include "ampere/mesh/slc.h"
#include "ampere/mesh/sam.h"
#include "ampere/mesh/cbusy.h"
#include "ampere/mesh/rcbusy.h"
#include "ampere/sc/pipeline.h"
#include "ampere/sc/poly_constructors.h"
#include "ampere/sc/sc.h"
#include "ampere/uarch_common/cache.h"

namespace ampere::mesh {

using namespace ampere::archx;
using namespace ampere::sc;

class HNCBusy : public CBusy, poly<HNCBusy>
{
public:
    HNCBusy(const sc_core::sc_module_name& name) : CBusy(name) {}

    void update(chi::CBusyState cbusy, int outstanding_reqs = -1, bool retry = false) override;

    Param<unsigned> threshold_low{"threshold_low", "Number of low CBusy's to qualify as low for the window", 120};
    Param<unsigned> threshold_medium{
        "threshold_medium", "Number of medium CBusy's to qualify as medium for the window", 64};
    Param<unsigned> threshold_high{"threshold_high", "Number of high CBusy's to qualify as high for the window", 8};

private:
    unsigned medium_busy{0};

    chi::CBusyState overall{chi::CBusyState::Low};
};

class HomeNode : public MeshModule,
                 public implements<HomeNode, MeshDevice, poly_constructor_default, poly_constructor_clock>,
                 MeshEvents
{
public:
    explicit HomeNode(const sc_core::sc_module_name& name) : HomeNode{name, nullptr} {}
    HomeNode(const sc_core::sc_module_name& name, sc::Clock* clock);

    MeshPort mesh_port;

    MeshPort& get_mesh_port(int i = 0) override { return mesh_port; }
    NodeId get_node_id(int i = 0) const override { return node_id; }

    // Testability hooks
    std::optional<PhysAddr> preload_slc(PhysAddr addr, soc::WarmupInfo::Action action)
    {
        return slc.preload(addr, action);
    }
    std::optional<PhysAddr> preload_slc(PhysAddr addr, NewSLC::State state) { return slc.preload(addr, state); }
    size_t tracker_occupancy() const { return tracker.occupancy(); }

protected:
    Param<chi::NodeId> node_id{"node_id", "Mesh Node identifier (for routing)", chi::NodeId{0}};
    Param<chi::NodeId> mc_id_0{"mc_id_0", "Node identifer of affiliated memory controller", chi::NodeId{1}};
    Param<chi::NodeId> mc_id_1{"mc_id_1", "Node identifer of affiliated memory controller", chi::NodeId{1}};
    Param<bool> enable_slc{"enable_slc", "Enable system-level cache", false};
    Param<bool> lax_inclusion{"lax_inclusion", "Be lax about inclusion rules (warm up can violate them today)", true};

    Event ev_schedule{"schedule", "Schedule a tracker entry to the pipeline"};
    Event ev_access{"access", "Access the cache and snoop filter"};
    Event ev_alloc{"allocate", "Allocate a tracker entry"};
    Event ev_dealloc{"deallocate", "Deallocate a tracker entry"};
    EventData<unsigned> evd_outstanding{"evd_outstanding", "number of outstanding requests"};
    EventData<unsigned> evd_occupancy{"evd_occupancy", "occupancy of home node tracker"};

    HomeNodeTracker tracker{"tracker"};

    Retry retry{"retry"};
    RetryRequester retry_requester{"retry_requester"};

    Submodule<CBusyBase, HNCBusy> rcbusy{"rcbusy"};
    unsigned int outstanding_requests{0};
    std::set<uint16_t> tracked_transactions{};

    std::deque<MeshReq> txreq;
    std::deque<MeshRsp> txrsp;
    std::deque<MeshSnp> txsnp;
    std::deque<MeshDat> txdat;

    SnoopFilter snoop_filter{"snoop_filter"};
    NewSLC slc{"slc"};

    Submodule<SAM, HNF_SAM> sam{"sam"};

    ZeroDelayOutIn<MeshDat> slc_hit_data_completion{"slc_hit_data_completion_dummy_pipeline"};

    bool run() override;
    void receive_requests();
    void receive_responses();

    void transmit();
    template<typename T, typename CallEvent>
    void transmit_channel(std::deque<T>& txq, OutOpt<T>& port, CallEvent ev);

    bool arbitrate(Pipeline<HomeNodeTrackerEntry*>::Data req);
    bool access(Pipeline<HomeNodeTrackerEntry*>::Data req);
    bool update(Pipeline<HomeNodeTrackerEntry*>::Data req);

    SFStatus access_sf(HomeNodeTrackerEntry& entry);
    CacheStatus access_cache(HomeNodeTrackerEntry& entry);

    Pipeline<HomeNodeTrackerEntry*> main_pipe{"main_pipe",
                                              {
                                                  AMPERE_PIPE_STAGE_M(HomeNode, arbitrate),
                                                  AMPERE_PIPE_STAGE_M(HomeNode, access),
                                                  AMPERE_PIPE_STAGE_M(HomeNode, update),
                                              }};

    void send_request(HomeNodeTrackerEntry& entry);
    void send_response(HomeNodeTrackerEntry& entry);
    void send_snoop(HomeNodeTrackerEntry& entry);
    void send_data(HomeNodeTrackerEntry& entry);
    void send_data_to_requester(HomeNodeTrackerEntry& entry);
    void send_delayed_data_to_requester();
    void send_data_to_memory(HomeNodeTrackerEntry& entry);

    uint8_t data_id_to_chunk(uint8_t data_id);
    NodeId choose_mc(PhysAddr addr);
};

} // namespace ampere::mesh

#endif // AMPERE_MESH_HOME_NODE_H

/*
 * vim: sw=4:ts=4:et:si:cindent:cino=\:0,(0,g0,t0:
 */
