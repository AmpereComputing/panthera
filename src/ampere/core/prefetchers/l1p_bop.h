//
// Copyright (c) 2023 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//
#pragma once
#include <cstdint>
#include <limits>
#include <vector>
#include <deque>
#include <set>
#include <optional>
#include <fmt/core.h>
#include <nlohmann/json.hpp>

#include "ampere/sc/sc.h"
#include "ampere/core/uop.h"
#include "ampere/archx/addr.h"
#include "ampere/core/backend_prefetcher.h"
#include "ampere/core/bloom-filter.h"

using json = nlohmann::json;

namespace ampere::core {

using namespace ampere::sc;

class OffsetHistogram
{
    std::map<std::int64_t, std::uint64_t> counts;
    std::deque<std::uint64_t> recent;
    std::uint64_t recent_entries;

public:
    OffsetHistogram() { recent_entries = 0; }
    void set(std::uint64_t entries) { recent_entries = entries; }
    void add(std::uint64_t va)
    {
        for (auto old : recent) {
            std::int64_t offset = va - old;
            auto itr = counts.find(offset);
            if (itr != counts.end()) {
                counts[offset] += 1;
            } else {
                counts[offset] = 1;
            }
        }

        recent.emplace_back(va);
        if (recent.size() > recent_entries) {
            recent.pop_front();
        }
    }
    void dump(std::ostream& os) const
    {
        json jlog;
        std::uint64_t indx = 0;
        for (auto [offset, count] : counts) {
            jlog[indx++] = {offset, count};
        }
        fmt::print(os, "Offset values histogram (start)\n");
        fmt::print(os, "{}\n", jlog.dump(4));
        fmt::print(os, "Offset values histogram (end)\n");
    }
}; // OffsetHistogram

// #define DEBUG_L1BOP_FILTER
#if defined(DEBUG_L1BOP_FILTER)
#define DBG_L1BOPFLTR(format_str, ...) fmt::print(stderr, "XX: " format_str "\n", ##__VA_ARGS__)
#else
#define DBG_L1BOPFLTR(format_str, ...)
#endif

// NOTE: not filtering by same-virtual-page

static std::uint64_t bloom_filter_sz = 4096;

static std::uint64_t hf1(std::uint64_t x)
{
    return x % bloom_filter_sz;
}
static std::uint64_t hf2(std::uint64_t x)
{
    return (x ^ (x >> 12)) % bloom_filter_sz;
}
static std::uint64_t hf3(std::uint64_t x)
{
    return (x ^ (x >> 24)) % bloom_filter_sz;
}
static std::uint64_t hf4(std::uint64_t x)
{
    return ((x >> 18) ^ (x >> 36)) % bloom_filter_sz;
}

static bool is_power_of_2(std::uint64_t x)
{
    if ((x & (x - 1)) == 0)
        return true;
    return false;
}
class RecentPrefetchHistoryTable
{
    std::deque<std::uint64_t> order;
    std::set<std::uint64_t> all;
    std::uint64_t limit{0};

public:
    void set_limit(std::uint64_t arg_limit) { limit = arg_limit; }
    void insert(std::uint64_t va)
    {
        if (limit) {
            if (order.size() >= limit) {
                std::uint64_t t = order.front();
                order.pop_front();
                all.erase(t);
            }
            all.insert(va);
            order.push_back(va);
        }
    }
    bool lookup(std::uint64_t va) const
    {
        // Return true if we find recent prefetches with this va.
        // Return false otherwise.
        if (limit) {
            return all.contains(va);
        }
        return false; // no entries implies not found
    }
};

class RecentRequestTable : public Module
{
    struct RecentRequestTableEntry
    {
        std::uint64_t pc;
        std::uint64_t tag; // data va
        bool valid;
        std::uint64_t timestamp;
        RecentRequestTableEntry() : pc(0), tag(0), valid(false), timestamp(0) {}
        void update(std::uint64_t va, std::uint64_t _pc, std::uint64_t now)
        {
            valid = true;
            pc = _pc;
            tag = va;
            timestamp = now;
        }
    };
    struct RecentRequestTableSet
    {
        std::vector<RecentRequestTableEntry> data;
        RecentRequestTableSet(std::uint64_t ways)
        {
            for (std::uint64_t i = 0; i < ways; i++) {
                data.emplace_back();
            }
        }
    };
    std::uint64_t shorten_va(std::uint64_t va) const
    {
        std::uint64_t sva = va >> 6; // lop off offset-in-line bits
        return sva & va_mask;
    }
    std::uint64_t va_to_set(std::uint64_t sva) const
    {
        std::uint64_t set_idx = 0;
        if (sets > 1) {
            // xor bits to spread out strides
            set_idx = ((sva >> (mul * ilog_sets)) ^ sva) % sets;
        }
        return set_idx;
    }

    std::uint64_t now{0}; // for lru
    std::uint64_t ilog_sets{0};
    std::uint64_t va_mask{0};

    std::vector<RecentRequestTableSet> sets_data;

public:
    Param<std::uint64_t> mul{"mul", "VA-to-set upper bits shift multiplier", 1};
    Param<std::uint64_t> ways{"ways", "Recent Request Table associativity", 1};
    Param<std::uint64_t> sets{"sets", "Recent Request  Table sets", 64};
    Param<std::uint64_t> va_bits{
        "va_bits", "number of virtual address bits to use for recent request table tags & set selection. (Max 58)", 12};
    RecentRequestTable(const sc_core::sc_module_name& name) : Module(name)
    {
        ilog_sets = 63 - __builtin_clzl(sets);
        AMPERE_ASSERT(va_bits <= 58, "va_bits value too large. Max 58");
        AMPERE_ASSERT(va_bits > 0, "va_bits value must be > 0");
        AMPERE_ASSERT(va_bits >= ilog_sets, "va_bits needs to be >= than the number of sets in recent request table");
        va_mask = (1ULL << va_bits) - 1ULL;
        AMPERE_ASSERT(is_power_of_2(sets), "L1BOP recent request table sets must be a power of 2");
        for (std::uint64_t i = 0; i < sets; i++) {
            sets_data.emplace_back(ways);
        }
    }
    std::optional<std::uint64_t> lookup(const std::uint64_t va, bool update_lru)
    {
        std::uint64_t short_va = shorten_va(va);
        std::uint64_t set_idx = va_to_set(short_va);
        for (std::uint64_t way = 0; way < ways; way++) {
            RecentRequestTableEntry& rre = sets_data[set_idx].data[way];
            if (rre.valid && rre.tag == short_va) {
                if (update_lru) {
                    rre.timestamp = now++;
                }
                return rre.pc;
            }
        }
        return std::nullopt;
    }
    void insert(const std::uint64_t va, const std::uint64_t pc)
    {
        std::uint64_t short_va = shorten_va(va);
        std::uint64_t set_idx = va_to_set(short_va);
        std::uint64_t min_timetamp = std::numeric_limits<std::uint64_t>::max();
        std::uint64_t min_pos = 0;
        now++;
        for (std::uint64_t way = 0; way < ways; way++) {
            RecentRequestTableEntry& rre = sets_data[set_idx].data[way];
            if (rre.valid == 0) {
                rre.update(short_va, pc, now);
                return;
            } else {
                if (rre.timestamp < min_timetamp) {
                    min_timetamp = rre.timestamp;
                    min_pos = way;
                }
            }
        }
        // replace entry with oldest timestamp
        AMPERE_ASSERT(min_timetamp < std::numeric_limits<std::uint64_t>::max(), "Min timestamp search failed");
        sets_data[set_idx].data[min_pos].update(short_va, pc, now);
    }

    void update(const std::uint64_t va, const std::uint64_t pc)
    {
        std::uint64_t short_va = shorten_va(va);
        std::uint64_t set_idx = va_to_set(short_va);
        now++;
        for (std::uint64_t way = 0; way < ways; way++) {
            RecentRequestTableEntry& rre = sets_data[set_idx].data[way];
            if (rre.valid && rre.tag == short_va) {
                rre.update(short_va, pc, now);
                return;
            }
        }
        AMPERE_ERROR("Did not find tag when attempting an update");
    }
}; // RecentRequestTable

class FilterTable : public Module
{
    struct FilterTableEntry
    {
        std::uint64_t counter;
        std::uint64_t tag;
        bool valid;
        std::uint64_t timestamp;
        FilterTableEntry() : counter(0), tag(0), valid(false), timestamp(0) {}
    };
    struct FilterTableSet
    {
        std::vector<FilterTableEntry> data;
        FilterTableSet(std::uint64_t ways)
        {
            for (std::uint64_t i = 0; i < ways; i++) {
                data.emplace_back();
            }
        }
    };
    std::uint64_t now{0};
    std::uint64_t ilog_sets{0};
    std::vector<FilterTableSet> sets_data;

public:
    Param<std::uint64_t> ways{"ways", "Filter Table associativity", 64};
    Param<std::uint64_t> sets{"sets", "Filter Table sets", 1};
    FilterTable(const sc_core::sc_module_name& name) : Module(name)
    {
        ilog_sets = 63 - __builtin_clzl(sets);
        AMPERE_ASSERT(is_power_of_2(sets), "L1BOP filter table sets must be a power of 2");
        for (std::uint64_t i = 0; i < sets; i++) {
            sets_data.emplace_back(ways);
        }
    }
    std::uint64_t pc_to_set(std::uint64_t pc) const
    {
        std::uint64_t set_idx = 0;
        if (sets > 1) {
            set_idx = (pc ^ (pc >> (2 * ilog_sets))) % sets;
        }
        return set_idx;
    }
    std::optional<std::uint64_t> lookup(const std::uint64_t pc, bool update_lru)
    {
        std::uint64_t set_idx = pc_to_set(pc);
        DBG_L1BOPFLTR("CHECK:           PC {:016x} set: {}\n", pc, set_idx);
        for (std::uint64_t way = 0; way < ways; way++) {
            FilterTableEntry& fte = sets_data[set_idx].data[way];
            DBG_L1BOPFLTR("TEST:          TAG {:016x} set: {} way: {} valid: {} ctr: {}\n",
                          fte.tag,
                          set_idx,
                          way,
                          fte.valid,
                          fte.counter);
            if (fte.valid && fte.tag == pc) {
                if (update_lru) {
                    DBG_L1BOPFLTR("HIT:           PC {:016x} set: {} way: {} ctr: {}\n", pc, set_idx, way, fte.counter);
                    fte.timestamp = now++;
                    fte.counter++;
                }
                return fte.counter;
            }
        }
        DBG_L1BOPFLTR("MISS:           PC {:016x} set: {}\n", pc, set_idx);
        return std::nullopt;
    }
    std::optional<std::uint64_t> insert_min_ctr(const std::uint64_t pc)
    {
        // return count of value replaced if valid, otherwise nullopt
        std::uint64_t set_idx = pc_to_set(pc);
        std::uint64_t min_counter = std::numeric_limits<std::uint64_t>::max();
        std::uint64_t min_pos = 0;
        for (std::uint64_t way = 0; way < ways; way++) {
            FilterTableEntry& fte = sets_data[set_idx].data[way];
            if (fte.valid == 0) { // find invalid entry first
                fte.counter = 1;
                fte.tag = pc;
                fte.valid = true;
                fte.timestamp = now++;
                DBG_L1BOPFLTR("INSERT INVALID:  PC {:016x} set: {} way: {}\n", pc, set_idx, way);
                return std::nullopt;
            } else if (fte.counter < min_counter) {
                min_counter = fte.counter;
                min_pos = way;
            }
        }
        // replace entry with minimum counter value
        AMPERE_ASSERT(min_counter < std::numeric_limits<std::uint64_t>::max(), "Min counter search failed");
        FilterTableEntry& fte = sets_data[set_idx].data[min_pos];
        fte.counter = 1;
        fte.valid = true; // redundant
        fte.tag = pc;
        fte.timestamp = now++;
        DBG_L1BOPFLTR("REPLACE LOW:  PC {:016x} set: {} way: {} min_ctr: {}\n", pc, set_idx, min_pos, min_counter);
        return min_counter;
    }

    void insert_lru(const std::uint64_t pc)
    { // not currently used
        std::uint64_t set_idx = pc_to_set(pc);

        std::uint64_t min_timetamp = std::numeric_limits<std::uint64_t>::max();
        std::uint64_t min_pos = 0;
        for (std::uint64_t way = 0; way < ways; way++) {
            FilterTableEntry& fte = sets_data[set_idx].data[way];
            if (fte.valid == 0) {
                fte.counter = 1;
                fte.tag = pc;
                fte.valid = true;
                fte.timestamp = now++;
                return;
            } else {
                if (fte.timestamp < min_timetamp) {
                    min_timetamp = fte.timestamp;
                    min_pos = way;
                }
            }
        }
        // replace entry with oldest timestamp
        AMPERE_ASSERT(min_timetamp < std::numeric_limits<std::uint64_t>::max(), "Min timestamp search failed");
        FilterTableEntry& fte = sets_data[set_idx].data[min_pos];
        fte.counter = 1;
        fte.valid = true; // redundant
        fte.tag = pc;
        fte.timestamp = now++;
    }

    void halve_counters()
    {
        for (std::uint64_t set = 0; set < sets; set++) {
            for (std::uint64_t way = 0; way < ways; way++) {
                FilterTableEntry& fte = sets_data[set].data[way];
                fte.counter = fte.counter >> 1;
                fte.valid = (fte.counter != 0); // turn off valid bit if hit zero
            }
        }
    }

}; // class FilterTable

struct ScoreWidget
{
    std::vector<std::uint64_t> scores;
    std::uint64_t max_round_score{0};
    std::uint64_t current_test_offset_index{0};
    std::uint64_t round_ctr{0};
    std::int64_t best_offset{0};
    const std::int64_t* offsets; // variable ptr to constant data
    std::uint64_t num_offsets;
    std::uint64_t roundmax;
    std::uint64_t badscore;
    std::uint64_t force_offset{0};

    ScoreWidget(const std::int64_t* offsets,
                std::uint64_t num_offsets,
                std::uint64_t roundmax,
                std::uint64_t badscore,
                std::uint64_t force_offset)
        : offsets(offsets), num_offsets(num_offsets), roundmax(roundmax), badscore(badscore), force_offset(force_offset)
    {
        start_epoch();
    }

    void start_epoch()
    {
        max_round_score = 0;
        scores = std::vector<std::uint64_t>(num_offsets); // clear scores
        round_ctr = 0;
        current_test_offset_index = 0;
    }

    bool advance_test_offset_pointer()
    { // returns true iff last round
        current_test_offset_index++;
        if (current_test_offset_index == num_offsets) {
            current_test_offset_index = 0;
            round_ctr++;
            return round_ctr >= roundmax;
        }
        return false;
    }

    std::uint64_t pick_new_best_offset(std::uint64_t& round_ctr_return)
    {
        // called after testing offset and reached MAXSCORE or MAXROUNDS
        std::uint64_t max_score_cur = scores[0];
        std::uint64_t max_score_pos = 0;
        for (std::uint64_t i = 1; i < num_offsets; i++) {
            if (max_score_cur < scores[i]) {
                max_score_pos = i;
                max_score_cur = scores[i];
            }
        }
        if (force_offset) {
            best_offset = force_offset;
        } else if (max_score_cur > badscore) {
            best_offset = offsets[max_score_pos];
            round_ctr_return = round_ctr;
        } else {
            best_offset = 0;
        }
        start_epoch();
        return best_offset;
    }
};

class L1BOP : public BackendPrefetcher<BackendMemReq>
{
public:
    Param<bool> page_fetch{"page_fetch", "fetch TLB on page miss", true}; // used in LSU
    Param<std::uint64_t> maxscore{"maxscore", "MAXSCORE parameter", 31};
    Param<std::uint64_t> badscore{"badscore", "BADSCORE parameter", 30};
    Param<std::uint64_t> roundmax{"roundmax", "ROUNDMAX parameter", 100};
    Param<std::uint64_t> recent_prefetch_history_slots{
        "recent_prefetch_history_slots", "Number of recent prefetches to keep track of", 0};
    Param<std::uint64_t> pc_bits{
        "pc_bits", "number of program counter bits to use for recent request and filter tables. (Max 62)", 12};
    Param<bool> use_filter_table{"use_filter_table", "Enable/disable the filter table", false};
    Param<std::uint64_t> offset_group{"offset_group", "Name of the offset list to use (0,1,2,3)", 0};
    Param<std::uint64_t> score_widgets{"score_widgets", "Number of score widget regions", 1};
    Param<std::uint64_t> max_filter_table_count{"max_filter_table_count", "Limit halving the filter table", 31};
    Param<bool> recycle_tlb_miss{
        "recycle_tlb_miss",
        "if prefetch candidate encounters TLB miss, hold it aside and re-issue it after miss handling is done",
        true};
    Param<uint64_t> force_offset{"force_offset", "If non-zero, always use this offset", 0};

    Param<bool> use_accuracy_filter{"use_accuracy_filter", "Enable the accuracy filter", false};
    Param<std::uint64_t> fill_incr{"fill_incr", "How much to bump the counter up on prefetch fills", 12};
    Param<std::uint64_t> use_decr{"use_decr", "How much to bump the counter down on prefetch uses", 32};
    Param<std::uint64_t> accuracy_threshold{"accuracy_threshold", "Limit for turning off prefetcher", 4095};

    // L2 BOP does not include demand fills in recent request table when prefetching (best_offset != 0)
    Param<bool> demand_fills_when_prefetching{"demand_fills_when_prefetching",
                                              "Include demands fills in the recent request table even when prefetching",
                                              false};

    Param<double> bloom_filter_hit_required_accuracy{
        "bloom_filter_hit_required_accuracy", "Limit for allowing prefetches", 0.40};
    Param<bool> use_bloom_filter{"use_bloom_filter", "Use L1BOP bloom filter", false};
    Param<bool> ignore_if_prefetched{"ignore_if_prefetched", "Ignore misses prefetched by other prefetchers", false};

    Param<bool> trigger_on_any_prefetches{
        "trigger_on_any_prefetches",
        "By default, L1BOP triggers on misses uses of its own prefetches. Set this to true"
        " to trigger L1BOP on uses of any prefetch, regardless of the prefetcher",
        false};

    Param<std::uint64_t> offset_histo{
        "offset_histo", "Number of recent entries to compare against. Zero disables profiling. (Must turn on dump)", 0};

    Event ev_ignore_miss{"ignore_miss", "Ignore miss because prefetched by other prefetch"};
    std::uint64_t accuracy_counter{0};

    Event ev_halve_filter_table{"halve_filter_table", "Halve the values in the filter table"};
    Event ev_recent_prefetch_dropped{"recent_prefetch_dropped", "L1BOP recent prefetch dropped to avoid duplicates"};
    Event ev_filter_drop{"filter_drop", "L1BOP prefetch dropped because missed in filter table"};
    Event ev_bloom_filter_drop{"bloom_filter_drop", "L1BOP prefetch dropped because low accuracy in bloom filter"};

    Event ev_train{"train", "L1BOP trained"};
    EventData<bool> evd_miss{"miss", "Training was due to a miss (1) or prefetch hit (0)"};

    Event ev_new_best_offset{"new_best_offset", "L1BOP picked new best offset"};
    Event ev_offset_alloc_ofb{"offset_alloc_ofb", "L1BOP allocated an OFB"};
    EventData<int> ev_offset{"offset", "New best offset"};
    EventData<int> ev_used_offset{"used_offset", "Used offset"};
    Event ev_training_rounds{"training_rounds", "L1BOP training phases"};
    EventData<std::uint64_t> ev_uint{"uint", "uint"};

    Event ev_filter_table_replacement{"filter_table_replacment", "Filter table replacement"};
    EventData<std::uint64_t> ev_ctr{"ctr", "Replaced filter counter value"};
    Event ev_inactive{"inactive", "L1BOP shutdown"};
    Event ev_prefetch_queue_overwrite{"prefetch_queue_overwrite", "L1BOP overwrote outgoing prefetch queue"};
    Event ev_prefetch_queue_issue{"prefetch_queue_issue", "L1BOP prefetch added to queue"};

private:
    std::uint64_t line_bytes{0};
    static const std::uint64_t num_offsets0;
    static const std::int64_t offsets0[]; // From Pierre's study
    static const std::uint64_t num_offsets1;
    static const std::int64_t offsets1[]; // From L2 BOP
    static const std::uint64_t num_offsets2;
    static const std::int64_t offsets2[]; // -63,...,-1,1,...,63
    static const std::uint64_t num_offsets3;
    static const std::int64_t offsets3[]; // -1,1...63 from profiling
    std::uint64_t num_offsets;
    const std::int64_t* offsets; // variable ptr to constant data

    std::vector<ScoreWidget> score_widget;

    BloomFilter bfilter;
    std::uint64_t bfilter_hits{0};
    std::uint64_t bfilter_inserts{0};
    bool bfilter_previous_sufficient_accuracy{false};

    FilterTable filter_table{"filter_table"};
    RecentRequestTable recent_request_table{"recent_request_table"};

    std::uint64_t pc_mask{0};
    std::uint64_t pc_mask_widget{0};
    RecentPrefetchHistoryTable recent_prefetch_history;
    OffsetHistogram offset_histogram;

public:
    // interface to L1 dcache for submitting prefetch requests. L1
    // dcache pulls from this queue
    enum class PrefetchEvent {
        DEMAND_MISS,
        DEMAND_MISS_HIT_OFB,
        USE_PREFETCHED_LINE,
    };

    OutInOpt<std::pair<BackendMemReq, PrefetchEvent>> pipe{"pipe"};

    L1BOP(const sc_core::sc_module_name& name) : BackendPrefetcher<BackendMemReq>(name), bfilter(bloom_filter_sz)
    {
        bfilter.add_hash_function(hf1);
        bfilter.add_hash_function(hf2);
        bfilter.add_hash_function(hf3);
        bfilter.add_hash_function(hf4);

        AMPERE_ASSERT(pc_bits < 63, "pc_bits value too large. Max 62");
        AMPERE_ASSERT(pc_bits > 0, "pc_bits value must be > 0");
        switch (offset_group) {
        case 0:
            num_offsets = num_offsets0;
            offsets = offsets0;
            break;
        case 1:
            num_offsets = num_offsets1;
            offsets = offsets1;
            break;
        case 2:
            num_offsets = num_offsets2;
            offsets = offsets2;
            break;
        case 3:
            num_offsets = num_offsets3;
            offsets = offsets3;
            break;
        default:
            AMPERE_ERROR("Need a value of 0, 1, 2 or 3 for offset_group parameter");
        }
        pc_mask = (1ULL << pc_bits) - 1ULL;
        AMPERE_ASSERT(score_widgets > 0, "L1BOP needs at least one score widget");
        AMPERE_ASSERT(is_power_of_2(score_widgets), "L1BOP score widgets must a power of 2");
        pc_mask_widget = score_widgets - 1ULL;
        line_bytes = 64; // MJC... parameter???
        recent_prefetch_history.set_limit(recent_prefetch_history_slots);
        for (size_t i = 0; i < score_widgets; i++) {
            score_widget.emplace_back(offsets, num_offsets, roundmax, badscore, force_offset);
        }

        if (offset_histo) {
            offset_histogram.set(offset_histo);
        }

        connect(pipe).name("pipe").latency(4);
    }

    bool run() override
    {
        before_training();
        while (pipe.can_take()) {
            const auto [req, prefetch_event] = pipe.take();
            auto va = req.virt_addr;
            switch (prefetch_event) {
            case PrefetchEvent::DEMAND_MISS:
                miss(va, req.uop);
                break;
            case PrefetchEvent::DEMAND_MISS_HIT_OFB:
            case PrefetchEvent::USE_PREFETCHED_LINE:
                hit_on_prefetch(va, req.uop);
                break;
            } // switch
            //}
        } // while
        end_of_training();
        return true;
    }

    void dump(std::ostream& os) const override
    {
        if (offset_histo) {
            offset_histogram.dump(os);
        }
    }

    void execute_impl(
        uint64_t index, BackendMemReq req, bool hit, bool prefetch, bool demand, PrefetchType prefetch_type) override
    {
        if (demand) {
            if (!hit) {
                if (!prefetch)
                    pipe.put(std::make_pair(req, PrefetchEvent::DEMAND_MISS));
                else if (prefetch_type == PrefetchType::L1BOP)
                    pipe.put(std::make_pair(req, PrefetchEvent::DEMAND_MISS_HIT_OFB));
            } else if (prefetch && (trigger_on_any_prefetches || prefetch_type == PrefetchType::L1BOP)) {
                pipe.put(std::make_pair(req, PrefetchEvent::USE_PREFETCHED_LINE));
            }
        } else if (!hit && prefetch_type == PrefetchType::PCP) {
            pipe.put(std::make_pair(req, PrefetchEvent::DEMAND_MISS));
        }
        if (offset_histo && req.phys_addr) {
            offset_histogram.add(req.phys_addr->value());
        }
    }

    void launch_impl(BackendMemReq req) override
    {
        if (req.uop->virt_addr) {
            int offset = static_cast<int64_t>(req.virt_addr.line_number())
                         - static_cast<int64_t>(req.uop->virt_addr->line_number());
            ev_offset_alloc_ofb(req, ev_used_offset(offset));
        }
    }

    using BackendPrefetcher<BackendMemReq>::fill;

    void fill_impl(BackendMemReq req, bool fb_pref, bool was_alloc_by_pf) override
    {
        if (req.uop) {
            if (fb_pref) {
                fill_for_prefetch_from_l2(req.virt_addr.line_address(), req.prefetch_type, req.uop);
            } else if (req.uop->phys_addr) {
                fill_for_demand_miss_from_l2(req.virt_addr.line_address(), req.uop);
            }
        }
    }

    void flush_impl(Flush flush_info) override
    {
        pipe.clear_if([&](const auto& e) { return e.first.uop && e.first.uop->alloc_id >= flush_info->alloc_id; });
    }

    bool commit_impl(uint64_t id, BackendMemReq req) override
    {
        pipe.clear_if([&](const auto& e) { return e.first.uop && e.first.uop->alloc_id == id; });
        return true;
    }

    PrefetchType prefetch_type() const override { return PrefetchType::L1BOP; }

    bool can_prefetch_page(BackendMemReq req) const override { return page_fetch; }

    void miss(VirtAddr va, const Uop& uop)
    {
        if (ignore_if_prefetched && uop->get_generated_L1_prefetch()) {
            ev_ignore_miss();
            return;
        }
        std::uint64_t line_addr = va.line_address().value();
        ev_train(uop, evd_miss(true), va.line_address());
        gen_prefetch(line_addr, uop);
        train_widget(line_addr, uop);
        if (bfilter.query(line_addr)) {
            bfilter_hits++;
        }
    }
    void hit_on_prefetch(VirtAddr va, const Uop& uop)
    {
        accuracy_counter = std::max(0UL, accuracy_counter - use_decr);
        std::uint64_t line_addr = va.line_address().value();
        ev_train(uop, evd_miss(false), va.line_address());
        gen_prefetch(line_addr, uop);
        train_widget(line_addr, uop);
    }

    void fill_for_prefetch_from_l2(VirtAddr fill_va, PrefetchType type, Uop& uop)
    {
        accuracy_counter += fill_incr;
        if (use_accuracy_filter && accuracy_counter > accuracy_threshold) {
            reset_score_widgets();
            accuracy_counter = 0;
        }
        ScoreWidget& widget = get_score_widget(uop);

        // We only insert L1BOP prefetches, unless the widget is not prefetching
        if (widget.best_offset == 0 || type == PrefetchType::L1BOP) {
            std::uint64_t rr_va = fill_va.line_address().value() - widget.best_offset * line_bytes;
            fill(rr_va, uop);
        }
    }

    void fill_for_demand_miss_from_l2(VirtAddr fill_va, Uop& uop)
    {
        std::uint64_t rr_va = fill_va.line_address().value();
        ScoreWidget& widget = get_score_widget(uop);
        // We insert demand fills if we have more than 1 score
        // widget, some might be prefetching and others might not
        // be so we insert demand fills regardless. Having more than
        // one score widget was a "what-if"

        // We insert demand fills when current widget is not prefetching.

        // We insert demand fills if the
        // demand_fills_when_prefetching knob is true.
        if (demand_fills_when_prefetching || score_widgets > 1 || widget.best_offset == 0) {
            fill(rr_va, uop);
        }
    }

    using BackendPrefetcher::can_allocate_pob;
    bool can_allocate_pob(BackendMemReq req, archx::EL current_level) const override { return recycle_tlb_miss; }

private:
    std::uint64_t extract_pc(const Uop& uop) const
    {
        std::uint64_t pc = uop->pc().value();
        return (pc >> 2ULL) & pc_mask;
    }
    std::uint64_t extract_pc_widget(const Uop& uop) const
    {
        std::uint64_t pc = uop->pc().value();
        return (pc >> 2ULL) & pc_mask_widget;
    }

    bool test_bloom_filter_accuracy()
    {
        if (!use_bloom_filter) {
            return true;
        }
        bool ok = false;
        // need to replace this with something more sensible.
        if (1.0 * bfilter_hits / bfilter_inserts > bloom_filter_hit_required_accuracy) {
            ok = true;
        } else if (bfilter_inserts < 40 && bfilter_previous_sufficient_accuracy) {
            // if we previously said the accuracy was sufficient,
            // don't stop prefetching until we have more data
            ok = true;
        }

        if (bfilter_inserts >= 256) { // FIXME: parameterize
            bfilter_previous_sufficient_accuracy = ok;
            bfilter.reset();
            bfilter_hits = 0;
            bfilter_inserts = 0;
        }
        return ok;
    }

    void gen_prefetch(std::uint64_t va, const Uop& uop)
    {
        ScoreWidget& widget = get_score_widget(uop);
        if (widget.best_offset) {
            VirtAddr pref_va(va + widget.best_offset * line_bytes);
            if (check_filter_table_prefetch(extract_pc(uop))) {
                bfilter.insert(pref_va.value());
                bfilter_inserts++;
                if (test_bloom_filter_accuracy()) {
                    if (recent_prefetch_history.lookup(pref_va.value()) == false) { // not recently prefetched
                        BackendMemReq pref(pref_va, {}, uop, this->prefetch_type());
                        pref.exception_level = std::nullopt;
                        if (prefetch_request->can_put()) {
                            prefetch_request->put(pref);
                            recent_prefetch_history.insert(pref_va.value());
                            ev_prefetch_queue_issue(uop, pref_va);
                        } else {
                            overflow(pref);
                        }
                    } else {
                        ev_recent_prefetch_dropped(uop, pref_va);
                    }
                } else {
                    ev_bloom_filter_drop(uop, pref_va);
                }
            } else {
                ev_filter_drop(uop, pref_va);
            }
        } else {
            ev_inactive();
        }
    }

    ScoreWidget& get_score_widget(const Uop& uop)
    {
        size_t widget_id = get_score_widget_id(uop);
        return score_widget[widget_id];
    }
    ScoreWidget& get_score_widget(size_t widget_id) { return score_widget[widget_id]; }
    size_t get_score_widget_id(const Uop& uop) const { return extract_pc_widget(uop); }

    void reset_score_widgets()
    {
        for (auto& w : score_widget) {
            w.start_epoch();
        }
    }

private:
    std::set<size_t> updated_widgets;
    std::set<size_t> new_max_widgets;

public:
    void train_widget(std::uint64_t va, const Uop& uop)
    {
        ScoreWidget& widget = get_score_widget(uop);
        size_t widget_id = get_score_widget_id(uop);
        bool new_max = test_offset_widget(widget, va, uop);
        if (new_max) {
            new_max_widgets.emplace(widget_id); // remember which widgets found a new max this cycle
        }
        updated_widgets.emplace(widget_id); // remember which widgets we used this cycle
    }
    void before_training()
    {
        updated_widgets.clear();
        new_max_widgets.clear();
    }
    void end_of_training()
    {
        for (auto& widget_id : updated_widgets) {
            ScoreWidget& widget = get_score_widget(widget_id);
            bool end_of_rounds = widget.advance_test_offset_pointer();
            bool new_max = (new_max_widgets.contains(widget_id));

            if (end_of_rounds || new_max) {
                std::uint64_t round_ctr{0};
                auto best_offset = widget.pick_new_best_offset(round_ctr);
                if (best_offset) {
                    ev_new_best_offset(ev_offset(best_offset));
                    ev_training_rounds(ev_uint(round_ctr));
                }
            }
        }
    }

    bool test_offset_widget(ScoreWidget& widget, std::uint64_t demand_va, const Uop& uop)
    {
        // test next BOP offset candidate
        // called on misses or hits on prefetched data.
        // updates scores, possibly updates rr table, possibly updates filter table
        const std::uint64_t test_addr = demand_va - offsets[widget.current_test_offset_index] * line_bytes;
        auto rre = recent_request_table.lookup(test_addr, false /*update lru*/); // returns optional of PC sig

        if (rre.has_value()) { // hit
            widget.scores[widget.current_test_offset_index]++;
            if (widget.scores[widget.current_test_offset_index] > widget.max_round_score) {
                widget.max_round_score = widget.scores[widget.current_test_offset_index];
                check_update_filter_table_rr_hit(rre.value());
            }
            if (widget.scores[widget.current_test_offset_index] >= maxscore) {
                return true;
            }
        }
        return false;
    }

    void fill(std::uint64_t rrva, Uop& uop)
    {
        std::uint64_t pcsig = extract_pc(uop);
        auto rre = recent_request_table.lookup(rrva, true /*update_lru*/);
        if (rre.has_value()) {          // hit
            if (rre.value() != pcsig) { // but different PC, so update PC
                recent_request_table.update(rrva, pcsig);
            }
        } else { // miss
            recent_request_table.insert(rrva, pcsig);
        }
    }

    void check_update_filter_table_rr_hit(std::uint64_t pc)
    {
        std::optional<std::uint64_t> hit = filter_table.lookup(pc, /*update_lru*/ true);
        if (hit.has_value()) {
            if (hit.value() >= max_filter_table_count) {
                DBG_L1BOPFLTR("HALVE COUNTERS\n");
                filter_table.halve_counters();
                ev_halve_filter_table();
            }
        } else { // miss
            auto replaced_ctr = filter_table.insert_min_ctr(pc);
            if (replaced_ctr.has_value()) {
                ev_filter_table_replacement(ev_ctr(replaced_ctr.value()));
            }
        }
    }

    bool check_filter_table_prefetch(std::uint64_t pc)
    {
        // return true if hit in filter table (or filter table disabled)
        if (!use_filter_table) {
            return true;
        }
        std::optional<std::uint64_t> hit = filter_table.lookup(pc, /*update_lru*/ true);
        if (hit.has_value()) { // hit
            if (hit.value() > 0) {
                return true;
            }
        }
        // miss, or hit and counter == 0
        return false;
    }

}; // class L1BOP

class L1P_BOP : public L1BOP, implementation<L1P_BOP, BackendPrefetcher<BackendMemReq>>
{
public:
    explicit L1P_BOP(const sc_core::sc_module_name& name);
};

} // namespace ampere::core
