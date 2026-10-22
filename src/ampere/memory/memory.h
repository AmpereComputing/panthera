//
// Copyright (c) 2018-2020 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//
#pragma once

#include <fstream>
#include <string>
#include <variant>

#include "ampere/mesh/cbusy.h"
#include "ampere/mesh/mesh_module.h"
#include "ampere/mesh/mesh_device.h"
#include "ampere/mesh/retry.h"
#include "ampere/sc/sc.h"
#include "ampere/memory/bandwidth_latency.h"
#include "ampere/memory/chp.h"
#include "ampere/memory/hif.h"

namespace ampere::memory {

using namespace ampere::mesh;
using namespace ampere::sc;
using namespace ampere::archx;

class Memory : public MeshModule,
               public implements<Memory, MeshDevice, poly_constructor_default, poly_constructor_clock>,
               public MeshEvents
{
public:
    explicit Memory(const sc_core::sc_module_name& name);
    Memory(const sc_core::sc_module_name& name, Clock* clock);
    void end_of_elaboration() override;
    bool run() override;
    void end_of_simulation() override;

    MeshPort& get_mesh_port(int i = 0) override { return chp ? chp->get_mesh_port() : mesh_port; }
    NodeId get_node_id(int i = 0) const override { return node_id; }

    InWithCreditOpt<HIFCmd> hif_rd_cmd{"hif_rd_cmd"};
    InWithCreditOpt<HIFCmd> hif_wr_cmd{"hif_wr_cmd"};
    OutOpt<HIFDat> hif_dat;

    // Expose instantaneous latency values for testing
    float get_latency(void);

private:
    Param<mesh::chi::NodeId> node_id{"node_id", "Mesh Node identifier (for routing)", mesh::chi::NodeId{0}};
    Param<size_t> max_reads_param{"max_reads", "maximum outstanding memory read requests", 64};
    Param<size_t> max_writes_param{"max_writes", "maximum outstanding memory write requests", 64};
    Param<bool> enforce_bandwidth_limits{"enforce_bandwidth_limits",
                                         "Limit outstanding requests to prevent exceeding maximum bandwidth per core",
                                         false};
    Param<bool> enforce_write_bandwidth_limits{
        "enforce_write_bandwidth_limits",
        "Limit outstanding writes to prevent exceeding maximum bandwidth per core",
        false};
    Param<float> bandwidth_overuse_penalty{
        "bandwidth_overuse_penalty",
        "When extrapolating latency beyond available bandwidth points apply this extra factor to latency slope",
        1.0};
    Param<float> bandwidth_scale_factor{
        "bandwidth_scale_factor", "Adjust bandwidth latency curve points by scale factor", 1.0};
    Param<bool> use_request_latency_curve{
        "use_request_latency_curve",
        "Convert input bandwidth-latency curve to request-latency curve to better model high request rate behavior",
        false};
    Param<bool> infer_cbusy_queue_occupancy{"infer_cbusy_queue_occupancy",
                                            "Back-calculate queue occupancy sent to cbusy from observed bandwidth "
                                            "rather than using actual queue occupancies",
                                            true};
    Param<float> latency_offset{"latency_offset", "Add a fixed offset latency to the bandwidth latency curve", 0.0};
    Param<bool> dump_memory_trace{"dump_memory_trace", "dump memory trace", false};
    Param<bool> use_chp{"use_chp", "Use the CHP module for the CHI interface", false};

    Param<std::string> memory_trace_file_name{"memory_trace_file_name", "memory trace file name", ""};
    std::ofstream outfile;

    Event ev_hif_recv_rd{"hif_recv_rd", "Receive a read command on the HIF"};
    Event ev_hif_recv_wr{"hif_recv_wr", "Receive a write command on the HIF"};
    Event ev_hif_send_dat{"hif_send_dat", "Send data back on the HIF"};

    float queue_shrink_factor{1.0};

    MeshPort mesh_port;
    CBusy cbusy{"cbusy"};

    CreditPool hif_rd_credits{CreditPool::explicit_size{}, "hif_rd", max_reads_param};
    CreditPool hif_wr_credits{CreditPool::explicit_size{}, "hif_wr", max_writes_param};

    std::unique_ptr<CHP> chp;

    struct MemoryRetry : public Retry
    {
        using Retry::Retry;
        int request_to_pcrdtype(const MeshReq& req) override
        {
            switch (req->Opcode) {
            case chi::RequestOpcode::ReadNoSnp:
            case chi::RequestOpcode::ReadNoSnpSep:
                return 0;
            case chi::RequestOpcode::WriteNoSnpFull:
            case chi::RequestOpcode::WriteNoSnpPtl:
                return 1;
            default:
                AMPERE_FATAL("Received unexpected CHI request {}", req->Opcode);
                return -1;
            }
        }
    };

    MemoryRetry retry{"retry"};

    const double clock_scale;

    void initialize();

    void send_responses();
    bool send_chi_data(MeshDat data, unsigned cbusy_threshold);
    bool send_hif_data(HIFCmd cmd);
    void receive_chi_requests();
    void receive_hif_requests();
    void compute_latency();
    void schedule_wakeup();
    float effective_bytes(size_t outstanding_reqs, Cycle last_rq_cycle);
    void dump(std::ostream&) const override;

    EventData<unsigned> occupancy_data{"occupancy", "Occupancy request queue at time of request"};
    EventData<unsigned> latency_data{"latency", "Memory latency (nanoseconds)"};
    EventData<unsigned> bandwidth_data{"bandwidth", "Memory bandwidth (MB/s)"};
    EventData<unsigned> bandwidth_rd_data{"bandwidth_rd", "Memory bandwidth - read (MB/s)"};
    EventData<unsigned> bandwidth_wr_data{"bandwidth_wr", "Memory bandwidth - write (MB/s)"};

    Event ev_mem_response_full{"mem_response_full",
                               "Memory response port full with at least one outstanding response ready"};
    Event ev_mem_data_full{"mem_data_full", "Memory data port full with at least one outstanding data response ready"};
    Event ev_mem_write_dealloc{"mem_write_dealloc", "Memory write transaction deallocated from write queue"};
    Event ev_latency_update{"latency_update", "Update the current memory latency"};

    std::multimap<Cycle, MeshRsp> response_queue;
    std::multimap<Cycle, std::variant<MeshDat, std::pair<HIFCmd, Credit>>> data_queue;
    std::multimap<Cycle, std::variant<uint16_t, std::pair<HIFCmd, Credit>>> write_queue;

    Param<float> max_bandwidth_param{"max_bandwidth", "maximum memory bandwidth", 10.0};
    Param<float> high_bandwidth_param{
        "high_bandwidth",
        "high memory bandwidth level -- this is per core and gets scaled by bandwidth_scale_factor",
        9.0};
    Param<float> low_bandwidth_param{
        "low_bandwidth",
        "low memory bandwidth level -- this is per core and gets scaled by bandwidth_scale_factor",
        8.0};
    Param<size_t> bw_latency_window_count_param{
        "bw_latency_window_count", "Number of windows to compute memory bandwidth", 75};
    Param<float> bw_latency_window_param{
        "bw_latency_window", "Size of the window to compute memory bandwidth in nanoseconds", 750};
    Param<json> bw_latency_param{"bw_latency_data",
                                 "Bandwidth Latency Curve Data list of pairs [GB/s, nanoseconds]",
                                 "[{\"write_percentage\": 100, \"bw_latency\": [[100000, 130]]}]"_json};

    BandwidthLatency bw_latency{bw_latency_param,
                                bw_latency_window_count_param,
                                64 * max_reads_param,
                                64 * max_writes_param,
                                bandwidth_scale_factor,
                                bandwidth_overuse_penalty,
                                latency_offset,
                                use_request_latency_curve};
    CycleDelta bw_latency_window{10};
    unsigned current_latency_cycles{0};
    Cycle current_cycle{0};
    Cycle last_read_rq_cycle{0};
    Cycle last_write_rq_cycle{0};
    Cycle latency_window_start{0};
    size_t current_requests{0};
    size_t outstanding_rd_reqs{0};
    size_t outstanding_wr_reqs{0};

    static const int bytes_per_request{64};
};

} // namespace ampere::memory

/*
 * vim: sw=4:ts=4:et:si:cindent:cino=\:0,(0,g0,t0:
 */
