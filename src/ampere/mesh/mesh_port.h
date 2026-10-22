//
// Copyright (c) 2020-2022 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//

#pragma once

#include <stdlib.h> // aligned_alloc

#include "ampere/mesh/mesh_dat.h"
#include "ampere/mesh/mesh_req.h"
#include "ampere/mesh/mesh_rsp.h"
#include "ampere/mesh/mesh_snp.h"
#include "ampere/sc/sc.h"

namespace ampere::mesh {

using namespace ampere::sc;

template<typename T>
class port_bundle
{
public:
    port_bundle() : ports{nullptr}, nports{0} {}

    port_bundle(const char* name, size_t nports_) { build(name, nports_); }

    void build(const char* name, size_t nports_)
    {
        nports = nports_;

        ports = static_cast<T*>(aligned_alloc(alignof(T), sizeof(T) * nports));

        for (size_t i = 0; i < nports; ++i)
            new (&ports[i]) T(fmt::format("{}_{}", name, i).c_str());
    }

    ~port_bundle()
    {
        for (size_t i = 0; i < nports; ++i)
            ports[i].~T();

        std::free(ports);
    }

    port_bundle(const port_bundle&) = delete;

    port_bundle(port_bundle&& other)
    {
        std::swap(ports, other.ports);
        nports = other.nports;
    }

    T& operator[](size_t index)
    {
        if (index >= nports)
            throw std::out_of_range{"port_bundle index out of range"};

        return ports[index];
    }

    size_t size() const { return nports; }

    // This method basically caches the result of calling `bind_count()` because
    // `bind_count()` on an sc_port is shockingly slow.
    //
    bool is_bound(size_t index) const
    {
        if (index >= nports)
            return false;

        if (m_is_bound.empty())
            init_is_bound();

        return m_is_bound[index];
    }

private:
    T* ports;
    size_t nports;

    mutable std::vector<bool> m_is_bound{};

    void init_is_bound() const
    {
        m_is_bound.resize(nports);

        for (size_t i = 0; i < nports; ++i)
            m_is_bound[i] = ports[i].bind_count() > 0;
    }
};

class MeshPort : public sc_core::sc_module
{
public:
    explicit MeshPort(sc_core::sc_module_name name, int nreq = 1, int nrsp = 1, int nsnp = 1, int ndat = 1);
    MeshPort() : MeshPort{sc_core::sc_module_name{"port"}, 1, 1, 1, 1} {}
    MeshPort(const MeshPort&) = delete;
    MeshPort& operator=(MeshPort const&) = delete;
    MeshPort(MeshPort&&) = delete;
    ~MeshPort() override = default;

    const int nreq;
    const int nrsp;
    const int nsnp;
    const int ndat;

protected:
    struct TxPorts
    {
        port_bundle<OutOpt<MeshReq>> req;
        port_bundle<OutOpt<MeshRsp>> rsp;
        port_bundle<OutOpt<MeshSnp>> snp;
        port_bundle<OutOpt<MeshDat>> dat;
    } _tx;

    struct RxPorts
    {
        port_bundle<InWithCreditOpt<MeshReq>> req;
        port_bundle<InWithCreditOpt<MeshRsp>> rsp;
        port_bundle<InWithCreditOpt<MeshSnp>> snp;
        port_bundle<InWithCreditOpt<MeshDat>> dat;
    } _rx;

public:
    void connect(MeshPort& port, const std::string& prefix = "");
    void bind(MeshPort& parent);

    struct TxPorts& tx() { return _tx; }
    struct RxPorts& rx() { return _rx; }
};

namespace {

template<typename T>
struct selector
{};

template<>
struct selector<MeshReq>
{
    static OutOpt<MeshReq>* tx_get(MeshPort& p, int n) { return &p.tx().req[n]; }
    static InWithCreditOpt<MeshReq>* rx_get(MeshPort& p, int n) { return &p.rx().req[n]; }
    static size_t tx_size(MeshPort& p) { return p.tx().req.size(); }
    static size_t rx_size(MeshPort& p) { return p.rx().req.size(); }
};

template<>
struct selector<MeshSnp>
{
    static OutOpt<MeshSnp>* tx_get(MeshPort& p, int n) { return &p.tx().snp[n]; }
    static InWithCreditOpt<MeshSnp>* rx_get(MeshPort& p, int n) { return &p.rx().snp[n]; }
    static size_t tx_size(MeshPort& p) { return p.tx().snp.size(); }
    static size_t rx_size(MeshPort& p) { return p.rx().snp.size(); }
};

template<>
struct selector<MeshRsp>
{
    static OutOpt<MeshRsp>* tx_get(MeshPort& p, int n) { return &p.tx().rsp[n]; }
    static InWithCreditOpt<MeshRsp>* rx_get(MeshPort& p, int n) { return &p.rx().rsp[n]; }
    static size_t tx_size(MeshPort& p) { return p.tx().rsp.size(); }
    static size_t rx_size(MeshPort& p) { return p.rx().rsp.size(); }
};

template<>
struct selector<MeshDat>
{
    static OutOpt<MeshDat>* tx_get(MeshPort& p, int n) { return &p.tx().dat[n]; }
    static InWithCreditOpt<MeshDat>* rx_get(MeshPort& p, int n) { return &p.rx().dat[n]; }
    static size_t tx_size(MeshPort& p) { return p.tx().dat.size(); }
    static size_t rx_size(MeshPort& p) { return p.rx().dat.size(); }
};

} // namespace

template<typename T>
OutOpt<T>* tx_get(MeshPort& p, int n = 0)
{
    return selector<T>::tx_get(p, n);
}

template<typename T>
InOpt<T>* rx_get(MeshPort& p, int n = 0)
{
    return selector<T>::rx_get(p, n);
}

template<typename T>
size_t tx_size(MeshPort& p)
{
    return selector<T>::tx_size(p);
}

template<typename T>
size_t rx_size(MeshPort& p)
{
    return selector<T>::rx_size(p);
}

} // namespace ampere::mesh
