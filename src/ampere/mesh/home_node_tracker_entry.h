//
// Copyright (c) 2020 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//

#ifndef AMPERE_MESH_TRACKER_ENTRY_H
#define AMPERE_MESH_TRACKER_ENTRY_H

#include "ampere/mesh/chi/snoop.h"
#include "ampere/mesh/chi/types.h"
#include "ampere/mesh/mesh_req.h"
#include "ampere/mesh/tracker.enum.h"
#include "ampere/uarch_common/cache.h"
#include <fmt/ostream.h>
#include <optional>

namespace ampere::mesh {

using namespace chi;
using uarch_common::Index;

enum {
    Wait4RRorRSD = 0b0000001,
    Wait4DBID = 0b0000010,
    Wait4Comp = 0b0000100,
    Wait4Data = 0b0001000,
    Wait4CompAck = 0b0010000,
    Wait4SnpCnt0 = 0b0100000,
    Wait4PCrd = 0b1000000,
};

struct HomeNodeTrackerEntry
{
    MeshReq request;
    SnoopStatus snoop_status;
    PhysAddr snoop_address;
    Index sf_set;
    int sf_way;
    uint64_t id;

    SFOp sf_op;
    CacheOp cache_op;

    std::optional<RequestOpcode> request_op;
    std::optional<ResponseOpcode> response_op;
    std::optional<SnoopOpcode> snoop_op;
    std::optional<SnoopOpcode> snoop_owner_op;
    std::optional<DataOpcode> data_op;

    ResponseStatus data_rsp_status;
    uint16_t dbid;

    uint8_t chunk_valid;

    HomeNodeTrackerEntry* wake;
    HomeNodeTrackerEntry* wake_after_clean;

    std::optional<PhysAddr> cache_victim_addr;

    bool locked_sf;
    bool sent_response;
    bool got_compack;
    bool is_mem_request;

    struct
    {
        MainState main;
        int wait;

        bool idle() const { return main == MainState::Idle && wait == 0; }
    } state;

    uint8_t retry_credit;
    bool has_credit;

    bool snp_fwd;

    bool slc_hit_data_pending;

    HomeNodeTrackerEntry(uint8_t id) : id{id} { reset(); }

    void reset()
    {
        request = nullptr;
        snoop_status = SnoopStatus{};
        snoop_address = 0_pa;
        sf_set = std::numeric_limits<std::uint64_t>::max();
        sf_way = -1;
        sf_op = SFOp::None;
        cache_op = CacheOp::None;
        request_op.reset();
        response_op.reset();
        snoop_op.reset();
        snoop_owner_op.reset();
        data_op.reset();
        chunk_valid = 0;
        wake = nullptr;
        wake_after_clean = nullptr;
        cache_victim_addr.reset();
        locked_sf = false;
        sent_response = false;
        got_compack = false;
        is_mem_request = false;
        state.main = MainState::Idle;
        state.wait = 0;
        retry_credit = 0;
        has_credit = false;
        snp_fwd = false;
        slc_hit_data_pending = false;
    }
};

} // namespace ampere::mesh

#endif // AMPERE_MESH_TRACKER_ENTRY_H
