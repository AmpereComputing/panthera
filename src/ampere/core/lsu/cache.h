//
// Copyright (c) 2018-2023 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//

#pragma once

#include "ampere/archx/addr.h"
#include "ampere/archx/translation.h"
#include "ampere/uarch_common/cache.h"
#include "ampere/core/backend_prefetcher.h"

#include <boost/bimap.hpp>

namespace ampere::core {

struct L1DCacheline
{
    L1DCacheline() = default;
    L1DCacheline(PhysAddr addr, PrefetchType pref_type, bool unique = false)
        : addr{addr}, prefetch_type(pref_type), unique(unique)
    {
    }
    L1DCacheline(PhysAddr addr, PrefetchType pref_type, bool unique, bool modified)
        : addr{addr}, prefetch_type(pref_type), unique(unique), modified(modified)
    {
    }

    PhysAddr addr;
    PrefetchType prefetch_type = PrefetchType::NONE;
    bool unique = false;
    bool modified = false;
};

using L1DCacheAddress = std::pair<VirtAddr, PhysAddr>;

class L1DCache : public ManagedCache<L1DCacheAddress, L1DCacheline>
{
    using Base = ManagedCache<L1DCacheAddress, L1DCacheline>;

    Param<size_t> phys_addr_tag_lower_bit{"phys_addr_tag_lower_bit", "tag bits used", PhysAddr::LINE_BITWIDTH + 1};

    size_t overlapping_tag_index_bits;

public:
    using Result = Base::Result;

    explicit L1DCache(const sc_core::sc_module_name& name)
        : Base(name, CacheEntry::default_entry_value<L1DCacheline>()),
          overlapping_tag_index_bits(get_index_bits() > Untranslated::bits ? get_index_bits() - Untranslated::bits : 0)
    {
    }

    inline static L1DCacheAddress address(VirtAddr virt_addr, PhysAddr phys_addr) { return {virt_addr, phys_addr}; }

    Index compute_set(VirtAddr address) const;
    Index compute_set(L1DCacheAddress address) const override { return compute_set(address.first); }

    uint64_t compute_tag(PhysAddr address) const;
    uint64_t compute_tag(L1DCacheAddress address) const override { return compute_tag(address.second); }

    auto set_contents(VirtAddr address) const { return set_contents_indexed(compute_set(address)); }
    std::vector<PhysAddr> aliased_set_addresses(VirtAddr addr) const;

    bool address_resident(PhysAddr addr) const;
    std::vector<VirtAddr> matches(PhysAddr phys_addr) const;
    std::vector<VirtAddr> aliases(VirtAddr virt_addr, PhysAddr phys_addr, bool exclude_self = true) const;

private:
    unsigned overlapping_sets() const { return 1U << overlapping_tag_index_bits; }

    template<typename P>
    void iter_aliases(P func, VirtAddr addr) const
    {
        for (unsigned i = 0, l = overlapping_sets(); i < l; ++i) {
            VirtAddr set_addr(addr.value() ^ (i << Untranslated::bits));
            for (const auto& entry : set_contents(set_addr))
                func(set_addr, entry);
        }
    }

    template<typename P>
    void find_aliases(P func, VirtAddr virt_addr, PhysAddr phys_addr, bool exclude_self = false) const
    {
        auto set = compute_set(virt_addr);
        for (unsigned i = 0, l = overlapping_sets(); i < l; ++i) {
            VirtAddr set_addr(virt_addr.value() ^ (i << Untranslated::bits));
            if ((!exclude_self || compute_set(set_addr) != set)) {
                if (auto entry = find({set_addr, phys_addr}); entry)
                    if (func(set_addr, entry))
                        return;
            }
        }
    }
};

} // namespace ampere::core

/*
 * vim: sw=4:ts=4:et:si:cindent:cino=\:0,(0,g0,t0:
 */
