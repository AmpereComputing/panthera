//
// Copyright (c) 2022 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//
#pragma once

#include "ampere/mesh/cbusy.h"
#include "ampere/mesh/injector/uop_source_prog.h"
#include "ampere/mesh/mesh_device.h"
#include "ampere/mesh/mesh_module.h"
#include "ampere/mesh/mesh_req.h"
#include "ampere/mesh/mesh_rsp.h"
#include "ampere/mesh/pager.h"
#include "ampere/mesh/retry.h"
#include "ampere/mesh/sam.h"
#include "ampere/sc/sc.h"

namespace ampere::mesh {

class CHIInjector
    : public MeshModule,
      public implements<CHIInjector, MeshDevice, sc::poly_constructor_default, sc::poly_constructor_clock>,
      MeshEvents
{
public:
    explicit CHIInjector(const sc_core::sc_module_name& name) : CHIInjector{name, nullptr} {}
    CHIInjector(const sc_core::sc_module_name& name, sc::Clock* clock);
    MeshPort& get_mesh_port(int i = 0) override { return mesh_port; }
    NodeId get_node_id(int i = 0) const override { return node_id; }

private:
    Param<bool> enable{"enable", "enable this injector", true};
    Param<NodeId> node_id{"node_id", "mesh node_id", 0};
    Param<std::vector<int>> duty_cycle_param{"duty_cycle", "duty cycle as a fraction: [num, den]", "[1, 4]"_json};
    Param<bool> dynamic_duty_cycle{"dynamic_duty_cycle", "adjust duty cycle dynamically based on retries", false};
    Param<bool> write_mode{"write_mode", "generate writes instead of reads", false};
    Param<int> send_data_after_compdbid_delay{
        "send_data_after_compdbid_delay",
        "Delay in TDMA after receiving CompDBID from memory, to receive data from TMEM",
        0};
    Param<bool> direct_memory_mode{"direct_memory_mode", "talk directory to a memory device", false};
    Param<float> chance_read{"chance_read", "chance of a read instead of write operation (float)", 1.0};
    Param<uint64_t> start_addr{"start_addr", "start of address range", 0};
    Param<uint64_t> stop_addr{"stop_addr", "end of address range", std::numeric_limits<uint64_t>::max()};
    Param<uint64_t> addr_mask{"addr_mask", "bits to mask out of generated address", 0x3f};
    Param<uint64_t> seed{"seed", "random seed", 42};
    Param<uint64_t> max_transactions{"max_transactions", "maximum number of transactions to generate", 0};
    Param<int64_t> braindead_limit{"braindead_limit", "maximum number of cycles since last completion", 100000};
    Param<uint64_t> heartbeat{"heartbeat", "print a heartbeat message every so many cycle", 0};
    Param<bool> evict_after_read{"evict_after_read", "generate an eviction for every read", false};
    Param<int> evict_after_read_delay{"evict_after_read_delay",
                                      "wait for N transactions before starting to evict (if evict_after_read == true)",
                                      1000};
    Param<int> num_rsp_ports{"num_rsp_ports", "number of response ports", 1};
    Param<bool> large_requests{"large_requests", "make large (512B) requests", false};

    MeshPort mesh_port{"mesh_port", 1, num_rsp_ports, 1, 1};

    std::unique_ptr<UniformRand> generator;

    void start_of_simulation() override;
    bool run() override;

    void update_duty_cycle();

    uint64_t bytes_per_packet{64};
    uint64_t data_received{0};
    uint64_t data_received_this_window{0};
    uint64_t data_sent{0};
    uint64_t data_sent_this_window{0};
    double bw_factor;
    double accumulated_latency{0.0};
    int retries{0};
    int rejects{0};

    RetryRequester retry{"retry"};
    Submodule<CBusyBase, CBusy> cbusy{"cbusy"};

    Submodule<SAM, CDRN_SAM> sam{"sam"};
    ConfigPort<PagingIfc, 1, sc_core::SC_ZERO_OR_MORE_BOUND> paging{"paging"};

    uint16_t txn_id{0};

    uint64_t total_transactions{0};
    Cycle last_completion{0};
    Cycle send_data_delay_in_cycles{0};
    std::map<Cycle, std::tuple<std::function<void(MeshRsp rsp)>, MeshRsp>> send_data_delay_queue;
    void send_data(MeshRsp rsp);

    struct TrackerEntry
    {
        TrackerEntry() : req{nullptr} {}
        TrackerEntry(MeshReq req) : req{req} {}
        MeshReq req;
        bool got_comp{false};
        bool got_data{false};
    };

    std::map<uint16_t, TrackerEntry> outstanding_reqs;
    std::map<uint64_t, MeshReq> waiting_for_credit;
    std::unordered_map<uint16_t, double> start_time;
    std::deque<MeshReq> retry_q;
    std::deque<MeshRsp> response_q;
    std::deque<std::pair<MeshDat, uint16_t>> data_q;

    std::deque<std::pair<PhysAddr, chi::FinalState>> cache;

    struct
    {
        int num{1};
        int den{4};
    } duty_cycle;
    ;

    int count{4};

    bool send_this_cycle();
    chi::RequestOpcode get_opcode();
};

} // namespace ampere::mesh
