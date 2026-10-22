//
// Copyright (c) 2020 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//

#include "ampere/mesh/mesh_device.h"

namespace ampere::mesh {

template<>
Event& MeshEvents::GetEvent<MeshReq>::send(MeshEvents& cls)
{
    return cls.ev_send_req;
}

template<>
Event& MeshEvents::GetEvent<MeshRsp>::send(MeshEvents& cls)
{
    return cls.ev_send_rsp;
}

template<>
Event& MeshEvents::GetEvent<MeshSnp>::send(MeshEvents& cls)
{
    return cls.ev_send_snp;
}

template<>
Event& MeshEvents::GetEvent<MeshDat>::send(MeshEvents& cls)
{
    return cls.ev_send_dat;
}

template<>
Event& MeshEvents::GetEvent<MeshReq>::recv(MeshEvents& cls)
{
    return cls.ev_recv_req;
}

template<>
Event& MeshEvents::GetEvent<MeshRsp>::recv(MeshEvents& cls)
{
    return cls.ev_recv_rsp;
}

template<>
Event& MeshEvents::GetEvent<MeshSnp>::recv(MeshEvents& cls)
{
    return cls.ev_recv_snp;
}

template<>
Event& MeshEvents::GetEvent<MeshDat>::recv(MeshEvents& cls)
{
    return cls.ev_recv_dat;
}

} // namespace ampere::mesh
