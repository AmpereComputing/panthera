//
// Copyright (c) 2021 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//

#ifndef AMPERE_CORE_L2C_BANK_H
#define AMPERE_CORE_L2C_BANK_H

#include "ampere/archx/addr.h"
#include "ampere/core/hash.h"
#include "ampere/core/l2/l2c_line.h"
#include "ampere/core/l2/l2c_op.h"
#include "ampere/core/l2/l2c_opcode.enum.h"
#include "ampere/core/l2/crp/l2_replacement_policy.h"
#include "ampere/core/l2/crp/nru.h"
#include "ampere/core/l2/crp/oracle.h"
#include "ampere/uarch_common/cache.h"
#include "ampere/uarch_common/memreq.h"
#include "ampere/sc/credit.h"
#include <optional>

namespace ampere::core::l2 {

using namespace archx;
using namespace uarch_common;

using Tag = uint64_t;

class Bank : public CacheConfiguration
{
public:
    explicit Bank(const sc_core::sc_module_name& name);

    Param<std::optional<size_t>> credit_pool{"credit_pool", "which credit pool to use for this bank", {}};
    Param<std::optional<size_t>> address_credit_pool{
        "address_credit_pool", "which address credit pool to use for this bank", {}};
    Param<std::optional<size_t>> eviction_credit_pool{
        "eviction_credit_pool", "which eviction data credit pool to use for this bank", {}};
    Param<std::optional<size_t>> write_credit_pool{
        "write_credit_pool", "which write data credit pool to use for this bank", {}};
    Param<std::optional<size_t>> data_credit_pool{
        "data_credit_pool", "which unified (eviction and write) data credit pool to use for this bank", {}};
    Param<std::optional<size_t>> stash_credit_pool{
        "stash_credit_pool", "which credit pool to use for this bank for stashes", {}};
    Param<bool> reserve_stash_fbes{"reserve_stash_fbes", "Dedicate separate FB entries for stashes", false};

    struct Victim
    {
        uint64_t way;
        l2::LineState state;
        PhysAddr addr;
        bool in_l1d;
        bool in_l1i;
        bool prefetch;

        Victim(uint64_t way, const Line& victim_line, bool in_l1d, bool in_l1i)
            : way(way),
              state(victim_line.state),
              addr(victim_line.addr),
              in_l1d(in_l1d),
              in_l1i(in_l1i),
              prefetch(victim_line.prefetch)
        {
        }
    };

    struct FillTarget
    {
        bool hit;
        uint64_t way;
        std::optional<Victim> victim;
        bool invalidate_l1i;

        FillTarget(bool hit, uint64_t way, std::optional<Victim> victim, bool invalidate_l1i)
            : hit(hit), way(way), victim(victim), invalidate_l1i(invalidate_l1i)
        {
        }
    };

    uint64_t bank_num{0};
    uint64_t bank_bits{3};
    uint64_t ways_mask{0};
    bool perfect_l1i_bits{false};
    bool perfect_l1d_bits{false};
    bool connor_set_hash{false};
    L2ReplacementPolicy* crp;
    InFlightStores in_flight_stores;
    uint64_t debug{0}; // only for debugging

    void initialize(uint64_t l2_ways_mask);
    boost::optional<Line&> lookup(PhysAddr addr, bool update_lru, MemReq req = nullptr);
    std::optional<Line> preload_in_l2(Line& line, MemReq req);
    void preload_in_l3(Line& line);
    FillTarget select_fill_target(Line& other, MemReq req = nullptr);
    FillTarget reserve_fill_target(Line& other, MemReq req = nullptr);
    std::pair<std::optional<Victim>, bool> fill_or_select_victim(Line& other, MemReq req = nullptr);
    bool busy_snoop_invalidated(PhysAddr addr) const;

    std::tuple<bool, bool, PhysAddr> replace(Line& replacement, int way, MemReq req = nullptr);

    bool would_fill_without_victim(PhysAddr addr, CacheType cache_type);

    bool is_available() const { return (busy_cycle_mask & 0x1) == 0; }
    bool can_schedule(const Op& op) const;
    void cancel_schedule(const Op& op, uint64_t cycle_mask);
    FBCredits schedule(const Op& op, uint64_t cycle_mask);

    void clear_l1_bits_in_set(Index set, uint64_t va_bits, bool iside);
    CacheLookup<PhysAddr> l1i_lookup{"l1i_lookup"};
    CacheLookup<PhysAddr> l1d_lookup{"l1d_lookup"};

    int get_line_bits() const { return line_bits; }
    int get_set_bits() const { return set_bits; }
    Index compute_set(PhysAddr addr) const;

    size_t get_credit_pool_num() { return credit_pool.val().value_or(bank_num); }
    size_t get_address_credit_pool_num() { return address_credit_pool.val().value_or(bank_num); }
    size_t get_eviction_credit_pool_num() { return eviction_credit_pool.val().value_or(bank_num); }
    size_t get_write_credit_pool_num() { return write_credit_pool.val().value_or(bank_num); }
    size_t get_data_credit_pool_num() { return data_credit_pool.val().value_or(bank_num); }
    size_t get_stash_credit_pool_num() { return stash_credit_pool.val().value_or(bank_num); }

    void set_fb_credits(CreditPool* p) { fb_credits = p; }
    void set_address_fb_credits(CreditPool* p) { address_fb_credits = p; }
    void set_eviction_fb_credits(CreditPool* p) { eviction_fb_credits = p; }
    void set_write_fb_credits(CreditPool* p) { write_fb_credits = p; }
    void set_data_fb_credits(CreditPool* p) { data_fb_credits = p; }
    void set_stash_fb_credits(CreditPool* p) { stash_fb_credits = p; }

private:
    std::tuple<int, int, int> count_l1_and_busy_bits() const; // Only for debugging
    CreditPool* fb_credits;
    CreditPool* address_fb_credits;
    CreditPool* eviction_fb_credits;
    CreditPool* write_fb_credits;
    CreditPool* data_fb_credits;
    CreditPool* stash_fb_credits;
    mutable uint64_t busy_cycle_mask{0};
    mutable Cycle last_busy_update{0};
    std::vector<Set> sets;

    uint64_t compute_way_mask(CacheType type) const;
    bool in_l1i(const Line& line) const;
    bool in_l1d(const Line& line) const;
    std::optional<Victim> fill_l3_victim(Line& other, Line& victim_line, uint64_t victim_way, MemReq req = nullptr);

    std::pair<boost::optional<Line&>, uint64_t> lookup_internal(Index index, Tag tag, bool update_lru, MemReq req);
    std::pair<Index, Tag> compute_index(PhysAddr addr) const;
    Tag compute_tag(PhysAddr addr) const;

    void update_busy_mask() const;

    void verify_cl_uniqueness_in_set(Index index, unsigned way, PhysAddr addr);
};

class BankSet : public Module
{
public:
    BankSet(const sc_core::sc_module_name& name, size_t size);

    // General hash parameters
    Param<uint64_t> bank_poly{"bank_hash_polynomial", "Rau polynomial integer for bank hashing", 11};

    // Bank hash parameters
    Param<unsigned> bank_hash_low{"low_bank_hash_bit", "Start bit for bank hash", 6};
    Param<unsigned> bank_hash_high{"high_bank_hash_bit", "End bit for bank hash", 11};
    Param<bool> simple_bank_hash{"simple_bank_hash", "Simple hash using only untranslated bits", false};
    Param<bool> connor_set_hash{"connor_set_hash", "New and improved hashing for better distribution", false};
    Param<uint64_t> ways_mask{"ways_mask", "Mask of ways which are used as L2 (0 = all ways)", 0};

    // FB credit pool parameters
    Param<size_t> num_credit_pools;
    Param<size_t> num_address_credit_pools;
    Param<size_t> num_eviction_credit_pools;
    Param<size_t> num_write_credit_pools;
    Param<size_t> num_data_credit_pools;
    Param<size_t> num_stash_credit_pools;

    RauHash bank_hash{bank_hash_high - bank_hash_low + 1, bank_poly};

    unsigned select(PhysAddr addr) const;
    uint64_t total_fill_buffers() const;

    Bank& operator[](size_t index) { return banks.at(index); }
    const Bank& operator[](size_t index) const { return banks.at(index); }

private:
    Submodule<L2ReplacementPolicy, NRU> crp{"l2_replacement_policy"};
    sc_core::sc_vector<Bank> banks;
    sc_core::sc_vector<CreditPool> fb_credits{"fb_credits"};
    sc_core::sc_vector<CreditPool> address_fb_credits{"address_fb_credits"};
    sc_core::sc_vector<CreditPool> eviction_fb_credits{"eviction_fb_credits"};
    sc_core::sc_vector<CreditPool> write_fb_credits{"write_fb_credits"};
    sc_core::sc_vector<CreditPool> data_fb_credits{"data_fb_credits"};
    sc_core::sc_vector<CreditPool> stash_fb_credits{"stash_fb_credits"};
};

} // namespace ampere::core::l2

#endif // AMPERE_CORE_L2C_BANK_H
