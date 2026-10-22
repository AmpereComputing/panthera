//
// Copyright (c) 2022 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//
#pragma once

#include "ampere/mesh/cbusy.h"
#include "ampere/mesh/injector/uop_source_prog.h"
#include "ampere/mesh/injector/uop_source.h"
#include "ampere/mesh/injector/uop_source_is.h"
#include "ampere/mesh/injector/io_tracker.h"
#include "ampere/mesh/mesh_device.h"
#include "ampere/mesh/mesh_module.h"
#include "ampere/mesh/mesh_req.h"
#include "ampere/mesh/mesh_rsp.h"
#include "ampere/mesh/pager.h"
#include "ampere/mesh/retry.h"
#include "ampere/mesh/sam.h"
#include "ampere/sc/sc.h"
#include "ampere/sc/counter.h"
#include "ampere/util/enum.h"
#include "ampere/sc/connect.h"
#include "ampere/sc/transaction.h"

make_ns_enum(ampere::mesh, TrackerEntryState, Invalid, Ready, Executing, Cancelled, Deallocating);

namespace ampere::mesh {

using namespace ampere::sc;

class IORInjector : public MeshModule,
                    public implements<IORInjector, MeshDevice, poly_constructor_default, poly_constructor_clock>,
                    MeshEvents
{
public:
    explicit IORInjector(const sc_core::sc_module_name& name) : IORInjector{name, nullptr} {}
    IORInjector(const sc_core::sc_module_name& name, sc::Clock* clock);
    MeshPort& get_mesh_port(int i) override { return mesh_port[i]; }
    NodeId get_node_id(int i = 0) const override { return node_id.val().at(i); }

private:
    using Type = ampere::mesh::Type;
    Param<bool> enable{"enable", "enable this injector", false};
    Param<std::vector<NodeId>> node_id{"node_id", "mesh node_id", "[]"_json};
    Param<int> delay_factor{
        "delay_factor", "frequency of mesh/frequency of PCIe", 1};                   // currently it can only be integer
    Param<int> pipe_delay{"pipe_delay", "delay between PCIe and IOR in cycles", 45}; // 22ns by default
    Param<bool> Cacheable{"Cacheable", "cacheable field of MemAttr", true};
    Param<bool> Allocate{"Allocate", "allocate field of MemAttr", false};
    Param<unsigned> nrt_size{"nrt_size", "size of non posted request tracker", 512};
    Param<unsigned> prt_size{"prt_size", "size of posted request tracker", 512};
    Param<unsigned> iord_tracker_size{"iord_size", "size of io_tracker", 512};
    Param<unsigned> iowr_tracker_size{"iowr_size", "size of io_tracker", 512};
    Param<Cycle> brain_dead_limit{
        "brain_dead_limit", "indicate brain dead after this number of cycles without a committed Uop", 60000};
    Param<Cycle> timeout_limit{"timeout_limit",
                               "max timeout that en entry can reach before sending WriteDataCancel",
                               2000}; // assuming worst case roundtrip for writes is 1000ns

    Param<int> num_rsp_ports{"num_rsp_ports", "number of response ports", 2};
    Param<unsigned> num_sources{"num_sources", "number of IO sources", 1};

    sc_core::sc_vector<MeshPort> mesh_port{"mesh_port"};

    sc_core::sc_vector<Submodule<UopSource, UopSourceProg>> ioop_source{"source", num_sources};

    std::map<int, int> in_delay;
    int nrt_alloc_delay{0};
    int nrt_dealloc_delay{0};
    int prt_alloc_delay{0};
    int prt_dealloc_delay{0};

    RetryRequester retry{"retry"};
    CBusy cbusy{"cbusy"};

    Submodule<SAM, IDRN_SAM> sam{"sam"};
    ConfigPort<PagingIfc, 1, sc_core::SC_ZERO_OR_MORE_BOUND> paging{"paging"};

    using TrackerEntryState = ampere::mesh::TrackerEntryState;
    struct TrackerEntryObj : public sc::Transaction
    {
        TrackerEntryObj(uint64_t id) : req{nullptr}, id{id} {}

        TrackerEntryObj(const TrackerEntryObj&) = delete;

        MeshReq req;
        MeshRsp rsp{nullptr};
        uint64_t id;
        TrackerEntryState state{TrackerEntryState::Invalid};
        bool Wait4DBIDResp{false};
        bool Wait4Comp{false};
        bool Wait4RR{false};
        bool Wait4Data{false};
        bool dat_sent{false};
        NodeId tgt_id;
        uint16_t txn_id;
        Cycle last_rcvd_dbid{0};
        uint64_t io_id;
        int stream_id;
        IOEntry io_entry{nullptr};
    };

    using TrackerEntry = transaction_ptr<TrackerEntryObj>;

    Cycle rd_last_commit_cycle{0};
    Cycle wr_last_commit_cycle{0};

    OutInOpt<IOEntry> pcie_to_ior_rd{"pcie_to_ior_rd"};
    OutInOpt<IOEntry> pcie_to_ior_wr{"pcie_to_ior_wr"};

    OutInOpt<TrackerEntry> ior_to_pcie_rd{"ior_to_pcie_rd"};
    OutInOpt<TrackerEntry> ior_to_pcie_wr{"ior_to_pcie_wr"};

    int read_str_id{0};
    int write_str_id{0};

    std::optional<IOEntry> io_rentry;
    uint64_t rtlp;
    std::optional<IOEntry> io_wentry;
    uint64_t wtlp;

    std::list<TrackerEntry> nrt;
    std::list<TrackerEntry> prt;
    IOTracker io_tracker{"io_tracker", num_sources, iord_tracker_size, iowr_tracker_size};
    std::deque<uint64_t> tracker_free_list;
    std::vector<std::deque<MeshReq>> request_q;
    std::map<uint64_t, MeshReq> waiting_for_credit;
    std::deque<MeshReq> cancelled_txn_q;
    std::deque<MeshReq> pending_read_q;
    std::deque<MeshReq> retry_q;
    std::map<uint64_t, MeshReq> outstanding_reqs;
    std::vector<std::deque<MeshRsp>> response_q;
    std::deque<std::pair<MeshDat, uint16_t>> wr_data_q;
    std::deque<uint64_t> dealloc_rq;
    std::deque<uint64_t> dealloc_wq;

    bool is_read(RequestOpcode opcode) const;
    bool is_write(RequestOpcode opcode) const;
    IORInjector::Type get_type(MeshReq req);
    IORInjector::TrackerEntry allocate(IOEntry& io, PhysAddr addr);
    void make_data_pkts(TrackerEntry& entry);
    void make_response(TrackerEntry& entry);
    IORInjector::TrackerEntry find_entry_by_id(uint64_t id);
    void sched_dealloc_prt(uint64_t id);
    void deallocate_io_tracker(TrackerEntry& entry);

    void allocate_io_tracker(int i);
    void transmit_rd_pcie_to_ior();
    void transmit_wr_pcie_to_ior();
    void receive_rd_pcie_to_ior();
    void receive_wr_pcie_to_ior();
    void make_request();
    void send_request(int mp);
    void receive_response(int port, int mp);
    void receive_data();
    void send_response(int mp);
    void send_data();
    void deallocate_nrt();
    void deallocate_prt();
    void receive_rd_ior_to_pcie();
    void receive_wr_ior_to_pcie();

    bool run() override;

    void check_for_braindeath();
    void dump(std::ostream& os) const override;

    Event ev_alloc{"allocate", "Allocate a tracker entry"};
    Event ev_no_alloc_prt{"no_alloc_prt", "Cannot allocate a PRT tracker entry"};
    Event ev_no_alloc_nrt{"no_alloc_nrt", "Cannot allocate a NRT tracker entry"};
    Event ev_dealloc_nrt{"deallocate_nrt", "Deallocate a NRT tracker entry"};
    Event ev_dealloc_prt{"deallocate_prt", "Deallocate a PRT tracker entry"};
    Event ev_wake{"wake", "Wake up a blocked entry"};
    Event ev_ior_refill{"ior_refill", "PCIe refill"};
    Event ev_reject{"reject", "A request is rejected (no credits)"};
    Event ev_fetch{"fetch", "Fetch an IO entry"};
    Event ev_read_fetch{"read_fetch", "Fetch an IO Read entry"};
    Event ev_write_fetch{"write_fetch", "Fetch an IO Write entry"};
    Event ev_alloc_io{"alloc_io", "Allocate an IOEntry"};
    Event ev_dealloc_io{"dealloc_io", "Deallocate an IOEntry"};
    Event ev_pcie_to_ior_rd_tx{"pcie_to_ior_rd_tx", "Transmit Read IO transaction from PCIe to IOR"};
    Event ev_pcie_to_ior_rd_rx{"pcie_to_ior_rd_rx", "Receive Read IO transaction from PCIe to IOR"};
    Event ev_ior_to_pcie_rd_tx{"ior_to_pcie_rd_tx", "Transmit Read transaction from IOR to PCIe"};
    Event ev_ior_to_pcie_rd_rx{"ior_to_pcie_rd_rx", "Receive Read transaction from IOR to PCIe"};
    Event ev_pcie_to_ior_wr_tx{"pcie_to_ior_wr_tx", "Transmit Write IO transaction from PCIe to IOR"};
    Event ev_pcie_to_ior_wr_rx{"pcie_to_ior_wr_rx", "Receive Write IO transaction from PCIe to IOR"};
    Event ev_ior_to_pcie_wr_tx{"ior_to_pcie_wr_tx", "Transmit Write transaction from IOR to PCIe"};
    Event ev_ior_to_pcie_wr_rx{"ior_to_pcie_wr_rx", "Receive Write transaction from IOR to PCIe"};
    EventData<uint64_t> evd_tracker_size{"tracker_size", "Size of tracker"};
    EventData<unsigned> evd_num_reads{"num_reads", "Number of outstanding reads"};
    EventData<unsigned> evd_num_writes{"num_writes", "Number of outstanding writes"};
    EventData<unsigned> evd_outstanding_iord{"outstanding_iord", "Number of outstanding IO Read Entries"};
    EventData<unsigned> evd_outstanding_iowr{"outstanding_iowr", "Number of outstanding IO Write Entries"};
    EventData<unsigned> evd_io{"io_source", "RP source index"};
    EventData<unsigned> evd_iotype{"iotype", "IO Type"};
    EventData<uint64_t> evd_blocking_id{"blocking_id", "ID of blocking entry"};
    EventData<uint64_t> evd_waking_id{"waking_id", "ID of wake entry"};
    EventData<uint64_t> evd_io_id{"io_id", "ID of IO Entry"};
};

} // namespace ampere::mesh
