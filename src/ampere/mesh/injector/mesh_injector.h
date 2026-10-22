//
// Copyright (c) 2020 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//

#ifndef AMPERE_MESH_MESH_INJECTOR_H
#define AMPERE_MESH_MESH_INJECTOR_H

#include <fmt/core.h>
#include <fmt/ostream.h>
#include <map>
#include <random>

#include "ampere/archx/register.h"
#include "ampere/archx/instruction_simple.h"
#include "ampere/mesh/cbusy.h"
#include "ampere/mesh/rcbusy.h"
#include "ampere/mesh/matrixcbusy.h"
#include "ampere/mesh/injector/uop_source.h"
#include "ampere/mesh/injector/uop_source_is.h"
#include "ampere/mesh/injector/uop_source_prog.h"
#include "ampere/mesh/control_module.h"
#include "ampere/mesh/mesh_device.h"
#include "ampere/mesh/mesh_module.h"
#include "ampere/mesh/mesh_port.h"
#include "ampere/mesh/mesh_req.h"
#include "ampere/mesh/pager.h"
#include "ampere/mesh/retry.h"
#include "ampere/mesh/sam.h"
#include "ampere/sc/sc.h"
#include "ampere/uarch_common/cache.h"

namespace ampere::soc {
class Warmup;
}

namespace ampere::mesh {

using namespace archx;
using namespace sc;
using namespace uarch_common;

struct Block
{
    enum Code {
        REGISTER,
        ADDRESS,
        SAME_CACHE_LINE,
        REQUEST_ID,
        MEM_RESPONSE,
        PCREDIT,
        BARRIER,
        MAX_CODE
    };

    Code code;
    uint64_t id;

    Block(Code code, uint64_t id) : code{code}, id{id} {}

    struct Compare
    {
        bool operator()(const Block& a, const Block& b) const
        {
            return a.code == b.code ? a.id < b.id : a.code < b.code;
        }
    };

    friend std::ostream& operator<<(std::ostream& out, const Block& block)
    {
        static const char* code2str[] = {
            "REGISTER", "ADDRESS", "SAME_CACHE_LINE", "REQUEST_ID", "MEM_RESPONSE", "PCREDIT", "BARRIER"};
        fmt::print(out, "{}({})", code2str[block.code], block.id);
        return out;
    }
};

template<typename T>
class WaitPool : std::multimap<Block, T, Block::Compare>
{
public:
    WaitPool() { counts.resize(Block::Code::MAX_CODE, 0); }
    using std::multimap<Block, T, Block::Compare>::size;

    void sleep(Block b, T obj)
    {
        ++obj->block_count;
        ++counts[b.code];
        this->insert({b, obj});
    }

    template<typename Action>
    void wake(Block b, Action action, bool only_one = false)
    {
        auto [first, last] = this->equal_range(b);

        while (first != last) {
            auto obj = first->second;
            first = this->erase(first);
            --counts[b.code];

            if (--obj->block_count == 0) {
                action(obj);
                if (only_one)
                    return;
            }
        }
    }

    template<typename Predicate, typename Action>
    void wake(Block b, Predicate predicate, Action action)
    {
        auto [first, last] = this->equal_range(b);

        while (first != last) {
            auto obj = first->second;

            if (predicate(obj)) {
                first = this->erase(first);
                --counts[b.code];

                if (--obj->block_count == 0)
                    action(obj);
            } else {
                ++first;
            }
        }
    }

    void roll_over(Block from, Block to, bool only_one = false)
    {
        std::vector<T> objs;
        wake(from, [&objs](T& obj) { objs.push_back(obj); }, only_one);
        for (auto& obj : objs)
            sleep(to, obj);
    }

    unsigned int sleep_count(Block::Code code) const { return counts[code]; }

    friend std::ostream& operator<<(std::ostream& out, const WaitPool<T>& wp)
    {
        for (auto& entry : wp)
            out << entry.second << " blocked on " << entry.first << '\n';
        return out;
    }

protected:
    std::vector<unsigned> counts;
};

struct MemReqObj
{
    int block_count{0};

    Uop uop;
    MeshReq req;
    MeshRsp rsp;
    MeshDat dat;
    MeshSnp snp;

    bool wait_comp{false};
    bool wait_data{false};
    bool stash_in_l3{false};

    explicit MemReqObj(MeshReq req) : MemReqObj{nullptr, req} { wait_comp = wait_data = is_read(req->Opcode); }
    MemReqObj(Uop uop, MeshReq req) : uop{uop}, req{req} { wait_comp = wait_data = is_read(req->Opcode); }
    MemReqObj(MeshSnp snp) : snp{snp} { stash_in_l3 = true; }
};

using MemReq = boost::local_shared_ptr<MemReqObj>;
static const int kRandSeed = 4;

class MeshInjector : public MeshModule,
                     public implements<MeshInjector, MeshDevice, poly_constructor_default, poly_constructor_clock>,
                     MeshEvents,
                     public Stasher
{
public:
    explicit MeshInjector(const sc_core::sc_module_name& name) : MeshInjector{name, nullptr} {}
    MeshInjector(const sc_core::sc_module_name& name, sc::Clock* clock);

    Param<int> num_rsp_ports{"num_rsp_ports", "number of response ports", 1};

    MeshPort mesh_port{"mesh_port", 1, num_rsp_ports, 1, 1};

    bool run() override;

    MeshPort& get_mesh_port(int i = 0) override { return mesh_port; }
    NodeId get_node_id(int i = 0) const override { return node_id; }
    const SAM* get_sam() const override { return sam.get(); }

    // Stasher interface
    bool can_stash() const override { return enable; }
    void stash_preload(PhysAddr, FinalState, NodeIdToStasher, chi::NodeId requester_node) override;
    void stash_invalidate(PhysAddr, NodeId) override;

    void dump(std::ostream& out = std::cout) const override;

    struct CacheEntry
    {
        PhysAddr addr;
        enum class State {
            I,
            UC,
            UD,
            SC,
            SD
        } state;

        bool is_unique() const { return state == State::UC || state == State::UD; }
        bool is_dirty() const { return state == State::UD || state == State::SD; }

        friend std::ostream& operator<<(std::ostream& os, State state)
        {
            switch (state) {
            case State::I:
                os << "I";
                return os;
            case State::UC:
                os << "UC";
                return os;
            case State::UD:
                os << "UD";
                return os;
            case State::SC:
                os << "SC";
                return os;
            case State::SD:
                os << "SD";
                return os;
            }
            return os;
        }

#ifdef USE_BOOST_SERIALIZATION
        template<class Archive>
        void serialize(Archive& ar, const unsigned int version)
        {
            ar & addr;
            ar & state;
        }
#endif
    };

    void preload_in_l2(PhysAddr addr, MeshInjector::CacheEntry::State state);
    void preload_in_l3(PhysAddr addr, MeshInjector::CacheEntry::State state);
    MeshInjector::CacheEntry::State peek_state_l2(PhysAddr addr);
    MeshInjector::CacheEntry::State peek_state_l3(PhysAddr addr);

    void dump_l2(std::ostream& os = std::cout);
    void dump_l3(std::ostream& os = std::cout);

#ifdef USE_BOOST_SERIALIZATION
    bool can_serialize() const { return true; }
    void serialize(boost::archive::text_oarchive& oa) const override;
    void deserialize(boost::archive::text_iarchive& ia) override;
    void serialize_cache(boost::archive::text_oarchive& oa, const Cache<PhysAddr, CacheEntry>& cache_data) const;
    void deserialize_cache(boost::archive::text_iarchive& ia, Cache<PhysAddr, CacheEntry>& cache_data);
#endif

protected:
    Param<bool> enable{"enable", "enabled this injector", true};
    Param<bool> stash_only{"stash_only", "This injector operates only as an L3 cache", false};
    Param<bool> throttle_retries{
        "throttle_retries", "CBusy request limit inihibts completion of retried requests", false};
    Param<NodeId> node_id{"node_id", "mesh node_id", "[0, 0, 0, 0]"_json};
    Param<size_t> max_outstanding_requests{
        "max_outstanding_requests", "maximum number of outstanding memory requests", 48};
    Param<size_t> max_outstanding_stashes{"max_outstanding_stashes", "maximum number of incoming stash requests", 8};
    Param<size_t> bank_hash_bits{"bank_hash_bits", "number of address bits to use in fillbuffer bank hash", 0};
    Param<size_t> max_pending_uops{"max_pending_uops", "maximum number of uncommitted uops", 1024};
    Param<size_t> max_executing_uops{"max_executing_uops", "maximum number of scheduled uops", 64};
    Param<int> max_commits_per_cycle{"max_commits_per_cycle", "maximum number of uops we can commit per cycle", 4};
    Param<int> max_loads_per_cycle{"max_loads_per_cycle", "maximum number of loads we can execute per cycle", 2};
    Param<int> max_stores_per_cycle{"max_stores_per_cycle", "maximum number of stores we can execute per cycle", 1};
    Param<int> max_nonmem_per_cycle{
        "max_nonmem_per_cycle", "maximum number of non-memory uops we can execute per cycle", 4};
    Param<Cycle> brain_dead_limit{
        "brain_dead_limit", "indicate brain dead after this number of cycles without a committed Uop", 60000};
    Param<std::uint64_t> heartbeat{"heartbeat", "print heartbeat message every N instructions", 1000000};
    Param<std::int64_t> data_offset{"data_offset", "physical address offset for data accesses", 0};
    Param<bool> dummy_evict{"dummy_evict", "dummy-evict lines from L2 to mirror core's eviction traffic", false};
    Param<uint64_t> dummy_evict_start_addr{
        "dummy_evict_start_addr", "start virtual addr of the addresses to dummy-evict", 281474976858112};
    Param<int> delay{"l2_pipe_delay", "L2 pipeline delay in ns", 5};
    Param<Cycle> dummy_evict_delay{"dummy_evict_delay", "delay before sending out the next dummy evict addr", 1};
    Param<bool> random_dummy_evict_delay{
        "random_dummy_evict_delay", "space out dummy evicts with pseudo-random delays", false};
    Param<int> dist_start{"dist_start", "start of distribution", 1};
    Param<int> dist_end{"dist_end", "end of distribution", 20};
    Param<std::vector<int>> duty_cycle_param{"duty_cycle", "duty cycle as a fraction: [num, den]", "[1, 1]"_json};
    Param<bool> evict_uc_with_evict{
        "evict_uc_with_evict", "evict UC lines with the CHI Evict message (useful for IOD standalone tests)", false};
    Param<bool> read_with_read_not_shared_dirty{
        "read_with_read_not_shared_dirty", "use the ReadNotSharedDirty CHI requests for reads", false};
    Param<bool> l3_evictions_demote_qos{
        "l3_evictions_demote_qos",
        "Requests servicing the eviction of L3 lines should count against the global QoS demote threshold",
        false};
    Param<bool> l3_evictions_throttle{
        "l3_evictions_throttle",
        "Requests servicing the eviction of L3 lines should contribute to the CBusy throttle threshold",
        true};

    Submodule<SAM, CDRN_SAM> sam{"sam"};
    ConfigPort<ControlModIfc, 1, sc_core::SC_ZERO_OR_MORE_BOUND> cm{"cm"};

    Event ev_instruction_commit{"instruction_commit", "An Instruction is committed"};
    Event ev_uop_commit{"uop_commit", "A Uop is committed"};
    Event ev_txn_id_alloc{"txn_id_alloc", "Allocate a Txn ID"};
    Event ev_txn_id_dealloc{"txn_id_dealloc", "Deallocate a Txn ID"};
    Event ev_stash_txn_id_alloc{"stash_txn_id_alloc", "Allocate a Reserved Txn ID"};
    Event ev_stash_txn_id_dealloc{"stash_txn_id_dealloc", "Deallocate a Reserved Txn ID"};
    Event ev_reject{"reject", "A request is rejected (no credits)"};
    Event ev_nonmem_comp{"nonmem_comp", "A non mem uop completed"};
    Event ev_load_exec{"load_exec", "A load uop is being executed"};
    Event ev_store_exec{"store_exec", "A store uop is being executed"};
    Event ev_barrier_comp{"barrier_comp", "A barrier instruction completed"};
    Event ev_writeback{"writeback", "Writeback of dirty data from the L2"};
    EventData<unsigned> evd_outstanding{"evd_outstanding", "number of outstanding requests"};
    Event ev_evict_dummy{"evict_dummy", "dummy L2 evict to mirror core's eviction traffic"};
    Event ev_read_dummy{"read_dummy", "dummy read to enable legal dummy L2 evicts"};
    Event ev_l2c_refill{"l2c_refill", "L2 refill (dataful only)"};
    Event ev_l3c_refill{"l3c_refill", "L3 refill (dataful only)"};
    Event ev_l2c_writeback{"l2c_writeback", "L2 writeback of dirty data"};
    Event ev_hit_l3{"hit_l3", "Cache lookup hits L3"};
    Event ev_l3_evict{"l3_evict", "Eviction from L3"};
    Event ev_l2_evict{"l2_evict", "Eviction from L2"};

    EventData<unsigned> evd_pcrd{"evd_pcrd", "number of requests waiting on pcrd"};
    EventData<unsigned> evd_bytes_loaded{"bytes_loaded", "number of bytes loaded by a uop"};
    EventData<unsigned> evd_bytes_stored{"bytes_stored", "number of bytes stored by a uop"};
    EventData<int> evd_l2_occupancy{"l2_occupancy", "l2 cache occupancy"};
    EventData<int> evd_data_pull{"data_pull", "Response packet stating injectors accepted stash"};

    Submodule<UopSource, UopSourceIS> uop_source{"source"};

    Cycle last_commit_cycle{0};
    std::uint64_t instructions{0};
    bool nothing_ready{true};
    int complete_count{0};

    std::deque<Uop> sched_q;
    std::deque<Uop> exec_q;
    std::deque<Uop> commit_q;
    std::deque<MemReq> request_q;
    std::deque<MemReq> priority_request_q;
    std::deque<MemReq> refill_q;
    std::deque<MemReq> refill_l3_q;
    std::deque<MemReq> writeout_q;
    std::deque<MeshRsp> response_q;
    std::deque<MeshDat> data_q;

    WaitPool<Uop> waiting_uops;
    std::map<LReg, Uop> producers;
    std::multimap<PhysAddr, std::pair<uint64_t, uint64_t>> stores;
    std::set<PhysAddr> pending_reqs;
    std::map<PhysAddr, CacheEntry::State> pending_evictions;

    WaitPool<MemReq> waiting_requests;

    RetryRequester retry{"retry"};

    // emit dummy evicts (to send stash snoops to the core)
    VirtAddr last_evicted_vaddr{VirtAddr(dummy_evict_start_addr)};
    std::deque<VirtAddr> dummy_evict_vaddrs;
    Cycle wait_next_dummy_evict{dummy_evict_delay};
    uint64_t dummy_uop_id{0};
    void emit_dummy_evict(); // control time distribution
    void create_dummy_evict_vaddr();
    std::mt19937 rng;
    std::uniform_int_distribution<int> udist;

    Cache<PhysAddr, CacheEntry> cache{"cache"};
    int l2_occupancy{0};
    Cache<PhysAddr, CacheEntry> l3c{"l3c"};
    Submodule<CBusyBase, CBusy> cbusy{"cbusy"};

    uint16_t num_banks;
    uint64_t bank_hash_mask;

    using RequestQueueBank = std::deque<uint16_t>;
    using RequestQueues = std::vector<RequestQueueBank>;

    RequestQueues m_txn_id_free_list;
    RequestQueues m_stash_txn_id_free_list;

    std::map<Cycle, std::tuple<std::function<void(MemReq req)>, MemReq>> l2_refill_delay_queue;
    std::map<Cycle, std::tuple<std::function<void(MemReq req)>, MemReq>> l3_refill_delay_queue;
    std::map<Cycle, std::tuple<std::function<void(MeshSnp snp)>, MeshSnp>> snp_fwd_delay_queue;
    std::map<Cycle, std::tuple<std::function<void(MeshSnp snp)>, MeshSnp>> snp_unique_delay_queue;
    std::map<Cycle, std::tuple<std::function<void(MeshSnp snp)>, MeshSnp>> snp_once_delay_queue;

    Cycle l2_pipe_delay_in_cycles{0};
    bool pipeline_has_barrier{false};

    struct
    {
        int num{1};
        int den{1};
    } duty_cycle;
    ;

    int duty_cycle_count{0};

    unsigned outstanding_requests{0};
    std::set<uint64_t> throttle_untracked_txns;
    std::set<uint64_t> qos_demote_untracked_txns;

    struct SnpState
    {
        CacheEntry::State cache_state;
        ResponseStatus resp_state;
        ResponseStatus fwd_state{RespComp_I};
    };

    int setup_dependencies(Uop uop);
    void mark_producer(Uop uop, LReg dst);
    void mark_consumer(Uop uop, LReg src);
    void mark_store(Uop uop, PhysAddr address, unsigned size);
    void mark_load(Uop uop, PhysAddr address, unsigned size);
    void unmark_producer(Uop uop, LReg dst);
    void unmark_store(Uop uop, PhysAddr address, unsigned size);
    void complete_uop(Uop uop);
    void remove_completed_uops();
    void mark_uop_ready(Uop& uop);
    void schedule_uop(Uop& uop);
    void wake_barrier_if_complete();

    void do_fetch();
    void prepare_uop(Uop uop);
    void do_execute();
    void exec_load(Uop uop);
    void exec_store(Uop uop);
    void exec_nonmem(Uop uop);
    void do_refill();
    void do_refill_l3();
    void l2_refill(MemReq req);
    void l3_refill(MemReq req);
    void do_commit();
    void do_writeout();
    void do_snp_fwd(MeshSnp snp);
    void do_snp_unique(MeshSnp snp);
    void do_snp_once(MeshSnp snp);
    SnpState compute_snoop_state(MeshSnp snp, bool is_l3, CacheEntry::State current_state);

    bool make_request(Uop uop, chi::RequestOpcode);
    void make_eviction(
        MemReq evictor, PhysAddr address, CacheEntry::State victim, chi::RequestOpcode opcode, bool evict_from_l2);
    void make_response(MemReq req, chi::ResponseOpcode opcode, uint64_t dbid);
    MeshRsp make_response(MeshSnp snp, chi::ResponseOpcode opcode);
    void make_data_response(MeshRsp rsp, chi::DataOpcode opcode, chi::ResponseStatus);
    void make_data_response(MeshSnp req, chi::DataOpcode opcode, chi::ResponseStatus);

    size_t select_bank(PhysAddr addr);

    std::optional<uint16_t> alloc_txn_id(size_t bank = 0, bool stash = false);
    void free_txn_id(uint16_t txn_id);

    void send_requests();
    void send_responses();
    void receive_snoops();
    void receive_responses();
    void receive_responses(InWithCreditOpt<MeshRsp>& port);
    void receive_data();
    void send_data();
    bool send_this_cycle();

    void check_for_braindeath();

    std::tuple<bool, std::optional<Uop>, std::optional<Uop>> split_uop_by_cacheline(Uop uop);
    static CacheEntry::State chi_state_to_cache_state(chi::FinalState state, bool make_dirty);
    static chi::FinalState cache_state_to_chi_state(CacheEntry::State state);
    Uop create_load_uop(ProgramCounter pc, VirtAddr virt_addr);
    std::optional<CacheEntry> fb_lookup(PhysAddr addr);

    int l2c_occupancy();
    int l3c_occupancy();

    friend class soc::Warmup;
};

} // namespace ampere::mesh

template<>
struct fmt::formatter<ampere::mesh::Block> : fmt::ostream_formatter
{};
template<>
struct fmt::formatter<ampere::mesh::MeshInjector::CacheEntry::State> : fmt::ostream_formatter
{};
template<typename T>
struct fmt::formatter<ampere::mesh::WaitPool<T>> : fmt::ostream_formatter
{};

#endif // AMPERE_MESH_MESH_INJECTOR_H
