//
// Copyright (c) 2018-2023 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//

#include "ampere/core/lsu/cache.h"
#include <iterator>

namespace ampere::core {

Index L1DCache::compute_set(VirtAddr address) const
{
    if (this->set_bits == 0)
        return 0;

    auto index = address.extract_range(this->line_bits + this->set_bits - 1, this->line_bits);
    if (index >= this->num_sets)
        throw std::logic_error("set index is larger than the number of sets");
    return index;
}

uint64_t L1DCache::compute_tag(PhysAddr address) const
{
    return address.extract_upper_range(phys_addr_tag_lower_bit);
}

bool L1DCache::address_resident(PhysAddr addr) const
{
    bool match = false;
    find_aliases(
        [&](auto set_addr, const auto& entry) {
            match = true;
            return true;
        },
        VirtAddr(addr.value()),
        addr);

    return match;
}

std::vector<VirtAddr> L1DCache::matches(PhysAddr phys_addr) const
{
    return aliases(VirtAddr(phys_addr.value()), phys_addr, false);
}

std::vector<VirtAddr> L1DCache::aliases(VirtAddr virt_addr, PhysAddr phys_addr, bool exclude_self) const
{
    std::vector<VirtAddr> matches;
    find_aliases(
        [&](auto set_addr, const auto& entry) {
            matches.emplace_back(set_addr);
            return false;
        },
        virt_addr,
        phys_addr,
        exclude_self);

    return matches;
}

std::vector<PhysAddr> L1DCache::aliased_set_addresses(VirtAddr addr) const
{
    std::vector<PhysAddr> entries;
    entries.reserve(num_ways * overlapping_sets());
    iter_aliases(
        [&](auto set_addr, const auto& entry) {
            entries.emplace_back(entry.addr);
            return false;
        },
        addr);

    return entries;
}

} // namespace ampere::core

/*
 * vim: sw=4:ts=4:et:si:cindent:cino=\:0,(0,g0,t0:
 */
