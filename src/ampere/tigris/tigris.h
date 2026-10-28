//
// Copyright (c) 2023 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//

#pragma once

#include <algorithm>
#include <boost/smart_ptr/local_shared_ptr.hpp>
#include <cstdlib>
#include <iostream>
#include <iterator>
#include <map>
#include <memory>
#include <optional>
#include <ostream>
#include <random>
#include <type_traits>
#include <unordered_map>
#include <utility>

#include "ampere/archx/addr.h"
#include "ampere/archx/barrier_type.h"
#include "ampere/archx/page_size.h"
#include "ampere/archx/sysreg.h"
#include "ampere/archx/translation.h"
#include "ampere/core/backend_prefetcher.h"
#include "ampere/core/fetch.h"
#include "ampere/core/prefetcher.h"
#include "ampere/core/prefetchers/oracle.h"
#include "ampere/sc/addr.h"
#include "ampere/sc/event.h"
#include "ampere/sc/module.h"
#include "ampere/sc/report.h"
#include "ampere/sc/submodule.h"
#include "ampere/tools/is_tools/is_base.h"
#include "ampere/uarch_common/cache.h"
#include "ampere/uarch_common/memop.h"
#include "ampere/uarch_common/memreq.h"

namespace ampere::tigris {

using ampere::core::BackendMemReq;
using ampere::core::Prefetcher;
using ampere::sc::connect;
using ampere::sc::implementation;
using ampere::sc::In;
using ampere::sc::InOpt;
using ampere::sc::Out;
using ampere::sc::OutOpt;
using ampere::sc::Submodule;
using archx::Bitfield;
using archx::Translation;
using uarch_common::MemReq;
using archx::operator""_pa;
using archx::operator""_va;
using archx::PhysAddr;
using archx::ProgramCounter;
using archx::VirtAddr;

using namespace uarch_common::MemOp;

/*
 * BiAddrTranslator is a "magic tlb". It maintains an on the fly mapping between
 * virtual and physical addresses which you update every time a new instruction
 * is streamed. It operates only on line_addresses. If you submit an address
 * that is not a line address, it will be truncated.
 *
 * The physical and virtual addresses 0x0 are special and should never be
 * added to BiAddrTranslator. They are used to signify a case where there is no
 * mapping.
 *
 * The BiAddrTranslator gets complicated in the presence of virtual aliasing or
 * "physical aliasing", i.e. when one virtual address is remapped to a new
 * physical address.
 * Virtual aliasing lookups from virt2phys are no problem. Looking up from
 * phys2virt will only reproduce the latest virtual address, this is not
 * perfect, but generally doesn't change performance much. Physical aliasing
 * lookups from phys2virt for the old phys address will still return the virtual
 * address that it is no longer mapped to. Lookups virt2phys will result in the
 * most recent physical address.
 */
struct BiAddrTranslator
{
    std::unordered_map<VirtAddr, PhysAddr> virt2phys = {};
    std::unordered_map<PhysAddr, VirtAddr> phys2virt = {};
    void add(VirtAddr virt_addr, PhysAddr phys_addr);

    VirtAddr get_virt_addr(PhysAddr phys_addr) const;
    /*
     * pc2va is defined in addr.h. It's just a cast
     */
    VirtAddr get_virt_addr(ProgramCounter pc) const;
    ProgramCounter get_pc(VirtAddr virt_addr) const;
    ProgramCounter get_pc(PhysAddr phys_addr) const;
    PhysAddr get_phys_addr(VirtAddr virt_addr) const;
    PhysAddr get_phys_addr(ProgramCounter pc) const;
    PhysAddr get_phys_addr(PhysAddr phys_addr) const;
    void dump() const;
};

template<typename addr_t, typename value>
class PhysicallyTaggedCache : public uarch_common::Cache<addr_t, value, uint64_t>
{
public:
    explicit PhysicallyTaggedCache(const sc_core::sc_module_name& name,
                                   boost::shared_ptr<BiAddrTranslator> cacheAddrTranslator)
        : uarch_common::Cache<addr_t, value>(name, uarch_common::CacheEntry::default_entry_value<value>()),
          cacheAddrTranslator{cacheAddrTranslator}
    {
    }
    uint64_t compute_tag(addr_t address) const override;

private:
    boost::shared_ptr<BiAddrTranslator> cacheAddrTranslator;
};

inline boost::shared_ptr<BiAddrTranslator> magic_addr_map = std::make_unique<BiAddrTranslator>();

class Tigris : public InstructionStreamHandler<Tigris>
{
public:
    using Base = InstructionStreamHandler<Tigris>;

    constexpr static bool needs_uops() { return false; }
    constexpr static bool needs_sysregs_updates() { return true; }
    constexpr static bool needs_memory_updates() { return true; }

    struct LevelSettings
    {
        bool needs_load_responses = false;
        bool needs_store_responses = false;
        LevelSettings() = default;
    };

    /*
     * TigrisMemReq is a wrapper which maintains a MemReq and a BackendMemReq.
     * These represent the same request/transaction. MemReq is the transaction,
     * and is the default request type used in tigris. BackendMemReq is used to
     * maintain compatibility with prefetchers
     */
    class TigrisMemReq
    {
    public:
        TigrisMemReq(const MemReq& memreq, const BackendMemReq& backend_req)
            : backend_req_(backend_req), memreq_(memreq)
        {
        }

        // These two constructors are only used for prefetches. some params
        // are hardcoded appropriately
        explicit TigrisMemReq(const BackendMemReq& backend_req)
            : backend_req_(backend_req),
              memreq_(new core::MemReqObj(backend_req.transaction->txid().value,
                                          backend_req.phys_addr.value(),
                                          Requester::LSU, // If you are
                                                          // instantiating from a backend req, it is assumed
                                                          // that this came from LSU
                                          READ,
                                          8,     // size
                                          false, // nontemporal
                                          backend_req.l2_prefetch_type,
                                          BarrierType::BAR_NONE,
                                          ATOMIC_NONE,
                                          SNOOP_NONE,
                                          archx::PageSize::Size::INVALID,
                                          0, // bytemask
                                          backend_req.virt_addr,
                                          backend_req.pc))
        {
            // pcp doesn't initialize phys_addr, so we do it here if
            // not defined
            if (backend_req_.phys_addr == 0_pa) {
                backend_req_.phys_addr = magic_addr_map->get_phys_addr(backend_req.virt_addr);
                memreq_->phys_addr = magic_addr_map->get_phys_addr(backend_req.virt_addr);
            }
        }

        explicit TigrisMemReq(const MemReq& memreq) : backend_req_(memreq), memreq_(memreq) {}

        const BackendMemReq& getBackendMemReq() const { return backend_req_; }
        const MemReq& getMemReq() const { return memreq_; }

    private:
        BackendMemReq backend_req_;
        MemReq memreq_;
    };

    using TMemReq = boost::local_shared_ptr<TigrisMemReq>;
    using MemoryRequest = std::pair<TMemReq, bool>;

    template<typename upper_addr_t, typename prefetcher_t>
    struct CacheLevel : sc::Module
    {
        using addr_t = typename prefetcher_t::addr_t;
        using prefetch_req_t = typename prefetcher_t::request_t;
        using prefetch_t = typename prefetcher_t::prefetch_type_t;

        // The addr_t and upper_addr_t must be of these types. Otherwise you
        // need to override translation functions for the new type
        static_assert(std::is_base_of<VirtAddr, addr_t>::value || std::is_base_of<PhysAddr, addr_t>::value
                          || std::is_base_of<ProgramCounter, addr_t>::value,
                      "CacheLevel template parameter, 'addr_t', must be of type VirtAddr or PhysAddr or PC");
        static_assert(
            std::is_base_of<VirtAddr, upper_addr_t>::value || std::is_base_of<PhysAddr, upper_addr_t>::value,
            "CacheLevel template parameter, 'upper_addr_t', template param must be of type VirtAddr or PhysAddr");

        Submodule<prefetcher_t> prefetcher{"prefetcher"};
        In<prefetch_req_t> prefetch_request{"prefetch_request"};

        constexpr static prefetch_t get_none_prefetch_t()
        {
            if constexpr (std::is_same<prefetch_t, ampere::core::PrefetchType>::value) {
                return ampere::core::PrefetchType::NONE;
            } else if constexpr (std::is_same<prefetch_t, PrefetchType>::value) {
                return ampere::uarch_common::MemOp::PrefetchType::NONE;
            }
        }

        struct Line
        {
            addr_t addr;

            std::optional<prefetch_t> prefetch_type = get_none_prefetch_t();
            bool used = false;
            bool warmup = false;

            Line() = default;
            Line(addr_t addr, prefetch_t prefetch_type, bool used = false, bool warmup = false)
                : addr(addr), used(used), warmup(warmup)
            {
                if (!used && !warmup)
                    this->prefetch_type = prefetch_type;
            }
        };

        struct Outstanding
        {
            TMemReq req;
            prefetch_t prefetch_type = get_none_prefetch_t();
            bool opportunisitic = false;
            unsigned needed = 0U;
            bool prefetch_hit = false;
            std::list<TMemReq> responses;
        };

        std::unique_ptr<uarch_common::Cache<addr_t, Line>> cache = nullptr;
        std::unordered_map<upper_addr_t, Outstanding> outstanding_requests;
        std::unordered_set<addr_t> unused_eviction;

        bool should_train(bool hit, const TMemReq& req) const;

        /*
         * Functions for translating between physical and virtual addresses, and
         * for selecting the appropriate fields from MemReq
         */
        template<typename addr_to_extract_t>
        addr_to_extract_t extract_addr(const TMemReq& req) const;
        const prefetch_req_t& extract_prefetch_req(const TMemReq& req) const;
        const prefetch_t extract_prefetch_type(const TMemReq& req) const;
        template<typename from_addr_t, typename to_addr_t>
        to_addr_t translate_addr(from_addr_t addr);

        struct Lower : LevelSettings
        {
            std::string request_name;
            In<MemoryRequest> request;
            std::string response_name;
            Out<MemoryRequest> response;
            std::unique_ptr<Out<addr_t>> evict_request;
            Lower(const std::string& level, LevelSettings settings)
                : LevelSettings(settings),
                  request_name(level + "_request"),
                  request(request_name.c_str()),
                  response_name(level + "_response"),
                  response(response_name.c_str())
            {
            }
        };

        std::unordered_map<Requester, std::unique_ptr<Lower>> lower;

        Out<MemoryRequest> fill_request{"fill_request"};
        In<MemoryRequest> fill_response{"fill_response"};

        std::unique_ptr<In<upper_addr_t>> upper_evict_request;
        std::list<TMemReq> remaining_responses;

        bool aligned = false;
        Requester request_type;
        sc::Param<bool> warmup{"warmup", "Cache participates in memory warmup (currently only enabled for l2)", false};
        sc::Param<bool> fill_loads{"fill_loads", "Fill loads", true};
        sc::Param<bool> fill_stores{"fill_stores", "Fill stores", true};
        sc::Param<bool> write_through{"write_through", "Write-through", false};
        sc::Param<bool> train_on_loads{"train_on_loads", "Train prefetcher on loads", true};
        sc::Param<bool> train_on_stores{"train_on_stores", "Train prefetcher on stores", false};
        sc::Param<bool> train_on_misses_only{"train_on_misses_only", "Train prefetcher cache misses only", false};
        sc::Param<bool> train_on_non_temporal{"train_on_non_temporal", "Train prefetcher on non-temporal", false};
        sc::Param<bool> drop_prefetches_on_fill_resources{
            "drop_prefetches_on_fill_resources", "Drop prefetch if no fill resources", false};
        sc::Param<unsigned> incoming_per_cycle{"incoming_per_cycle", "Incoming requests per cycle", 1};
        sc::Param<unsigned> fill_latency{"fill_latency", "Fill latency", 1};
        sc::Param<unsigned> request_latency{"request_latency", "Request latency", 1};
        sc::Param<unsigned> outstanding{"outstanding", "Amount of outstanding requests", 1};

        struct
        {
            sc::Event prefetch_already_present{"prefetch_already_present",
                                               "Prefetched line is already present in cache"};
            sc::Event prefetch_triggered_late{"prefetch_triggered_late", "Prefetch trigger occurred after a demand"};
            sc::Event prefetch_triggered_again{"prefetch_triggered_again",
                                               "Prefetch trigger occurred for already prefetched line"};
            sc::Event prefetch_upgrade{"prefetch_upgrade", "Prefetch line hit by demand while in-flight"};
            sc::Event prefetch_useful{"prefetch_useful", "Prefetch line was used at least once"};
            sc::Event evicted_unused{"evicted_unused", "Unused line evicted"};
            sc::Event unused_brought_back{"unused_brought_back", "Unused line was brought back in"};

            sc::Event load_access{"load_access", "Load access"};
            sc::Event store_access{"store_access", "Store access"};
            sc::Event fill_buffer_hit{"fill_buffer_hit", "Access hit outstanding request"};
            sc::EventData<bool> access_hit{"hit", "Access hit"};

            sc::Event refill_prefetch{"refill_prefetch", "Fill prefetch"};
            sc::Event refill_load{"refill_load", "Fill load"};
            sc::Event refill_store{"refill_store", "Fill store"};

        } event;

        /*
         * By default the CacheLevel is tagged with the same address type as it
         * is indexed.
         * If you specify physically_tagged then addresses are translated to
         * physical before computing the tag
         */
        CacheLevel(const sc_core::sc_module_name& name,
                   Requester type,
                   bool physically_tagged = false,
                   bool aligned = false);

        void setup(const Tigris& tigris);

        void connect_source(const std::string& name,
                            OutOpt<MemoryRequest>& requests,
                            InOpt<MemoryRequest>& responses,
                            LevelSettings settings);

        /*
         * This is a method of "this" CacheLevel which takes the upper cache as
         * an input. It constructs the level=upper.lower and connects level's
         * ports to this
         */
        template<typename upper_level_upper_addr_t, typename upper_level_prefetcher_t>
        void connect_upper(CacheLevel<upper_level_upper_addr_t, upper_level_prefetcher_t>& upper_level, Requester type);

        void connect_memory();

        bool can_put_outstanding() const;

        bool can_request(const TMemReq& req = nullptr) const;

        unsigned max_request_parts() const { return aligned ? 1 : 2; }
        bool empty() const { return outstanding_requests.empty(); }

        void preload(addr_t addr, unsigned int size, bool write, bool code);

        std::tuple<uarch_common::CacheResult<Line>, bool, bool> lookup_and_request(PhysAddr phys_addr,
                                                                                   VirtAddr virt_addr,
                                                                                   const TMemReq& req,
                                                                                   Lower* requester = nullptr);

        bool can_accept(Requester type = Requester::ANY) const;

        void handle_request(Lower& requester, const TMemReq& req, bool split);

        void handle_prefetch_requests();

        void handle_requests();

        void evict(const Line& victim, const TMemReq& req);

        bool can_evict() const;

        void handle_invalidates();

        void handle_responses();

        bool can_fill() const;

        bool should_fill(const TMemReq& req) const;

        bool needs_response(Lower& requester, const TMemReq& req) const;

        void handle_fills();

        bool run();

    protected:
    };

    uint64_t memory_request_id = 0;
    std::deque<boost::local_shared_ptr<archx::Instruction>> code_requests;
    std::deque<std::tuple<boost::local_shared_ptr<archx::Instruction>, unsigned, bool>> requests;
    std::map<uint64_t, std::pair<TMemReq, bool>> waiting_requests;

    struct
    {
        sc::Event load_execute{"load_execute", "Load executed"};
        sc::Event store_execute{"store_execute", "Store executed"};
        sc::Event code_request{"code_request", "Code request"};
        sc::Event memory_request{"memory_request", "Memory request"};
        sc::Event memory_request_reordered{"memory_request_reordered", "Memory request was reordered"};
        sc::EventData<int> reorder_distance{"reorder_distance", "Reorder distance"};

    } event;

    sc::Param<float> reorder_probability;
    sc::Param<float> reorder_amount;
    sc::Param<unsigned> requests_per_cycle;
    sc::Param<unsigned> code_requests_per_cycle;
    sc::Param<unsigned> request_depth;
    sc::Param<unsigned> request_granularity;
    sc::Param<unsigned> commit_depth;
    sc::Param<bool> wait_for_code_response;
    sc::Param<bool> wait_for_load_response;
    sc::Param<bool> wait_for_store_response;
    sc::Param<uint64_t> braindead_threshold;
    sc::Param<bool> magic_tlb_dump_at_end{
        "magic_tlb_dump_at_end", "Dump the magic tlb translation map at end of simulation", false};

    size_t reordered_until = 0;
    std::mt19937 rp;
    std::mt19937 rd;
    std::uniform_real_distribution<float> probabilty;
    std::geometric_distribution<int> distance;
    uint64_t last_activity_cycle = 0;

    OutOpt<MemoryRequest> memory_op_request{"memory_op_request"};
    InOpt<MemoryRequest> memory_op_response{"memory_op_response"};

    OutOpt<MemoryRequest> code_memory_request{"code_memory_request"};
    InOpt<MemoryRequest> code_memory_response{"code_memory_response"};

    struct MemoryModel : sc::Module
    {
        explicit MemoryModel(const sc_core::sc_module_name& name) : sc::Module(name) {}

        virtual bool accepts_code() const { return false; }
        virtual bool can_accept_code() const { return true; }
        virtual void connect_code(OutOpt<MemoryRequest>& requests,
                                  InOpt<MemoryRequest>& responses,
                                  LevelSettings settings)
        {
        }

        virtual bool accepts_data() const { return false; }
        virtual bool can_accept_data() const { return true; }
        virtual void connect_data(OutOpt<MemoryRequest>& requests,
                                  InOpt<MemoryRequest>& responses,
                                  LevelSettings setting)
        {
        }

        virtual void setup(const Tigris& tigris) {}

        virtual bool has_work() const { return false; }
        virtual void preload(PhysAddr addr, unsigned int size, bool write, bool code) {}
    };

    struct CodeOnlyCache : virtual MemoryModel, virtual implementation<CodeOnlyCache, MemoryModel>
    {
        // TODO Eventually this needs to be updated to use Prefetcher<Fetch>
        CacheLevel<PhysAddr, Prefetcher<MemReq>> l1i;

        explicit CodeOnlyCache(const sc_core::sc_module_name& name, bool memory_connected = true)
            : MemoryModel(name), l1i("l1i", Requester::ICF)
        {
            if (memory_connected)
                l1i.connect_memory();
        }

        void setup(const Tigris& tigris) override { l1i.setup(tigris); }
        bool accepts_code() const override { return true; }
        bool can_accept_code() const override { return l1i.can_accept(); }
        void connect_code(OutOpt<MemoryRequest>& requests,
                          InOpt<MemoryRequest>& responses,
                          LevelSettings settings) override
        {
            l1i.connect_source("icf", requests, responses, settings);
        }
    };

    struct DataOnlyCache : virtual MemoryModel, virtual implementation<DataOnlyCache, MemoryModel>
    {
        CacheLevel<PhysAddr, core::BackendPrefetcher<BackendMemReq>> l1d;

        explicit DataOnlyCache(const sc_core::sc_module_name& name, bool memory_connected = true)
            : MemoryModel(name), l1d("l1d", Requester::LSU, true)
        {
            if (memory_connected)
                l1d.connect_memory();
        }

        void setup(const Tigris& tigris) override { l1d.setup(tigris); }
        bool accepts_data() const override { return true; }
        bool can_accept_data() const override { return l1d.can_accept(); }
        void connect_data(OutOpt<MemoryRequest>& requests,
                          InOpt<MemoryRequest>& responses,
                          LevelSettings settings) override
        {
            l1d.connect_source("lsu", requests, responses, settings);
        }

        bool has_work() const override { return !l1d.empty(); }
        // TODO used for when we add memory warmup back
        // void preload(PhysAddr addr, unsigned int size, bool write, bool code) override
        //{
        //     l1d.preload(addr, size, write, code);
        // }
    };

    struct SingleLevelCache : CodeOnlyCache, DataOnlyCache, implementation<SingleLevelCache, MemoryModel>
    {
        explicit SingleLevelCache(const sc_core::sc_module_name& name)
            : MemoryModel(name), CodeOnlyCache(name), DataOnlyCache(name)
        {
        }
        void setup(const Tigris& tigris) override
        {
            CodeOnlyCache::setup(tigris);
            DataOnlyCache::setup(tigris);
        }
    };

    struct MultiLevelCache : CodeOnlyCache, DataOnlyCache, implementation<MultiLevelCache, MemoryModel>
    {
        CacheLevel<PhysAddr, Prefetcher<MemReq>> l2;

        explicit MultiLevelCache(const sc_core::sc_module_name& name)
            : MemoryModel(name),
              CodeOnlyCache(name, false),
              DataOnlyCache(name, false),
              l2("l2", Requester::L2C, false, true)
        {
            l1i.connect_upper<PhysAddr, Prefetcher<MemReq>>(l2, Requester::ICF);
            l1d.connect_upper<PhysAddr, Prefetcher<MemReq>>(l2, Requester::LSU);
            l2.connect_memory();
        }

        void setup(const Tigris& tigris) override
        {
            CodeOnlyCache::setup(tigris);
            DataOnlyCache::setup(tigris);
            l2.setup(tigris);
        }
        bool has_work() const override { return DataOnlyCache::has_work() || !l2.empty(); }
        void preload(PhysAddr addr, unsigned int size, bool write, bool code) override
        {
            l2.preload(addr, size, write, code);
        }
    };

    Submodule<MemoryModel, MultiLevelCache> memory_model;

    bool require_load_response() const { return commit_depth > 0 && wait_for_load_response; }
    bool require_store_response() const { return commit_depth > 0 && wait_for_store_response; }
    bool require_any_response() const { return require_load_response() || require_store_response(); }

    Tigris(const sc_core::sc_module_name& name,
           sc::Clock* clk,
           sc::Environment* env,
           const InstructionStreamRunnerConfig& config)
        : Base{name, clk, env, config},
          reorder_probability{*env, "reorder_probability", "Reorder probability", 0},
          reorder_amount{*env, "reorder_amount", "Distance of reordering between requests", 0},
          requests_per_cycle{*env, "requests_per_cycle", "Memory requests per cycle", 1},
          code_requests_per_cycle{*env, "code_requests_per_cycle", "Code memory requests per cycle", 1},
          request_depth{*env, "request_depth", "Depth of requests", 0},
          request_granularity{*env, "request_granularity", "Request size granularity (bytes)", 16},
          commit_depth{*env, "commit_depth", "Depth of commit", 0},
          wait_for_code_response{*env, "wait_for_code_response", "Need code response to proceed", false},
          wait_for_load_response{*env, "wait_for_load_response", "Need load response for commit", true},
          wait_for_store_response{*env, "wait_for_store_response", "Need store response for commit", true},
          braindead_threshold{*env, "braindead_threshold", "Cycle threshold for braindead", 100000},
          rp(0),
          rd(0),
          probabilty(0, 1.0),
          distance(1.0 / reorder_amount),
          memory_model("memory")
    {
        AMPERE_ASSERT(!wait_for_code_response, "Unsupported");
        memory_model->setup(*this);
        if (memory_model->accepts_code())
            memory_model->connect_code(code_memory_request, code_memory_response, {});
        if (memory_model->accepts_data()) {
            LevelSettings settings;
            settings.needs_load_responses = require_load_response();
            settings.needs_store_responses = require_store_response();
            memory_model->connect_data(memory_op_request, memory_op_response, settings);
        }
    }

    void reorder_requests();

    void handle_code_requests();
    void handle_requests();
    void handle_code_responses();
    void handle_responses();

    bool run() override;

    static BarrierType barrier_type(archx::Barrier_Type type);
    static core::MemOp::Type memop_type(const archx::Instruction& instruction);
    static bool supported_memory_op(const archx::Instruction& instruction);

    uint32_t memop_size(const archx::Instruction& instruction, unsigned index) const;

    TMemReq handle_code(const archx::Instruction& instruction);
    MemoryRequest handle_memory_op(const archx::Instruction& instruction, unsigned index, bool split_hi_half);

    bool can_process() const { return !request_depth || requests.size() < request_depth; }
    bool is_complete() const { return requests.empty() && !memory_model->has_work(); }

    void warmup_memory(PhysAddr addr, unsigned int size, bool write, bool code);

    unsigned scatter_gather_parts(const archx::Instruction& instruction) const;
    bool should_execute_memop(const archx::Instruction& instruction, unsigned int index) const;
    void handle_instruction(const boost::local_shared_ptr<archx::Instruction>& instruction);
    void end_of_simulation() override;
};

} // namespace ampere::tigris

