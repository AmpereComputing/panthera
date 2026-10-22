//
// Copyright (c) 2019 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//

#include "ampere/core/prefetchers/l2p_spp.h"
#include "ampere/uarch_common/memop.h"
#include "ampere/uarch_common/memreq.h"

namespace ampere::core {

// Updates the pattern table entry.
std::optional<PtEntry::DtEntry> PtEntry::update_index(int d)
{
    uint32_t lowest_prob = UINT32_MAX;
    int lowest_prob_index = 0;

    // Loop through all stored deltas looking for match
    for (unsigned i = 0; i < deltas.size(); i++) {
        /* If delta matches we found an existing pattern, increase the
         * probability scores */
        if (deltas[i].delta == d) {
            deltas[i].c_delta++;
            c_sig++;

            /* Handle overflow by decreasing all counters equally if any are
               about to overflow */
            if (deltas[i].c_delta >= 15 || c_sig >= 15) {
                for (unsigned k = 0; k < deltas.size(); k++) {
                    deltas[k].c_delta >>= 1;
                }
                c_sig >>= 1;
            }
            return std::nullopt;
        }

        /*  This is an small optimization that prevents us from looping over
         *  the delta index twice if we don't find an entry that matches our
         *  delta.  We track which entry we should replace here and do the
         *  replacement after this loop if needed. */
        if (deltas[i].c_delta < lowest_prob) {
            lowest_prob = deltas[i].c_delta;
            lowest_prob_index = i;
        }
    }

    /* We did not find an delta entry in pattern table, replace the least
       probability delta of an accurate prefetch */
    DtEntry delta_victim = deltas[lowest_prob_index];
    deltas[lowest_prob_index].delta = d;
    deltas[lowest_prob_index].c_delta = 1;
    c_sig++;
    /* Handle overflow by decreasing all counters equally if any are
       about to overflow */
    if (c_sig >= 15) {
        for (unsigned k = 0; k < deltas.size(); k++) {
            deltas[k].c_delta >>= 1;
        }
        c_sig >>= 1;
    }

    return std::optional<DtEntry>{delta_victim};
}

// Returns the delta with the largest confidence of being correct.
std::pair<int32_t, float> PtEntry::get_delta()
{
    uint32_t cd = 0;
    int32_t d = 0;
    float confidence = 0;

    for (unsigned i = 0; i < deltas.size(); i++) {
        if (deltas[i].c_delta > cd) {
            cd = deltas[i].c_delta;
            d = deltas[i].delta;
        }
    }
    if (c_sig != 0) {
        confidence = (float)cd / c_sig;
    } else {
        confidence = 0;
    }

    return {d, confidence};
}

/* Calculate a new compressed representation of the signature pattern from an
 * older signature pattern.  */
uint64_t L2P_SPP::calc_signature(uint64_t old_sig, int32_t delta)
{
    uint64_t signature_mask = (1 << signature_size) - 1;
    return ((old_sig << signature_shift) ^ delta) & signature_mask;
}

void L2P_SPP::prefetch(MemReq req, uint64_t signature, float probability = 1)
{
    PhysAddr pref_addr = req->phys_addr;
    int cross_page_cnt = 0;

    while (true) {
        PtEntry pt_entry = pattern_table[signature];
        auto delta = pt_entry.get_delta();
        ev_pattern_table_access(req);

        /* Probability of prefetch is a product of previous prefetches.  We get
           less sure the farther we prefetch into the future */
        probability *= delta.second;

        if (alpha.alpha() * probability > threshold) {
            if (prefetch_request->can_put()) {
                PhysAddr page_offset{get_offset(pref_addr)};
                pref_addr += (delta.first << line_bits);

                /* Crossing page, we can't prefetch this address since we
                   do not yet know the correct physical page.  However, we
                   store it in global history buffer for quick learning. */
                // if (!pref_addr.same_page_4k(req->phys_addr)) {
                if (get_page_number(pref_addr) != get_page_number(req->phys_addr)) {
                    GhEntry gh_entry{.signature = signature,
                                     .path_confidence = probability,
                                     .last_offset = (int32_t)(page_offset.value()
                                                              - (1 << (signature_table.get_line_bits() - line_bits))),
                                     .delta = (int32_t)(delta.first)};
                    gh_buffer.push_back(gh_entry);
                    ev_cross_page_predict(req);

                    /* Prevent endless prefetch loop where we always cross pages and
                     * we continue with the same prefetch signature.  Only allow
                     * 3 page crossings max. */
                    if (cross_page_cnt++ >= max_page_crossings) {
                        ev_page_cross_prevent(req);
                        break;
                    }
                } else {
                    /* Lookup prefetch address in our recently prefetch cache
                     * filter and do not prefetch if we find this in the queue.
                     * This prevents the prefetcher from sending the same address
                     * multiple times.  */
                    if (auto entry = prefetch_filter.lookup(req, pref_addr)) {
                        if (entry->valid) {
                            // We already prefetched this address, move to next prediction
                            ev_filter_prevent(req);
                            continue;
                        } else {
                            // Reset existing invalidated entry
                            entry->valid = true;
                            entry->useful = false;
                        }
                    } else {
                        // Add to prefetch filter, to prevent prefetching address in future
                        prefetch_filter.refill(req, pref_addr, PfEntry());
                    }

                    // Send prefetch request to L2 cache
                    auto prefetch_req = make_child<MemReq>(
                        req,
                        MemReqObj::prefetch(
                            pref_addr.value(), pref_addr, uarch_common::MemOp::PREFETCH, prefetch_type()));
                    prefetch_request->put(prefetch_req);
                    alpha.increment_total();
                    ev_prefetch(req);
                }

                // Update signature to allow continuation of prefetching further
                signature = calc_signature(signature, delta.first);
            } else {
                ev_limited_by_l2c(req);
                break;
            }
        } else {
            ev_threshold_low(req);
            break;
        }
    }
}

/* Called from L2 when we can perform prefetcher training and perform prefetch
 * requests. */
void L2P_SPP::train_impl(MemReq req, bool hit, bool prefetch, bool demand)
{
    // ignore i-side requests for now
    switch (req->origin) {
    case MemOp::Requester::LSU:
    case MemOp::Requester::LSU_PREFETCH:
    case MemOp::Requester::DMMU:
    case MemOp::Requester::L2C:
    case MemOp::Requester::PREFETCH:
        break;
    default:
        return;
    }

    /* If this address matches something in our preftch filter, remove it */
    if (auto entry = prefetch_filter.lookup(req, req->phys_addr, false, true)) {
        if (entry->valid && !entry->useful) {
            entry->useful = true;
            alpha.increment_useful();
        }
    }

    /* Lookup page in signature cache table.  If it exists we already have a
       recognizable pattern.  We need to update the signature. */

    // If page found with a signature pattern, use this one
    auto st_entry = signature_table.lookup(req, req->phys_addr);

    /* If we don't have a signature pattern for page address in the cache we
       first search the global history table to see if we can associate the
       new page with an existing pattern signature. */
    if (!st_entry) {
        /* Search GHR to see if we are continuing a recognized pattern signature
           across a different physical page.  If so populate signature table in
           st entry to skip training */
        PhysAddr page_offset{get_offset(req->phys_addr)};
        for (unsigned i = 0; i < gh_size; i++) {
            GhEntry gh_entry = gh_buffer[i];
            int32_t diff = gh_entry.last_offset + gh_entry.delta;
            diff = (diff % 64 + 64) % 64;

            if ((uint32_t)diff == page_offset.value()) {
                // Create a St Entry with this signature and resume prefetching without training
                StEntry resume_entry;
                resume_entry.last_offset = (uint32_t)diff;
                resume_entry.valid = true;
                resume_entry.signature = calc_signature(gh_entry.signature, gh_entry.delta);
                signature_table.refill(req, req->phys_addr, resume_entry);
                ev_cross_page_resume(req);
                L2P_SPP::prefetch(req, resume_entry.signature);
                return;
            }
        }

        /* Page not found in signature pattern.  We also didn't match any known
           entries in the global history buffer.  Create new entry in the signature
           table replacing LRU */
        StEntry new_entry;
        new_entry.signature = get_offset(req->phys_addr);
        new_entry.last_offset = 0;
        new_entry.valid = true;
        signature_table.refill(req, req->phys_addr, new_entry);
        st_entry = signature_table.lookup(req, req->phys_addr, false, true);
    }

    uint32_t offset = get_offset(req->phys_addr);
    int32_t delta = ((int32_t)offset - (int32_t)st_entry->last_offset);

    // Don't fill our tables with stride 0 prefetches
    if (delta == 0) {
        ev_same_line_prevent(req);
        return;
    }

    // Update Pattern table probabilities
    PtEntry &pt_entry = pattern_table[st_entry->signature];
    auto dt_victim = pt_entry.update_index(delta);
    if (dt_victim) {
        ev_pattern_table_refill(req);
        if (dt_victim->c_delta > 0) {
            ev_pattern_table_evict(req);
        }
    }

    // Update the signature with new offset
    st_entry->signature = calc_signature(st_entry->signature, delta);
    st_entry->last_offset = offset;

    // Start the Prefetches
    L2P_SPP::prefetch(req, st_entry->signature);

    // Composite with a spatial fallback prefetch
    if (fb_spatial) {
        if (!hit) {
            if (prefetch_request->can_put()) {
                auto pref_addr = PhysAddr{req->phys_addr.value() ^ 64};
                auto prefetch_req = make_child<MemReq>(req, pref_addr.value(), pref_addr, MemOp::Requester::PREFETCH);
                prefetch_request->put(prefetch_req);
                ev_fb_prefetch_spatial(req);
            }
        }
    }
}

void L2P_SPP::evict_impl(PhysAddr pa, bool prefetch, bool demand)
{
    // TODO: Need to create my own class with eviction, for now this is a
    // hack/workaround
    Txn transaction = make<Txn>();

    if (auto entry = prefetch_filter.lookup(transaction, pa, false, true)) {
        ev_filter_evict();
        entry->valid = false;
    }
}

void L2P_SPP::drop_impl(MemReq req, PrefetchDropReason reason)
{
    /* When an address is dropped from L2 we need to remove it from our tracking
     * queue.  This address is also marked as useful to allow proper training.
     * Example we predicted a prefetch of a cache line that was already in L2
     * because it was brought in for a store. */
    if (auto entry = prefetch_filter.lookup(req, req->phys_addr, false, true)) {
        ev_filter_drop_l2p();
        alpha.increment_useful();
    }
}

void L2P_SPP::overflow_impl(MemReq req)
{
    /* If we overflows the L2 fifo we need to remove the overflow prefetch from
     * our tracking queue. */
    if (auto entry = prefetch_filter.lookup(req, req->phys_addr, false, true)) {
        entry->valid = false;
        ev_filter_overflow_l2p();
        alpha.decrement_total();
    }
}

} // namespace ampere::core

