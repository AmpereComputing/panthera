//
// Copyright (c) 2018-2024 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//

#pragma once

#include "ampere/core/l2/l2c.h"
#include "ampere/core/perfect_fe.h"
#include "ampere/core/bpu.h"
#include "ampere/core/gpc.h"
#include "ampere/core/icf.h"
#include "ampere/core/idr.h"
#include "ampere/core/ixu.h"
#include "ampere/core/lsu.h"
#include "ampere/core/mmu.h"
#include "ampere/mesh/mesh_device.h"
#include "ampere/sc/module.h"
#include "ampere/sc/poly.h"
#include "ampere/sc/poly_constructors.h"
#include "ampere/sc/submodule.h"

namespace ampere::soc {
class Warmup;
}

namespace ampere::core {

using namespace ampere::sc;

class Core : public Module,
             public implements<Core,
                               mesh::MeshDevice,
                               poly_constructor_default,
                               poly_constructor_clock,
                               poly_constructor_clock_env>,
             public mesh::Stasher
{
public:
    Core(const sc_core::sc_module_name& name);
    Core(const sc_core::sc_module_name& name, Clock* clock, Environment* env);
    Core(const sc_core::sc_module_name& name, Clock* clock);

    mesh::MeshPort& get_mesh_port(int i = 0) override { return l2c.mesh_port; }
    mesh::NodeId get_node_id(int i = 0) const override { return node_id; }
    const mesh::SAM* get_sam() const override { return l2c.sam.get(); }

    // Stasher interface
    bool can_stash() const override { return true; }
    void stash_preload(PhysAddr, FinalState, mesh::NodeIdToStasher, mesh::chi::NodeId requester_node) override {};
    void stash_invalidate(PhysAddr, NodeId) override{};
    void ms_warmstart(std::string filename) { l2c.ms_warmstart(filename); };
    std::pair<PhysAddr, mesh::chi::FinalState> warmstart_access(ms::Access ms_access)
    {
        return l2c.warmstart_access(ms_access, true);
    }
    void warmstart_complete() { l2c.warmstart_complete(); }

protected:
    void end_of_elaboration() override;
    void end_of_simulation() override;

private:
    void construct();

    Param<std::string> instruction_stream_name{"instruction_stream_name", "name of the instruction stream file", ""};
    Param<uint64_t> skip_instructions{
        "skip_instructions", "Number of instructions to skip from the beginning of the instruction stream", 0};
    Param<uint64_t> machine_width{"machine_width", "4wide or 6wide", 4};
    Param<uint64_t> ixu_width{"ixu_width", "Number of IXU execution pipes, used for TDA", 6};
    Param<uint64_t> fsu_width{"fsu_width", "Number of FSU execution pipes, used for TDA", 3};
    Param<bool> speculative_memory_image{
        "speculative_memory_image", "Load or generate memory image for wrong-path fetch", true};
    Param<bool> use_wrong_path_emulation{"use_wrong_path_emulation", "Use emulation for wrong-path addresses", true};
    Param<std::optional<Cycle>> trap_cycles{"trap_cycles", "pause simulation at cycle", {}};

    // 0 = off; higher values are more do validation (see driver_is.h)
    Param<std::uint64_t> validate_emulation{"validate_emulation", "Validate emulation", 0};
    Param<std::string> mem_image_path{"mem_image_path", "Memory image (*.mem) file path", ""};

    Param<bool> perfect_frontend{"perfect_frontend", "Use the infinitely-fast perfect frontend", false};
    Param<bool> unified_mmu{"unified_mmu", "One MMU to rule them all", false};
    Param<std::vector<std::string>> decoders{
        "decoders", "list of decoder names, in the order they should be checked", "[]"_json};

    // Node ID for the mesh
    Param<mesh::chi::NodeId> node_id{"node_id", "Mesh Node identifier (for routing)", mesh::chi::NodeId{0}};

    Event sim_end_event{"sim_end", "Simulation has ended"};
    EventData<uint64_t> total_cycles{"total_cyles", "Cycles completed in sim"};

    std::unique_ptr<archx::Driver> driver;
    std::unique_ptr<archx::Driver> lsu_oracle_driver;
    std::unique_ptr<archx::Driver> l2_oracle_driver;
    std::unique_ptr<PerfectFE> pfe;
    std::unique_ptr<BPU> bpu;
    std::unique_ptr<ICF> icf;
    std::unique_ptr<MMU> immu;
    IDR idr{"idr"};
    LSU lsu{"lsu"};
    MMU dmmu{"dmmu", static_cast<unsigned>(lsu.load_pipes), lsu.sta_pipes()};
    L2C l2c{"l2c"};

    // scheduler and exec/agu units
    std::unique_ptr<IXU> ixu;

    GPC gpc{"gpc"};

    friend class soc::Warmup;
};

} // namespace ampere::core

/*
 * vim: sw=4:ts=4:et:si:cindent:cino=\:0,(0,g0,t0:
 */
