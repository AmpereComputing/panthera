//
// Copyright (c) 2021-2022 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//

// Abstract tool for trying to measure effect of caching uops (emitted
// by decode).  Includes a loop-buffer and icache. The central concept
// is a a fetch-chunk (a group of ARM instructions).

#include <boost/filesystem/path.hpp>
#include <nlohmann/json.hpp>

#include <iostream>
#include <iomanip>
#include <cassert>
#include <deque>
#include <optional>
#include <algorithm>
#include <utility>
#include <sstream>
#include <cmath>
#include <cstdlib>
#include <map>
#include <tuple>

#include "ampere/archx/instruction_is.h"
#include "ampere/archx/furiosa.h"
#include "ampere/archx/uop.h"
#include "ampere/archx/uop_info.h"
#include "ampere/instruction_stream/instruction_stream.h"
#include "ampere/util/bits.h"

#include <chrono>

typedef uint_fast32_t uint_t;

using hires_clock = std::chrono::high_resolution_clock;
using duration_ms = std::chrono::duration<double, std::milli>;
using json = nlohmann::json;
namespace fs = boost::filesystem;
using namespace ampere;

static const char nl = '\n';

static const uint32_t MAX_INSTR_PER_ENTRY = 16;
static const uint32_t MAX_UOPS_PER_INSTRUCTION = 16;
static const uint32_t MAX_UOPS_PER_ENTRY = MAX_UOPS_PER_INSTRUCTION * MAX_INSTR_PER_ENTRY;

static double mround(double d, int n)
{
    const double multiplier = std::pow(10.0, n);
    return std::round(d * multiplier) / multiplier;
}

static double kround(double d)
{
    return mround(d, 4);
}

struct stats_t
{
    uint64_t instructions{0};
    uint64_t icache_hits{0};
    uint64_t icache_misses{0};
    uint64_t icache_repeat_accesses{0};
    uint64_t fetch_chunks{0};
    uint64_t xop_cache_hit{0};
    uint64_t xop_cache_miss{0};
    uint64_t looping_in_fetch_chunk{0};
    uint64_t uops_sourced_by_xcache{0};
    uint64_t uops_sourced_by_decode{0};
    uint64_t uops_sourced_by_loopbuf{0};
    uint64_t fused{0};
    uint64_t exceeded_uops_per_entry_limit{0}; // overall
    uint64_t uops_sourced_by_decode_too_many_uops_for_fetch_chunk{0};
    uint64_t uops_per_entry[MAX_UOPS_PER_ENTRY] = {0};
    std::chrono::time_point<std::chrono::high_resolution_clock> t1{};
    std::string heatmap{};
    std::string heatmap_normalized{};
    std::string set_stats{};

    uint64_t loop_buffer_inserts{};
    uint64_t loop_buffer_hits{};
    uint64_t loop_buffer_misses{};
};

struct loop_buffer_config_t
{
    uint32_t entries;
    loop_buffer_config_t(uint32_t entries) : entries(entries) {}
};

struct loop_buffer_entry_t
{
    uint_t nuops;
};

class loop_buffer_t
{
    const loop_buffer_config_t config;
    loop_buffer_entry_t* loopbuf;
    uint64_t* tag;

public:
    loop_buffer_t(const loop_buffer_config_t& config) : config(config)
    {
        loopbuf = new loop_buffer_entry_t[config.entries];
        tag = new uint64_t[config.entries];
    }
    ~loop_buffer_t()
    {
        delete[] loopbuf;
        delete[] tag;
    }

    uint_t get_entry(uint64_t pc) const { return (pc >> 2) % config.entries; }

    void insert(uint64_t pc, uint_t nuops)
    { // loopbuf
        auto ei = get_entry(pc);
        tag[ei] = pc;
        loopbuf[ei].nuops = nuops;
    }

    bool query(uint64_t pc, uint32_t& nuops) const
    { // loopbuf
        auto ei = get_entry(pc);
        if (tag[ei] == pc) {
            nuops = loopbuf[ei].nuops;
            return true;
        }
        nuops = 0;
        return false;
    }
    bool query(uint64_t pc) const
    { // loopbuf
        auto ei = get_entry(pc);
        return tag[ei] == pc;
    }
};

static void print_addr(uint64_t va, std::ostream& o = std::cerr)
{
    o << std::hex << std::setfill('0') << std::setw(16);
    o << va;
    o << std::dec << std::setfill(' ');
}

typedef uint32_t heatmap_ctr_t;

struct icache_entry_t
{
    uint32_t valid{};
    uint64_t line_addr{};
    uint64_t timestamp{}; // for replacement decisions
};

struct xop_cache_entry_t
{
    uint32_t valid{};
    uint32_t num_inst{}; // # of arm instr
    uint32_t num_uops{}; // # of machine uops
    uint64_t pc_start{};
    uint64_t timestamp{}; // for replacement decisions

    heatmap_ctr_t heatmap[MAX_UOPS_PER_ENTRY] = {};
    uint64_t insertions{};
    uint64_t hits{};

    uint64_t total_heat() const
    {
        uint64_t total_heat{0};
        for (uint32_t i = 0; i < MAX_UOPS_PER_ENTRY; ++i) {
            total_heat += heatmap[i];
        }
        return total_heat;
    }
    heatmap_ctr_t max_heat() const
    {
        heatmap_ctr_t max_heat{0};
        for (uint32_t i = 0; i < MAX_UOPS_PER_ENTRY; ++i) {
            max_heat = std::max(max_heat, heatmap[i]);
        }
        return max_heat;
    }
    void use(uint32_t used_uops)
    {
        assert(used_uops < MAX_UOPS_PER_ENTRY);
        for (uint32_t i = 0; i < used_uops; ++i) {
            heatmap[i]++;
        }
    }
    void dump_stat(std::ostream& o) const
    {
        o << "Insert: " << std::setw(9) << insertions << "   Hits: " << std::setw(9) << hits << "   H/I: " << std::fixed
          << std::setprecision(3) << std::setw(9) << kround(1.0 * hits / insertions);
    }
    void dump(std::ostream& o, uint32_t max_uops) const
    {
        for (uint32_t i = 0; i < max_uops; i++) {
            o << std::setw(8) << heatmap[i] << ' ';
        }
    }

    void dump_normalized(std::ostream& o, uint64_t n, uint32_t max_uops) const
    {
        o << std::fixed << std::setprecision(3);
        for (uint32_t i = 0; i < max_uops; i++) {
            o << std::setw(8) << 1.0 * heatmap[i] / n << ' ';
        }
    }
};

struct icache_config_t
{
    uint32_t sets;
    uint32_t ways;
    uint32_t bytes_per_line;
    uint32_t verbose;
    icache_config_t(uint32_t sets = 64, uint32_t ways = 4, uint32_t bytes_per_line = 64)
        : sets(sets), ways(ways), bytes_per_line(bytes_per_line), verbose(0)
    {
        assert(sets > 0);
        assert(ways > 0);
        assert(bytes_per_line > 0);
    }
};

struct xop_cache_config_t
{
    uint32_t sets;
    uint32_t ways;
    uint32_t fetch_chunk_bytes;
    uint32_t verbose;
    uint32_t max_instr_per_entry;
    uint32_t max_uops_per_entry;
    bool emit_heatmap;
    xop_cache_config_t(uint32_t sets = 64,
                       uint32_t ways = 4,
                       uint32_t fetch_chunk_bytes = 32,
                       uint32_t max_instr_per_entry = 1000,
                       uint32_t max_uops_per_entry = 1000,
                       uint32_t emit_heatmap = false)
        : sets(sets),
          ways(ways),
          fetch_chunk_bytes(fetch_chunk_bytes),
          verbose(0),
          max_instr_per_entry(max_instr_per_entry),
          max_uops_per_entry(max_uops_per_entry),
          emit_heatmap(emit_heatmap)
    {
        assert(sets > 0);
        assert(ways > 0);
        assert(max_instr_per_entry > 0);
        assert(max_uops_per_entry > 0);
    }
};

std::ostream& operator<<(std::ostream& o, const xop_cache_config_t& x)
{
    o << "sets " << x.sets << nl;
    o << "ways " << x.ways << nl;
    o << "fetch_chunk_bytes   " << x.fetch_chunk_bytes << nl;
    o << "max instr per entry " << x.max_instr_per_entry << nl;
    o << "max uops per entry  " << x.max_uops_per_entry << nl;
    return o;
}

static uint64_t ilog2(uint64_t x)
{
    assert(x >= 1);
    return util::log2(x);
}

class icache_t
{
    icache_config_t config;
    uint64_t limit; // sets * ways
    uint64_t line_shift;
    uint64_t refs; // for lru/mru
    icache_entry_t* cache;

    // FIXME: stats
public:
    icache_t(const icache_config_t& config)
        : config(config),
          limit(config.sets * config.ways),
          line_shift(ilog2(config.bytes_per_line)),
          refs(0),
          cache(new icache_entry_t[limit])
    {
    }
    ~icache_t() { delete[] cache; }

    uint64_t line_addr(uint64_t va) const { return va >> line_shift; }

private:
    uint32_t find_lru(uint32_t set) const
    {
        icache_entry_t* row = cache + (set * config.ways);
        assert(set < config.sets);
        uint64_t cur_low = row[0].timestamp;
        uint32_t low_pos = 0;
        for (uint32_t way = 0; way < config.ways; way++) {
            icache_entry_t* p = row + way;
            if (!p->valid)
                return way;
            else if (p->timestamp < cur_low) {
                cur_low = p->timestamp;
                low_pos = way;
            }
        }
        return low_pos;
    }

    void make_mru(uint32_t set, uint32_t way)
    {
        assert(set < config.sets && way < config.ways);
        icache_entry_t* row = cache + (set * config.ways);
        row[way].timestamp = refs++;
    }

public:
    // icache
    bool query(uint64_t va)
    {
        const uint64_t masked_addr = line_addr(va);
        const uint32_t set = static_cast<uint32_t>(masked_addr % config.sets);
        icache_entry_t* row = cache + (set * config.ways);
        assert(set < config.sets);
        for (uint32_t way = 0; way < config.ways; way++) {
            icache_entry_t* p = row + way;
            if (p->valid && masked_addr == p->line_addr) { // hit
                make_mru(set, way);
                return true;
            }
        }
        return false;
    }

    void insert(uint64_t va)
    {
        const uint64_t masked_addr = line_addr(va);
        const uint32_t set = static_cast<uint32_t>(masked_addr % config.sets);
        icache_entry_t* row = cache + (set * config.ways);
        assert(set < config.sets);
        const uint32_t lru_way = find_lru(set);
        icache_entry_t* p = row + lru_way;
        p->valid = true;
        p->line_addr = masked_addr;
        make_mru(set, lru_way);
    }
};

class xop_cache_t
{
public:
    xop_cache_config_t config;

private:
    const uint32_t limit;
    const uint32_t sets_log2;
    uint64_t refs; // for lru/mru
    xop_cache_entry_t* cache;

    // FIXME: stats
    uint64_t* set_queries;

public:
    xop_cache_t(const xop_cache_config_t& config)
        : config(config),
          limit(config.sets * config.ways),
          sets_log2(ilog2(config.sets)),
          refs(0),
          cache(new xop_cache_entry_t[limit]),
          set_queries(new uint64_t[config.sets])
    {
        for (uint32_t i = 0; i < config.sets; i++)
            set_queries[i] = 0;
    }
    ~xop_cache_t()
    {
        delete[] cache;
        delete[] set_queries;
    }

    void dump(std::ostream& o) const
    {
        for (uint32_t set = 0; set < config.sets; ++set) {
            xop_cache_entry_t* row = cache + (set * config.ways);
            o << "set " << std::setw(2) << std::setfill('0') << set << ": " << std::setfill(' ')
              << "(mupe=" << config.max_uops_per_entry << ") ";
            for (uint32_t way = 0; way < config.ways; ++way) {
                xop_cache_entry_t* p = row + way;
                p->dump(o, config.max_uops_per_entry);
                o << " /// ";
            }
            o << nl;
        }
    }

    void dump_normalized(std::ostream& o) const
    {
        heatmap_ctr_t n = 0;
        for (uint32_t set = 0; set < config.sets; ++set) {
            xop_cache_entry_t* row = cache + (set * config.ways);
            for (uint32_t way = 0; way < config.ways; ++way) {
                xop_cache_entry_t* p = row + way;
                n = std::max(n, p->max_heat());
            }
        }

        for (uint32_t set = 0; set < config.sets; ++set) {
            xop_cache_entry_t* row = cache + (set * config.ways);
            o << "set " << std::setw(2) << std::setfill('0') << set << ": " << std::setfill(' ');
            for (uint32_t way = 0; way < config.ways; ++way) {
                xop_cache_entry_t* p = row + way;
                p->dump_normalized(o, n, config.max_uops_per_entry);
                o << " ///  ";
            }
            o << nl;
        }
    }

    void dump_iq_stats(std::ostream& o) const
    {
        for (uint32_t set = 0; set < config.sets; ++set) {
            xop_cache_entry_t* row = cache + (set * config.ways);
            for (uint32_t way = 0; way < config.ways; ++way) {
                o << "set " << std::setw(2) << std::setfill('0') << set << " "
                  << " way " << std::setw(1) << way << std::setfill(' ') << " queries: " << std::setw(9)
                  << set_queries[set] << "   ";
                xop_cache_entry_t* p = row + way;
                p->dump_stat(o);
                o << nl;
            }
            o << nl;
        }
    }

private:
    uint32_t find_lru(uint32_t set) const
    {
        xop_cache_entry_t* row = cache + (set * config.ways);
        assert(set < config.sets);
        uint64_t cur_low = row[0].timestamp;
        uint32_t low_pos = 0;
        for (uint32_t way = 0; way < config.ways; way++) {
            xop_cache_entry_t* p = row + way;
            if (!p->valid)
                return way;
            else if (p->timestamp < cur_low) {
                cur_low = p->timestamp;
                low_pos = way;
            }
        }
        return low_pos;
    }

    void make_mru(uint32_t set, uint32_t way)
    {
        assert(set < config.sets && way < config.ways);
        xop_cache_entry_t* row = cache + (set * config.ways);
        row[way].timestamp = refs++;
    }

    uint32_t get_set(uint64_t va) const
    {
        // using an XOR to spread out data more uniformly.
        // Be careful: in 64set configs, this uses on bit above 4kb...(v2p xlation).
        const uint64_t t = (va >> 2) ^ (va >> (2 + sets_log2));
        const uint32_t set = static_cast<uint32_t>(t % config.sets);
        assert(set < config.sets);
        return set;
    }

public:
    // xop cache
    bool query(uint64_t va, uint32_t ninst, uint32_t nuop)
    {
        const uint32_t set = get_set(va);
        const uint32_t row_id = set * config.ways;
        xop_cache_entry_t* row = cache + row_id;
        set_queries[set]++;
        for (uint32_t way = 0; way < config.ways; way++) {
            xop_cache_entry_t* p = row + way;
            if (p->valid && p->pc_start == va && p->num_inst == ninst) { // hit
                make_mru(set, way);
                p->use(nuop); // heatmap
                p->hits++;
                return true;
            }
        }
        return false;
    }

    void insert(uint64_t va, uint32_t ninst, uint32_t nuop)
    {
        const uint32_t set = get_set(va);
        xop_cache_entry_t* row = cache + (set * config.ways);
        const uint32_t lru_way = find_lru(set);
        xop_cache_entry_t* p = row + lru_way;
        p->valid = true;
        p->pc_start = va;
        p->num_inst = ninst;
        p->num_uops = nuop;
        p->insertions++;
        make_mru(set, lru_way);
    }
};

struct xsim_fc_iq_entry_t
{
    is::Instruction instr;
    uint64_t pc{0};
    uint32_t nuops{0};
    // if fused, we set the # of instr to 0, and accumulate the
    // uops on the first instr of the pair. So if nuops=0, we
    // have the the 2nd instr of a pair. And if ninstr > 1,
    // have a fused pair. Some instr could have 0 uops as well.
    uint32_t ninstr{0};
    xsim_fc_iq_entry_t(const is::Instruction& instr, uint32_t nuops, uint32_t ninstr = 1)
        : instr(instr), nuops(nuops), ninstr(ninstr)
    {
        pc = instr.program_counter().va();
    }
    bool fused() const { return ninstr > 1; }
    bool prev_fused() const { return ninstr == 0; }
};

struct xsim_fetch_chunk_t
{
    bool valid{false};
    uint64_t va{};       // aligned VA of chunk
    uint64_t pc_start{}; // instr PC of start >= va
    uint32_t ninst{};
    uint32_t nuop{};
    std::deque<xsim_fc_iq_entry_t> iq{};
};

static void tick()
{
    static uint64_t i = 0;
    static size_t len = 0;
    static const char bs = 8;
    ++i;
    const uint32_t mil = 1000 * 1000;
    if ((i % mil) == 0) {
        if (len)
            for (size_t i = 0; i < len; ++i)
                std::cerr << bs;
        std::stringstream ss;
        ss << i / mil << "M";
        len = ss.str().size();
        std::cerr << ss.str() << std::flush;
    }
}

static inline ampere::archx::UopSet decode(uint32_t itext)
{
    auto uops = archx::furiosa::decode(itext);
    return uops;
}

static inline ampere::archx::UopSet decode_fusion(uint32_t itext1, uint32_t itext2, bool& fused)
{
    uint32_t insts[2] = {itext1, itext2};
    auto [fusion_type, uops] = archx::furiosa::decode_fusion(insts, /*fusion_skip_checks*/ false);
    if (uops.size() == 0) { // no fusion
        fused = false;
        return decode(itext1);
    }
    fused = true;
    return std::move(uops);
    (void)fusion_type;
}

static void print_uops(const ampere::archx::UopSet& uops)
{
    for (const auto& uop : uops) {
        std::cout << " [ " << uop->opcode;

        if (uop->info->barrier_type != ampere::archx::BarrierType::NONE)
            std::cout << uop->info->barrier_type;

        if (uop->info->unit != ampere::archx::Uopunit::None)
            std::cout << uop->info->unit;

        std::cout << " ]";
    }
}

static void print_instruction [[maybe_unused]] (const is::Instruction& instruction, std::ostream& o = std::cerr)
{
    uint64_t pc = instruction.program_counter().va();
    auto uops = decode(instruction.opcode());
    auto uinfo = archx::furiosa::decode_inst_info(instruction.opcode()); // InstInfo*
    uint32_t n = uops.size();

    print_addr(pc, o);
    o << " uops: " << n;
    if (archx::is_branch(uinfo->branch_type)) {
        o << " branch";
    }
    print_uops(uops);
    o << std::endl;
}

class xsim_t
{
public:
    stats_t stats{};
    is::InstructionStreamReader* rdr{};
    bool working{true};
    bool started{false}; // gate waiting for "start" control event
    std::deque<is::Instruction> iq{};
    uint64_t mask{};
    xsim_fetch_chunk_t current_fc{};

    icache_t icache;
    xop_cache_t xcache;
    loop_buffer_t loopbuf;

    bool end_of_trace() const { return !working; }

    void get_instruction()
    {
        is::Event event;
        while (working && rdr->read(event)) {
            if (event.has_control()) {
                switch (event.control().type()) {
                case is::Control::START_MEASUREMENT:
                    started = true;
                    break;
                case is::Control::STOP_MEASUREMENT:
                    working = false;
                    return;
                default:
                    exit(EXIT_FAILURE); // unknown control
                    break;
                }
            } else if (started && event.has_instruction()) {
                is::Instruction instruction = event.instruction();
                iq.emplace_back(instruction);
                tick();
                ++stats.instructions;
                return;
            } else {
                // ignore event
            }
        }
        working = false;
#if 0
        is::Instruction instruction;
        if (working && rdr->read(instruction)) {
            iq.emplace_back(instruction);
            tick();
            ++stats.instructions;
        }
        else {
            working=false;
        }
#endif
    }

    void get_two_instr()
    {
        while (working && iq.size() < 2) // need two to try to fuse
            get_instruction();
    }

    void add_two_to_fetch_chunk(is::Instruction& i1, is::Instruction& i2, uint32_t nuops)
    {
        current_fc.iq.emplace_back(i1, nuops, 2);
        current_fc.iq.emplace_back(i2, 0, 0);
        iq.pop_front();
        iq.pop_front();
        ++stats.fused;
    }

    void get_next_fetch_chunk()
    {
        // how to handle fusion:
        // - if only one instr (at end of trace), don't fuse
        // - get two instructions.
        // - only fuse if in same fetch chunk (how many do we lose?)
        // - if fusible, add both
        // - if not fusible, add first and continue working with 2nd and subsequent instr.

        get_two_instr();

        if (!working && iq.empty()) {
            current_fc.valid = false;
            return;
        }

        uint64_t pc = iq.front().program_counter().va();
        current_fc.valid = true;
        current_fc.va = pc & mask;
        current_fc.pc_start = pc;
        auto instruction = iq.front();
        uint32_t ninst = 0;
        uint64_t last_pc = pc;

        ampere::archx::UopSet uops;
        bool fused = false;
        if (iq.size() >= 2) { // try fusing
            auto second_instr = iq.at(1);
            uint64_t second_pc = second_instr.program_counter().va();
            if (pc + 4 == second_pc && (pc & mask) == (second_pc & mask)) {
                uops = decode_fusion(instruction.opcode(), second_instr.opcode(), fused);
                if (fused) {
                    add_two_to_fetch_chunk(instruction, second_instr, uops.size());
                    ++stats.fused;
                    ninst = 2;
                    last_pc = second_pc;
                }
            }
        }

        if (!fused) {
            uops = decode(instruction.opcode());
            current_fc.iq.emplace_back(instruction, uops.size());
            iq.pop_front();
            ninst = 1;
        }
        ++stats.fetch_chunks;
        uint32_t nuop = uops.size();

        while (!iq.empty()) {
            get_two_instr();

            auto instruction = iq.front();
            const uint64_t npc = instruction.program_counter().va();
            if (npc != last_pc + 4) // branching out
                break;
            last_pc = npc;
            if ((npc & mask) != current_fc.va) // trace left the current chunk.
                break;
            if (npc <= pc) { // avoid looping in the fetch chunk.
                ++stats.looping_in_fetch_chunk;
                break;
            }

            ampere::archx::UopSet uops;
            bool fused = false;
            if (iq.size() >= 2) { // try fusing
                auto second_instr = iq.at(1);
                uint64_t second_pc = second_instr.program_counter().va();
                if (npc + 4 == second_pc && (npc & mask) == (second_pc & mask)) {
                    uops = decode_fusion(instruction.opcode(), second_instr.opcode(), fused);
                    if (fused) {
                        add_two_to_fetch_chunk(instruction, second_instr, uops.size());
                        ninst += 2;
                        pc = std::max(pc, second_pc); // only forward motion in fetch chunk.
                    }
                }
            }

            if (!fused) {
                pc = std::max(pc, npc); // only forward motion in fetch chunk.
                uops = decode(instruction.opcode());
                current_fc.iq.emplace_back(instruction, uops.size());
                iq.pop_front();
                ++ninst;
            }
            nuop += uops.size();
        } // while
        current_fc.ninst = ninst;
        current_fc.nuop = nuop;
    }

    // XXX
    bool lookup_loop_buffer(uint64_t pc1, uint64_t pc2, uint_t* nuops_array)
    {
        bool hitting = true;
        uint64_t pc = pc1;

        assert(pc1 <= pc2);
        assert(pc1 + 32 > pc2); // FIXME: parameterize
        uint_t i = 0;
        while (pc <= pc2 && hitting) {
            if (nuops_array[i]) // skip fused instr
                hitting = loopbuf.query(pc);
            pc += 4;
            ++i;
        }
        if (hitting)
            ++stats.loop_buffer_hits;
        else
            ++stats.loop_buffer_misses;
        return hitting;
    }

    void install_loop_buffer(uint64_t pc1, uint64_t pc2, uint_t* nuops_array, uint_t ninst)
    {
        uint64_t pc = pc1;
        assert(pc1 <= pc2);
        assert(pc1 + 32 > pc2); // FIXME: parameterize
        uint_t i = 0;
        while (pc <= pc2) {
            assert(i < ninst);
            if (nuops_array[i]) // zero for fused stuff
                loopbuf.insert(pc, nuops_array[i]);
            stats.uops_sourced_by_loopbuf += nuops_array[i];
            pc += 4;
            i++;
        }
        ++stats.loop_buffer_inserts;
    }

    bool lookup_xop_buffer(uint64_t pc1, [[maybe_unused]] uint64_t pc2, uint_t ninst, uint_t nuop)
    {
        if (nuop > xcache.config.max_uops_per_entry) {
            ++stats.exceeded_uops_per_entry_limit;
            return false;
        }
        if (xcache.query(pc1, ninst, nuop)) {
            ++stats.xop_cache_hit;
            stats.uops_sourced_by_xcache += nuop;
            return true;
        }
        ++stats.xop_cache_miss;
        return false;
    }

    void install_xcache(uint64_t pc1, uint64_t pc2, uint_t* nuops_array, uint_t ninst, uint_t nuop)
    {
        xcache.insert(pc1, ninst, nuop);
    }

    bool icache_lookup_and_insert(uint64_t pc)
    {
        static uint64_t prev_addr = 0;
        // we might look for the same fetch chunk multiple times. Ignore repeated sequential requests for same line.
        const uint64_t masked_addr = icache.line_addr(pc);
        if (prev_addr == masked_addr) {
            ++stats.icache_repeat_accesses;
        } else {
            prev_addr = masked_addr;
            if (icache.query(masked_addr)) {
                ++stats.icache_hits;
            } else {
                ++stats.icache_misses;
                icache.insert(masked_addr);
            }
        }
        return false;
    }

    xsim_t(const icache_config_t& iconfig, const xop_cache_config_t& xconfig, const loop_buffer_config_t& lconfig)
        : icache(iconfig), xcache(xconfig), loopbuf(lconfig)
    {
        stats.t1 = hires_clock::now();
        mask = ~static_cast<uint64_t>(xcache.config.fetch_chunk_bytes - 1);
    }

    stats_t study_trace(const fs::path& stream)
    {
        const bool verbose = false;
        rdr = new is::InstructionStreamReader(stream);
        assert(rdr->version() >= 3);
        std::cerr << "Working: ";
        const uint_t ndec = 5;  // FIXME: parameterize
        uint_t iifc = 0;        // "instruction in fetch chunk". allows decode to span fetch chunks
        uint_t nuops_array[32]; // FIXME: parameterize - instructions / fetch chunk
        // nuops_array[i] will be zero for fused instructions

        while (true) {
            get_next_fetch_chunk();
            iifc = 0;
            if (end_of_trace())
                goto terminate;
            if (verbose)
                std::cerr << "==========================" << nl;
            while (iifc < current_fc.ninst) { // finish processing fetch chunk
                uint64_t first = 0, last = 0;
                uint_t nuops = 0, icount = 0;
                uint_t lim = std::min(ndec, current_fc.ninst - iifc);
                if (verbose)
                    std::cerr << "------------------------ lim: " << lim << nl;
                for (uint_t i = 0; i < lim; i++) { // walk the group of instr we will decode this pass
                    assert(iifc < current_fc.ninst);
                    assert(!current_fc.iq.empty());
                    auto iqe = current_fc.iq.front();
                    if (verbose) {
                        std::cerr << "total uops prior: " << nuops << "+" << iqe.nuops << " then: ";
                        if (iqe.fused())
                            std::cerr << "FUSED ";
                        else
                            std::cerr << "      ";
                        print_instruction(iqe.instr);
                    }
                    if (iqe.fused() && i == lim - 1) { // Too many macro instr  for this chunk
                        break;
                    }
                    if (nuops + iqe.nuops > xcache.config.max_uops_per_entry) { // FIXME or ndec?
                        ++stats.exceeded_uops_per_entry_limit;
                        break;
                    }
                    nuops_array[i] = iqe.nuops;
                    nuops += iqe.nuops;
                    if (i == 0) {
                        first = iqe.pc;
                    } else {
                        last = iqe.pc;
                    }
                    ++icount;

                    current_fc.iq.pop_front();
                    ++iifc;
                } // for

                bool ok = true;
                if (first == 0) { // no instr - because first instr was too many uops for decode or xop cache. take one
                                  // instruction
                    ok = false;
                    first = current_fc.pc_start;
                    last = first;
                    auto iqe = current_fc.iq.front();
                    nuops = iqe.nuops;
                    icount = 1;
                    nuops_array[0] = nuops;
                    if (iqe.fused()) {
                        current_fc.iq.pop_front();
                        nuops_array[1] = 0;
                        icount = 2;
                        ++iifc;
                    }
                    current_fc.iq.pop_front();
                    ++iifc;

                } else if (last == 0) { // only 1 instr...
                    last = first;
                    if (!current_fc.iq.empty()) {
                        auto niqe = current_fc.iq.front();
                        if (niqe.prev_fused()) {
                            current_fc.iq.pop_front();
                            nuops_array[1] = 0;
                            icount = 2;
                            ++iifc;
                        }
                    }
                }
                if (lookup_loop_buffer(first, last, nuops_array)) {
                } else if (ok && lookup_xop_buffer(first, last, icount, nuops)) {
                    install_loop_buffer(first, last, nuops_array, icount);
                } else { // we'd query icache and decode as normal for this one.
                    icache_lookup_and_insert(first);
                    stats.uops_sourced_by_decode += nuops;

                    install_loop_buffer(first, last, nuops_array, icount);
                    if (ok)
                        install_xcache(first, last, nuops_array, icount, nuops);
                }
            } // while (iifc < current_fc.ninst)
        } // while(1)
    terminate:
        std::cerr << " done" << std::endl;

        if (xcache.config.emit_heatmap) {
            std::stringstream h, hn;
            // FIXME: dump to string and jam into json...
            h << "Raw heatmap" << nl;
            xcache.dump(h);
            hn << "Normalized heatmap" << nl;
            xcache.dump_normalized(hn);
            stats.heatmap = h.str();
            stats.heatmap_normalized = hn.str();
        }
        std::stringstream set_stats;

        xcache.dump_iq_stats(set_stats);
        stats.set_stats = set_stats.str();

        return stats;
    }
};

static void histogram_uop [[maybe_unused]] (xsim_t& xsim, uint32_t nuop)
{
    uint32_t hist_bin;
    if (xsim.current_fc.nuop >= MAX_UOPS_PER_ENTRY)
        hist_bin = MAX_UOPS_PER_ENTRY - 1; // clip
    else
        hist_bin = xsim.current_fc.nuop;
    xsim.stats.uops_per_entry[hist_bin]++;
}

static void print_counts(const std::string& trace,
                         const stats_t& stats,
                         const icache_config_t& iconfig,
                         const xop_cache_config_t& xconfig,
                         const loop_buffer_config_t& lconfig)
{
    json jsv;
    auto t2 = hires_clock::now();
    jsv["trace"] = trace;
    jsv["elapsed"] = mround(duration_ms(t2 - stats.t1).count(), 1);
    jsv["icache_sets"] = iconfig.sets;
    jsv["icache_ways"] = iconfig.ways;
    jsv["icache_bytes_per_line"] = iconfig.bytes_per_line;
    jsv["icache_capacity_bytes"] = iconfig.bytes_per_line * iconfig.sets * iconfig.ways;
    jsv["icache_hits"] = stats.icache_hits;
    jsv["icache_misses"] = stats.icache_misses;
    jsv["icache_repeat_accesses"] = stats.icache_repeat_accesses;
    jsv["icache_repeat_accesses_per_instruction"] = kround(1.0 * stats.icache_repeat_accesses / stats.instructions);
    jsv["icache_miss_rate"] =
        kround(1.0 * stats.icache_misses / (stats.icache_repeat_accesses + stats.icache_hits + stats.icache_misses));
    jsv["icache_mpki"] = kround(1000.0 * stats.icache_misses / stats.instructions);

    jsv["xcache_capacity_uops"] = xconfig.max_uops_per_entry * xconfig.sets * xconfig.ways;
    jsv["xop_sets"] = xconfig.sets;
    jsv["xop_ways"] = xconfig.ways;
    jsv["fetch_chunk_bytes"] = xconfig.fetch_chunk_bytes;
    jsv["max_instr_per_entry"] = xconfig.max_instr_per_entry;
    jsv["max_uops_per_entry"] = xconfig.max_uops_per_entry;
    jsv["instructions"] = stats.instructions;
    jsv["fused"] = stats.fused;
    jsv["exceeded_uops_per_entry_limit"] = stats.exceeded_uops_per_entry_limit;
    jsv["pct_fetch_chunks_too_many_uops"] = kround(100.0 * stats.exceeded_uops_per_entry_limit / stats.fetch_chunks);

    jsv["pct_fused"] = kround(100.0 * stats.fused / stats.instructions);
    jsv["fetch_chunks"] = stats.fetch_chunks;
    jsv["looping_in_fetch_chunk"] = stats.looping_in_fetch_chunk;
    uint64_t total_uops = stats.uops_sourced_by_xcache + stats.uops_sourced_by_decode + stats.uops_sourced_by_loopbuf;
    jsv["total_uops"] = total_uops;
    jsv["uops_sourced_by_xcache"] = stats.uops_sourced_by_xcache;
    jsv["uops_sourced_by_decode"] = stats.uops_sourced_by_decode;
    jsv["uops_sourced_by_loopbuf"] = stats.uops_sourced_by_loopbuf;
    jsv["uops_sourced_by_decode_too_many_uops_for_fetch_chunk"] =
        stats.uops_sourced_by_decode_too_many_uops_for_fetch_chunk;
    jsv["pct_uops_sourced_by_xcache"] = kround(100.0 * stats.uops_sourced_by_xcache / total_uops);
    jsv["pct_uops_sourced_by_decode"] = kround(100.0 * stats.uops_sourced_by_decode / total_uops);
    jsv["pct_uops_sourced_by_loopbuf"] = kround(100.0 * stats.uops_sourced_by_loopbuf / total_uops);
    jsv["pct_uops_sourced_by_decode_too_many_uops_for_fetch_chunk"] =
        kround(100.0 * stats.uops_sourced_by_decode_too_many_uops_for_fetch_chunk / total_uops);

    jsv["uops_per_instr"] = kround(1.0 * total_uops / stats.instructions);

    jsv["xop_cache_hit"] = stats.xop_cache_hit;
    jsv["xop_cache_miss"] = stats.xop_cache_miss;

    jsv["xop_cache_miss_rate"] = kround(1.0 * stats.xop_cache_miss / (stats.xop_cache_miss + stats.xop_cache_hit));

    jsv["xop_cache_mpki"] = kround(1000.0 * stats.xop_cache_miss / stats.instructions);

    jsv["heatmap"] = stats.heatmap;
    jsv["heatmap_normalized"] = stats.heatmap_normalized;
    jsv["set_stats"] = stats.set_stats;

    jsv["loopbuf_entries"] = lconfig.entries;
    jsv["loopbuf_inserts"] = stats.loop_buffer_inserts;
    jsv["loopbuf_hits"] = stats.loop_buffer_hits;
    jsv["loopbuf_misses"] = stats.loop_buffer_misses;
    jsv["loopbuf_miss_rate"] =
        kround(1.0 * stats.loop_buffer_misses / (stats.loop_buffer_misses + stats.loop_buffer_hits));
    jsv["loopbuf_mpki"] = kround(1000.0 * stats.loop_buffer_misses / stats.instructions);

    uint64_t total = 0;
    for (uint32_t i = 0; i < MAX_UOPS_PER_ENTRY; ++i)
        total += stats.uops_per_entry[i];

    double cdf = 0.0;
    for (uint32_t i = 0; i < MAX_UOPS_PER_ENTRY; ++i) {
        if (stats.uops_per_entry[i] == 0 && i > 25)
            continue;
        double pct = 100.0 * stats.uops_per_entry[i] / total;
        cdf += pct;
        jsv["histogram_uop_per_entry"][i] = {
            {"uops", i}, {"freq", stats.uops_per_entry[i]}, {"pct", kround(pct)}, {"cdf", kround(cdf)}};
    }

    std::cout << jsv.dump(4) << nl;
}

static std::string trace_name(fs::path p)
{
    if (p.has_extension()) {
        while (p.extension() != ".is")
            p = p.stem();

        if (p.has_extension())
            p = p.stem();
    }

    return p.native();
}

static uint32_t get_int(std::deque<std::string>& cmdline)
{
    if (cmdline.empty()) {
        std::cerr << "Need an integer argument for option\n";
        exit(EXIT_FAILURE);
    }
    uint32_t r = std::stoi(cmdline.front());
    cmdline.pop_front();
    return r;
}
static std::string get_string(std::deque<std::string>& cmdline)
{
    if (cmdline.empty()) {
        std::cerr << "Need an string argument for option\n";
        exit(EXIT_FAILURE);
    }
    std::string s = cmdline.front();
    cmdline.pop_front();
    return s;
}

void usage(char const* prog)
{
    std::cerr << "Usage " << prog << " [options] -i trace-stream\n";
    char const* umsg[] = {"-i PATH       file path to trace stream (required)",
                          "-v N          verbosity",
                          "--sets SETS   Default 64",
                          "--ways WAYS   Default 4",
                          "--fcb BYTES   fetch chunk size, bytes. Default 32",
                          "--mipe N      (not currently used)",
                          "--mupe N      max uops per entry. Default 1000 (unlimited)",
                          "--heatmap     emit raw and normalized heatmaps to stdout",
                          nullptr};

    for (int i = 0; umsg[i]; i++)
        std::cerr << "\t" << umsg[i] << nl;
    exit(EXIT_FAILURE);
}

int main(int argc, char** argv)
{
    fs::path instruction_stream;
    bool got_path = false;
    std::deque<std::string> cmdline(argv + 1, argv + argc);
    xop_cache_config_t xconfig;

    while (!cmdline.empty()) {
        auto arg = cmdline.front();
        cmdline.pop_front();

        if (arg == "-v") {
            xconfig.verbose = get_int(cmdline);
        } else if (arg == "--sets") {
            xconfig.sets = get_int(cmdline);
        } else if (arg == "--ways") {
            xconfig.ways = get_int(cmdline);
        } else if (arg == "--mupe") {
            xconfig.max_uops_per_entry = get_int(cmdline);
        } else if (arg == "--mipe") {
            xconfig.max_instr_per_entry = get_int(cmdline);
        } else if (arg == "--fcb") {
            xconfig.fetch_chunk_bytes = get_int(cmdline);
        } else if (arg == "--heatmap") {
            xconfig.emit_heatmap = true;
        } else if (arg == "--help" || arg == "-h") {
            usage(argv[0]);
        } else if (arg == "-i" and !got_path) {
            instruction_stream = get_string(cmdline);
            got_path = true;
        } else {
            usage(argv[0]);
        }
    }
    if (!got_path) {
        std::cerr << "Missing required path with -i option" << nl;
        std::cerr << "See help message with --help option." << nl;
        exit(EXIT_FAILURE);
    }

    std::cerr << "config = " << xconfig << std::endl;

    icache_config_t iconfig(64, 4, 64); // 16KB = 64*4*64=2^(6+2+6)=2^14 // FIXME: parameterize
    loop_buffer_config_t lconfig(64);   // FIXME: parameterize
    xsim_t xsim(iconfig, xconfig, lconfig);
    print_counts(trace_name(instruction_stream), xsim.study_trace(instruction_stream), iconfig, xconfig, lconfig);
}

/*
 * vim: sw=4:ts=4:et:si:cindent:cino=\:0,(0,g0,t0:
 */
