//
// Copyright (c) 2020-2022 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//
#include "ampere/mesh/mesh_port.h"

namespace ampere::mesh {

MeshPort::MeshPort(sc_core::sc_module_name name, int nreq, int nrsp, int nsnp, int ndat)
    : sc_core::sc_module(name), nreq{nreq}, nrsp{nrsp}, nsnp{nsnp}, ndat{ndat}
{
    tx().req.build("req_tx", nreq);
    rx().req.build("req_rx", nreq);

    tx().rsp.build("rsp_tx", nrsp);
    rx().rsp.build("rsp_rx", nrsp);

    tx().snp.build("snp_tx", nsnp);
    rx().snp.build("snp_rx", nsnp);

    tx().dat.build("dat_tx", ndat);
    rx().dat.build("dat_rx", ndat);
}

static std::string port_name(const std::string& name, const std::string& prefix, int index)
{
    // This looks weird, but yes we actually want to return an empty string if there's no prefix.
    if (prefix.empty())
        return prefix;
    else
        return fmt::format("{}_{}_{}", prefix, name, index);
}

void MeshPort::connect(MeshPort& port, const std::string& prefix)
{
    for (int i = 0; i < std::min(nreq, port.nreq); ++i)
        sc::connect(port.tx().req[i], rx().req[i]).name(port_name("req_tx", prefix, i));

    for (int i = 0; i < std::min(nrsp, port.nrsp); ++i)
        sc::connect(port.tx().rsp[i], rx().rsp[i]).name(port_name("rsp_tx", prefix, i));

    for (int i = 0; i < std::min(nsnp, port.nsnp); ++i)
        sc::connect(port.tx().snp[i], rx().snp[i]).name(port_name("snp_tx", prefix, i));

    for (int i = 0; i < std::min(ndat, port.ndat); ++i)
        sc::connect(port.tx().dat[i], rx().dat[i]).name(port_name("dat_tx", prefix, i));

    for (int i = 0; i < std::min(nreq, port.nreq); ++i)
        sc::connect(tx().req[i], port.rx().req[i]).name(port_name("req_rx", prefix, i));

    for (int i = 0; i < std::min(nrsp, port.nrsp); ++i)
        sc::connect(tx().rsp[i], port.rx().rsp[i]).name(port_name("rsp_rx", prefix, i));

    for (int i = 0; i < std::min(nsnp, port.nsnp); ++i)
        sc::connect(tx().snp[i], port.rx().snp[i]).name(port_name("snp_rx", prefix, i));

    for (int i = 0; i < std::min(ndat, port.ndat); ++i)
        sc::connect(tx().dat[i], port.rx().dat[i]).name(port_name("dat_rx", prefix, i));
};

void MeshPort::bind(MeshPort& parent)
{
    AMPERE_ASSERT(nreq == parent.nreq,
                  "cannot bind a MeshPort to another MeshPort with differing numbers of request channels");
    AMPERE_ASSERT(nrsp == parent.nrsp,
                  "cannot bind a MeshPort to another MeshPort with differing numbers of response channels");
    AMPERE_ASSERT(nsnp == parent.nsnp,
                  "cannot bind a MeshPort to another MeshPort with differing numbers of snoop channels");
    AMPERE_ASSERT(ndat == parent.ndat,
                  "cannot bind a MeshPort to another MeshPort with differing numbers of data channels");

    for (int i = 0; i < nreq; ++i) {
        tx().req[i].bind(parent.tx().req[i]);
        rx().req[i].bind(parent.rx().req[i]);
    }

    for (int i = 0; i < nrsp; ++i) {
        tx().rsp[i].bind(parent.tx().rsp[i]);
        rx().rsp[i].bind(parent.rx().rsp[i]);
    }

    for (int i = 0; i < nsnp; ++i) {
        tx().snp[i].bind(parent.tx().snp[i]);
        rx().snp[i].bind(parent.rx().snp[i]);
    }

    for (int i = 0; i < ndat; ++i) {
        tx().dat[i].bind(parent.tx().dat[i]);
        rx().dat[i].bind(parent.rx().dat[i]);
    }
};

} // namespace ampere::mesh
