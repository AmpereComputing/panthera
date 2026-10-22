//
// Copyright (c) 2023 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//
#pragma once

#include "ampere/mesh/mesh_dat.h"
#include "ampere/mesh/mesh_rsp.h"
#include "ampere/mesh/pha_tracker_entry.h"

namespace ampere::mesh::pha_fsm {

struct PhaCacheStateFsmNoTransition : public std::runtime_error
{
    using std::runtime_error::runtime_error;
};

int pha_state_fsm_recv_dat(mesh::PHATrackerEntry& entry, MeshDat dat);
int pha_state_fsm_recv_rsp(mesh::PHATrackerEntry& entry, MeshRsp rsp);
int pha_state_fsm_pipe(mesh::PHATrackerEntry& entry);

} // namespace ampere::mesh::pha_fsm
