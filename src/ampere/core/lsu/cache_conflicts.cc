//
// Copyright (c) 2018-2023 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//
#include "ampere/core/lsu/cache_conflicts.h"
#include <algorithm>
#include <bitset>
#include <cstdint>
#include <limits>
#include "ampere/archx/addr.h"
#include "ampere/core/lsu/cache.h"

namespace ampere::core {

LSUCacheConflicts::LSUCacheConflicts(L1DCache& cache, size_t load_pipes)
    : cache(cache), previous_load_accesses(load_pipes), previous_load_bank_accesses(load_pipes * 8)
{
    AMPERE_ASSERT_ERROR(
        !(enable_refill_tag_conflict && enable_fill_read_chunk_bank_conflicts),
        "Only one of 'enable_refill_tag_conflict' and 'enable_fill_read_chunk_bank_conflicts' should be enabled");
    AMPERE_ASSERT_ERROR((banks * bank_size) <= MAX_ACCESS_SIZE,
                        "Total bank coverage (banks * bank_size) is greater than largest access size");

    if (set_bank_mask_param) {
        AMPERE_ASSERT_ERROR(std::bitset<64>(*set_bank_mask_param).count()
                                == static_cast<size_t>(log2(cache.get_num_sets()) - log2(set_banks)),
                            "Invalid set-bank mask");
        set_bank_mask = *set_bank_mask_param;
    } else {
        set_bank_mask = ~static_cast<unsigned>(log2(set_banks));
    }
}

void LSUCacheConflicts::reset()
{
    for (auto [port, access] : load_accesses)
        previous_load_accesses[port] = access;

    if (back_to_back_any_way_bank) {
        for (auto [port, access] : load_accesses) {
            const auto& [addr, size] = access;
            unsigned banks = banks_to_read_aligned(addr, size);
            for (unsigned b = offset_to_bank(addr), i = 0; i < banks; b = (b + 1) % banks, ++i)
                previous_load_bank_accesses[banks * port + b] = access;
        }
    }

    evictions.clear();
    load_accesses.clear();
    load_addresses.clear();
    store_retirement_addresses.clear();

    older_load_write_conflict = false;
}

unsigned LSUCacheConflicts::offset_to_bank(VirtAddr addr) const
{
    return (addr.line_offset() / bank_size) % banks;
}

unsigned LSUCacheConflicts::banks_to_read(VirtAddr addr, unsigned size) const
{
    return size ? std::min<unsigned>(((size - 1 + (addr.line_offset() % bank_size)) / bank_size) + 1, banks) : 0;
}

uint64_t LSUCacheConflicts::bank_offset(VirtAddr addr, unsigned bank) const
{
    if (bank < offset_to_bank(addr))
        bank += banks;
    return (addr.line_offset() / (bank_size * banks)) + (bank * bank_size);
}

unsigned LSUCacheConflicts::banks_to_read_aligned(VirtAddr addr, unsigned size) const
{
    return banks_to_read(addr, std::min<unsigned>(size, VirtAddr::LINE_SIZE - addr.line_offset()));
}

bool LSUCacheConflicts::same_chunk_check(VirtAddr va1, unsigned size1, VirtAddr va2, unsigned size2) const
{
    // Two loads conflict if they access any of the same 8-byte chunks
    return (va1.chunk_bitmap(size1) & va2.chunk_bitmap(size2)) != 0;
}

bool LSUCacheConflicts::same_chunk_bank_check(VirtAddr va1, unsigned size1, VirtAddr va2, unsigned size2) const
{
    // A load conflicts with a retiring store if it accesses any of the same chunks, and also
    // the same bank (meaning VA[7:6] is the same)
    return same_chunk_check(va1, size1, va2, size2) && (va1.extract_range(7, 6) == va2.extract_range(7, 6));
}

bool LSUCacheConflicts::same_set_bank_check(VirtAddr va1, VirtAddr va2) const
{
    // Adjacent pairs of sets share a set-bank
    return (cache.compute_set(va1) & set_bank_mask) == (cache.compute_set(va2) & set_bank_mask);
}

bool LSUCacheConflicts::same_byte_in_set_bank_check(const VirtAddr va1,
                                                    unsigned size1,
                                                    const VirtAddr va2,
                                                    unsigned size2) const
{
    return ((va1.line_bytemask(size1) & va2.line_bytemask(size2)) && same_set_bank_check(va1, va2));
}

// Load vs load conflict
bool LSUCacheConflicts::is_read_read_chunk_conflict(VirtAddr virt_addr,
                                                    unsigned size,
                                                    unsigned dl1_port,
                                                    const std::vector<PortAccess>& inflights) const
{
    if (enable_read_read_chunk_conflicts) {
        for (const auto& access : inflights) {
            if ((duplicate_loads_allowed && virt_addr.same_cache_line(access.addr))
                || (duplicate_exact_loads_allowed && virt_addr == access.addr && size == access.size))
                continue;

            if (threecyc_conflict) {
                // 3-cycle DL1 conflicts if the loads access the same byte and 7 set address bits match
                if (dl1_port != access.port && same_byte_in_set_bank_check(virt_addr, size, access.addr, access.size))
                    return true;
            } else if (same_chunk_check(virt_addr, size, access.addr, access.size)) {
                return true;
            }
        }
    }
    return false;
}

// Store-retirement vs load conflict (same chunk and bank)
bool LSUCacheConflicts::is_write_read_chunk_bank_conflict(VirtAddr virt_addr,
                                                          unsigned size,
                                                          unsigned dl1_port,
                                                          const std::vector<Access>& inflights) const
{
    if (enable_write_read_chunk_bank_conflicts) {
        for (const auto& [addr, memsize] : inflights) {
            if (threecyc_conflict) {
                if (dl1_port == 0) {
                    // Port 0: conflict if writing same byte that load is reading AND address[13:7] matches
                    if (same_byte_in_set_bank_check(virt_addr, size, addr, memsize))
                        return true;
                } else if (virt_addr.line_bytepair_mask(size) & addr.line_bytepair_mask(memsize)) {
                    // Port 1: conflict if writing same byte-pair that load is reading
                    return true;
                }
            } else if (same_chunk_bank_check(virt_addr, size, addr, memsize)) {
                return true;
            }
        }
    }
    return false;
}

bool LSUCacheConflicts::same_set_any_way(VirtAddr set_addr, VirtAddr way_addr, PhysAddr way_phys_addr) const
{
    return cache.compute_set(way_addr) == cache.compute_set(set_addr) && cache.find({way_addr, way_phys_addr});
}

void LSUCacheConflicts::load_cache_read(
    const ConstUop& uop, VirtAddr virt_addr, unsigned datasize, unsigned dl1_port, const Conflicts& conflicts)
{
    // Software prefetches don't read the DL1 so they don't conflict with anything
    if (uop->is_sw_prefetch())
        return;

    load_accesses.emplace(dl1_port, Access(virt_addr, datasize));

    // If this load suppressed its read, then other loads will not conflict with it
    if (!(back_to_back_read_no_conflicts_other && conflicts.back_to_back.complete))
        load_addresses.emplace_back(virt_addr, datasize, dl1_port);

    event.load_cache_read(uop, event.banks_used(banks_to_read_aligned(virt_addr, datasize)));
    if (conflicts.back_to_back.banks_used > 0)
        event.back_to_back_read(uop, event.banks_used(conflicts.back_to_back.banks_used));
}

LSUCacheConflicts::BackToBack LSUCacheConflicts::is_back_to_back_cache_access(VirtAddr virt_addr,
                                                                              PhysAddr phys_addr,
                                                                              unsigned datasize,
                                                                              unsigned dl1_port) const
{
    unsigned used = 0;
    unsigned banks_read = banks_to_read_aligned(virt_addr, datasize);
    auto [previous_addr, previous_size] = previous_load_accesses[dl1_port];
    bool same_previous_access = previous_addr == virt_addr && previous_size == datasize;
    if (same_previous_access) {
        used = banks_read;
    } else if (back_to_back_any_way) {
        if (!back_to_back_any_way_bank) {
            same_previous_access = virt_addr.line_offset() == previous_addr.line_offset() && previous_size == datasize
                                   && same_set_any_way(previous_addr, virt_addr, phys_addr);
            if (same_previous_access)
                used = banks_read;
        } else if (datasize > 0) {
            for (unsigned b = offset_to_bank(virt_addr), i = 0; i < banks_read; b = (b + 1) % banks, ++i) {
                const auto& previous_bank_addr = previous_load_bank_accesses[banks * dl1_port + b].addr;
                auto bank_addr_offset = bank_offset(virt_addr, b);
                auto previous_bank_addr_offset = bank_offset(previous_bank_addr, b);
                if (bank_addr_offset == previous_bank_addr_offset
                    && same_set_any_way(previous_bank_addr, virt_addr, phys_addr))
                    ++used;
            }
            same_previous_access = (used == banks_read);
        }
    }

    return {same_previous_access, used};
}

LSUCacheConflicts::Conflicts LSUCacheConflicts::load_cache_conflicts(
    const ConstUop& uop, VirtAddr virt_addr, PhysAddr phys_addr, unsigned datasize, unsigned dl1_port)
{
    Conflicts conflicts;

    // Software prefetches don't read the DL1 so they don't conflict with anything
    if (uop->is_sw_prefetch())
        return conflicts;

    conflicts.read = is_read_read_chunk_conflict(virt_addr, datasize, dl1_port, load_addresses);
    conflicts.write = is_write_read_chunk_bank_conflict(virt_addr, datasize, dl1_port, store_retirement_addresses);
    conflicts.fill =
        (enable_fill_read_chunk_bank_conflicts && l2c_refill_tag_conflict
         && is_write_read_chunk_bank_conflict(virt_addr, datasize, dl1_port, l2c_refill_tag_conflict.addresses));
    conflicts.eviction = evictions_cause_recycle && evictions.contains(dl1_port);

    conflicts.back_to_back = is_back_to_back_cache_access(virt_addr, phys_addr, datasize, dl1_port);

    // If this load looks at its own read-suppress signal, then it knows that it can't conflict with anything
    // because it didn't need to read the cache
    if (conflicts.back_to_back.complete && back_to_back_read_no_conflicts_self)
        conflicts.reset_conflicts();

    if (load_addresses.empty()) {
        // Remember that the older load this cycle had a write conflict
        if (conflicts.write)
            older_load_write_conflict = true;
    } else {
        // If the younger load has a read conflict with the older load, it will recycle. But if the older load had a
        // write conflict, then theoretically the younger load could be allowed to go. Count how often that happens to
        // see if the optimization is worth implementing. (Current answer: no, would help less than 1% of conflicts.)
        if (conflicts.read && older_load_write_conflict && !conflicts.write)
            event.read_and_write_conflict(uop);
    }

    return conflicts;
}

bool LSUCacheConflicts::same_byte_in_block(uint64_t line_bytes)
{
    uint64_t prev_bytes = 0;
    for (unsigned shift = 0; shift < VirtAddr::LINE_SIZE; shift += 16) {
        auto block_bytes = (line_bytes >> shift) & BLOCK_MASK;
        if (prev_bytes & block_bytes)
            return true;
        prev_bytes |= block_bytes;
    }

    return false;
}

bool LSUCacheConflicts::can_load_combine(unsigned dl1_port, const Access& access)
{
    auto this_first_byte = access.addr;
    auto access_range = access;
    bool can_combine = false;

    auto [start, end] = load_accesses.equal_range(dl1_port);

    for (auto it = start; it != end; ++it) {
        const auto& prev_access = it->second;
        access_range.expand(prev_access);

        if (!*combine_within || *combine_within > BLOCK_SIZE) {
            // First ignore any bytes which are exactly the same
            auto combined_mask = prev_access.line_bytemask() ^ access.line_bytemask();
            // Then see if we are accessing two bytes with the same offset mod 16
            can_combine = !same_byte_in_block(combined_mask);
            if (!can_combine)
                return false;
            if (*combine_within == VirtAddr::LINE_SIZE && !prev_access.addr.same_cache_line(this_first_byte))
                return false;
        }
    }

    if (*combine_within > 0 && *combine_within <= BLOCK_SIZE) {
        // All loads are within a 16-byte block
        return access_range.addr.masked_address(*combine_within)
               == access_range.last_byte_addr().masked_address(*combine_within);
    }

    return can_combine;
}

bool LSUCacheConflicts::refills_participate_in_conflicts(const L2RefillArbitration& refills) const
{
    return ((enable_refill_tag_conflict || enable_fill_read_chunk_bank_conflicts)
            && (spec_fills_cause_conflicts || refills.guaranteed()));
}

bool LSUCacheConflicts::load_refill_conflict_check() const
{
    // Conflict check if l2c response received previous cycle. If so, loads scheduled this cycle
    // will conflict with PA tag read from PA tag write from refill. Loads scheduled this cycle must recycle.
    // After completing ld_recycle pipeline, will be set eligible to reschedule
    return enable_refill_tag_conflict && l2c_refill_tag_conflict;
}

bool LSUCacheConflicts::load_port1_refill_conflict(unsigned dl1_port) const
{
    return (enable_fill_read_chunk_bank_conflicts && l2c_refill_tag_conflict && dl1_port != 0);
}

bool LSUCacheConflicts::try_eviction(VirtAddr virt_set_addr)
{
    if (eviction_port) {
        if (enable_fill_evict_conflicts && enable_fill_read_chunk_bank_conflicts && l2c_refill_tag_conflict
            && is_write_read_chunk_bank_conflict(
                virt_set_addr, VirtAddr::LINE_SIZE, *eviction_port, l2c_refill_tag_conflict.addresses))
        {
            return false;
        }

        load_addresses.emplace_back(virt_set_addr, VirtAddr::LINE_SIZE, *eviction_port);

        if (evictions_cause_recycle)
            evictions.emplace(*eviction_port);
    }

    return true;
}

void LSUCacheConflicts::store_retired(VirtAddr virt_addr, unsigned datasize)
{
    for (auto& port : previous_load_accesses) {
        auto [addr, size] = port;
        if (address_range_a_intersects_b(addr, size, virt_addr, datasize, 1))
            port = {};
    }

    // FIXME: should we clear out previous banks here too?
}

void LSUCacheConflicts::retired_store_wrote_cache(VirtAddr virt_addr, unsigned datasize)
{
    store_retirement_addresses.emplace_back(virt_addr, datasize);
}

} // namespace ampere::core

/*
 * vim: sw=4:ts=4:et:si:cindent:cino=\:0,(0,g0,t0:
 */
