//
// Copyright (c) 2019-2020 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//

#include "ampere/core/prefetchers/l2p_bop.h"
#include "ampere/archx/addr.h"
#include "ampere/archx/page_size.h"
#include "ampere/core/prefetcher.h"
#include "ampere/sc/report.h"
#include "ampere/uarch_common/memop.h"
#include "ampere/uarch_common/memreq.h"
#include <string>

namespace ampere::core {

using namespace uarch_common;

/*************************************************************************
 * Constructor for derived class best-offset prefetcher (BOP)
 *
 *************************************************************************/
L2P_BOP::L2P_BOP(sc_core::sc_module_name name)
    : Prefetcher(name), rr_table(rr_table_size), rr_idxbits(log2(rr_table_size.val())), los(offsets->size())
{
    AMPERE_ASSERT_ERROR(rr_table_size == exp2(rr_idxbits), "BOP table size must be power of two");
    AMPERE_ASSERT_ERROR(!prefetch_second_best || bad_score <= second_bad_score,
                        "Second-best bad score is less than best bad score");

    connect(spatial.prefetch_request, spatial_request).name("spatial_request").latency(0).bandwidth(get_bandwidth());
    connect(nextline.prefetch_request, nextline_request).name("nextline_request").latency(0).bandwidth(get_bandwidth());

    best.offset = force_offset;
}

/*************************************************************************
 * helper functions for the recent requests table.
 *************************************************************************/
unsigned L2P_BOP::rr_tag(uint64_t line_addr) const
{
    return truncate(line_addr >> rr_idxbits, rr_tagbits);
}

unsigned L2P_BOP::rr_index(uint64_t line_addr) const
{
    return truncate(line_addr ^ (line_addr >> rr_idxbits), rr_idxbits);
}

void L2P_BOP::rr_insert(uint64_t line_addr, bool upgraded, MemReq req)
{
    unsigned i = rr_index(line_addr);
    unsigned t = rr_tag(line_addr);

    assert(i < rr_table_size);

    rr_table[i].tag = t;
    rr_table[i].upgraded = upgraded;
    rr_table[i].phys_line_addr = PhysAddr(line_addr);

    ev_rrt_update(req, ev_rrtidx(i), ev_rrttag(t), PhysAddr{line_addr << 6}, ev_rrtupg(upgraded));
}

bool L2P_BOP::rr_hit(uint64_t line_addr) const
{
    unsigned i = rr_index(line_addr);
    unsigned t = rr_tag(line_addr);
    return (rr_table[i].tag == t);
}

// Only for computing precise stats
MemOp::PrefetchType L2P_BOP::prefetch_type() const
{
    return MemOp::PrefetchType::BOP_BEST_OFFSET;
}

// Sizes which aren't explicitly supported just get mapped
// to the next smaller page size:
//
// L2C_PS_4K,       // PS_4K, PS_16K            51:12
// L2C_PS_64K,      // PS_64K                   51:16
// L2C_PS_2M,       // PS_2M, PS_32M            51:21
// L2C_PS_512M      // PS_512M, PS_1G, PS_4T    51:29
PageSize::Size L2P_BOP::consolidated_page_size(MemReq req)
{
    switch (req->page_size) {
    case PageSize::Size::PS_4KB:
        return PageSize::Size::PS_4KB;
    case PageSize::Size::PS_64KB:
        return PageSize::Size::PS_64KB;
    case PageSize::Size::PS_2MB:
        return PageSize::Size::PS_2MB;
    case PageSize::Size::PS_512MB:
    case PageSize::Size::PS_1GB:
    case PageSize::Size::PS_4TB:
        return PageSize::Size::PS_512MB;
    default:
        return PageSize::Size::PS_4KB;
    }
}

// Takes the page size from a memory request and sets the current granule size (for use
// in writing the RR table on fills), and returns the page size boundary we should not
// prefetch across.
PageSize::Size L2P_BOP::resolve_page_size(MemReq req)
{
    if (force_4k_pages) {
        current_granule = PageSize::Size::PS_4KB;
        return PageSize::Size::PS_4KB;
    }

    if (force_64k_pages) {
        current_granule = PageSize::Size::PS_64KB;
        return PageSize::Size::PS_64KB;
    }

    AMPERE_ASSERT_ERROR(req->page_size != PageSize::Size::INVALID, "Saw prefetch train with invalid page size\n");

    // L2C does not actually know the granule size. On LSU requests it gets a two-bit
    // vector which specifies a reduced set of page sizes.
    //
    // All other requesters (ICF, MMUs), and writes into the RR table, don't know the
    // page or granule size and just assume 4KB.
    //
    //
    auto prefetch_boundary = consolidated_page_size(req);
    current_granule = PageSize::Size::PS_4KB;

    // Now look at parameters to figure out whether the boundary can use
    // large pages or just the granule size
    switch (req->origin) {
    case MemOp::ICF:
        return iside_large_page ? prefetch_boundary : current_granule;
    case MemOp::IMMU:
    case MemOp::DMMU:
        return mmu_large_page ? prefetch_boundary : current_granule;
    case MemOp::LSU:
    case MemOp::LSU_PREFETCH:
        return dside_large_page ? prefetch_boundary : current_granule;
    default:
        return PageSize::Size::PS_4KB;
    }
}

bool L2P_BOP::dedicated_set(uint64_t index) const
{
    return (index & dyn_pf_mask) == 0;
}

void L2P_BOP::perform_dyn_pf_adj(MemReq req)
{
    if (!bop_turned_on) {
        if (dyncounter.get_value() < dyn_pf_threshold)
            bop_turned_on = true;
        ev_dyn_pf_shutdown_bop(req, ev_dyncounter(dyncounter.get_value()));
    } else if (dyncounter.get_value() >= dyn_pf_threshold) {
        bop_turned_on = false;
        ev_dyn_pf_toggle_bop(req, ev_dyncounter(dyncounter.get_value()));
    }
}

void L2P_BOP::drop_impl(MemReq req, PrefetchDropReason reason)
{
    switch (req->prefetch_type) {
    case MemOp::PrefetchType::BOP_BEST_OFFSET:
        break;
    case MemOp::PrefetchType::BOP_SECOND_OFFSET:
        ev_drop_sbop(req, PrefetcherData<MemReq>::get_addr(req));
        break;
    case MemOp::PrefetchType::NEXTLINE:
        nextline.drop(req, reason);
        break;
    case MemOp::PrefetchType::SPATIAL:
        spatial.drop(req, reason);
        break;
    case MemOp::PrefetchType::NONE:
    default:
        return;
    }
}

void L2P_BOP::overflow_impl(MemReq req)
{
    switch (req->prefetch_type) {
    case MemOp::PrefetchType::BOP_BEST_OFFSET:
        break;
    case MemOp::PrefetchType::BOP_SECOND_OFFSET:
        ev_overflow_sbop(req, PrefetcherData<MemReq>::get_addr(req));
        break;
    case MemOp::PrefetchType::NEXTLINE:
        nextline.overflow(req);
        break;
    case MemOp::PrefetchType::SPATIAL:
        spatial.overflow(req);
        break;
    case MemOp::PrefetchType::NONE:
    default:
        return;
    }
}

void L2P_BOP::launch_impl(MemReq req)
{
    switch (req->prefetch_type) {
    case MemOp::PrefetchType::BOP_BEST_OFFSET:
        break;
    case MemOp::PrefetchType::BOP_SECOND_OFFSET:
        ev_launch_sbop(req, ev_pf_src(PrefetcherData<MemReq>::get_type(req)), PrefetcherData<MemReq>::get_addr(req));
        break;
    case MemOp::PrefetchType::NEXTLINE:
        nextline.launch(req);
        break;
    case MemOp::PrefetchType::SPATIAL:
        spatial.launch(req);
        break;
    case MemOp::PrefetchType::NONE:
    default:
        return;
    }
}

/*************************************************************************
 * Train function, called by L2 when training should occur.
 *  req: non-prefetch request which caused the lookup that initiated training
 *  prefetch: indicates line hit by lookup was installed by a prefetch
 *  upgrade: indicates the prefetched line hit by lookup was upgraded by a
 *           demand prior to being filled
 *************************************************************************/
void L2P_BOP::train_impl(
    uint64_t index, MemReq req, bool hit, bool prefetch, bool demand, MemOp::PrefetchType prefetch_type)
{
    if (prefetch) {
        dyncounter -= dyn_pf_decr_value;

        switch (prefetch_type) {
        case MemOp::PrefetchType::BOP_BEST_OFFSET:
            break;
        case MemOp::PrefetchType::BOP_SECOND_OFFSET:
            ev_hit_sbop(req, ev_upgraded(demand));
            break;
        case MemOp::PrefetchType::NEXTLINE:
            nextline.ev_hit(req, ev_upgraded(demand));
            break;
        case MemOp::PrefetchType::SPATIAL:
            spatial.ev_hit(req, ev_upgraded(demand));
            break;
        case MemOp::PrefetchType::NONE:
            break;
        }
    }

    // Whether we train may depend on the requester
    switch (req->origin) {
    case MemOp::LSU:
    case MemOp::LSU_PREFETCH:
        break;
    case MemOp::ICF:
        if (prefetch_data_only)
            return;
    case MemOp::IMMU:
    case MemOp::DMMU:
        if (mmu_nextline) {
            nextline.train_impl(req, hit, prefetch, demand);

            while (nextline_request->can_take()) {
                auto sreq = nextline_request->take();
                if (prefetch_request->can_put()) {
                    prefetch_request->put(sreq);
                    ev_nextline_pf(req, sreq->phys_addr);
                }
            }
        }
        if (prefetch_data_only)
            return;
        break;
    case MemOp::PREFETCH:
    case MemOp::L2C:
        AMPERE_ERROR("Unexpected request origin in BOP");
        break;
    default:
        return;
    }

    if (dyn_pf_enable) {
        dyncounterMap[dyncounter.get_value()]++;
        perform_dyn_pf_adj(req);
    }

    auto line_addr = use_virt_addr ? req->virt_addr.line_number() : req->phys_addr.line_number();
    last_train_line = PhysAddr(line_addr);

    if (hit && !prefetch)
        return;

    prefetches_sent = 0;
    PageSize::Size page_size = resolve_page_size(req);

    for (unsigned i = 0; i < train_per_miss; i++) {
        if (use_virt_addr)
            ev_train(req, evd_offset(offsets.val()[los.offset_ptr]), req->virt_addr.line_address(), ev_hitmiss(hit));
        else
            ev_train(req, evd_offset(offsets.val()[los.offset_ptr]), req->phys_addr.line_address(), ev_hitmiss(hit));

        // run the learning algorithm
        learn_best_offset(line_addr, req, page_size);
    }

    // Prefetch from the best offset
    if (best.is_active() && (bop_turned_on || dedicated_set(index)))
        generate_offset_prefetch(req, best, page_size, MemOp::PrefetchType::BOP_BEST_OFFSET);

    // Prefetch from the second-best offset too
    if (prefetch_second_best && second_best.is_active())
        generate_offset_prefetch(req, second_best, page_size, MemOp::PrefetchType::BOP_SECOND_OFFSET);

    // If next-line prefetching is enabled, do that too, if we did not already fetch the previous line
    if (bop_nextline && (!hit || nextline_on_hits) && prefetches_sent < prefetches_per_miss) {
        if (prefetch_request->can_put() && !best.is_nextline() && !(prefetch_second_best && second_best.is_nextline()))
        {
            // Copy the current request to a new one so we can set it to the resolved page size from above
            auto nl_req = make_child<MemReq>(req, *req);

            nextline.train_impl(nl_req, hit, prefetch, demand);

            while (nextline_request->can_take()) {
                auto sreq = nextline_request->take();
                if (prefetch_request->can_put()) {
                    prefetch_request->put(sreq);
                    prefetches_sent++;
                    ev_nextline_pf(req, sreq->phys_addr);
                }
            }
        }
    }

    // Previously we only did a spatial prefetch if the BOP offset was not 1 or -1,
    // but it seems to be better to do it all the time -- though we do avoid
    // doing a spatial prefetch to the same line the BOP just prefetched.
    // Also we do spatial even if BOP did not prefetch anything, because it is
    // disabled or would have crossed a page boundary.
    if (bop_spatial && (!hit || spatial_on_hits) && prefetches_sent < prefetches_per_miss
        && (MemOp::ICF != req->origin || bop_iside_spatial))
    {
        if (prefetch_request->can_put() && !best.is_spatial(req->phys_addr)
            && !(prefetch_second_best && second_best.is_spatial(req->phys_addr))
            && (!bop_nextline || ((req->phys_addr.line_number() & 0x1) != 0)))
        {
            spatial.train_impl(req, hit, prefetch, demand);

            while (spatial_request->can_take()) {
                auto sreq = spatial_request->take();
                if (prefetch_request->can_put()) {
                    prefetch_request->put(sreq);
                    prefetches_sent++;
                    ev_spatial_pf(req, sreq->phys_addr);
                }
            }
        }
    }
}

void L2P_BOP::generate_offset_prefetch(MemReq req,
                                       PickedOffset& offset,
                                       PageSize::Size page_size,
                                       MemOp::PrefetchType type)
{
    auto phys_pf = PhysAddr::from_line_number(offset.next(req->phys_addr.line_number()));
    auto virt_pf = VirtAddr::from_line_number(offset.next(req->virt_addr.line_number()));

    if (phys_pf.same_page(req->phys_addr, page_size)) {
        // If the prefetch is in the same page as the triggering request we can just use the PA
        try_launch_request(req, virt_pf, phys_pf, page_size, type);
    } else if (use_virt_addr) {
        // Otherwise if VA prefetching is enabled we send it to LSU for translation
        translate_prefetch_request->put({virt_pf, req, type});
        ev_bop_pf(req, virt_pf, evd_offset(offset.offset));
        ev_send_va_to_lsu(req, virt_pf);
    } else {
        // Without VA prefetching, we just drop prefetches that cross a page boundary
        ev_page_cross(req, evd_offset(offset.offset));
    }
}

bool L2P_BOP::try_launch_request(
    MemReq req, VirtAddr virt_addr, PhysAddr phys_addr, PageSize::Size page_size, MemOp::PrefetchType prefetch_type)
{
    if (!prefetch_request->can_put())
        return false;

    if (prefetches_sent >= prefetches_per_miss)
        return false;

    // generate and send prefetch request
    auto pref_req = make_child<MemReq>(
        req,
        MemReqObj::prefetch(
            phys_addr.value(), phys_addr, virt_addr, MemOp::Requester::PREFETCH, prefetch_type, page_size));
    prefetch_request->put(pref_req);

    if (pref_req->prefetch_type == MemOp::PrefetchType::BOP_BEST_OFFSET)
        ev_bop_pf(req, pref_req->phys_addr, evd_offset(best.offset));
    else if (pref_req->prefetch_type == MemOp::PrefetchType::BOP_SECOND_OFFSET)
        ev_sbop_pf(req, pref_req->phys_addr, evd_offset(second_best.offset));

    prefetches_sent++;

    return true;
}

int L2P_BOP::find_best_or_second_best(bool exclude_best)
{
    int offset{0};
    unsigned score{0};
    for (uint64_t i = 0; i < los.score.size(); i++) {
        if (exclude_best && offsets.val()[i] == los.best_offset)
            continue;

        if (los.score[i] > score) {
            score = los.score[i];
            offset = offsets.val()[i];
        }
    }

    return offset;
}

void L2P_BOP::check_offsets()
{
    // The best and second-best offsets should never be the same,
    // unless they are both 0.
    AMPERE_ASSERT_ERROR(force_offset || !best.is_active() || best.offset != second_best.offset,
                        "Best and second-best BOP offsets are the same\n");

    // If we're using the tiebreak, then we should always have chosen the offset with the
    // highest score; if there's a tie then we should choose the first one in the offset table.
    if (use_tiebreak) {
        auto check_best_offset = find_best_or_second_best(false /* find best */);
        AMPERE_ASSERT(check_best_offset == los.best_offset,
                      "BOP chose {} but should have chosen {}",
                      los.best_offset,
                      check_best_offset);

        // Second-best is computed the same way as best, excluding the best offset from consideration
        auto check_second_offset = find_best_or_second_best(true /* find second best */);
        AMPERE_ASSERT(check_second_offset == los.second_offset,
                      "SBOP chose {} but should have chosen {}",
                      los.second_offset,
                      check_second_offset);
    }
}

/*************************************************************************
 * The learn_best_offset function tests one offset at a time, trying to
 * determine if the current line would have been successfully prefetched with
 * that offset
 *************************************************************************/
void L2P_BOP::learn_best_offset(uint64_t line_addr, MemReq req, PageSize::Size page_size)
{
    auto test_offset = offsets.val()[los.offset_ptr];
    bool early_out{false}; // short circuit the round and just choose BO now
    auto test_line_addr = line_addr - test_offset;
    max_page_size = std::max<PageSize::Size>(max_page_size, page_size);

    // To evaluate offset X, we see whether the address of the miss minus X is in the
    // recent request table. But for physically-addressed prefetches that only makes
    // sense if addr-X is in the same page as addr, so first check that.
    auto phys_line = PhysAddr(line_addr);
    auto phys_test = PhysAddr(test_line_addr);
    auto same_page = phys_line.cache_lines_in_same_page(phys_test, page_size);

    if ((skip_cross_page_check_at_train || use_virt_addr || same_page) && rr_hit(test_line_addr)) {
        // The current line would likely have been prefetched successfully
        // with that offset, so let's increment the score.
        los.score[los.offset_ptr]++;
        ev_bop_score_update(req, evd_offset(offsets.val()[los.offset_ptr]), evd_new_score(los.score[los.offset_ptr]));

        // Check whether this offset is now the best
        if (los.is_new_best_score(use_tiebreak)) {
            // If we just found a new best, then the former best is now second-best --
            // unless the new best is the same as the old best, which happens whenever
            // the best offset's score goes up.
            if (test_offset != los.best_offset) {
                los.second_offset = los.best_offset;
                los.second_index = los.best_index;
                los.second_score = los.max_score;
            }

            // set the new max
            los.max_score = los.score[los.offset_ptr];
            los.best_offset = test_offset;
            los.best_index = los.offset_ptr;

            // if we have achieved the max score, break out and reset the training
            if (score_max == los.max_score) {
                early_out = true;
            }
        } else if (los.is_new_second_best_score(use_tiebreak)) {
            // If this offset wasn't a new best, then it might be a new second-best.
            los.second_score = los.score[los.offset_ptr];
            los.second_offset = test_offset;
            los.second_index = los.offset_ptr;
        }
    }

    if (los.end_of_round() || early_out) {
        // one round of training is complete
        los.round_num++;

        bool learning_complete = false;

        if (score_max <= los.max_score) {
            learning_complete = true;
            ev_max_score_reset(req,
                               ev_bo(los.best_offset),
                               ev_sbo(los.second_offset),
                               ev_bo_score(los.max_score),
                               ev_sbo_score(los.second_score));
            AMPERE_UNCATEGORIZED_DEBUG("cycle {}: max score, offset {}\n", get_cycle(), los.best_offset);
        }

        if (round_max <= los.round_num) {
            learning_complete = true;
            ev_round_num_reset(req,
                               ev_bo(los.best_offset),
                               ev_sbo(los.second_offset),
                               ev_bo_score(los.max_score),
                               ev_sbo_score(los.second_score));
            AMPERE_UNCATEGORIZED_DEBUG(
                "cycle {}: max rounds, offset {} score {}\n", get_cycle(), los.best_offset, los.max_score);
        }

        if (learning_complete) {
            // learning phase is finished, time to update the prefetch offset
            best = force_offset ? force_offset : los.best_offset;
            second_best = los.second_offset;

            current_max_page_size = max_page_size;
            max_page_size = PageSize::Size::PS_4KB;

            // assertions to check that we computed offsets correctly
            check_offsets();

            if (los.max_score <= bad_score && !force_offset) {
                // accuracy is likely to be very low, so turn off prefetch
                ev_bad_score_shutdown(req);
                best.disable();
            }

            // Separate cutoff for disabling the second-best offset
            if (los.second_score <= second_bad_score) {
                second_best.disable();
            }

            // new learning phase starts
            los.reset();
            return;
        }
    }

    // prepare to test the next offset
    los.next_offset();
}

/*************************************************************************
 * Called when prefetch generated a fill into the cache. fb_pref bit
 * indicates that the request is tagged as prefetched in fb or not.
 * this can be 0 for a line that was hit by a demand request in fb.
 * Here we insert useful entries into the recent request table for BOP.
 *************************************************************************/
void L2P_BOP::fill_impl(uint64_t index, MemReq req, bool fb_pref, bool was_alloc_by_pf)
{
    if (was_alloc_by_pf)
        dyncounter += dyn_pf_incr_value;

    // We only pay attention to non-prefetch fills if the current offset is 0,
    // or if prefetching is throttled due to low accuracy
    if ((insert_demands_only_if_inactive && best.is_active()) && bop_turned_on && !was_alloc_by_pf)
        return;

    if (prefetch_fill_data_only) {
        switch (req->origin) {
        case MemOp::ICF:
        case MemOp::IMMU:
        case MemOp::DMMU:
            return;
        default:
            break;
        }
    }

    // Prefetches should have a prefetch type associated with them, and non-prefetches should not
    AMPERE_ASSERT_ERROR(!was_alloc_by_pf || req->prefetch_type != MemOp::PrefetchType::NONE,
                        "Prefetch had an unknown type");
    AMPERE_ASSERT_ERROR(was_alloc_by_pf || req->prefetch_type == MemOp::PrefetchType::NONE,
                        "Request has a prefetch type but is not a prefetch");

    // grab the line that was filled
    auto line_addr = use_virt_addr ? req->virt_addr.line_number() : req->phys_addr.line_number();

    // Compute the address of the line which spawned this prefetch
    // The calculation of the base address depends on which of the 4 possible prefetchers
    // originated the prefetch which just completed.
    auto base_line_addr = line_addr;
    switch (req->prefetch_type) {
    case MemOp::PrefetchType::BOP_BEST_OFFSET:
        base_line_addr = best.remove_offset(line_addr);
        break;
    case MemOp::PrefetchType::BOP_SECOND_OFFSET:
        ev_fill_sbop(req, ev_upgraded(was_alloc_by_pf && !fb_pref));
        base_line_addr = second_best.remove_offset(line_addr);
        break;
    case MemOp::PrefetchType::SPATIAL:
        spatial.fill(index, req, fb_pref, was_alloc_by_pf);
        base_line_addr = line_addr ^ 0x1;
        break;
    case MemOp::PrefetchType::NEXTLINE:
        nextline.fill(index, req, fb_pref, was_alloc_by_pf);
        base_line_addr = line_addr - 1;
        break;
    case MemOp::PrefetchType::NONE:
        // This case is only used when the current offset is 0, and we are just writing
        // all fills into the RR table
        base_line_addr = line_addr;
        break;
    }

    // Insert the base address into the recent-request table, as long as
    // it is within the same page. For fills we no longer have the page
    // size associated with the original request, so just use the current
    // page granule size.
    auto page_size = current_granule;
    if (use_max_page_size_at_insertion)
        page_size = current_max_page_size;

    auto phys_line = PhysAddr(line_addr);
    auto phys_base = PhysAddr(base_line_addr);
    if (use_virt_addr || skip_cross_page_check_at_insertion || phys_line.cache_lines_in_same_page(phys_base, page_size))
    {
        rr_insert(base_line_addr, was_alloc_by_pf && !fb_pref, req);
    }
}

/*************************************************************************
 * Debug function for L2P watch window
 * Combines the offset table with the recent request table
 *************************************************************************/
void L2P_BOP::table_print(std::ostream& outstream) const
{
    unsigned idx{0};

    fmt::print(outstream,
               "\ncurr_pf_offset = {}, second_pf_offset = {}, round_num/max = {}/{}, last train = {}\n",
               best.offset,
               second_best.offset,
               los.round_num,
               round_max,
               last_train_line);

    fmt::print(outstream, "---------------+---------+-------------------------------------\n");
    fmt::print(outstream, "offsets TABLE  |         |  RECENT REQUEST TABLE\n");
    fmt::print(outstream, "               | <-- --> |            \n");

    fmt::print(outstream,
               "{1:>4} | {2:>7} | {3:>7} | {4:>11} | {5:3} | {6:>12}\n",
               9,
               "os",
               "score",
               "os/idx",
               "phys_line",
               "tag",
               "delta from last");

    fmt::print(outstream, "-----+---------+---------+-------------+-----+-----------------\n");

    for (const RREntry& rr_entry : rr_table) {
        std::string s_offset("-"), s_score("-"), s_best("    "), s_arrow("   ");

        if (idx < offsets->size()) {
            s_offset = std::to_string(offsets->at(idx));
            s_score = std::to_string(los.score[idx]);
            if (offsets->at(idx) == los.best_offset) {
                if (offsets->at(idx) == los.second_offset)
                    s_best = "<BS>"; // pointer to current best offset
                else
                    s_best = "<B> ";
            } else if (offsets->at(idx) == los.second_offset) {
                s_best = "<S> ";
            }

            if (idx == los.offset_ptr) {
                s_arrow = "<*>"; // pointer to current testing offset
            }
        }

        fmt::print(outstream,
                   "{1:>4} | {2:>3}{3:>3} | {4:>3}{5:>4} | {6:>11} | {7:>3x} | {8:>12x}\n",
                   9,
                   s_offset,
                   s_best,
                   s_score,
                   s_arrow,
                   idx,
                   rr_entry.phys_line_addr,
                   rr_entry.tag,
                   last_train_line - rr_entry.phys_line_addr);

        idx++;
    }
    fmt::print(outstream, "\n");
}

void L2P_BOP::table_print() const
{
    table_print(std::cout);
}

void L2P_BOP::dump(std::ostream& os) const
{
    fmt::print(os, "= L2P_BOP ==================================================\n");
    table_print(os);
    fmt::print(os, "============================================================\n");
    if (dyn_pf_enable) {
        for (const auto& [key, value] : dyncounterMap) {
            fmt::print(os, " {} => {}\n", key, value);
        }
    }
}

} // namespace ampere::core

/*
 * vim: sw=4:ts=4:et:si:cindent:cino=\:0,(0,g0,t0:
 */
