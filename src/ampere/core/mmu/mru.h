//
// Copyright (c) 2018-2020 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//

#pragma once

#include <fmt/core.h>
#include <fmt/ostream.h>
#include <optional>
#include <vector>

#include "ampere/archx/page_size.h"

namespace ampere::core {

using archx::PageSize;

class MMUMRU
{
public:
    MMUMRU()
        : mru{PageSize::Size::PS_4KB,
              PageSize::Size::PS_64KB,
              PageSize::Size::PS_2MB,
              PageSize::Size::PS_512MB,
              PageSize::Size::PS_1GB,
              PageSize::Size::PS_4TB},
          size_count(size_t(PageSize::MAX))
    {
    }

    MMUMRU(const MMUMRU& other) = default;
    MMUMRU& operator=(const MMUMRU& other) = default;

    // Set the given page size as most recently used
    void set_mru(PageSize ps)
    {
        if (mru[0] != ps) {
            for (auto it = mru.begin(); it != mru.end(); it++) {
                if (*it == ps) {
                    mru.erase(it);
                    break;
                }
            }
            mru.insert(mru.begin(), ps);
        }
    }

    void increment_page_counter(PageSize ps, int amount = 1)
    {
        if (amount < 0 && ((unsigned)(-amount)) > size_count[ps.index()])
            size_count[ps.index()] = 0;
        else
            size_count[ps.index()] += amount;
    }

    void decrement_page_counter(PageSize ps) { increment_page_counter(ps, -1); }

    void dump(std::ostream& os) const
    {
        fmt::print(os, "\nMRU: ");
        for (auto it = mru.begin(); it != mru.end(); it++) {
            if (it != mru.begin())
                fmt::print(os, ", ");
            fmt::print(os, "{}({})", std::string(*it), size_count[it->index()]);
        }
        fmt::print(os, "\n");
    }

protected:
    std::vector<PageSize> mru;
    std::vector<uint64_t> size_count;
};

class MMUMRUCopy : private MMUMRU
{
public:
    MMUMRUCopy() = delete;
    // Return a copy of the current MRU object on which next_size() will be
    // called to determine which page sizes to search in the TLB
    explicit MMUMRUCopy(const MMUMRU& other) : MMUMRU(other), lookup_it(mru.begin()) {}
    // The default r-value constructor results in a busted iterator
    MMUMRUCopy(const MMUMRUCopy&& other) : MMUMRU(other), lookup_it(mru.begin()) {}
    MMUMRUCopy& operator=(const MMUMRU& other)
    {
        MMUMRU::operator=(other);
        lookup_it = mru.begin();
        return *this;
    }

    // Returns the next page size for which to try a TLB lookup
    std::optional<PageSize> next_size()
    {
        lookup_it++;
        while (lookup_it != mru.end()) {
            PageSize candidate = *lookup_it;
            if (size_count[candidate.index()] > 0)
                return {candidate};
            lookup_it++;
        }
        return std::nullopt;
    }

    // Return the next page size for TLB lookup, but leave the iterator pointing to the current size
    std::optional<PageSize> current_size()
    {
        while (lookup_it != mru.end()) {
            PageSize candidate = *lookup_it;
            if (size_count[candidate.index()] > 0)
                return {candidate};
            lookup_it++;
        }
        return std::nullopt;
    }

private:
    std::vector<PageSize>::iterator lookup_it;
};

} // namespace ampere::core

/*
 * vim: sw=4:ts=4:et:si:cindent:cino=\:0,(0,g0,t0:
 */
