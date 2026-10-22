//
// Copyright (c) 2018-2019 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//
#include "ampere/sc/connect.h"
#include <string>
#include "ampere/sc/param.h"

namespace ampere::sc {

ConnectionSetup::ConnectionSetup(Module* module, std::unique_ptr<ConnectionFactory> factory)
    : module{module}, factory{std::move(factory)}
{
}

ConnectionSetup::~ConnectionSetup()
{
    make_and_bind();
}

ConnectionSettings ConnectionSetup::bind()
{
    make_and_bind();
    return *this;
}

ConnectionSetup& ConnectionSetup::name(std::string nm)
{
    m_name = std::move(nm);
    return *this;
}

ConnectionSetup& ConnectionSetup::default_latency(CycleDelta lat)
{
    m_default_latency = lat;
    return *this;
}

ConnectionSetup& ConnectionSetup::latency(CycleDelta lat)
{
    m_latency = lat;
    return *this;
}

ConnectionSetup& ConnectionSetup::return_latency(CycleDelta ret_lat)
{
    m_return_latency = ret_lat;
    return *this;
}

ConnectionSetup& ConnectionSetup::capacity(size_t cap)
{
    m_capacity = cap;
    return *this;
}

ConnectionSetup& ConnectionSetup::default_bandwidth(size_t bw)
{
    m_default_rd_bandwidth = bw;
    m_default_wr_bandwidth = bw;
    return *this;
}

ConnectionSetup& ConnectionSetup::bandwidth(size_t bw)
{
    m_rd_bandwidth = bw;
    m_wr_bandwidth = bw;
    return *this;
}

ConnectionSetup& ConnectionSetup::rd_bandwidth(size_t bw)
{
    m_rd_bandwidth = bw;
    return *this;
}

ConnectionSetup& ConnectionSetup::wr_bandwidth(size_t bw)
{
    m_wr_bandwidth = bw;
    return *this;
}

ConnectionSetup& ConnectionSetup::delay(sc_core::sc_time d)
{
    m_delay = d;
    return *this;
}

ConnectionSetup& ConnectionSetup::return_delay(sc_core::sc_time d)
{
    m_return_delay = d;
    return *this;
}

ConnectionSetup& ConnectionSetup::lossy()
{
    m_lossy = true;
    return potentially_lossy();
}

ConnectionSetup& ConnectionSetup::potentially_lossy()
{
    m_lossy_allowed = true;
    return *this;
}

ConnectionSetup& ConnectionSetup::allow_zero_delay_self_connection()
{
    m_allow_zero_delay_self_connection = true;
    return *this;
}

void ConnectionSetup::make_and_bind()
{
    if (!factory)
        return;

    if (m_name.empty())
        m_name = sc_core::sc_gen_unique_name("fifo");

    if (factory->forwards_from()) {
        factory->forward();
    } else {
        update_from_params();
        factory->make(*this); // intentionally leaks
    }
    factory = nullptr;
}

void ConnectionSetup::update_from_params()
{
    auto& registry = get_param_registry();
    std::string conn_name = get_full_name();

    if (m_lossy_allowed) {
        update_if_not_set(&m_lossy, registry, conn_name + ".lossy", "allow items to be overwritten (dropped)", false);
    }

    update_if_not_set(&m_latency,
                      registry,
                      conn_name + ".latency",
                      "cycles after write that data becomes readable",
                      m_default_latency);
    update_if_not_set(&m_return_latency,
                      registry,
                      conn_name + ".return_latency",
                      "cycles after read that an entry can be reused",
                      *m_latency);
    update_if_not_set(&m_capacity,
                      registry,
                      conn_name + ".capacity",
                      "count of data items that can be stored indefinitely",
                      is_lossy() ? 1 : 0);

    auto p_rbw = update_if_not_set(&m_rd_bandwidth,
                                   registry,
                                   conn_name + ".rd_bandwidth",
                                   "number of items readable per cycle",
                                   m_default_rd_bandwidth);
    auto p_wbw = update_if_not_set(&m_wr_bandwidth,
                                   registry,
                                   conn_name + ".wr_bandwidth",
                                   "number of items writeable per cycle",
                                   m_default_wr_bandwidth);

    update_if_not_set(&m_delay, registry, conn_name + ".delay", "delay of clock-crossing FIFO", "0ns");
    update_if_not_set(&m_return_delay,
                      registry,
                      conn_name + ".return_delay",
                      "credit return delay ofr clock-crossing FIFO",
                      m_delay->to_string());

    bool rbw_is_default = p_rbw && p_rbw->using_default();
    bool wbw_is_default = p_wbw && p_wbw->using_default();

    if (rbw_is_default || wbw_is_default) {
        auto& xbw = registry.add_parameter_with_type<size_t>(
            conn_name + ".bandwidth", "number of items readable/writeable per cycle", 1, "default");

        if (!xbw.using_default()) {
            if (rbw_is_default)
                m_rd_bandwidth = from_json_T<size_t>(xbw.value());
            if (wbw_is_default)
                m_wr_bandwidth = from_json_T<size_t>(xbw.value());
        }
    }
}

ParameterRegistry& ConnectionSetup::get_param_registry()
{
    if (module)
        return module->get_environment()->parameters;
    else
        return Environment::get_root()->parameters;
}

std::string ConnectionSetup::get_local_name() const
{
    auto* parent = get_nearest<Module>();
    if (!module || !parent)
        return get_name();

    std::string module_name = module->name();
    std::string parent_name = parent->name();
    if (auto it = module_name.find(parent_name); it == 0)
        module_name = module_name.substr(parent_name.size());

    std::replace(module_name.begin(), module_name.end(), '.', '_');
    if (module_name.empty())
        return m_name;
    return module_name + "_" + m_name;
}

std::string ConnectionSetup::get_full_name() const
{
    if (module)
        return std::string{module->name()} + "." + m_name;
    else
        return m_name;
}

template<typename T>
ParameterRegistry::Parameter* ConnectionSetup::update_if_not_set(std::optional<T>* pvalue,
                                                                 ParameterRegistry& registry,
                                                                 const std::string& pname,
                                                                 const std::string& description,
                                                                 const json& default_value)
{
    if (!*pvalue) {
        auto& param = registry.add_parameter_with_type<T>(pname, description, default_value, "default");
        *pvalue = from_json_T<T>(param.value());

        return &param;
    }

    return nullptr;
}

} // namespace ampere::sc

/*
 * vim: sw=4:ts=4:et:si:cindent:cino=\:0,(0,g0,t0:
 */
