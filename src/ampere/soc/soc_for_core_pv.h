//
// Copyright (c) 2022 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//
#pragma once

#include "ampere/core/core.h"
#include "ampere/memory/mem_responder.h"
#include "ampere/soc/soc_base.h"

namespace ampere::soc {

using namespace core;
using namespace sc;

class SocForCorePV : public SocBase, poly<SocForCorePV>
{
public:
    explicit SocForCorePV(const sc_core::sc_module_name& name);

private:
    std::unique_ptr<Core> core;
    std::unique_ptr<memory::MemResponder> mem_responder;

    Param<int> data_width{"data_bus_width", "width of the mesh data bus (in bits)", 256};

    SystemAddressMap system_address_map{"system_address_map"};

    // MeshConfigIfc

    int data_bus_width() const override { return data_width; }
    int channel(const chi::NodeId id) const override { return 0; }
};

} // namespace ampere::soc
