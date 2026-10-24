//
// Copyright (c) 2019 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//

#ifndef AMPERE_CORE_L2P_BOP_H
#define AMPERE_CORE_L2P_BOP_H

#include "ampere/archx/page_size.h"
#include "ampere/core/prefetcher.h"
#include "ampere/sc/sc.h"
#include "ampere/sc/submodule.h"
#include "ampere/archx/addr.h"
#include "ampere/core/prefetchers/l2p_spatial.h"
#include "ampere/core/prefetchers/l2p_nextline.h"

namespace ampere::core {

using namespace ampere::sc;
using namespace ampere::archx;

/* BOP: Best Offset Prefetcher
 * Based on the work of Pierre Michaud
 * https://hal.inria.fr/hal-01254863/document
 */

class L2P_BOP : public Prefetcher<MemReq>, implementation<L2P_BOP, Prefetcher<MemReq>>
{
public:
    explicit L2P_BOP(sc_core::sc_module_name name);

protected:
    class L2P_BOP_Spatial : public L2P_Spatial
    {
    public:
        using L2P_Spatial::L2P_Spatial;
        using L2P_Spatial::train_impl;
    };
    L2P_BOP_Spatial spatial{"spatial"};

    class L2P_BOP_NextLine : public L2P_NextLine
    {
    public:
        using L2P_NextLine::L2P_NextLine;
        using L2P_NextLine::train_impl;
    };
    L2P_BOP_NextLine nextline{"nextline"};

    void train_impl(
        uint64_t index, MemReq req, bool hit, bool prefetch, bool demand, MemOp::PrefetchType prefetch_type) override;
    void fill_impl(uint64_t index, MemReq req, bool fb_pref, bool was_alloc_by_pf) override;
    void launch_impl(MemReq req) override;
    void drop_impl(MemReq req, PrefetchDropReason reason) override;
    void overflow_impl(MemReq req) override;
    MemOp::PrefetchType prefetch_type() const override;
    void perform_dyn_pf_adj(MemReq req);

private:
    struct PickedOffset
    {
        int offset{0}; // 7 bits (6-bit value + 1 sign bit)

        PickedOffset() = default;
        PickedOffset(int offset) : offset(offset) {}
        void disable() { offset = 0; }
        bool is_active() const { return offset != 0; }
        bool is_nextline() const { return offset == 1; }
        bool is_spatial(PhysAddr addr) const
        {
            return (addr.line_number() & 0x1) == 0 ? (offset == 1) : (offset == -1);
        }
        uint64_t next(uint64_t line_addr) const { return line_addr + offset; }
        uint64_t remove_offset(uint64_t line_addr) const { return line_addr - offset; }
    };

    void table_print(std::ostream& os) const;
    void table_print() const;
    void dump(std::ostream& os) const override;
    void learn_best_offset(uint64_t line_addr, MemReq req, PageSize::Size page_size);
    static PageSize::Size consolidated_page_size(MemReq req);
    PageSize::Size resolve_page_size(MemReq req);
    void generate_offset_prefetch(MemReq req, PickedOffset& offset, PageSize::Size page_size, MemOp::PrefetchType type);
    bool try_launch_request(MemReq req,
                            VirtAddr virt_addr,
                            PhysAddr phys_addr,
                            PageSize::Size page_size,
                            MemOp::PrefetchType prefetch_type);
    void check_offsets();
    int find_best_or_second_best(bool exclude_best);

    In<MemReq> spatial_request{"spatial_request_in"};
    In<MemReq> nextline_request{"nextline_request_in"};

    PhysAddr last_train_line{0}; // instrumentation
    PageSize::Size current_granule{PageSize::Size::PS_4KB};
    PageSize::Size current_max_page_size{PageSize::Size::PS_4KB};
    PageSize::Size max_page_size{PageSize::Size::PS_4KB};

    /* Default offsets: Because prefetch cannot cross 4KB-page boundaries, there
     * is no need to consider offsets greater than 63. However, with pages
     * larger than 4KB, it would be beneficial to consider larger offsets. We
     * don't do that here. 32 entries * 7 bits = 224 bits of static state.
     *
     * Note that this table is sorted in priority order, so a lower-indexed offset
     * will win if scores are tied.
     */
    static inline const std::vector<int> DEFAULT_OFFSETS = {1,  2,  3,  4,  5,  6,  7,  8,   9,   10, 11,
                                                            12, 13, 14, 15, 16, 18, 20, 24,  28,  30, 32,
                                                            36, 40, -1, -2, -3, -4, -5, -12, -18, -44};

    /*
     * PREFETCHER PARAMETERS
     */

    Param<bool> dyn_pf_enable{
        "dyn_pf_enable", "disable prefetcher upon seeing accuracy lower than a threshold for specific sets", false};
    Param<unsigned> dyn_pf_incr_value{
        "dyn_pf_incr_value", "increment the dynamic counter by this value on a prf fill", 12};
    Param<unsigned> dyn_pf_decr_value{
        "dyn_pf_decr_value", "decrement the dynamic counter by this value on a prf hit", 32};
    Param<unsigned> dyn_pf_threshold{
        "dyn_pf_threshold", "threshold for accuracy based adjustment to run prefetching off", 4095};
    Param<uint64_t> dyn_pf_mask{"dyn_pf_mask", "Mask of set bits used for policy selection", 0x7f};
    Param<unsigned> initial_dyncounter{"initial_dyncounter", "Reset value of the dynamic accuracy counter", 0};
    Param<unsigned> max_dyncounter{"max_dyncounter", "Saturation value of dynamic accuracy counter", 65535};

    Param<std::vector<int>> offsets{
        "offsets",
        "Offsets to use. Table is in priority order, so lower-indexed offset will win if scores are tied.",
        DEFAULT_OFFSETS};

    Param<size_t> rr_table_size{"rr_table_size", "size of the recent requests table", 128};
    Param<unsigned> rr_tagbits{"rr_tagbits", "number of bits to use for tag", 12};
    Param<unsigned> score_max{"score_max", "max score allowed in learning before resetting", 31};
    Param<unsigned> round_max{"round_max", "max rounds in learning before resetting", 127};
    Param<unsigned> bad_score{"bad_score", "turn off prefetcher if max_score is below this threshold", 2};
    Param<unsigned> second_bad_score{
        "second_bad_score", "turn off prefetch of second-best offset if max_score is below this threshold", 7};
    Param<bool> use_tiebreak{
        "use_tiebreak", "break ties among offsets by preferring an offset with a lower index in the table", false};

    Param<bool> force_4k_pages{"force_4k_pages", "Force BOP to assume 4k pages for page boundary comparisons", false};
    Param<bool> force_64k_pages{
        "force_64k_pages", "Force BOP to assume 64k pages for page boundary comparisons", false};

    Param<bool> skip_cross_page_check_at_train{
        "skip_cross_page_check_at_train", "Skip checking for page-crossing at train", false};
    Param<bool> skip_cross_page_check_at_insertion{
        "skip_cross_page_check_at_insertion", "Skip checking for page-crossing at RRT insertion", false};
    Param<bool> use_max_page_size_at_insertion{
        "use_max_page_size_at_insertion",
        "Take max page-size seen while training and use for RRT insertion granularity",
        false};
    Param<bool> insert_demands_only_if_inactive{
        "insert_demands_only_if_inactive", "Only insert demand fills into RRT if BOP is inactive", true};

    Param<bool> iside_large_page{"iside_large_page", "ICF sends large-page indication for prefetching", false};
    Param<bool> mmu_large_page{"mmu_large_page", "MMUs send large-page indication for prefetching", false};
    Param<bool> dside_large_page{"dside_large_page", "LSU sends large-page indication for prefetching", true};

    Param<unsigned> train_per_miss{"train_per_miss", "Number of offsets to train on per L2 miss", 1};
    Param<bool> prefetch_second_best{"prefetch_second_best", "Prefetch from the second-best offset also", false};
    Param<bool> prefetch_data_only{
        "prefetch_data_only", "Only train BOP and trigger prefetches on LSU requests", false};
    Param<bool> prefetch_fill_data_only{
        "prefetch_fill_data_only", "Only insert LSU (or prefetch) fill requests into BOP RRT", false};
    Param<bool> bop_iside_spatial{"bop_iside_spatial", "BOP trains and issues on ICF requests", true};
    Param<bool> bop_spatial{"bop_spatial", "BOP also issues spatial prefetches", false};
    Param<bool> bop_nextline{"bop_nextline", "BOP also issues next-line prefetches", false};
    Param<bool> mmu_nextline{"mmu_nextline", "Do next-line prefetches for MMU requests", false};
    Param<bool> spatial_on_hits{"bop_spatial_on_hits", "Do spatial prefetches on hits to prefetched lines", true};
    Param<bool> nextline_on_hits{"bop_nextline_on_hits", "Do nextline prefetches on hits to prefetched lines", true};
    Param<unsigned> prefetches_per_miss{
        "prefetches_per_miss", "Max number of prefetches that can be spawned by a single miss", 4};
    Param<bool> use_virt_addr{"use_virt_addr",
                              "Operate on VAs instead of PAs, and send page-crossing prefetches to LSU for translation",
                              false};
    Param<int> force_offset{"force_offset", "If non-zero, always prefetch with this offset", 0};

    Event ev_max_score_reset{"reset_maxscore", "BOP table reset: one offset hit max_score"};
    Event ev_round_num_reset{"reset_maxrounds", "BOP table reset: N rounds learning completed"};
    Event ev_bad_score_shutdown{"bad_score_shutdown", "BOP turned off: low scores in training"};
    Event ev_page_cross{"page_cross", "prefetch not issued since it crossed a page boundary"};
    Event ev_rrt_update{"rrt_update", "fill writes back and recent-request table is updated"};
    Event ev_train{"train", "BOP was trained"};
    Event ev_spatial_pf{"trigger_spatial", "BOP sent out a basic spatial prefetch"};
    Event ev_nextline_pf{"trigger_nextline", "BOP sent out a next-line prefetch"};
    Event ev_bop_pf{"trigger_bop", "BOP sent out a BO prefetch"};
    Event ev_sbop_pf{"trigger_sbop", "BOP sent out an SBO prefetch"};
    Event ev_send_va_to_lsu{"send_va_to_lsu", "BOP or SBOP sent a VA prefetch to LSU for translation"};

    Event ev_hit_sbop{"sbop_hit", "SBOP Prefetched line was hit"};
    Event ev_fill_sbop{"sbop_fill", "SBOP Prefetched line resulted in a fill"};
    Event ev_drop_sbop{"sbop_drop", "SBOP Prefetched line was superseded by pre-existing demand request or line"};
    Event ev_overflow_sbop{"sbop_overflow", "SBOP Prefetcher queue lost oldest existing prefetch prior to scheduling"};
    Event ev_launch_sbop{"sbop_launch", "SBOP Prefetch was launched"};
    Event ev_dyn_pf_toggle_bop{
        "dyn_pf_toggle_bop",
        "number of toggles in BOP shut down due to low accuracy with dynamic prefetching adjustment"};
    Event ev_dyn_pf_shutdown_bop{
        "dyn_pf_shutdown_bop",
        "number of prefetch trains that BOP was shut down due to low accuracy with dynamic prefetching adjustment"};
    Event ev_bop_score_update{"bop_score_update", "offset's score increased as a result of training"};

    EventData<int> ev_bo{"best_offset", "New best offset"};
    EventData<int> ev_sbo{"second_offset", "New second-best offset"};
    EventData<unsigned> ev_bo_score{"best_score", "Score of new best offset"};
    EventData<unsigned> ev_sbo_score{"second_best_score", "Score of new second-best offset"};
    EventData<bool> ev_hitmiss{"train_by_hit", "BOP trained by hit to a prefetched line"};
    EventData<bool> ev_rrtupg{"rrt_upgraded", "BOP RRT insert was upgraded"};
    EventData<unsigned> ev_rrtidx{"rrt_index", "BOP index into recent request table"};
    EventData<unsigned> ev_rrttag{"rrt_tag", "BOP tag into recent request table"};
    EventData<unsigned> ev_dyncounter{"dyncounter", "BOP tag into recent request table"};
    EventData<int> evd_offset{"offset", "offset being used or trained"};
    EventData<int> evd_new_score{"new_score", "new score of offset which just increased"};

    bool bop_turned_on{true};
    SaturatingCounter dyncounter{0, max_dyncounter.val(), initial_dyncounter.val()};
    std::map<uint64_t, unsigned> dyncounterMap;
    unsigned prefetches_sent{0};

    /*
     * PREFETCHER STATE
     */
    PickedOffset best;
    PickedOffset second_best;

    // Recent Requests (RR) table
    struct RREntry; // 64x12 = 768 bits for 64 entry RR XXX plus one for upgrade
    std::vector<RREntry> rr_table;
    const unsigned rr_idxbits;

    // Learning Offset table
    struct learning_offset_scores
    {
        std::vector<unsigned> score; // log2 score_max = 5 bits per entry
        unsigned max_score{0};       // log2 score_max = 5 bits
        unsigned second_score{0};
        int best_offset{0}; // 7 bits (6-bit value + 1 sign bit)
        int second_offset{0};
        unsigned best_index{0};   // index in score table of best offset
        unsigned second_index{0}; // index in score table of second-best offset
        unsigned round_num{0};    // log2 round_max = 7 bits
        unsigned offset_ptr{0};   // log2 NUM_OFFSETS = 5 bits

        explicit learning_offset_scores(size_t offsets) : score(offsets, 0) {}

        bool is_new_best_score(bool use_tiebreak)
        {
            // If the new score is better, just take it
            if (score[offset_ptr] > max_score)
                return true;

            // If tiebreaks are enabled, prefer the first entry in the offset table. The table is sorted
            // to favor smaller offsets over larger ones, and positive over negative.
            if (use_tiebreak && score[offset_ptr] == max_score && offset_ptr < best_index)
                return true;

            return false;
        }

        bool is_new_second_best_score(bool use_tiebreak)
        {
            if (use_tiebreak) {
                // Break ties by preferring the first entry in the offset table.
                if (score[offset_ptr] > second_score)
                    return true;
                if (score[offset_ptr] == second_score && offset_ptr < second_index)
                    return true;
            } else {
                // Without tiebreak, take a new second best if it is better than the current second best, and
                // its score is less than the current best. That second part is a bug which means the offset we
                // choose is not always, in fact, the second best.
                if (score[offset_ptr] > second_score && score[offset_ptr] < max_score)
                    return true;
            }

            return false;
        }

        void next_offset()
        {
            offset_ptr++;
            if (offset_ptr == score.size())
                offset_ptr = 0;
        }

        bool end_of_round() const { return offset_ptr == (score.size() - 1); }

        void reset()
        {
            std::fill(score.begin(), score.end(), 0);
            max_score = 0;
            second_score = 0;
            best_offset = 0;
            second_offset = 0;
            best_index = 0;
            second_index = 0;
            round_num = 0;
            offset_ptr = 0;
        }
    } los; // 32x5+5+7+7+5 = 184 bits

    /*
     * Total prefetcher SRAM: 7 +  768 + 184 =  959 bits for  64 entry RR
     *                        7 + 1536 + 184 = 1727 bits for 128 entry RR
     */

    /*
     * HELPER FUNCTIONS
     */
    unsigned rr_tag(uint64_t line_addr) const;
    unsigned rr_index(uint64_t line_addr) const;
    void rr_insert(uint64_t line_addr, bool upgraded, MemReq req);
    bool rr_hit(uint64_t line_addr) const;

    bool dedicated_set(uint64_t index) const;

    unsigned truncate(uint64_t x, unsigned nbits) const { return (x & ((1 << nbits) - 1)); }
};

// Recent Request Entry
struct L2P_BOP::RREntry
{
    archx::PhysAddr phys_line_addr{0}; // line address of insertion, just for observability
    unsigned tag{0};                   // higher bits of address past the index (in hardware)
    bool upgraded;                     // indicates if the prefetch was promoted by demand request
};

} // namespace ampere::core
#endif // AMPERE_CORE_L2P_BOP_H

/*
 * vim: sw=4:ts=4:et:si:cindent:cino=\:0,(0,g0,t0:
 */
