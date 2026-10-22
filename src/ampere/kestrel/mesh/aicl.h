//
// Copyright (c) 2024 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//
#pragma once

#include "ampere/kestrel/mesh/cluster.h"
#include "ampere/mesh/chi/types.h"
#include "ampere/mesh/sam.h"
#include "ampere/sc/sc.h"
#include "ampere/soc/soc_base.h"

namespace ampere::kestrel {

using namespace ampere::mesh;
using namespace ampere::sc;

class AICL : public soc::SocBase, poly<AICL>
{
public:
    explicit AICL(const sc_core::sc_module_name& name);

    int data_bus_width() const override { return 512; }
    int channel(const chi::NodeId id) const override { return 0; }

private:
    Param<size_t> num_clusters{"num_clusters", "Number of clusters in this AICL", 2};

    SystemAddressMap system_address_map{"system_address_map"};

    sc_core::sc_vector<Cluster> cluster{"cluster", num_clusters};
};

} // namespace ampere::kestrel
