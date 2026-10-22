//
// Copyright (c) 2020 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//

#include "ampere/mesh/slc.h"
#include "ampere/mesh/mesh_req.h"
#include "ampere/memory_stream/memory_stream.h"
#include "ampere/sc/sc.h"
#include "ampere/uarch_common/memreq.h"

namespace ampere::mesh {

CacheStatus NewSLC::combined_read(const MeshReq& req, PhysAddr addr, bool invalidate)
{
    auto hit = array.lookup(req, addr);

    if (hit) {
        auto state = hit->second;
        AMPERE_ASSERT(state != State::INVALID, "SLC read hit on invalid line");

        bool do_inval = invalidate || all_hits_invalidate;

        if (do_inval)
            array.invalidate(req, addr);

        // If we are not invalidating the hit line, always report a clean hit so that the line goes to the
        // core as clean. We only report a dirty hit if invalidating dirty data, because in that case the
        // dirty state must be transferred to the core so that it's not lost.
        if (state == State::DIRTY && do_inval)
            return CacheStatus::HitDirty;
        else
            return CacheStatus::HitClean;
    }

    return CacheStatus::Miss;
}

CacheStatus NewSLC::read(const MeshReq& req, PhysAddr addr)
{
    return combined_read(req, addr, false /* invalidate on hit */);
}

CacheStatus NewSLC::read_invalidate(const MeshReq& req, PhysAddr addr)
{
    return combined_read(req, addr, true /* invalidate on hit */);
}

std::pair<CacheStatus, PhysAddr> NewSLC::fill(const MeshReq& req, PhysAddr addr, State state)
{
    // If the line is already dirty in the SLC, then it should stay dirty
    auto hit = array.lookup(req, addr, false /* update LRU */, true /* suppress event */);
    AMPERE_ASSERT(!(hit && hit->second == State::INVALID), "SLC fill hit on invalid line");
    if (hit && hit->second == State::DIRTY)
        state = State::DIRTY;

    auto victim = array.refill(req, addr, std::make_pair(addr, state));

    AMPERE_ASSERT(!(hit && victim), "Fill hit in SLC but also evicted something");
    AMPERE_ASSERT(!(victim && victim->second == State::INVALID), "SLC victim was an invalid line");

    if (victim && victim->second == State::DIRTY)
        return {CacheStatus::HitDirty, victim->first};
    else
        return {CacheStatus::Miss, 0_pa};
}

// called after all objects exist and before simulation starts
void NewSLC::end_of_elaboration()
{
    // if slc memory stream exists, run warmstart
    if (memory_stream_name.val().length() && warmstart_enable)
        ms_warmstart(memory_stream_name);
}

// read in memory stream file and warm up just the SLC
void NewSLC::ms_warmstart(std::string filename)
{
    ms::MemoryStreamReader ms_reader(filename.c_str());
    ms::Access ms_access;
    long access_count(0);

    fmt::print(std::cout, "Warmstart: SLC with file {}\n", filename);

    while (ms_reader.read(ms_access)) {
        State state;

        // grab address and calculate location
        auto addr = PhysAddr(ms_access.pa()).line_address();

        // grab the access type
        if (ms::Access_Type_WRITE == ms_access.type()) {
            state = State::DIRTY;
        } else {
            state = State::CLEAN;

            // disabling uc writeevict means only code and dirty data would
            // use the SLC, and clean data would not be loaded
            if (!warmstart_uc_writeevict && ms::Access_Source_DATA == ms_access.source())
                continue;
        }

        // craft the request
        MemReq req;

        // lookup cache: return false means missed
        if (!array.lookup(req, addr, true, true /* suppress event */, false)) {
            // evict and refill: right now this just drops in place
            array.refill(req, addr, std::make_pair(addr, state), true /* suppress event */);
        }

        // clear out to read next transaction
        ms_access.Clear();
        access_count++;
    }
    fmt::print(std::cout, "Warmstart: SLC completed {} transactions\n", access_count);
}

std::optional<PhysAddr> NewSLC::preload_fill(PhysAddr addr, State state)
{
    auto txn = Txn{};

    if (!array.lookup(txn, addr, true /* update LRU */, true /* suppress event */)) {
        auto victim = array.refill(txn, addr, std::make_pair(addr, state), true /* suppress event */);
        if (victim.has_value())
            return victim.value().first;
        else
            return 0x0_pa; // filled into invalid way
    }

    return std::nullopt;
}

std::optional<PhysAddr> NewSLC::preload(PhysAddr addr, soc::WarmupInfo::Action action)
{
    auto req = MeshReq{};

    switch (action) {
    case soc::WarmupInfo::READ:
        if (read(req, addr) == CacheStatus::Miss)
            return std::nullopt;
        else
            return addr;
    case soc::WarmupInfo::READ_UNIQUE:
        if (read_invalidate(req, addr) == CacheStatus::Miss)
            return std::nullopt;
        else
            return addr;
    case soc::WarmupInfo::FILL_CLEAN:
        return preload_fill(addr, State::CLEAN);
    case soc::WarmupInfo::FILL_DIRTY:
        return preload_fill(addr, State::DIRTY);
    default:
        AMPERE_ERROR("Unexpected SLC preload action");
        return std::nullopt;
    }
}

std::optional<PhysAddr> NewSLC::preload(PhysAddr addr, State state)
{
    auto action = (state == State::DIRTY) ? soc::WarmupInfo::FILL_DIRTY : soc::WarmupInfo::FILL_CLEAN;
    return preload(addr, action);
}

} // namespace ampere::mesh

/*
 * vim: sw=4:ts=4:et:si:cindent:cino=\:0,(0,g0,t0:
 */
