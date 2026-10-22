//
// Copyright (c) 2019 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//

#ifndef AMPERE_CORE_L2P_SPP_H
#define AMPERE_CORE_L2P_SPP_H

#include "ampere/uarch_common/cache.h"
#include "ampere/core/prefetcher.h"
#include "ampere/sc/sc.h"
#include "ampere/sc/submodule.h"
#include <stdint.h>

namespace ampere::core {

using namespace ampere::sc;

// Represents a pattern table entry.  Each entry is 48 bits
class PtEntry
{
public:
    struct DtEntry
    {
        int32_t delta;
        uint32_t c_delta;
    };

    explicit PtEntry(int d_size) : deltas(d_size), c_sig{0} {}
    std::optional<DtEntry> update_index(int delta);
    std::pair<int32_t, float> get_delta();

private:
    std::vector<DtEntry> deltas; // (7 bits + 4 bits) * delta_cnt
    uint8_t c_sig;               // 4 bits
};

class SppAlpha
{
public:
    SppAlpha(unsigned size) : counter_max(size) {}
    void increment_useful()
    {
        c_useful++;
        check_alpha();
    }
    void decrement_useful()
    {
        c_useful--;
        check_alpha();
    }
    void increment_total()
    {
        c_total++;
        check_alpha();
    }
    void decrement_total()
    {
        c_total--;
        check_alpha();
    }
    float alpha() { return ((float)c_useful) / c_total; }

private:
    // Accuracy Counters, used to calculate alpha
    unsigned c_total = 1;
    unsigned c_useful = 1;
    unsigned counter_max;

    /* Alpha is a measurement of how useful the prefetcher is currently behaving.
     * whenever increasing c_useful or c_total, check_alpha must be called to
     * prevent wrapping. */
    void check_alpha()
    {
        if (c_useful > counter_max || c_total > counter_max) {
            c_useful >>= 1;
            c_total >>= 1;
        }
        // Don't divide my zero
        if (c_total < 1) {
            c_total = 1;
        }
    }
};

class L2P_SPP : public Prefetcher<MemReq>, implementation<L2P_SPP, Prefetcher<MemReq>>
{
public:
    explicit L2P_SPP(sc_core::sc_module_name name) : Prefetcher<MemReq>(name) {}

private:
    const size_t line_bits{6};
    /* Threshold from 0 - 1.0 that is the probability needed from a prefetch to
     * actually send the request to the L2 */
    Param<float> threshold{"threshold",
                           "Threshold confidence we must be above"
                           " when issuing prefetch",
                           0.0};

    // signature size holds a compact form of delta cache line access
    Param<unsigned> signature_size{"signature_size", "Size of compress pattern signature", 6};

    /* A larger signature_shift stores less history per pattern, A smaller signature_shift stores
     * more history but some aliasing will occur.  This history is less accurate */
    Param<unsigned> signature_shift{"signature_shift", "Amount to compress signature", 3};

    // Number elements used to track prefetches across physical pages.
    Param<unsigned> gh_size{"gh_size", "Size of the Global History Register (entries)", 8};

    // Number of deltas to track per signature.
    Param<unsigned> delta_cnt{"delta_cnt", "Number of different stride deltas to track per signature", 2};

    /* We issue a spatial prefetch if there is room in the port yet SPP falls
     * below threshold confidence to use its own tables. */
    Param<bool> fb_spatial{"fb_spatial", "Enable spatial as a fallback", false};

    Param<int> max_page_crossings{
        "max_page_crossings", "Maximum page crossings allowed to be preloaded into global history buffer", "3"};

    Event ev_pattern_table_access{"pattern_table_access", "Pattern table accessed"};
    Event ev_pattern_table_refill{"pattern_table_refill", "New delta added for signature"};
    Event ev_pattern_table_evict{"pattern_table_evict", "Active delta evicted to fill new delta for signature"};

    Event ev_fb_prefetch_spatial{"spatial_pf",
                                 "We issued a spatial prefetch instead"
                                 "of a SPP prefetch because SPP fell below confidence threshold"};

    Event ev_cross_page_predict{"cross_page_predict",
                                "Predicted a pattern, but its"
                                "on a different physical page, tracking it in GH table to learn quickly"};

    Event ev_cross_page_resume{"cross_page_resume",
                               "Pattern matched in new"
                               "physical page, resume without relearning"};

    Event ev_limited_by_l2c{"limited_by_l2c",
                            "The prefetcher could have "
                            "prefetched further out, but was limited by bandwidth of L2C prefetch requests port"};

    Event ev_threshold_low{"threshold_low", "Threshold was too low to initiate prefetch"};
    Event ev_page_cross_prevent{"page_cross_prevent", "Inhibit page crossing after max_page_crossings reached"};
    Event ev_filter_prevent{"filter_prevent", "Prefetch already found in prefetch filter"};
    Event ev_filter_evict{"filter_evict", "L2 evicted address in prefetch filter"};
    Event ev_prefetch{"prefetch", "Prefetch triggered"};
    Event ev_filter_overflow_l2p{"overflow_l2p", "Overflow"};
    Event ev_filter_drop_l2p{"drop_l2p", "Drop"};
    Event ev_same_line_prevent{"same_line_prevent", "Stride within same line as last entry in signature ignored"};

    /* Signature entry element.  This data structure associates a page with a
     * signature.  A signature is a compress form of several existing delta
     * strides */
    struct StEntry
    {
        bool valid{false};
        uint32_t last_offset{0}; // 6 Bits
        uint64_t signature{0};
    };

    /* Prefetch filter element.  This data is used to track outstanding prefetch
     * requests.  It also tracks which requests were sent and if they were useful. */
    struct PfEntry
    {
        PfEntry() : valid(true), useful(false) {}
        bool valid;
        bool useful;
    };

    /* Global History element.  This structure of elements allows a prefetch
     * signature pattern to resume across physical pages without entirely retraining. */
    struct GhEntry
    {
        uint64_t signature;
        float path_confidence;
        int32_t last_offset;
        int32_t delta;
    };

    // Prefetcher Storage Tables
    Cache<PhysAddr, StEntry> signature_table{"spp_signature_table"};
    std::vector<PtEntry> pattern_table{std::vector<PtEntry>(1 << signature_size, PtEntry(delta_cnt))};
    Cache<PhysAddr, PfEntry> prefetch_filter{"spp_filter_cache"};
    boost::circular_buffer<GhEntry> gh_buffer{gh_size};
    SppAlpha alpha{1023}; // 20 bits for tracking

    void prefetch(MemReq req, uint64_t signature, float probability);
    uint64_t calc_signature(uint64_t old_sig, int32_t delta);
    uint32_t get_offset(PhysAddr k)
    {
        // Return cache line
        return k.extract_range(signature_table.get_line_bits() - 1, line_bits);
    }
    uint64_t get_page_number(PhysAddr k)
    {
        // Return cache line
        return k.extract_range(63, signature_table.get_line_bits());
    }

protected:
    void train_impl(MemReq req, bool hit, bool prefetch, bool demand) override;
    void evict_impl(PhysAddr pa, bool prefetch, bool demand) override;
    void drop_impl(MemReq req, PrefetchDropReason reason) override;
    void overflow_impl(MemReq req) override;
};

} // namespace ampere::core
#endif // AMPERE_CORE_L2P_SPP_H

/*
 * vim: sw=4:ts=4:et:si:cindent:cino=\:0,(0,g0,t0:
 */
