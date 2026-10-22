//
// Copyright (c) 2024 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//
// The CHP ("CHI Port") is a very ambiguously named module that refers to the
// interface between Matrix and a generic memory endpoint.
//
// Its job is to manage the CHI protocol requests that will be made of the
// memory controller; mostly operations like ReadNoSnp and WriteNoSnp. It
// handles retry, CBusy, PrefetchTgt, and partial writes (RMW). The goal is
// to send nothing more than simple cache-line sized Read and Write operations
// to the memory controller core.
//
// High level diagram:
//
//                  +-------------------------+
//                  |          CHP            |
//                  +----+               +----+
// Mesh     <-->    |CHI |               |HIF |  <-->  Memory
//   ReadNoSnp      |port|               |port|    RD
//   ReadNoSnpSep   +----+               +----+    WR
//   WriteNoSnpPtl  |                         |
//   WriteNoSnpFull +-------------------------+
//   ...
//
#pragma once

#include <deque>
#include <map>
#include <utility>

#include "ampere/memory/hif.h"
#include "ampere/mesh/matrixcbusy.h"
#include "ampere/mesh/mesh_device.h"
#include "ampere/mesh/mesh_module.h"
#include "ampere/mesh/mesh_req.h"
#include "ampere/mesh/retry.h"
#include "ampere/sc/sc.h"
#include "ampere/sc/transaction.h"

make_ns_enum(ampere::memory, ARB, NONE, PREFETCH, IRON, GOLD, PLATINUM, RMW);

namespace ampere::memory {

using namespace ampere::mesh;
using namespace ampere::sc;

/** RRT (Read Request Tracker)
 *
 *  This table stores incoming read requests until they are sent to the
 *  HIF interface, at which point they move to the RDT (Read Data Tracker)
 *  to await data return.
 *
 *  In addition to keeping read requests in a table, we keep separate
 *  FIFOs partitioned by QoS to track which entries are ready to forward.
 *  This makes the QoS-based arbitration more efficient because we can
 *  instantly see the oldest ready entry at each QoS level.
 *
 *  The CHP trackers in general are implemented as a vector of entries
 *  along with a free list of ids. This allows the table of entries to
 *  be allocated and deallocated randomly (i.e., not FIFO or LIFO).
 */
class RRT
{
public:
    explicit RRT(size_t size)
    {
        entries.resize(size);
        id_free_list.resize(size);
        std::iota(id_free_list.begin(), id_free_list.end(), 0);
    }

    unsigned occupancy() const { return entries.size() - id_free_list.size(); }

    int allocate(MeshReq req, std::optional<int> pf_hit, bool pf_has_data, std::optional<int> wr_hit)
    {
        int id = get_id();
        entries[id] = Entry{req, pf_hit, wr_hit};

        if (wr_hit.has_value())
            blocked_by_write[*wr_hit] = id;
        else if (pf_hit.has_value() && !pf_has_data)
            blocked_by_pft[*pf_hit] = id;
        else
            ready_queues[qos_to_arb(req->QoS)].push_back(id);

        return id;
    }

    bool ready() const
    {
        return std::any_of(ready_queues.begin(), ready_queues.end(), [](const auto& e) { return !e.second.empty(); });
    }

    bool ready(ARB arb) const { return ready_queues.contains(arb) && !ready_queues.at(arb).empty(); }

    void wake_blocked_on_write(int wr_id)
    {
        auto blocked = blocked_by_write.find(wr_id);

        if (blocked != blocked_by_write.end()) {
            auto& entry = entries[blocked->second];
            entry.wr_hit.reset();
            ready_queues[qos_to_arb(entry.req->QoS)].push_back(blocked->second);
            blocked_by_write.erase(blocked);
        }
    }

    void wake_blocked_on_pft(int prt_id)
    {
        auto blocked = blocked_by_pft.find(prt_id);

        if (blocked != blocked_by_pft.end()) {
            auto& entry = entries[blocked->second];
            ready_queues[qos_to_arb(entry.req->QoS)].push_back(blocked->second);
            blocked_by_pft.erase(blocked);
        }
    }

    std::tuple<int, MeshReq, std::optional<int>> dequeue(ARB arb)
    {
        AMPERE_ASSERT(!ready_queues.at(arb).empty(), "attempt to dequeue from an empty ready queue");
        int id = ready_queues[arb].front();
        ready_queues[arb].pop_front();
        auto req = entries[id].req;
        auto pf_hit = entries[id].pf_hit;
        deallocate(id);
        return {id, req, pf_hit};
    }

    void dump(std::ostream& os) const
    {
        int index = 0;

        for (const auto& entry : entries) {
            if (entry.req) {
                fmt::print(os,
                           "[{:2d}] Opcode={} SrcID={} TxnID={} txid={} PFHit={} WRHit={}\n",
                           index,
                           entry.req->Opcode,
                           entry.req->SrcID,
                           entry.req->TxnID,
                           entry.req->txid().value,
                           entry.pf_hit.has_value() ? *entry.pf_hit : -1,
                           entry.wr_hit.has_value() ? *entry.wr_hit : -1);
            }
            ++index;
        }
    }

private:
    struct Entry
    {
        MeshReq req{};
        std::optional<int> pf_hit{};
        std::optional<int> wr_hit{};
    };

    std::vector<Entry> entries;
    std::deque<int> id_free_list;
    std::map<ARB, std::deque<int>> ready_queues;
    std::map<int, int> blocked_by_write;
    std::map<int, int> blocked_by_pft;

    int get_id()
    {
        AMPERE_ASSERT(!id_free_list.empty(), "cannot allocate into full RRT");
        int id = id_free_list.front();
        id_free_list.pop_front();
        return id;
    }

    void deallocate(int id)
    {
        entries[id] = Entry{};
        id_free_list.push_back(id);
    }

    static ARB qos_to_arb(chi::Qos qos)
    {
        if (qos.is_platinum())
            return ARB::PLATINUM;
        else if (qos.is_gold())
            return ARB::GOLD;
        else if (qos.is_iron())
            return ARB::IRON;
        else
            AMPERE_ERROR("unrecognized QoS {}", std::to_underlying(qos.to_level()));
        return ARB::NONE;
    }
};

/** WRT (Write Request Tracker)
 *
 *  This table stores incoming write requests until they are sent to the
 *  HIF interface. Writes aren't ready to forward until they have data, so
 *  we keep a separate "ready" queue for writes that have their data to
 *  make arbitration efficient.
 */
class WRT
{
public:
    Param<size_t> max_outstanding_rmw{
        "max_outstanding_rmw", "Maximum number of outstanding underfill reads (RMW tracker size)", 64};

    explicit WRT(size_t size)
    {
        entries.resize(size);
        id_free_list.resize(size);
        std::iota(id_free_list.begin(), id_free_list.end(), 0);
    }

    unsigned occupancy() const { return entries.size() - id_free_list.size(); }

    int allocate(MeshReq req)
    {
        int id = get_id();
        auto older_matching_wr_id = match(req->Addr);
        entries[id] = Entry{req, older_matching_wr_id};

        if (older_matching_wr_id.has_value())
            blocked_by_write[*older_matching_wr_id] = id;
        else if (entries[id].is_rmw())
            ready_rmw_queue.push_back(id);

        return id;
    }

    void receive_data(int id)
    {
        auto& entry = entries.at(id);
        AMPERE_ASSERT(entry.req, "data received for invalid entry {}", id);
        entry.has_data = true;

        if (!entry.wr_hit && (!entry.is_rmw() || entry.has_rmw_read_data))
            ready_queue.push_back(id);
    }

    MeshReq receive_data_rmw(int id)
    {
        auto& entry = entries.at(id);
        AMPERE_ASSERT(entry.req, "data received for invalid entry {}", id);
        AMPERE_ASSERT(entry.is_rmw(), "underfill data received for non-partial write entry {}", id);
        entry.has_rmw_read_data = true;
        --outstanding_rmw;

        if (!entry.wr_hit && entry.has_data)
            ready_queue.push_back(id);

        return entry.req;
    }

    void wake_blocked_on_write(int wr_id)
    {
        auto blocked = blocked_by_write.find(wr_id);

        if (blocked != blocked_by_write.end()) {
            auto& entry = entries[blocked->second];
            entry.wr_hit.reset();
            blocked_by_write.erase(blocked);
            if (entry.has_data && (!entry.is_rmw() || entry.has_rmw_read_data))
                ready_queue.push_back(blocked->second);
        }
    }

    bool ready(ARB arb = ARB::NONE) const
    {
        if (arb == ARB::RMW)
            return !ready_rmw_queue.empty() && outstanding_rmw <= max_outstanding_rmw;
        else
            return !ready_queue.empty();
    }

    std::pair<int, MeshReq> dequeue()
    {
        AMPERE_ASSERT(!ready_queue.empty(), "attempt to dequeue from an empty ready queue");
        int id = ready_queue.front();
        ready_queue.pop_front();
        MeshReq req = entries[id].req;
        deallocate(id);
        return {id, req};
    }

    std::pair<int, MeshReq> dequeue_rmw()
    {
        AMPERE_ASSERT(!ready_rmw_queue.empty(), "attempt to dequeue from an empty ready_rmw queue");
        int id = ready_rmw_queue.front();
        ready_rmw_queue.pop_front();
        MeshReq req = entries[id].req;
        ++outstanding_rmw;
        return {id, req};
    }

    std::optional<int> match(PhysAddr addr) const
    {
        int index = 0;

        for (const auto& entry : entries) {
            if (entry.req && addr == entry.req->Addr && !entry.wr_hit.has_value())
                return index;
            ++index;
        }

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wmaybe-uninitialized"
        return {};
#pragma GCC diagnostic pop
    }

private:
    struct Entry
    {
        Entry() = default;
        Entry(MeshReq req, std::optional<int> wr_hit) : req{req}, wr_hit{wr_hit} {}

        MeshReq req{};
        std::optional<int> wr_hit{};
        bool has_data{false};
        bool has_rmw_read_data{false};

        bool is_rmw() const { return req->Opcode == chi::RequestOpcode::WriteNoSnpPtl; }
    };

    std::vector<Entry> entries;
    std::deque<int> id_free_list;
    std::deque<int> ready_queue;
    std::deque<int> ready_rmw_queue;
    std::map<int, int> blocked_by_write;
    size_t outstanding_rmw{0};

    int get_id()
    {
        AMPERE_ASSERT(!id_free_list.empty(), "cannot allocate into full WRT");
        int id = id_free_list.front();
        id_free_list.pop_front();
        return id;
    }

    void deallocate(int id)
    {
        entries[id] = Entry{};
        id_free_list.push_back(id);
    }
};

/** PRT (Prefetch Request Tracker)
 *
 *  The Prefetch Request Tracker tracks PrefetchTgt requests and data
 *  until a demand read hits and the data can be returned. Allocation
 *  is similar to the RRT in that we still have to look for RAW
 *  (Read-After-Write) conflicts by matching against the WRT conflicts.
 *
 *  Deallocation from the PRT is a bit different. Entries hang around
 *  until they get kicked out: when a new entry wants to allocate, if
 *  we don't have any space we deallocate the oldest entry which has
 *  data but has not been hit by any demand read. Note that such an
 *  entry may have been hit by a read a while back and already returned
 *  data and responses; if so, then it goes back into a state of "not
 *  having been hit".
 */
class PRT
{
public:
    explicit PRT(size_t size)
    {
        entries.resize(size);
        id_free_list.resize(size);
        std::iota(id_free_list.begin(), id_free_list.end(), 0);
    }

    int allocate(MeshReq req, std::optional<int> wr_hit)
    {
        int id = get_id();

        if (id != -1) {
            entries[id] = Entry{req, wr_hit};

            if (!wr_hit)
                ready_queue.push_back(id);
        }

        return id;
    }

    bool ready() const { return !ready_queue.empty(); }

    MeshReq receive_data(int id)
    {
        auto& entry = entries.at(id);
        entry.state = Entry::State::DAT_RCVD;
        return entry.req;
    }

    void use_data(int id)
    {
        auto& entry = entries.at(id);
        AMPERE_ASSERT(entry.rd_hit_count > 0, "tried to use data in a PRT entry with 0 read hits");
        --entry.rd_hit_count;
    }

    std::pair<int, MeshReq> dequeue()
    {
        AMPERE_ASSERT(!ready_queue.empty(), "attempt to dequeue from an empty ready queue");
        int id = ready_queue.front();
        ready_queue.pop_front();
        auto req = entries[id].req;
        entries[id].state = Entry::State::REQ_SENT;
        return {id, req};
    }

    std::pair<std::optional<int>, bool> match(PhysAddr addr)
    {
        int index = 0;

        for (auto& entry : entries) {
            if (entry.req && addr == entry.req->Addr) {
                ++entry.rd_hit_count;
                return {index, entry.state == Entry::State::DAT_RCVD};
            }
            ++index;
        }

        // #pragma GCC diagnostic push
        // #pragma GCC diagnostic ignored "-Wmaybe-uninitialized"
        return {std::nullopt, false};
        // #pragma GCC diagnostic pop
    }

    void wake_blocked_on_write(int wr_id)
    {
        auto blocked = blocked_by_write.find(wr_id);

        if (blocked != blocked_by_write.end()) {
            auto& entry = entries[blocked->second];
            entry.wr_hit.reset();
            ready_queue.push_back(blocked->second);
            blocked_by_write.erase(blocked);
        }
    }

private:
    struct Entry
    {
        Entry() = default;
        Entry(MeshReq req, std::optional<int> wr_hit) : req{req}, wr_hit{wr_hit} {}

        enum class State {
            NEW,
            REQ_SENT,
            DAT_RCVD
        };

        MeshReq req;
        std::optional<int> wr_hit{};
        State state{State::NEW};
        int rd_hit_count{0};
    };

    std::vector<Entry> entries;
    std::deque<int> id_free_list;
    std::deque<int> alloc_order;
    std::deque<int> ready_queue;
    std::map<int, int> blocked_by_write;

    int get_id()
    {
        if (id_free_list.empty()) {
            for (auto it = alloc_order.begin(); it != alloc_order.end(); ++it) {
                auto& entry = entries[*it];

                if (entry.state == Entry::State::DAT_RCVD && entry.rd_hit_count == 0) {
                    deallocate(*it);
                    alloc_order.erase(it);
                    break;
                }
            }
        }

        if (id_free_list.empty()) {
            return -1;
        } else {
            int id = id_free_list.front();
            id_free_list.pop_front();
            alloc_order.push_back(id);
            return id;
        }
    }

    void deallocate(int id)
    {
        entries[id] = Entry{};
        id_free_list.push_back(id);
    }
};

/** RDT (Read Data Tracker)
 *
 *  This table tracks reads which have been forwarded to the HIF but
 *  which have not yet received data back. Once data is received, then
 *  responses and data can be sent back over the CHI interface.
 */
class RDT
{
public:
    explicit RDT(size_t size)
    {
        entries.resize(size);
        id_free_list.resize(size);
        std::iota(id_free_list.begin(), id_free_list.end(), 0);
    }

    bool has_free_entry() const { return !id_free_list.empty(); }

    int allocate(MeshReq req)
    {
        int id = get_id();
        entries[id] = Entry{req};
        return id;
    }

    bool ready() const { return !ready_queue.empty(); }

    MeshReq receive_data(int id)
    {
        auto& entry = entries.at(id);
        entry.has_data = true;
        ready_queue.push_back(id);
        return entry.req;
    }

    std::pair<int, MeshReq> dequeue()
    {
        AMPERE_ASSERT(!ready_queue.empty(), "attempt to dequeue from an empty ready queue");
        int id = ready_queue.front();
        ready_queue.pop_front();
        MeshReq req = entries[id].req;
        deallocate(id);
        return {id, req};
    }

private:
    struct Entry
    {
        Entry() = default;
        explicit Entry(MeshReq req) : req{req} {}

        MeshReq req{};
        bool has_data{false};
    };

    std::vector<Entry> entries;
    std::deque<int> id_free_list;
    std::deque<int> ready_queue;

    int get_id()
    {
        AMPERE_ASSERT(!id_free_list.empty(), "cannot allocate into full RDT");
        int id = id_free_list.front();
        id_free_list.pop_front();
        return id;
    }

    void deallocate(int id)
    {
        entries[id] = Entry{};
        id_free_list.push_back(id);
    }
};

/** CHP ("CHI Port") for the memory controller.
 *
 *  The general operation of the CHP is to receive requests from the mesh,
 *  determine whether they can be processed or must be retried, and then
 *  allocate them into various trackers/queues until they can be dispatched
 *  through the HIF interface and eventually completed by sending the
 *  appropriate CHI respones/data and deallocating from internal structures.
 *
 *  Roughly speaking, a "queue" in the terminology below is a FIFO structure
 *  which feeds into, say, the arbiter driving the HIF interface, while a
 *  "tracker" is a randomly accessed structure that expects to be indexed by
 *  an id (such as when read data returns, or when write data arrives from
 *  the mesh).
 *
 *  There is a bit of a disconnect between the arrangement of the real CHP
 *  and this model, done for ease of modeling. The real CHP has one structure
 *  for reads, one for writes, and one for prefetches. This CHP model has
 *  a series of read queues, one per QoS level, where "prefetch" is added as
 *  an additional pseudo-QoS level. This allows us to arbitrate easily among
 *  the heads of each queue.
 */
class CHP : public MeshModule, public implements<CHP, MeshDevice>
{
public:
    explicit CHP(const sc_core::sc_module_name& name);
    ~CHP() override;
    CHP(const CHP&) = delete;
    CHP(CHP&&) = delete;
    CHP& operator=(const CHP&) = delete;
    CHP& operator=(CHP&&) = delete;

    bool run() override;

    MeshPort& get_mesh_port(int i = 0) override { return mesh_port; }
    NodeId get_node_id(int i = 0) const override { return node_id; }

    Out<HIFCmd> hif_rd_cmd{"hif_rd_cmd"};
    Out<HIFCmd> hif_wr_cmd{"hif_wr_cmd"};
    In<HIFDat> hif_dat{"hif_dat"};

private:
    Param<mesh::chi::NodeId> node_id{"node_id", "Mesh Node identifier (for routing)", default_node_id()};
    Param<size_t> rrt_size{"rrt_size", "Size of the Read Request Tracker", 64};
    Param<size_t> wrt_size{"wrt_size", "Size of the Write Request Tracker", 64};
    Param<size_t> prt_size{"prt_size", "Size of the Prefetch Request Tracker", 32};
    Param<size_t> rdt_size{"rdt_size", "Size of the Read Data Tracker", 256};
    Param<std::map<std::string, int>> arb_counter_param{
        "arb_counters", "Reset values for the arbitration counters for various QoS settings", R"({
        "platinum": 8,
        "gold": 4,
        "iron": 2,
        "prefetch": 1,
        "rmw": 2
    })"_json};
    Param<CycleDelta> data_return_delay{"data_return_delay", "Minimum delay from HIF to mesh data return in cycles", 5};
    Param<bool> split_cbusy{
        "split_cbusy", "Instead of a unified CBusy for the CHP maintain separate read and write CBusy state", true};

    Event ev_alloc_rrt{"alloc_rrt", "Allocate a request into the RRT"};
    Event ev_alloc_prt{"alloc_prt", "Allocate a request into the PRT"};
    Event ev_alloc_wrt{"alloc_wrt", "Allocate a request into the WRT"};
    Event ev_select_rd{"select_rd", "Select a read request in the arbiter"};
    Event ev_select_wr{"select_wr", "Select a write request in the arbiter"};
    Event ev_hit_prefetch{"hit_prefetch", "A selected read hit an existing prefetch"};
    Event ev_hif_data_return{"hif_data_return", "Read data is returned on the HIF"};

    EventData<int> evd_hif_id{"hif_id", "HIF request id"};
    EventData<bool> evd_prefetch{"prefetch", "Request is a prefetch"};

    MeshEvents* ev;
    bool local_mesh_events;

    MeshPort mesh_port{"mesh_port"};
    MatrixCBusy cbusy{"cbusy"};
    MatrixCBusy write_cbusy{"write_cbusy"};

    struct CHPRetry : public Retry
    {
        using Retry::Retry;

        int request_to_pcrdtype(const MeshReq& req) override { return is_read(req->Opcode) ? 0 : 1; }
    };

    CHPRetry retry{"retry"};

    RRT rrt{rrt_size};
    WRT wrt{wrt_size};
    PRT prt{prt_size};
    RDT rdt{rdt_size};

    std::map<ARB, int> arb_counter;
    std::map<ARB, int> arb_counter_reset_values;

    std::deque<MeshRsp> rspq;
    std::deque<std::pair<MeshDat, Cycle>> datq;

    bool receive_request(Pipeline<MeshReq>::Data data);
    bool enqueue_request(Pipeline<MeshReq>::Data data);

    Pipeline<MeshReq> request_pipe{
        "request_pipe", {AMPERE_PIPE_STAGE_M(CHP, receive_request), AMPERE_PIPE_STAGE_M(CHP, enqueue_request)}};

    unsigned get_occupancy() const { return rrt.occupancy() + wrt.occupancy(); }

    void arbitrate();
    void grant_credit(uint8_t pcrd_type);
    void generate_data_response(MeshReq, DataSource data_source);
    void pull_data(MeshReq req, int id);
    void receive_data_from_hif();
    void receive_data_from_mesh();
    void receive_responses_from_mesh();
    void retry_request(MeshReq req, int pcrd_type);
    void send_data_to_mesh();
    void send_responses_to_mesh();
    bool work_to_do() const;

    NodeId default_node_id()
    {
        const auto* dev = get_nearest_ancestor<MeshDevice>(this);
        if (dev)
            return dev->get_node_id();
        else
            return NodeId{0};
    }

    void dump(std::ostream& os) const override
    {
        fmt::print(os, "==== {}.RRT ====\n", name());
        rrt.dump(os);
        fmt::print(os, "========\n");
    }
};

} // namespace ampere::memory
