//
// Copyright (c) 2020 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//

#ifndef AMPERE_MESH_MODULE_H
#define AMPERE_MESH_MODULE_H

#include "ampere/mesh/mesh_config.h"
#include "ampere/mesh/mesh_dat.h"
#include "ampere/mesh/mesh_req.h"
#include "ampere/sc/ancestor.h"
#include "ampere/sc/sc.h"

namespace ampere::mesh {

class MeshModule : public sc::Module
{
public:
    MeshConfigPort config{"mesh_config"};

    MeshModule(const sc_core::sc_module_name& name) : sc::Module(name) { setup_config_port(); }

    MeshModule(const sc_core::sc_module_name& name, sc::Clock* clock) : sc::Module(name, clock) { setup_config_port(); }

private:
    void setup_config_port()
    {
        MeshModule* ancestor = sc::get_nearest_ancestor<MeshModule>(this);

        if (ancestor != nullptr) {
            config.bind(ancestor->config);
        } else {
            auto config_ifc = sc::get_nearest_ancestor<MeshConfigIfc>(this);

            if (config_ifc != nullptr)
                config.bind(*config_ifc);
        }
    }

protected:
    /** Helper function similar to make_child() for making all the data packets in one go.
     */
    template<typename P, typename... Args>
    std::vector<MeshDat> make_data(sc::transaction_ptr<P>& p, Args&&... args)
    {
        std::vector<MeshDat> pkts;
        make_data(pkts, p, std::forward<Args>(args)...);
        return pkts;
    }

    template<typename Container, typename P, typename... Args>
    void make_data(Container& cont, sc::transaction_ptr<P>& p, Args&&... args)
    {
        auto num_pkts = 64 * 8 / config->data_bus_width();
        AMPERE_ASSERT(num_pkts == 1 || num_pkts == 2 || num_pkts == 4, "unexpected number of data packets");

        uint8_t data_id = 0;
        uint8_t data_id_inc = (num_pkts == 2) ? 2 : 1;

        for (; num_pkts > 0; --num_pkts, data_id += data_id_inc) {
            auto pkt = make_clone<MeshDat>(p, std::forward<Args>(args)...);
            pkt->DataID = data_id;
            cont.push_back(pkt);
        }
    }

    template<typename Container, typename... Args>
    void make_data(Container& cont, sc::transaction_ptr<MeshReqObj>& p, Args&&... args)
    {
        size_t num_pkts =
            std::max((p->RSVDC.LargeReq() ? p->RSVDC.ReqLenBytes() * 8 : 64 * 8) / config->data_bus_width(), 1);
        AMPERE_ASSERT(num_pkts == 1 || num_pkts == 2 || num_pkts == 4, "unexpected number of data packets");

        uint8_t data_id = 0;
        uint8_t data_id_inc = (num_pkts == 2) ? 2 : 1;

        for (; num_pkts > 0; --num_pkts, data_id += data_id_inc) {
            auto pkt = make_clone<MeshDat>(p, std::forward<Args>(args)...);
            pkt->DataID = data_id;
            cont.push_back(pkt);
        }
    }

    template<typename T, typename... Args>
    T* make_submod(const char* prefix, std::initializer_list<std::pair<std::string, sc::json>> settings, Args... args)
    {
        auto& params = get_environment()->parameters;
        const auto* submod_name = sc_core::sc_gen_unique_name(prefix);

        for (const auto& setting : settings)
            params.add_setting(
                fmt::format("{}.{}.{}", name(), submod_name, setting.first), setting.second, "submod config");

        auto dev = std::make_unique<T>(submod_name, std::forward<Args>(args)...);
        T* ret = dev.get();
        m_submodules.push_back(std::move(dev));
        return ret;
    }

    template<typename T, typename... Args>
    T* make_submod(const char* prefix, Args... args)
    {
        const auto* submod_name = sc_core::sc_gen_unique_name(prefix);
        auto dev = std::make_unique<T>(submod_name, std::forward<Args>(args)...);
        T* ret = dev.get();
        m_submodules.push_back(std::move(dev));
        return ret;
    }

    uint8_t get_die_id() const;

    std::vector<std::unique_ptr<sc::Module>> m_submodules;
};

} // namespace ampere::mesh

#endif // AMPERE_MESH_MODULE_H

/*
 * vim: sw=4:ts=4:et:si:cindent:cino=\:0,(0,g0,t0:
 */
