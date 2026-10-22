//
// Copyright (c) 2022 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//
#pragma once

#include "ampere/sc/module.h"
#include "ampere/sc/param.h"
#include "ampere/sc/report.h"

namespace ampere::sc {

template<typename IF, int N = 1, sc_core::sc_port_policy P = sc_core::SC_ONE_OR_MORE_BOUND>
class ConfigPort : public Module
{
public:
    ConfigPort(const sc_core::sc_module_name& name) : Module{name} {}

    void before_end_of_elaboration() override;

    IF* operator->() { return static_cast<IF*>(port.get_interface()); }
    const IF* operator->() const { return static_cast<const IF*>(port.get_interface()); }

    IF* operator[](size_t idx) { return port[idx]; }
    const IF* operator[](size_t idx) const { return port[idx]; }

    void bind(IF& ifc) { port.bind(ifc); } // for manual connection
    bool is_connected() const { return port.size() > 0; }

protected:
    Param<std::string> connect{"connect", "global interface object to connect to", ""};

    sc_core::sc_port<IF, N, P> port;
};

template<typename IF, int N, sc_core::sc_port_policy P>
void ConfigPort<IF, N, P>::before_end_of_elaboration()
{
    if (!this->connect.val().empty()) {
        auto obj = sc_core::sc_find_object(this->connect.val().c_str());

        if (!obj)
            AMPERE_ERROR("could not find object \"{}\" to connect to", this->connect.val());

        auto ifc = dynamic_cast<IF*>(obj);

        if (!ifc)
            AMPERE_FATAL("object \"{}\" is not the proper interface type", this->name());

        this->port.bind(*ifc);
    }
}

} // namespace ampere::sc
