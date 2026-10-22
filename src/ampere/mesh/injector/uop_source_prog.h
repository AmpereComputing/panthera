//
// Copyright (c) 2021 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//

#ifndef AMPERE_MESH_INJECTOR_UOP_SOURCE_PROG_H
#define AMPERE_MESH_INJECTOR_UOP_SOURCE_PROG_H

#include <random>
#include <variant>

#include "ampere/mesh/injector/uop_source.h"
#include "ampere/mesh/pager.h"
#include "ampere/sc/sc.h"
#include "ampere/memory_stream/memory_stream.h"

namespace ampere::mesh {

using namespace archx;
using namespace sc;

struct GeneratorIfc
{
    virtual ~GeneratorIfc() = default;
    virtual PhysAddr get_addr() = 0;
    virtual void advance(uint64_t tlp_size) = 0;
    virtual void advance() = 0;
    virtual void reset() = 0;

    void set_region(const std::string& reg) { region = reg; }
    const std::string& get_region() const { return region; }

private:
    std::string region;
};

class StridedGenerator : public GeneratorIfc
{
public:
    StridedGenerator(PhysAddr start, uint64_t stride) : StridedGenerator{start, 0_pa, stride} {}
    StridedGenerator(PhysAddr start, PhysAddr stop, uint64_t stride)
        : start{start}, stop{stop}, current_addr{start}, stride{stride}
    {
    }

    PhysAddr get_addr() override { return current_addr; }
    void advance(uint64_t tlp_size) override
    {
        current_addr += tlp_size * stride;
        if (stop.value() && current_addr >= stop)
            current_addr = start + (current_addr - stop);
    }
    void advance() override
    {
        current_addr += stride;
        if (stop.value() && current_addr >= stop)
            current_addr = start + (current_addr - stop);
    }
    void reset() override { current_addr = start; }

private:
    PhysAddr start;
    PhysAddr stop;
    PhysAddr current_addr;

    uint64_t stride;
};

class ListGenerator : public GeneratorIfc
{
public:
    ListGenerator(std::vector<PhysAddr> address_list) : address_list{address_list} {}

    PhysAddr get_addr() override { return address_list.at(addr_ptr); }
    void advance(uint64_t tlp_size) override { return; }
    void advance() override { addr_ptr = ((addr_ptr + 1) >= address_list.size()) ? 0 : (addr_ptr + 1); }
    void reset() override { addr_ptr = 0; }

private:
    uint64_t addr_ptr{0};
    std::vector<PhysAddr> address_list;
};

template<class Distribution, class Engine = std::mt19937_64>
class RandomGenerator : public GeneratorIfc
{
public:
    RandomGenerator(PhysAddr start, PhysAddr stop, uint64_t mask = 0, uint64_t seed = 0)
        : mask{mask}, seed{seed}, distribution{start.value(), stop.value()}
    {
        reset();
    }

    PhysAddr get_addr() override { return current_addr; }
    void advance(uint64_t tlp_size) override { current_addr = PhysAddr{distribution(engine) & ~mask}; }
    void advance() override { current_addr = PhysAddr{distribution(engine) & ~mask}; }

    void reset() override
    {
        if (seed)
            engine.seed(seed);
        else
            engine.seed();
        advance();
    }

private:
    PhysAddr current_addr;
    uint64_t mask;
    uint64_t seed;

    Distribution distribution;
    Engine engine;
};

using UniformRand = RandomGenerator<std::uniform_int_distribution<uint64_t>>;

class LatMemReadGenerator : public GeneratorIfc
{
public:
    LatMemReadGenerator(PhysAddr start, PhysAddr stop, int granule = 8, uint64_t seed = 0)
        : start{start}, stop{stop}, granule{granule}, seed{seed}
    {
        reset();
    }

    PhysAddr get_addr() override { return current_addr; }

    void advance() override
    {
        size_t index = (current_addr - start) / granule;
        current_addr = arena.at(index);
    }

    void advance(uint64_t tlp_size) override { advance(); }

    void reset() override
    {
        gen_chase();
        current_addr = arena.at(0);
    }

private:
    PhysAddr start;
    PhysAddr stop;
    PhysAddr current_addr;
    int granule;
    uint64_t seed;

    std::vector<PhysAddr> arena;

    void gen_chase();
    std::vector<uint32_t> gen_random_permutation(size_t nr) const;
};

class UopSourceProg : public UopSource, poly<UopSourceProg>
{
public:
    explicit UopSourceProg(const sc_core::sc_module_name& name);
    Param<json> program{"program", "program object", {}};
    Param<std::vector<json>> warmup{"warmup", "warm up table", "[]"_json};
    Param<std::vector<std::string>> decoders{
        "decoders", "list of decoder names, in the order they should be checked", "[]"_json};
    Param<std::vector<json>> parameters{"parameters", "dynamic parameters for the program", "[]"_json};
    Param<bool> enable_warmup{"enable_warmup", "enable warming of memory regions", false};
    Param<bool> warmup_only{"warmup_only", "enable only warmup", false};
    Param<std::string> warmup_ms{"warmup_ms", "name of the memory stream file used for (warmstart)", ""};
    Param<bool> enable_events_on_start{"enable_events_on_start",
                                       "turn on events at program start (set this to false if using time base warmup)",
                                       false};

    std::optional<Uop> fetch_uop() override;
    std::optional<IOop> fetch_io() override;
    std::pair<chi::IOType, uint64_t> peek_io() override;
    std::optional<IOop> fetch_read_io() override;
    std::optional<IOop> fetch_write_io() override;
    void commit_uop(Uop) override;
    uint64_t take_next_id() override { return ++uop_id; }
    uint64_t get_ping_pong_mask() override { return ping_pong; };

protected:
    ConfigPort<PagingIfc, 1, sc_core::SC_ZERO_OR_MORE_BOUND> paging{"paging"};

    void end_of_elaboration() override;
    void start_of_simulation() override;

private:
    Event ev_loop{"loop", "starting the next loop iteration"};

    struct Insn
    {
        ProgramCounter pc;
        std::variant<uint32_t, chi::IOType> opcode;
        PhysAddr pa;
        GeneratorIfc* generator;
        bool advance;
        uint64_t repeat_count{1};
        uint64_t tlp_size{0};
        bool barrier;
    };

    struct WarmupEntry
    {
        WarmupEntry(PhysAddr start_addr, PhysAddr stop_addr, std::vector<soc::WarmupInfo::Action> actions)
            : start_addr(start_addr), stop_addr(stop_addr), actions(actions)
        {
        }

        PhysAddr start_addr;
        PhysAddr stop_addr;
        std::vector<soc::WarmupInfo::Action> actions;

        void set_region(const std::string& reg) { region = reg; }
        const std::string& get_region() const { return region; }

    private:
        std::string region;
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

    std::map<std::string, std::vector<Insn>> block;
    size_t block_index{0};
    size_t read_block_idx{0};
    size_t write_block_idx{0};

    std::vector<WarmupEntry> warmup_table;
    size_t warmup_index{0};
    size_t action_index{0};

    WarmupMs ms_warmup{warmup_ms.val()};

    uint64_t uop_id{0};
    uint64_t last_uop_id{std::numeric_limits<uint64_t>::max()};
    uint64_t instructions{0};
    uint64_t loop_count{1};
    int64_t repeat_count{1};
    bool stopped{false};
    uint64_t ping_pong{0};

    std::shared_ptr<archx::Instruction> archx_instruction;

    UopSet uop_set;
    size_t uop_index{0};

    std::map<std::string, std::unique_ptr<GeneratorIfc>> generators;

    std::map<std::string, Param<json>> dynamic_parameters;

    PhysAddr get_address(const Insn& insn);

    void compile();
    void do_warmup();
    void set_address(Insn& compiled_insn, const json& insn);
    void set_repeat(Insn& compiled_insn, const json& insn);
    void set_barrier(Insn& compiled_insn, const json& insn);
    void set_tlp_size(Insn& compiled_insn, const json& insn);
    void setup_generators(const json& generator_list);
    void setup_parameters();
    std::optional<soc::WarmupInfo> gen_warmup();
    std::optional<soc::WarmupInfo> gen_warmup_table();
    std::optional<soc::WarmupInfo> gen_warmup_ms();
    uint64_t get_uint64(const json& jval);
    std::string get_str(const json& jval);
    bool get_bool(const json& jval);
    std::vector<PhysAddr> get_addr_list(const json& jval);
    uint64_t maybe_get_uint64(const json& jval, const char* field, uint64_t default_value);
    bool maybe_get_bool(const json& jval, const char* field);
    void set_ior_node_id();
};

} // namespace ampere::mesh

#endif // AMPERE_MESH_INJECTOR_UOP_SOURCE_PROG_H
