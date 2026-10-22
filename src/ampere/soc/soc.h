//
// Copyright (c) 2020-2022 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//
#pragma once

#include "ampere/core/core.h"
#include "ampere/mesh/home_node.h"
#include "ampere/mesh/mesh.h"
#include "ampere/mesh/mesh_config.h"
#include "ampere/mesh/mesh_device.h"
#include "ampere/mesh/idt.h"
#include "ampere/mesh/sam.h"
#include "ampere/soc/soc_base.h"
#include "ampere/soc/die.h"
#include "ampere/soc/warmup.h"

namespace ampere::soc {

using namespace ampere::sc;
using namespace ampere::core;
using namespace ampere::archx;
using namespace ampere::mesh;

class Soc : public SocBase, poly<Soc>
{
public:
    explicit Soc(const sc_core::sc_module_name& name);
    Soc(const sc_core::sc_module_name& name, Clock* clock, Environment* env);
    void dump(std::ostream& os) const override;

private:
    Param<uint8_t> socket_id{"socket_id", "socket id for this SoC", 0};
    Param<size_t> num_die{"num_die", "number of die in this SoC", 0};
    Param<std::vector<json>> devices_cfg{"devices", "configuration of devices attached to the mesh", "[]"_json};
    Param<std::vector<json>> rdi_cfg{"rdi", "definition of RDI connections", "[]"_json};
    Param<int> data_width{"data_bus_width", "width of the mesh data bus (in bits)", 256};
    Param<std::vector<json>> channel_cfg{"channels", "target to channel mapping", "[]"_json};
    Param<bool> warmstart_enable{"warmstart_enable", "enable warmstart for SLC", true};
    Param<bool> unified_warmstart{"unified_warmstart", "SOC memory stream is used to warm up L2 and SLC", true};
    Param<std::string> warmup_ms{
        "warmup_ms", "name of the memory stream file used for preloading SL cache (warmstart)", ""};
    Param<std::string> warmstart_table_file{"warmstart_table_file", "file containing a warmstart table", ""};
    Param<json> warmstart_table{"warmstart_table", "warmstart the SLC with a table of addresses and states", "[]"_json};
    Param<bool> warmstart_uc_writeevict{
        "warmstart_uc_writeevict", "disable this to load up only code or dirty data", true};

    std::unique_ptr<Mesh> mesh;
    sc_core::sc_vector<Die> die{"die", num_die};

    SystemAddressMap system_address_map{"system_address_map"};

    ControlMod controlmod{"controlmod"};

    std::vector<std::unique_ptr<MeshDevice>> devices;
    std::map<chi::NodeId, MeshDevice*> device_index;
    std::map<std::tuple<uint8_t, uint8_t>, MeshDevice*> device_tags;
    std::map<std::tuple<uint8_t, uint8_t>, std::tuple<std::string, std::vector<NodeId>>> nodeid_tags;
    std::map<std::tuple<uint8_t, uint8_t, uint8_t>, int> channel_map;

    void construct();
    void construct_channel_map();
    void attach_devices();
    void attach_devices_to_die(Die& die);
    void attach_devices_using_z();
    void connect_die();
    std::string gen_device_name(const std::string& prefix, const std::string& type_name) const;
    const std::vector<json>& get_channel_map() const { return channel_cfg.val(); }

    template<typename T = MeshDevice>
    T* make_device(const std::string& type_name, const std::string& dev_name, Clock* clock = nullptr);
    MeshDevice* make_device_with_node_id(const std::string& type_name,
                                         const std::string& dev_name,
                                         NodeId node_id,
                                         Clock* clock);
    MeshDevice* make_device_from_cfg(const json& dev_cfg, NodeId node_id, Clock* clock);
    std::vector<MeshDevice*> make_devices(
        Die* d, const std::map<uint8_t, json>& devices, uint8_t x, uint8_t y, uint8_t port);
    std::vector<MeshDevice*> make_devices(
        Die* d, const std::map<uint8_t, json>& devices, uint8_t x, uint8_t y, uint8_t z, uint8_t port);
    IDT* get_rdi_obj(uint8_t x, uint8_t y, uint8_t port);

    // void end_of_elaboration() override;
    void start_of_simulation() override;

    void warmstart_from_ms(const std::string& filename);
    void warmstart_from_table(const json& json_table);
    std::optional<PhysAddr> warm(PhysAddr addr, WarmupInfo::Action action, const SAM* sam);
    std::optional<PhysAddr> warm(PhysAddr addr, chi::FinalState state, const SAM* sam);
    void warm(PhysAddr addr, chi::FinalState state, chi::NodeId requester_node, const SAM* sam);

    Warmup warmup{"warmup"};

    // All of this is to support the ability to periodically dump tracker state, which can be
    // useful for debugging deadlocks or other cases when the trackers get clogged up over
    // time.
    //
    Param<sc_core::sc_time> pha_dump_interval{
        "pha_dump_interval", "Time interval for dumping PHA trackers", sc_core::SC_ZERO_TIME};
    Param<std::string> pha_dump_file{"pha_dump_file", "File name to dump PHA tracker contents to", "pha_dump.txt"};
    void periodic_pha_dump();
    void dump_phas(std::ostream&) const;
    void dump_injectors(std::ostream&) const;
    sc_core::sc_event pha_dump_event;
    std::unique_ptr<std::ostream> pha_dump_os{nullptr};

    SC_HAS_PROCESS(Soc);

public:
    size_t get_num_die() const override { return num_die; }
    int data_bus_width() const override { return data_width; }
    int channel(const chi::NodeId id) const override;
    void register_warmup(uint64_t num_transactions, std::function<std::optional<WarmupInfo>()> gen_warmup) override
    {
        warmup.register_warmup(num_transactions, gen_warmup);
    }

    std::vector<NodeId> stash_nodes(uint8_t die_id) const override;
    std::set<NodeId> pmha_nodes(void) const override;
};

} // namespace ampere::soc

/*
 * vim: sw=4:ts=4:et:si:cindent:cino=\:0,(0,g0,t0:
 */
