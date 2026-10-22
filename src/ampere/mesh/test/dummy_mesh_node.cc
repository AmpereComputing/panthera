//
// Copyright (c) 2020 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//

#include "ampere/mesh/test/dummy_mesh_node.h"

namespace ampere::mesh {

bool DummyMeshNode::run()
{
    size_t i = starting_port;

    for (;;) {
        if (!req && mesh_ports[i].rx().req[0]->can_take())
            req = mesh_ports[i].rx().req[0]->take();
        if (!rsp && mesh_ports[i].rx().rsp[0]->can_take())
            rsp = mesh_ports[i].rx().rsp[0]->take();
        if (!rsp && mesh_ports[i].rx().rsp.is_bound(1) && mesh_ports[i].rx().rsp[1]->can_take())
            rsp = mesh_ports[i].rx().rsp[1]->take();
        if (!snp && mesh_ports[i].rx().snp[0]->can_take())
            snp = mesh_ports[i].rx().snp[0]->take();
        if (!dat && mesh_ports[i].rx().dat[0]->can_take())
            dat = mesh_ports[i].rx().dat[0]->take();

        if (req && mesh_ports[req->TgtID.id()].tx().req[0]->can_put()) {
            mesh_ports[req->TgtID.id()].tx().req[0]->put(req);
            req.reset();
        }
        if (rsp && mesh_ports[rsp->TgtID.id()].tx().rsp[0]->can_put()) {
            mesh_ports[rsp->TgtID.id()].tx().rsp[0]->put(rsp);
            rsp.reset();
        }
        if (rsp && mesh_ports[rsp->TgtID.id()].tx().rsp.is_bound(1)
            && mesh_ports[rsp->TgtID.id()].tx().rsp[1]->can_put())
        {
            mesh_ports[rsp->TgtID.id()].tx().rsp[1]->put(rsp);
            rsp.reset();
        }
        if (snp && mesh_ports[snp->TgtID.id()].tx().snp[0]->can_put()) {
            mesh_ports[snp->TgtID.id()].tx().snp[0]->put(snp);
            snp.reset();
        }
        if (dat && mesh_ports[dat->TgtID.id()].tx().dat[0]->can_put()) {
            mesh_ports[dat->TgtID.id()].tx().dat[0]->put(dat);
            dat.reset();
        }

        if (++i == ports)
            i = 0;

        if (i == starting_port)
            break;
    }

    if (++starting_port == ports)
        starting_port = 0;

    return true;
}

} // namespace ampere::mesh
