//
// Copyright (c) 2020-2022 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//

#include <iostream>
#include <stdexcept>
#include "ampere/soc/soc.h"
#include "ampere/mesh/cpx.h"
#include "ampere/mesh/pha.h"
#include "ampere/mesh/injector/mesh_injector.h"
#include "ampere/memory_stream/memory_stream.h"

namespace ampere::soc {

Soc::Soc(const sc_core::sc_module_name& name) : SocBase{name}
{
    construct();
}

Soc::Soc(const sc_core::sc_module_name& name, Clock* clock, Environment* env) : SocBase{name, clock, env}
{
    construct();
}

void Soc::construct()
{
    construct_channel_map();
    if (num_die == 0) {
        mesh = std::make_unique<Mesh>("mesh", socket_id, 0);
        attach_devices();
    } else {
        for (auto& d : die)
            attach_devices_to_die(d);
        connect_die();
    }

    if (pha_dump_interval.val() != sc_core::SC_ZERO_TIME) {
        SC_METHOD(periodic_pha_dump);
        sensitive << pha_dump_event;
        dont_initialize();
        pha_dump_event.notify(pha_dump_interval);
        pha_dump_os = std::make_unique<std::ofstream>(pha_dump_file);
    }
}

void Soc::attach_devices()
{
    using Coord = std::tuple<uint8_t, uint8_t, uint8_t>;
    std::map<Coord, std::map<uint8_t, json>> device_map;

    // First build a map of devices by coordinate. This allows us to gather all the devices
    // which are attached to a single port together. It also implicitly allows later device
    // assignments to override earlier ones, just as for normal scalar parameters.
    //
    for (auto& dev_cfg : devices_cfg.val()) {
        auto [x, y, port, device] = dev_cfg["position"].get<std::tuple<uint8_t, uint8_t, uint8_t, uint8_t>>();
        device_map[{x, y, port}][device] = dev_cfg;
    }

    for (auto& [coord, devices] : device_map) {
        auto [x, y, port] = coord;
        auto attach_point = &mesh->get_device_port(x, y, port);
        size_t num_devices = devices.size();

        auto& dev_cfg = devices.begin()->second;

        if (dev_cfg.contains("via")) {
            DieConnector* rdi;

            if (dev_cfg["via"].get<std::string>() == "rdi")
                rdi = make_device<RDI>("RDI", fmt::format("rdi_X{}_Y{}_P{}", x, y, port));
            else if (dev_cfg["via"].get<std::string>() == "idt")
                rdi = make_device<IDT>("IDT", fmt::format("rdi_X{}_Y{}_P{}", x, y, port));
            else
                throw std::runtime_error(fmt::format("unknown via {}", dev_cfg["via"].get<std::string>()));

            rdi->a.connect(*attach_point, fmt::format("rdi_X{}_Y{}_P{}", x, y, port));
            attach_point = &rdi->b;
        }

        if (num_devices > 1) {
            auto cpx_name = fmt::format("cpx_X{}_Y{}_P{}", x, y, port);
            get_environment()->parameters.add_setting(
                fmt::format("{}.{}.n", name(), cpx_name), num_devices, "Soc config");
            auto cpx = make_device<CPX>("CPX", cpx_name);
            auto device_vec = make_devices(nullptr, devices, x, y, port);

            for (size_t i = 0; i < device_vec.size(); ++i)
                device_vec[i]->get_mesh_port().connect(cpx->device_port[i],
                                                       fmt::format("X{}_Y{}_P{}_D{}", x, y, port, i));

            cpx->mesh_port.connect(*attach_point, fmt::format("X{}_Y{}_P{}", x, y, port));
        } else {
            auto node_id = NodeId{x, y, port, 0};
            auto& dev_cfg = devices.begin()->second;
            auto device = make_device_from_cfg(dev_cfg, node_id, nullptr);

            device->get_mesh_port().connect(*attach_point, fmt::format("X{}_Y{}_P{}", x, y, port));
        }
    }
}

static int get_nrsp(const std::vector<MeshDevice*>& device_vec, uint8_t x, uint8_t y, uint8_t p, uint8_t die_id)
{
    int nrsp = device_vec[0]->get_mesh_port().nrsp;

    for (auto& dev : device_vec) {
        if (dev->get_mesh_port().nrsp != nrsp)
            AMPERE_ERROR("number of response channels disagree between devices connected to port [{},{},{}] on die {}",
                         x,
                         y,
                         p,
                         die_id);
    }

    return nrsp;
}

void Soc::attach_devices_to_die(Die& d)
{
    using Coord = std::tuple<uint8_t, uint8_t, uint8_t, uint8_t>;
    std::map<Coord, std::map<uint8_t, json>> device_map;

    bool using_z = false;

    // First build a map of devices by coordinate. This allows us to gather all the devices
    // which are attached to a single port together. It also implicitly allows later device
    // assignments to override earlier ones, just as for normal scalar parameters.
    //
    for (auto& dev_cfg : d.get_devices()) {
        auto num_coords = dev_cfg["position"].size();

        if (num_coords == 4) {
            auto [x, y, port, device] = dev_cfg["position"].get<std::tuple<uint8_t, uint8_t, uint8_t, uint8_t>>();
            device_map[{y, x, 0, port}][device] = dev_cfg;
        } else if (num_coords == 5) {
            auto [x, y, z, port, device] =
                dev_cfg["position"].get<std::tuple<uint8_t, uint8_t, uint8_t, uint8_t, uint8_t>>();
            device_map[{y, x, z, port}][device] = dev_cfg;
            using_z = true;
        } else {
            AMPERE_ERROR("expected 4 or 5 entries in \"position\"; got {}", num_coords);
        }
    }

    for (auto& dev_cfg : d.get_devices()) {
        if (dev_cfg.contains("agent_id") && dev_cfg.contains("devtag")) {
            auto agent_id = dev_cfg["agent_id"].get<uint8_t>();
            auto dev_tag = dev_cfg["devtag"].get<uint8_t>();
            auto tag = std::make_tuple(d.get_die_id(), dev_tag);
            auto nodeid = NodeId{d.get_type(), socket_id, d.get_die_id(), agent_id};
            auto it = nodeid_tags.find(tag);
            if (it == nodeid_tags.end()) {
                auto type_name = dev_cfg["type"].get<std::string>();
                auto prefix = dev_cfg.contains("prefix") ? dev_cfg["prefix"].get<std::string>() : "";
                auto dev_name = gen_device_name(prefix, type_name);
                std::vector<NodeId> nid;
                nid.push_back(nodeid);
                nodeid_tags[tag] = std::make_tuple(dev_name, nid);
            } else {
                auto dev_name = std::get<0>(nodeid_tags[tag]);
                auto nid = std::get<1>(nodeid_tags[tag]);
                nid.push_back(nodeid);
                auto& params = get_environment()->parameters;
                params.add_setting(fmt::format("{}.{}.node_id", name(), dev_name), nid, "Soc config");
            }
        }
    }

    for (auto& [coord, devices] : device_map) {
        auto [y, x, z, port] = coord;
        auto attach_point = &d.get_mesh().get_device_port(x, y, z, port);
        size_t num_devices = devices.size();

        auto& dev_cfg = devices.begin()->second;

        if (dev_cfg.contains("via")) {
            if (dev_cfg["via"].get<std::string>() == "rdi") {
                auto rdi = d.get_rdi_obj(x, y, port);
                rdi->a.connect(*attach_point,
                               using_z ? fmt::format("rdi_X{}_Y{}_Z{}_P{}", x, y, z, port)
                                       : fmt::format("rdi_X{}_Y{}_P{}", x, y, port));
                attach_point = &rdi->b;
            }
        }

        auto device_vec = make_devices(&d, devices, x, y, z, port);
        int nrsp = get_nrsp(device_vec, x, y, port, d.get_die_id());

        bool use_cpx = device_vec.size() > 1 || (nrsp != attach_point->nrsp);
        std::string conn_name;

        if (use_cpx) {
            std::string cpx_name;

            if (using_z)
                cpx_name = fmt::format("cpx_die{}_X{}_Y{}_Z{}_P{}", d.get_die_id(), x, y, z, port);
            else
                cpx_name = fmt::format("cpx_die{}_X{}_Y{}_P{}", d.get_die_id(), x, y, port);

            get_environment()->parameters.add_setting(
                fmt::format("{}.{}.n", name(), cpx_name), num_devices, "Soc config");
            get_environment()->parameters.add_setting(fmt::format("{}.{}.nrsp", name(), cpx_name), nrsp, "Soc config");

            auto cpx = make_device<CPX>("CPX", cpx_name, d.get_clock());

            for (size_t i = 0; i < device_vec.size(); ++i) {
                std::string dev_conn_name;

                if (using_z)
                    dev_conn_name = fmt::format("die{}_X{}_Y{}_Z{}_P{}_D{}", d.get_die_id(), x, y, z, port, i);
                else
                    dev_conn_name = fmt::format("die{}_X{}_Y{}_P{}_D{}", d.get_die_id(), x, y, port, i);

                device_vec[i]->get_mesh_port().connect(cpx->device_port[i], dev_conn_name);
            }

            if (using_z)
                conn_name = fmt::format("die{}_X{}_Y{}_Z{}_P{}", d.get_die_id(), x, y, z, port);
            else
                conn_name = fmt::format("die{}_X{}_Y{}_P{}", d.get_die_id(), x, y, port);

            cpx->mesh_port.connect(*attach_point, conn_name);
        } else {
            if (dev_cfg.contains("MeshPort")) {
                auto mesh_port = dev_cfg["MeshPort"].get<int>();

                if (using_z)
                    conn_name = fmt::format("die{}_X{}_Y{}_Z{}_P{}_MP{}", d.get_die_id(), x, y, z, port, mesh_port);
                else
                    conn_name = fmt::format("die{}_X{}_Y{}_P{}_MP{}", d.get_die_id(), x, y, port, mesh_port);

                device_vec[0]->get_mesh_port(mesh_port).connect(*attach_point, conn_name);
            } else {
                if (using_z)
                    conn_name = fmt::format("die{}_X{}_Y{}_Z{}_P{}", d.get_die_id(), x, y, z, port);
                else
                    conn_name = fmt::format("die{}_X{}_Y{}_P{}", d.get_die_id(), x, y, port);

                device_vec[0]->get_mesh_port().connect(*attach_point, conn_name);
            }
        }
    }
}

void Soc::connect_die()
{
    for (auto& rdi_conn_cfg : rdi_cfg.val()) {
        auto a_die_num = rdi_conn_cfg["a"]["die"].get<int>();
        auto b_die_num = rdi_conn_cfg["b"]["die"].get<int>();

        auto& a_die = die.at(a_die_num);
        auto& b_die = die.at(b_die_num);

        NodeId a_node_id;
        NodeId b_node_id;

        rdi_conn_cfg["a"]["position"].get_to(a_node_id);
        rdi_conn_cfg["b"]["position"].get_to(b_node_id);

        auto& a_pos = a_die.get_mesh().get_device_port(a_node_id.x(), a_node_id.y(), a_node_id.port());
        auto& b_pos = b_die.get_mesh().get_device_port(b_node_id.x(), b_node_id.y(), b_node_id.port());

        auto rdi_name = fmt::format("rdi_die{}_X{}_Y{}_P{}_to_die{}_X{}_Y{}_P{}",
                                    a_die_num,
                                    a_node_id.x(),
                                    a_node_id.y(),
                                    a_node_id.port(),
                                    b_die_num,
                                    b_node_id.x(),
                                    b_node_id.y(),
                                    b_node_id.port());

        if (rdi_conn_cfg.contains("parameters")) {
            for (auto& it : rdi_conn_cfg["parameters"].items()) {
                get_environment()->parameters.add_setting(
                    fmt::format("{}.{}.{}", name(), rdi_name, it.key()), it.value(), "Soc config");
            }
        }

        auto rdi = make_device<DieConnector>(
            "DieConnector",
            rdi_name,
            a_die.get_clock()); // FIXME: Which clock? RDI should really be like a clock crossing

        rdi->a.connect(a_pos, fmt::format("{}_conn_a", rdi_name));
        rdi->b.connect(b_pos, fmt::format("{}_conn_b", rdi_name));

        IDT* idt = dynamic_cast<IDT*>(rdi);

        if (idt != nullptr) {
            idt->set_a_to_b_lrid(b_node_id, b_die_num);
            idt->set_b_to_a_lrid(a_node_id, a_die_num);
        }
    }
}

template<typename T>
T* Soc::make_device(const std::string& type_name, const std::string& dev_name, Clock* clock)
{
    // Provide an opportunity for the user to override the type with a specific parameter.
    auto full_dev_type_name = fmt::format("{}.{}.type", name(), dev_name);
    auto setting = get_environment()->parameters.match_setting(full_dev_type_name);

    std::string real_type_name;

    if (setting)
        real_type_name = setting->value().get<std::string>();
    else
        real_type_name = type_name;

    std::unique_ptr<T> dev;

    if (clock)
        dev = make_registered_module<T>(real_type_name, dev_name, clock);
    else
        dev = make_registered_module<T>(real_type_name, dev_name);

    T* ret = dev.get();
    auto mesh_dev = std::unique_ptr<MeshDevice>(static_cast<MeshDevice*>(dev.release()));
    devices.push_back(std::move(mesh_dev));
    return ret;
}

MeshDevice* Soc::make_device_with_node_id(const std::string& type_name,
                                          const std::string& dev_name,
                                          NodeId node_id,
                                          Clock* clock)
{
    auto& params = get_environment()->parameters;
    params.add_setting(fmt::format("{}.{}.node_id", name(), dev_name), node_id, "Soc config");
    auto dev = make_device<MeshDevice>(type_name, dev_name, clock);
    device_index[node_id] = dev;
    return dev;
}

MeshDevice* Soc::make_device_from_cfg(const json& dev_cfg, NodeId node_id, Clock* clock)
{
    auto type_name = dev_cfg["type"].get<std::string>();
    auto prefix = dev_cfg.contains("prefix") ? dev_cfg["prefix"].get<std::string>() : "";
    auto dev_name = gen_device_name(prefix, type_name);
    return make_device_with_node_id(type_name, dev_name, node_id, clock);
}

std::vector<MeshDevice*> Soc::make_devices(
    Die* d, const std::map<uint8_t, json>& devices, uint8_t x, uint8_t y, uint8_t port)
{
    return make_devices(d, devices, x, y, 0, port);
}

std::vector<MeshDevice*> Soc::make_devices(
    Die* d, const std::map<uint8_t, json>& devices, uint8_t x, uint8_t y, uint8_t z, uint8_t port)
{
    std::vector<MeshDevice*> built_devices;

    for (auto& [device, dev_cfg] : devices) {
        if (d) {
            auto nodeid = NodeId{d->get_type(), socket_id, d->get_die_id(), dev_cfg["agent_id"]};
            if (dev_cfg.contains("devtag")) {
                auto tag = std::make_tuple(d->get_die_id(), dev_cfg["devtag"]);
                auto it = device_tags.find(tag);
                if (it == device_tags.end()) {
                    auto type_name = dev_cfg["type"].get<std::string>();
                    auto dev_name = std::get<0>(nodeid_tags[tag]);
                    auto dev = make_device<MeshDevice>(type_name, dev_name, d->get_clock());
                    device_tags[tag] = dev;
                    built_devices.push_back(dev);
                } else {
                    built_devices.push_back(device_tags[tag]);
                }
            } else {
                built_devices.push_back(make_device_from_cfg(dev_cfg, nodeid, d->get_clock()));
            }
        } else {
            AMPERE_ASSERT(z == 0, "unexpected non-zero z ({}) when constructing legacy NodeId", z);
            built_devices.push_back(make_device_from_cfg(dev_cfg, NodeId{x, y, port, device}, nullptr));
        }
    }

    return built_devices;
}

std::string Soc::gen_device_name(const std::string& prefix, const std::string& type_name) const
{
    std::string instance_name_root;

    if (prefix.empty()) {
        instance_name_root = type_name;
        std::transform(instance_name_root.begin(), instance_name_root.end(), instance_name_root.begin(), ::tolower);
    } else {
        instance_name_root = prefix;
    }

    return sc_core::sc_gen_unique_name(instance_name_root.c_str());
}

void Soc::construct_channel_map(void)
{
    for (auto& channel : get_channel_map()) {
        auto [x, y, port] = channel["tgt"].get<std::tuple<uint8_t, uint8_t, uint8_t>>();
        channel_map[{x, y, port}] = channel["channel"].get<int>();
    }
}

int Soc::channel(const chi::NodeId id) const
{
    if (channel_map.size()) {
        std::tuple<uint8_t, uint8_t, uint8_t> channel_id(id.x(), id.y(), id.port());
        auto ch = channel_map.find(channel_id);
        if (ch != channel_map.end())
            return ch->second;
    }

    return 0;
}

std::vector<NodeId> Soc::stash_nodes(uint8_t die_id) const
{
    std::vector<NodeId> node_list;

    for (auto& [node_id, dev] : device_index) {
        if (node_id.die_id() == die_id) {
            auto stasher = dynamic_cast<Stasher*>(dev);
            if (stasher && stasher->can_stash())
                node_list.push_back(node_id);
        }
    }

    return node_list;
}

std::set<NodeId> Soc::pmha_nodes() const
{
    std::set<NodeId> pmha_nodes;

    for (auto& [node_id, dev] : device_index) {
        auto pha = dynamic_cast<mesh::PHA*>(dev);
        if (pha && pha->is_pmha())
            pmha_nodes.emplace(node_id);
    }

    return pmha_nodes;
}

void Soc::start_of_simulation()
{
    if (warmstart_enable) {
        if (!warmup_ms.val().empty())
            warmstart_from_ms(warmup_ms);

        if (!warmstart_table_file.val().empty()) {
            std::ifstream infile{warmstart_table_file.val()};
            json table;
            infile >> table;
            warmstart_from_table(table);
        }

        warmstart_from_table(warmstart_table);
        warmup.warmstart_from_warmstarters();
    }
}

void Soc::warmstart_from_ms(const std::string& filename)
{
    ms::MemoryStreamReader ms_reader(filename.c_str());
    ms::Access ms_access;
    ms::Event ms_event;
    uint64_t warmup_fills{0};
    uint64_t warmup_fill_invalid{0};
    uint64_t warmup_hits{0};
    uint64_t warmup_evicts{0};
    uint64_t warmup_fill_again{0};
    std::set<PhysAddr> warmup_lines;

    fmt::print(std::cout, "Warmstart: {} with file {}\n", unified_warmstart ? "Unified" : "SLC", filename);

    // Warmup transactions are injected from the "perspective" of a particular core or
    // injector. Namely, we need access to the SAM that will route our warmup request to
    // the correct agent to receive it. This did not used to be this case in this warmup
    // function, which is why the code below seems a little kludgy. I'm not sure how to
    // improve it; possibly we could require that a warm-up core be pre-declared in a
    // config file, but that would require a lot of separate changes to config files. So
    // instead we do this search to find the first device that seems to have a suitable
    // kind of requester SAM (RN_SAM or CDRN_SAM).
    //
    auto warmup_node = std::find_if(device_index.begin(), device_index.end(), [](const auto& dev) -> bool {
        auto [nid, device] = dev;
        auto sam = device->get_sam();
        if (dynamic_cast<const RN_SAM*>(sam) || dynamic_cast<const CDRN_SAM*>(sam))
            return true;
        return false;
    });

    AMPERE_ASSERT(warmup_node != device_index.end(),
                  "could not find a SAM through which to inject warmup transactions");
    const SAM* sam = warmup_node->second->get_sam();
    auto core = dynamic_cast<ampere::core::Core*>(warmup_node->second);
    AMPERE_ASSERT(!(unified_warmstart && !core), "Warmup node is not a core, can't run unified warmstart");

    while (true) {
        if (1 == ms_reader.version()) {
            if (!ms_reader.ProtobufStreamReader::read(ms_access))
                break;
        } else {
            if (!ms_reader.read(ms_event))
                break;
            if (ms_event.has_control()) {
                const auto& control = ms_event.control();
                // v2 memory streams filter through a 2MB cache, so we want to use the cache dump at the end to preload
                // the L2. But v3 streams filter through L1-sized caches, so the L2 has already seen the correct
                // traffic. Also, if we're only warming up the SLC then we should always ignore the core-cache-sized
                // dump at the end of the stream.
                if (control.type() == ms::Control_Type_END_MEMORY && (ms_reader.version() == 3 || !unified_warmstart))
                    break;
                continue;
            }
            AMPERE_ASSERT(ms_event.has_access(), "Memory event has no access");
            ms_access.Swap(ms_event.mutable_access());
        }

        // skip bogus PAs of zero
        auto ms_addr = PhysAddr(ms_access.pa()).line_address();
        if (ms_addr == 0x0_pa)
            continue;

        std::optional<PhysAddr> slc_victim;
        std::optional<PhysAddr> slc_warmup_addr;
        if (unified_warmstart) {
            auto [l2_victim_addr, l2_victim_state] = core->warmstart_access(ms_access);

            if (l2_victim_state != chi::FinalState::I) {
                // L2 returned a victim, which means this access was a miss. So SLC will first see
                // the L2 miss (which, if it hits in SLC, might invalidate a line), and then it
                // will see a fill of the L2's victim.
                auto read_action =
                    (ms_access.type() == ms::Access_Type_WRITE) ? WarmupInfo::READ_UNIQUE : WarmupInfo::READ;
                auto hit = warm(ms_addr, read_action, sam);
                if (hit)
                    warmup_hits++;

                auto slc_fill_action =
                    (l2_victim_state == chi::FinalState::UD) ? WarmupInfo::FILL_DIRTY : WarmupInfo::FILL_CLEAN;
                slc_victim = warm(l2_victim_addr, slc_fill_action, sam);
                slc_warmup_addr = l2_victim_addr;
            }
        } else {
            // grab the access type
            chi::FinalState state;
            if (ms::Access_Type_WRITE == ms_access.type()) {
                state = chi::FinalState::UD;
            } else {
                state = chi::FinalState::UC;

                // disabling uc writeevict means only code and dirty data would
                // use the SLC, and clean data would not be loaded
                if (!warmstart_uc_writeevict && ms::Access_Source_DATA == ms_access.source())
                    continue;
            }

            slc_victim = warm(ms_addr, state, sam);
            slc_warmup_addr = ms_addr;
        }

        // Track warmup statistics, if we sent something to the SLC. slc_victim will be null
        // on hit, 0 on a fill to invalid way, or address of the victim.
        if (slc_warmup_addr) {
            warmup_fills++;
            auto [_, new_line] = warmup_lines.insert(*slc_warmup_addr);
            if (!new_line)
                warmup_fill_again++;
            if (slc_victim) {
                if (*slc_victim != 0x0_pa)
                    warmup_evicts++;
                else
                    warmup_fill_invalid++;
            } else {
                warmup_hits++;
            }
        }

        // clear out to read next transaction
        ms_access.Clear();
    }

    if (unified_warmstart)
        core->warmstart_complete();

    fmt::print(std::cout,
               "Warmstart: SLC completed {} fills with {} unique lines: {} fills to invalid way, {} hits, {} "
               "evicts, {} duplicate fills\n",
               warmup_fills,
               warmup_lines.size(),
               warmup_fill_invalid,
               warmup_hits,
               warmup_evicts,
               warmup_fill_again);
}

static chi::FinalState state_name_to_state(const std::string& state_name)
{
    if (state_name == "I")
        return chi::FinalState::I;
    else if (state_name == "SC")
        return chi::FinalState::SC;
    else if (state_name == "UC")
        return chi::FinalState::UC;
    else if (state_name == "UD")
        return chi::FinalState::UD;
    else if (state_name == "SD")
        return chi::FinalState::SD;

    AMPERE_FATAL("unrecognized cache state {}", state_name);
    return chi::FinalState::I;
}

void Soc::warmstart_from_table(const json& json_table)
{
    auto table = json_table.get<std::vector<json>>();

    for (auto& entry : table) {
        PhysAddr addr;
        PhysAddr stop_addr;

        if (entry.contains("address")) {
            addr = PhysAddr{entry["address"].get<std::uint64_t>()};
            stop_addr = addr + 64;
        } else if (entry.contains("start_address")) {
            addr = PhysAddr{entry["start_address"].get<std::uint64_t>()};
            stop_addr = PhysAddr{entry["stop_address"].get<std::uint64_t>()};
        }

        auto state_name = entry["state"].get<std::string>();
        auto state = state_name_to_state(state_name);

        uint8_t socket_id{0};
        uint8_t die_id{0};
        bool warm_slc{true};

        if (entry.contains("die_id")) {
            socket_id = entry["socket_id"].get<uint8_t>();
            die_id = entry["die_id"].get<uint8_t>();
            warm_slc = false;
        }

        auto warmup_node =
            std::find_if(device_index.begin(), device_index.end(), [socket_id, die_id](const auto& dev) -> bool {
                auto [nid, device] = dev;
                if (nid.nodeid_type() != NodeId::NodeId_Type::Global
                    || (nid.socket_id() == socket_id && nid.die_id() == die_id))
                {
                    auto sam = device->get_sam();
                    if (dynamic_cast<const RN_SAM*>(sam) || dynamic_cast<const CDRN_SAM*>(sam))
                        return true;
                }
                return false;
            });

        AMPERE_ASSERT(warmup_node != device_index.end(),
                      "could not find a SAM through which to inject warmup transactions");
        const SAM* sam = warmup_node->second->get_sam();

        if (warm_slc) {
            while (addr < stop_addr) {
                warm(addr, state, sam);
                addr += 64;
            }
        } else {
            while (addr < stop_addr) {
                warm(addr, state, warmup_node->first, sam);
                addr += 64;
            }
        }
    }
}

std::optional<PhysAddr> Soc::warm(PhysAddr addr, WarmupInfo::Action action, const SAM* sam)
{
    AMPERE_ASSERT(num_die == 0, "warm() without socket/die should not be called in multi-die mode");
    auto node_id = sam->lookup(addr);
    auto hn = dynamic_cast<HomeNode*>(device_index.at(node_id));
    AMPERE_ASSERT(hn, "device at node {} is not a HomeNode", node_id);

    return hn->preload_slc(addr, action);
}

std::optional<PhysAddr> Soc::warm(PhysAddr addr, chi::FinalState state, const SAM* sam)
{
    if (state == chi::FinalState::SC || state == chi::FinalState::UC)
        return warm(addr, WarmupInfo::FILL_CLEAN, sam);
    else if (state == chi::FinalState::SD || state == chi::FinalState::UD)
        return warm(addr, WarmupInfo::FILL_DIRTY, sam);
    else
        AMPERE_ERROR("SOC warm() got an unexpected state {}", state);

    return std::nullopt;
}

void Soc::warm(PhysAddr addr, chi::FinalState state, chi::NodeId requester_node, const SAM* sam)
{
    auto node_id_to_stasher = [this](NodeId node_id) -> Stasher* {
        auto dev = device_index.at(node_id);
        auto stasher = dynamic_cast<Stasher*>(dev);
        AMPERE_ASSERT(stasher, "Expected {} to be a Stasher; it was not", node_id);
        return stasher;
    };

    AMPERE_ASSERT(num_die > 0, "warm() with socket/die should not be called in non-multi-die mode");
    auto node_id = sam->lookup(addr);
    auto stasher = node_id_to_stasher(node_id);
    stasher->stash_preload(addr, state, node_id_to_stasher, requester_node);
}

void Soc::dump(std::ostream& os) const
{
    auto oldest_req = MeshReqObj::get_oldest_active_request();

    if (oldest_req) {
        fmt::print("oldest active mesh request: Opcode={} Addr=0x{} SrcID={} TgtID={} TxnID={}\n",
                   oldest_req->Opcode,
                   oldest_req->Addr,
                   oldest_req->SrcID,
                   oldest_req->TgtID,
                   oldest_req->TxnID);
    }
}

void Soc::dump_phas(std::ostream& os) const
{
    fmt::print(os, "==== PHA DUMP AT TIME {} ====\n", sc_core::sc_time_stamp());
    for (auto& child : get_child_objects()) {
        auto pha = dynamic_cast<mesh::PHA*>(child);
        if (pha)
            pha->dump_tracker(os);
    }
}

void Soc::dump_injectors(std::ostream& os) const
{
    fmt::print(os, "==== INJECTOR DUMP AT TIME {} ====\n", sc_core::sc_time_stamp());
    for (auto& child : get_child_objects()) {
        auto inj = dynamic_cast<mesh::MeshInjector*>(child);
        if (inj)
            inj->dump(os);
    }
}

void Soc::periodic_pha_dump()
{
    dump_phas(*pha_dump_os.get());
    dump_injectors(*pha_dump_os.get());
    pha_dump_event.notify(pha_dump_interval.val());
}

} // namespace ampere::soc

/*
 * vim: sw=4:ts=4:et:si:cindent:cino=\:0,(0,g0,t0:
 */
