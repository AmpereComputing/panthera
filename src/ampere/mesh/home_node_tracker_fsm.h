//
// Copyright (c) 2020 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//

#ifndef AMPERE_MESH_TRACKER_FSM_H
#define AMPERE_MESH_TRACKER_FSM_H

#include "ampere/mesh/tracker.enum.h"
#include <fmt/ostream.h>

namespace ampere::mesh {

class HomeNodeTracker;
struct HomeNodeTrackerEntry;

namespace tracker_fsm {

struct Initialize
{
    bool blocked;
};
struct MainPipeStatus
{
    SFStatus sf_status;
    CacheStatus cache_status;
};
struct SnoopResponse
{
    bool forward{false};
};
struct CompAck
{};
struct DataRcvd
{
    uint8_t chunk_valid{0};
    bool fill_cache{false};
    bool is_dirty{false};
};
struct DataReq
{
    uint16_t dbid;
};
struct RetryAck
{
    uint8_t pcrdtype;
};
struct CreditGrant
{
    uint8_t pcrdtype;
};
struct ReadReceipt
{};

void process_event(HomeNodeTracker& tracker, HomeNodeTrackerEntry& entry, const Initialize& ev);
void process_event(HomeNodeTracker& tracker, HomeNodeTrackerEntry& entry, const MainPipeStatus& ev);
void process_event(HomeNodeTracker& tracker, HomeNodeTrackerEntry& entry, const SnoopResponse& ev);
void process_event(HomeNodeTracker& tracker, HomeNodeTrackerEntry& entry, const CompAck& ev);
void process_event(HomeNodeTracker& tracker, HomeNodeTrackerEntry& entry, const DataRcvd& ev);
void process_event(HomeNodeTracker& tracker, HomeNodeTrackerEntry& entry, const DataReq& ev);
void process_event(HomeNodeTracker& tracker, HomeNodeTrackerEntry& entry, const RetryAck& ev);
void process_event(HomeNodeTracker& tracker, HomeNodeTrackerEntry& entry, const CreditGrant& ev);
void process_event(HomeNodeTracker& tracker, HomeNodeTrackerEntry& entry, const ReadReceipt& ev);

} // namespace tracker_fsm

} // namespace ampere::mesh

#endif // AMPERE_MESH_TRACKER_FSM_H
