//
// Copyright (c) 2021 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//
#include "ampere/mesh/mesh_req.h"

namespace ampere::mesh {

std::map<std::uint64_t, MeshReqObj*> MeshReqObj::active_requests;

MeshReqObj* MeshReqObj::get_oldest_active_request()
{
    if (active_requests.empty())
        return nullptr;
    else
        return active_requests.begin()->second;
}

} // namespace ampere::mesh
