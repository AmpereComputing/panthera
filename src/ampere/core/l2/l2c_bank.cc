//
// Copyright (c) 2021 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//

#include "ampere/core/l2/l2c_bank.h"
#include "ampere/archx/addr.h"
namespace ampere::core::l2 {

BankSet::BankSet(const sc_core::sc_module_name& name, size_t size)
    : Module{name},
      num_credit_pools{"num_credit_pools", "number of (potentially shared) credits pool", size},
      num_address_credit_pools{"num_address_credit_pools", "number of (potentially shared) address credits pool", size},
      num_eviction_credit_pools{
          "num_eviction_credit_pools", "number of (potentially shared) eviction data credits pool", size},
      num_write_credit_pools{"num_write_credit_pools", "number of (potentially shared) write data credits pool", size},
      num_data_credit_pools{"num_data_credit_pools", "number of (potentially shared) unified data credits pool", size},
      num_stash_credit_pools{"num_stash_credit_pools", "number of (potentially shared) stash credits pool", size},
      banks{"bank", size}
{
    AMPERE_ASSERT(util::is_power_of_two(size), "Number of L2 banks must be a power of 2");
    AMPERE_ASSERT(simple_bank_hash || size == 8, "Old non-simple bank hash only works for 8 banks");

    fb_credits.init(num_credit_pools);
    address_fb_credits.init(num_address_credit_pools);
    eviction_fb_credits.init(num_eviction_credit_pools);
    write_fb_credits.init(num_write_credit_pools);
    data_fb_credits.init(num_data_credit_pools);
    stash_fb_credits.init(num_stash_credit_pools);

    // Copy some parameters into each of the banks. This makes the parameters easier to use
    // since they can be set once at l2c.banks instead of in each individual bank.
    for (uint64_t i = 0; i < size; i++) {
        banks[i].initialize(ways_mask ? ways_mask : util::mask(banks[0].get_num_ways()));
        banks[i].bank_num = i;
        banks[i].bank_bits = util::bitsize(size - 1);
        banks[i].connor_set_hash = connor_set_hash;

        AMPERE_ASSERT(banks[i].get_num_sets() == banks[0].get_num_sets(),
                      "All L2 banks must have the same number of sets");
        AMPERE_ASSERT(banks[i].get_num_ways() == banks[0].get_num_ways(),
                      "All L2 banks must have the same number of ways");
    }

    crp->init(size, banks[0].get_num_sets(), banks[0].get_num_ways(), banks[0].ways_mask);
    for (auto& bank : banks) {
        bank.set_fb_credits(&fb_credits.at(bank.get_credit_pool_num()));
        bank.set_address_fb_credits(&address_fb_credits.at(bank.get_address_credit_pool_num()));
        bank.set_eviction_fb_credits(&eviction_fb_credits.at(bank.get_eviction_credit_pool_num()));
        bank.set_write_fb_credits(&write_fb_credits.at(bank.get_write_credit_pool_num()));
        bank.set_data_fb_credits(&data_fb_credits.at(bank.get_data_credit_pool_num()));
        bank.set_stash_fb_credits(&stash_fb_credits.at(bank.get_stash_credit_pool_num()));
        bank.crp = crp.get();
    }
}

uint64_t BankSet::total_fill_buffers() const
{
    uint64_t unified_total{0}, address_total{0};

    for (const auto& pool : fb_credits)
        unified_total += pool.get_size();
    for (const auto& pool : address_fb_credits)
        address_total += pool.get_size();

    // Controlling credits are either unified FB credits or split address credits. When one
    // credit type is non-controlling it is set to a large number to avoid throttling on
    // acquires of that credit type. Therefore the effective max outstanding requests is
    // determined by whichever of these controlling fb types has the smallest total

    return std::min(unified_total, address_total);
}

unsigned BankSet::select(PhysAddr addr) const
{
    // NOTE: any changes to the hash need to be reflected in the tracing tools for L2 warmstart (see
    // src/ampere/tracing_tools/cache.h).
    if (simple_bank_hash) {
        auto num_bank_id_bits = util::log2(banks.size());
        auto bank_hash_addr_bits = addr.extract_range(bank_hash_high, bank_hash_low);
        return util::fold_to_n_bits(bank_hash_addr_bits, num_bank_id_bits);
    }

    // Old Rau hash used on Furiosa, Ripley, and Trinity
    return bank_hash(addr.extract_range(bank_hash_high, bank_hash_low));
}

Bank::Bank(const sc_core::sc_module_name& name) : CacheConfiguration{name}
{
    sets.resize(num_sets);

    for (auto& set : sets)
        set.resize(num_ways);

    line_bits = util::bitsize(63);
    set_bits = util::bitsize(num_sets - 1);
}

void Bank::initialize(uint64_t l2_ways_mask)
{
    ways_mask = l2_ways_mask;

    // assign L3 cache type to lines in L3 ways
    for (auto way = 0u; way < num_ways; way++) {
        if ((ways_mask & (1 << way)) == 0 /* L3 way */) {
            for (auto& set : sets) {
                set[way].line.cache_type = CacheType::L3;
            }
        }
    }
}

bool Bank::in_l1i(const Line& line) const
{
    if (perfect_l1i_bits)
        return l1i_lookup->address_resident(line.addr);
    return line.in_l1i;
}

bool Bank::in_l1d(const Line& line) const
{
    if (perfect_l1d_bits)
        return l1d_lookup->address_resident(line.addr);
    return line.in_l1d;
}

void Bank::update_busy_mask() const
{
    // busy_cycle_mask should be shifted right once per cycle, and the LSB tells us whether the bank is busy in this
    // cycle. Since we don't know that the bank will run every cycle, check how long it has been since the last time we
    // ran and update the busy mask accordingly.
    auto since_busy_update = get_cycle() - last_busy_update;
    if (since_busy_update) {
        if (since_busy_update >= 64)
            busy_cycle_mask = 0;
        else
            busy_cycle_mask >>= since_busy_update;
        last_busy_update = get_cycle();
    }
}

bool Bank::can_schedule(const Op& op) const
{
    update_busy_mask();

    const auto credits_available = [this]() -> bool {
        return (fb_credits->can_acquire() && address_fb_credits->can_acquire(2) && data_fb_credits->can_acquire(2)
                && eviction_fb_credits->can_acquire() && write_fb_credits->can_acquire());
    };

    return (is_available()
            && (op->has_fb() || !needs_fill_buffer(op->opcode) || credits_available()
                || (op->opcode == l2::Opcode::ALLOC_L3)));
}

void Bank::cancel_schedule(const Op& op, uint64_t cycle_mask)
{
    busy_cycle_mask = cycle_mask;
    if (op->fb_credits)
        op->fb_credits.release();
}

FBCredits Bank::schedule(const Op& op, uint64_t cycle_mask)
{
    busy_cycle_mask |= cycle_mask;

    if (needs_fill_buffer(op->opcode)) {
        if (op->opcode == l2::Opcode::ALLOC_L3) {
            if (!(reserve_stash_fbes ? stash_fb_credits->can_acquire() : fb_credits->can_acquire()))
                return {}; // stash is scheduled even if no FB entries are available

            if (reserve_stash_fbes)
                return {stash_fb_credits->acquire(),
                        address_fb_credits->acquire(),
                        address_fb_credits->acquire(),
                        data_fb_credits->acquire(),
                        data_fb_credits->acquire(),
                        eviction_fb_credits->acquire(),
                        write_fb_credits->acquire()};
        }

        if (!op->has_fb()) {
            return {fb_credits->acquire(),
                    address_fb_credits->acquire(),
                    address_fb_credits->acquire(),
                    data_fb_credits->acquire(),
                    data_fb_credits->acquire(),
                    eviction_fb_credits->acquire(),
                    write_fb_credits->acquire()};
        }
    }

    return {};
}

std::pair<boost::optional<Line&>, uint64_t> Bank::lookup_internal(Index index, Tag tag, bool update_lru, MemReq req)
{
    for (auto way = 0U; way < sets[index].size(); way++) {
        auto& entry = sets[index][way];

        if (entry.line.state != LineState::I && entry.tag == tag) {
            if (update_lru)
                crp->touch(
                    bank_num, index, way, L2ReplacementPolicy::TouchOp::LOOKUP, true /* demand */, sets[index], req);
            return {entry.line, way};
        }
    }

    return {{}, 0};
}

boost::optional<Line&> Bank::lookup(PhysAddr addr, bool update_lru, MemReq req)
{
    auto [index, tag] = compute_index(addr);
    auto [line, way] = lookup_internal(index, tag, update_lru, req);
    return line;
}

bool Bank::busy_snoop_invalidated(PhysAddr addr) const
{
    auto [index, tag] = compute_index(addr);
    for (const auto& entry : sets[index]) {
        if (entry.line.state == LineState::I && entry.tag == tag && entry.line.busy)
            return true;
    }

    return false;
}

std::optional<Line> Bank::preload_in_l2(Line& other, MemReq req)
{
    auto [index, tag] = compute_index(other.addr);
    auto [hit, way] = lookup_internal(index, tag, false /* update LRU */, req);

    if (hit) {
        if (req->type == MemOp::Type::WRITE) {
            hit->state = l2::LineState::UD;
            crp->touch(bank_num, index, way, L2ReplacementPolicy::TouchOp::UPDATE, true /* demand */, sets[index], req);
        } else {
            crp->touch(bank_num, index, way, L2ReplacementPolicy::TouchOp::LOOKUP, true /* demand */, sets[index], req);
        }
        return std::nullopt;
    } else {
        auto way = crp->victim(bank_num, index, sets[index], compute_way_mask(other.cache_type));
        auto& victim = sets[index][way].line;
        sets[index][way].tag = tag;
        std::swap(victim, other);
        crp->touch(bank_num, index, way, L2ReplacementPolicy::TouchOp::REFILL, true /* demand */, sets[index], req);
        return other; // after the swap() above, other now contains the victim
    }
}

void Bank::preload_in_l3(Line& other)
{
    other.cache_type = CacheType::L3;
    auto victim = fill_or_select_victim(other).first;

    if (victim)
        replace(other, victim->way);
}

uint64_t Bank::compute_way_mask(CacheType type) const
{
    switch (type) {
    case CacheType::L2:
        return ways_mask; // returns a bit-mask with set bits indicating L2 ways
    case CacheType::L3:
        return (~ways_mask) & util::mask(num_ways); // returns a bit-mask with set bits indicating L3 ways
    }

    return 0;
}

Bank::FillTarget Bank::select_fill_target(Line& other, MemReq req)
{
    auto [index, tag] = compute_index(other.addr);
    auto [hit, hit_way] = lookup_internal(index, tag, false, nullptr);

    // A fill for a read/write/prefetch may find that the line already resides in L2 array.
    // Scenario A: Write op hitting L2 line in SC state issues a chi request.
    // Scenario B: 1) Write op is scheduled after a read op to the same addr is filled (DO_FILL); and
    // 2) the write op fills after the read op has already inserted the line in the L2 array (DO_READ_DEALLOC).
    // Scenario C: a read or prefetch may encounter a FILLED_PENDING_UPGRADE FB entry for the same addr,
    // allocate a new FB entry and issue a new CHI request.
    if (hit) {
        bool invalidate_l1i = false;
        // This is an update to an existing line in the L2
        AMPERE_ASSERT(hit->addr == other.addr,
                      "Line address {} and incoming address {} are not the same for update",
                      hit->addr,
                      other.addr);

        // Updates can only come from writes, which are always demand since we have no store prefetcher
        crp->touch(bank_num, index, hit_way, L2ReplacementPolicy::TouchOp::UPDATE, true /* demand */, sets[index], req);

        // IL1 is snooped (legacy code for likely_shared). DL1 isn't snooped for upgrades.
        if (hit->state == l2::LineState::SC && in_l1i(*hit))
            invalidate_l1i = true;

        // Since this is an update, other.state will be in UC state, which should be equal to
        // hit.state except when hit.state is in UD state.
        if (hit->state == l2::LineState::UD && other.state == l2::LineState::UC)
            hit->state = l2::LineState::UD;
        else
            hit->state = other.state;

        hit->prefetch = other.prefetch;
        hit->busy = other.busy;
        hit->va_bits = other.va_bits;

        return {true, hit_way, {}, invalidate_l1i};
    } else {
        auto victim_way = crp->victim(bank_num, index, sets[index], compute_way_mask(other.cache_type));
        auto& victim_line = sets[index][victim_way].line;
        AMPERE_ASSERT(!victim_line.busy, "Chose a victim line which is already busy");
        bool stash = other.cache_type == CacheType::L3;
        if (stash)
            AMPERE_ASSERT((ways_mask & (1 << victim_way)) == 0,
                          "Stash fill replaces a line from L2.\n"); // victim should come from an L3 way
        victim_line.busy = true;

        if (victim_line.state == l2::LineState::I) {
            return {false, victim_way, {}, false};
        } else {
            if (stash)
                AMPERE_ASSERT(victim_line.cache_type == CacheType::L3,
                              "A cacheline of type L2 was occupying an L3 way.\n");
            return {
                false, victim_way, Victim{victim_way, victim_line, in_l1d(victim_line), in_l1i(victim_line)}, false};
        }
    }
}

Bank::FillTarget Bank::reserve_fill_target(Line& other, MemReq req)
{
    auto [hit, way, victim, invalidate_l1i] = select_fill_target(other, req);

    auto [index, tag] = compute_index(other.addr);

    sets[index][way].line.busy = true;
    if (!hit) {
        sets[index][way].line.state = l2::LineState::I;
    }

    return {hit, way, victim, invalidate_l1i};
}

std::pair<std::optional<Bank::Victim>, bool> Bank::fill_or_select_victim(Line& other, MemReq req)
{
    auto [hit, way, victim, invalidate_l1i] = select_fill_target(other, req);

    if (!hit && !victim) {
        auto [index, tag] = compute_index(other.addr);
        bool demand_fill = !other.prefetch;
        verify_cl_uniqueness_in_set(index, way, other.addr);
        auto& line = sets[index][way].line;
        sets[index][way].tag = tag;
        std::swap(line, other);
        crp->touch(bank_num, index, way, L2ReplacementPolicy::TouchOp::REFILL, demand_fill, sets[index], req);
    }

    return {victim, invalidate_l1i};
}

std::optional<Bank::Victim> Bank::fill_l3_victim(Line& other, Line& victim_line, uint64_t victim_way, MemReq req)
{
    auto [index, tag] = compute_index(other.addr);
    if (victim_line.state == l2::LineState::I) {
        sets[index][victim_way].tag = tag;
        std::swap(victim_line, other);
        // FIXME: not sure whether the demand/prefetch bit is really correct
        crp->touch(
            bank_num, index, victim_way, L2ReplacementPolicy::TouchOp::REFILL, true /* demand */, sets[index], req);
        return {};
    } else {
        return Victim{victim_way, victim_line, in_l1d(victim_line), in_l1i(victim_line)};
    }
}

bool Bank::would_fill_without_victim(PhysAddr addr, CacheType cache_type)
{
    auto [index, tag] = compute_index(addr);
    auto [hit, way] = lookup_internal(index, tag, false, nullptr);

    if (hit) {
        return true;
    } else {
        auto way = crp->victim(bank_num, index, sets[index], compute_way_mask(cache_type));
        auto& line = sets[index][way].line;

        return line.state == l2::LineState::I;
    }
}

std::tuple<bool, bool, PhysAddr> Bank::replace(Line& replacement, int way, MemReq req)
{
    auto [index, tag] = compute_index(replacement.addr);
    bool demand_fill = !replacement.prefetch;

    verify_cl_uniqueness_in_set(index, way, replacement.addr);

    auto& victim_line = sets[index][way].line;

    bool victim_l1i = in_l1i(victim_line);
    bool victim_l1d = in_l1d(victim_line);
    PhysAddr victim_addr = victim_line.addr;

    sets[index][way].tag = tag;
    std::swap(victim_line, replacement);
    crp->touch(bank_num, index, way, L2ReplacementPolicy::TouchOp::REFILL, demand_fill, sets[index], req);

    return {victim_l1i, victim_l1d, victim_addr};
}

void Bank::clear_l1_bits_in_set(Index index, uint64_t va_bits, bool iside)
{
    if (iside) {
        for (auto& w : sets[index])
            w.line.in_l1i = false;
    } else {
        for (auto& w : sets[index]) {
            if (w.line.va_bits == va_bits)
                w.line.in_l1d = false;
        }
    }
}

std::tuple<int, int, int> Bank::count_l1_and_busy_bits() const
{
    int in_l1i{0}, in_l1d{0}, busy{0};

    for (const auto& set : sets) {
        for (const auto& w : set) {
            if (this->in_l1d(w.line))
                ++in_l1d;
            if (this->in_l1i(w.line))
                ++in_l1i;
            if (w.line.busy)
                ++busy;
        }
    }

    return {in_l1i, in_l1d, busy};
}

std::pair<Index, Tag> Bank::compute_index(PhysAddr addr) const
{
    return {compute_set(addr), compute_tag(addr)};
}

Index Bank::compute_set(PhysAddr addr) const
{
    // NOTE: any changes to the hash need to be reflected in the tracing tools for L2 warmstart (see
    // src/ampere/tracing_tools/cache.h).

    // Starting with Connor, the low log2(num_banks) bits are only part of the bank hash, and to get the set index
    // we XOR the next two groups of log2(num_sets) bits above that.
    // For the current Connor POR (8 banks, 512 sets) this works out to PA[17:9] ^ PA[26:18]
    if (connor_set_hash) {
        int low_half_lsb = bank_bits + 6;
        int low_half_msb = low_half_lsb + set_bits - 1;
        int high_half_lsb = low_half_msb + 1;
        int high_half_msb = high_half_lsb + set_bits - 1;
        return addr.extract_range(low_half_msb, low_half_lsb) ^ addr.extract_range(high_half_msb, high_half_lsb);
    }

    uint64_t hi{0};
    uint64_t non_bank_bits = 6 - bank_bits;

    if (set_bits > non_bank_bits) {
        uint64_t hi_hash_start = set_bits + 12 - non_bank_bits;
        uint64_t hi_lower = addr.extract_range(hi_hash_start - 1, 12);
        uint64_t hi_upper = addr.extract_range(hi_hash_start + set_bits - non_bank_bits - 1, hi_hash_start);
        hi = hi_lower ^ hi_upper;
    }

    uint64_t lo = addr.extract_range(11, 6 + bank_bits);
    uint64_t bank_index = (hi << non_bank_bits) | lo;
    bank_index &= (1 << set_bits) - 1;

    return bank_index;
}

Tag Bank::compute_tag(PhysAddr addr) const
{
    return addr.extract_range(63, line_bits + set_bits);
}

/* Inputs: addr to be inserted in a given <index, way>
 * Output: check that no other way contains the same addr.
 */
void Bank::verify_cl_uniqueness_in_set(Index index, unsigned way, PhysAddr addr)
{
    for (auto test_way = 0u; test_way < sets[index].size(); test_way++) {
        auto& entry = sets[index][test_way];
        if (entry.line.state != LineState::I && test_way != way && entry.line.addr == addr)
            AMPERE_ERROR("Same cache line addr {} occupies 2 ways in L2/L3", addr);
    }
}

} // namespace ampere::core::l2
