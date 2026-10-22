//
// Copyright (c) 2020 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//
#pragma once

#include "ampere/mesh/mesh_req.h"
#include "ampere/mesh/tracker.enum.h"
#include "ampere/soc/warmup_info.h"
#include "ampere/sc/sc.h"
#include "ampere/uarch_common/cache.h"
#include "ampere/uarch_common/memreq.h"
#include "ampere/uarch_common/memresp.h"

namespace ampere::mesh {

using namespace ampere::sc;
using namespace ampere::archx;
using namespace ampere::uarch_common;

class NewSLC : public Module
{
public:
    explicit NewSLC(const sc_core::sc_module_name& name) : Module{name} {}

    enum class State {
        INVALID,
        CLEAN,
        DIRTY
    };

    CacheStatus read(const MeshReq& req, PhysAddr addr);
    CacheStatus read_invalidate(const MeshReq& req, PhysAddr addr);
    CacheStatus combined_read(const MeshReq& req, PhysAddr addr, bool invalidate);
    std::pair<CacheStatus, PhysAddr> fill(const MeshReq& req, PhysAddr addr, State state);

    // Testability hook
    std::optional<PhysAddr> preload_fill(PhysAddr addr, State state);
    std::optional<PhysAddr> preload(PhysAddr addr, soc::WarmupInfo::Action action);
    std::optional<PhysAddr> preload(PhysAddr addr, State state);

private:
    Cache<PhysAddr, std::pair<PhysAddr, State>> array{"array"};
    Param<bool> all_hits_invalidate{"all_hits_invalidate", "Invalidate line on any read hit", false};
    Param<bool> warmstart_enable{"warmstart_enable", "enable warmstart for SLC", true};
    Param<bool> warmstart_uc_writeevict{
        "warmstart_uc_writeevict", "disable this to load up only code or dirty data", true};
    Param<std::string> memory_stream_name{
        "warmup_ms", "name of the memory stream file used for preloading SL cache (warmstart)", ""};
    void ms_warmstart(std::string filename);

protected:
    void end_of_elaboration() override;
};

} // namespace ampere::mesh

/*
 * vim: sw=4:ts=4:et:si:cindent:cino=\:0,(0,g0,t0:
 */
