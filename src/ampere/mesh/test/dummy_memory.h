//
// Copyright (c) 2020 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//

#ifndef AMPERE_MESH_TEST_DUMMY_MEMORY_H
#define AMPERE_MESH_TEST_DUMMY_MEMORY_H

#include <deque>
#include <map>
#include <set>
#include <vector>

#include "ampere/mesh/mesh_port.h"
#include "ampere/sc/sc.h"

namespace ampere::mesh {

class DummyMemory : public sc::Module
{
public:
    DummyMemory(const sc_core::sc_module_name& name, chi::NodeId id) : sc::Module{name}, m_id{id}
    {
        free_dbid_list.resize(64);
        std::iota(free_dbid_list.begin(), free_dbid_list.end(), 0);
    }
    MeshPort mesh_port;

    bool run() override;
    bool has_write(PhysAddr addr) const { return writes.contains(addr); }
    bool has_prefetch_tgt(PhysAddr addr) const { return prefetch_targets.contains(addr); }

    void save_requests() { save_incoming_requests = true; }
    const std::vector<MeshReq>& get_saved_requests() const { return saved_requests; }

private:
    chi::NodeId m_id;
    std::deque<uint16_t> free_dbid_list;
    std::map<uint16_t, PhysAddr> pending_writes;
    std::set<PhysAddr> writes;
    std::set<PhysAddr> prefetch_targets;
    std::vector<MeshReq> saved_requests;
    bool save_incoming_requests{false};

    MeshDat make_data_from_req(MeshReq& req, chi::DataOpcode opcode);
    uint16_t get_dbid();
    void free_dbid(uint16_t id);
};

} // namespace ampere::mesh

#endif // AMPERE_MESH_TEST_DUMMY_MEMORY_H
