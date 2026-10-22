//
// Copyright (c) 2022 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//
#pragma once

#include <map>
#include <set>

#include "ampere/sc/sc.h"
#include "ampere/mesh/mesh_config.h"

namespace ampere::mesh {

using namespace archx;
using namespace sc;

class PagingIfc : public sc_core::sc_interface
{
public:
    virtual PhysAddr translate(VirtAddr addr, const std::string& region) = 0;
    virtual sc::PageSize get_page_size() const = 0;
    virtual unsigned get_pa_bits() const = 0;
};

class RandomPager : public sc::Module, public mesh::PagingIfc
{
public:
    RandomPager(const sc_core::sc_module_name& name);
    const char* kind() const override { return "paging"; }

    PhysAddr translate(VirtAddr addr, const std::string& region) override;
    PageSize get_page_size() const override { return ps; }
    unsigned get_pa_bits() const override { return pa_bits; }

private:
    Param<PageSize> ps{"page_size", "page size (4Kb, 2Mb, 1Gb, 64Kb, 512Mb, 4Tb)", PageSize(PageSize::Size::PS_4KB)};
    Param<unsigned> pa_bits{"pa_bits", "number of physical address bits", 47};

    using MapType = std::map<std::pair<std::string, VirtAddr>, PhysAddr>;
    MapType page_tables;
    std::set<PhysAddr> phys_pages;
    uint64_t pa_mask;

    MapType::iterator allocate_random_entry(VirtAddr page_addr, const std::string& region);
};

} // namespace ampere::mesh
