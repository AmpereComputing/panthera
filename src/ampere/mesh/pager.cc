//
// Copyright (c) 2022 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//
#include "ampere/mesh/pager.h"

namespace ampere::mesh {

using namespace archx;

RandomPager::RandomPager(const sc_core::sc_module_name& name) : sc::Module{name}
{
    pa_mask = util::mask(get_pa_bits() - 1, ps.val().page_bits());
}

PhysAddr RandomPager::translate(VirtAddr addr, const std::string& region)
{
    auto page_addr = addr.page_address(ps);
    auto entry = page_tables.find({region, page_addr});

    if (entry == page_tables.end())
        entry = allocate_random_entry(page_addr, region);

    return entry->second + addr.extract_range(ps.val().page_bits() - 1, 0);
}

auto RandomPager::allocate_random_entry(VirtAddr page_addr, const std::string& region) -> MapType::iterator
{
    PhysAddr phys_page_addr;
    int i;

    for (i = 0; i < 100; i++) {
        phys_page_addr = PhysAddr{sc::Random::get<uint64_t>() & pa_mask};
        if (!phys_pages.contains(phys_page_addr))
            break;
    }

    AMPERE_ASSERT(i < 100, "tried 100 times and failed to get a unique random page!");

    phys_pages.insert(phys_page_addr);

    auto [it, inserted] = page_tables.emplace(
        std::piecewise_construct, std::forward_as_tuple(region, page_addr), std::forward_as_tuple(phys_page_addr));
    return it;
}

} // namespace ampere::mesh
