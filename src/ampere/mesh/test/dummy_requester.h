//
// Copyright (c) 2020 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//

#pragma once

#include <deque>
#include <gmock/gmock.h>

#include "ampere/mesh/chi/types.h"
#include "ampere/mesh/mesh_port.h"
#include "ampere/mesh/mesh_req.h"
#include "ampere/sc/sc.h"

namespace ampere::mesh {

using namespace archx;
using namespace chi;
using namespace sc;

struct RequesterAnnouncerIfc : public sc_core::sc_interface
{
    virtual void response(ResponseOpcode opcode, uint16_t txn, uint16_t dbid) = 0;
    virtual void snoop(SnoopOpcode opcode, uint16_t txn) = 0;
    virtual void data(DataOpcode opcode, uint16_t txn, uint16_t dbid) = 0;
};

class MockRequesterAnnouncer : public RequesterAnnouncerIfc
{
public:
    MOCK_METHOD(void, response, (ResponseOpcode, uint16_t, uint16_t), (override));
    MOCK_METHOD(void, snoop, (SnoopOpcode, uint16_t), (override));
    MOCK_METHOD(void, data, (DataOpcode, uint16_t, uint16_t), (override));
};

class DummyRequester : public Module
{
public:
    DummyRequester(const sc_core::sc_module_name& name, NodeId id) : Module{name}, m_id{id} {}

    void send_request(
        NodeId tgt, uint16_t txn, RequestOpcode opcode, PhysAddr address, RequestOrder order = RequestOrder::NoOrder);
    void send_request_ls(NodeId tgt, uint16_t txn, RequestOpcode opcode, PhysAddr address);
    void send_request_retry(NodeId tgt, uint16_t txn, RequestOpcode opcode, PhysAddr address);
    void send_request(MeshReq req);
    void send_credited_request(NodeId tgt, uint16_t txn, RequestOpcode opcode, PhysAddr address, uint8_t credit);
    void send_response(NodeId tgt, uint16_t txn, ResponseOpcode opcode);
    void send_response(NodeId tgt, uint16_t txn, ResponseOpcode opcode, uint16_t dbid);
    void send_response(NodeId tgt, uint16_t txn, ResponseOpcode opcode, uint16_t dbid, ResponseStatus resp);
    void send_response(MeshRsp rsp);
    void send_data(NodeId tgt, uint16_t txn, DataOpcode opcode, uint16_t dbid, ResponseStatus resp);
    void send_data(MeshDat dat);
    void send_snoop(NodeId tgt, uint16_t txn, SnoopOpcode opcode, PhysAddr address);

    MeshPort mesh_port{"port", 1, 2, 1, 1};

    sc_core::sc_port<RequesterAnnouncerIfc, 1, sc_core::SC_ZERO_OR_MORE_BOUND> announcer;

protected:
    bool run() override;

    void transmit();
    template<typename T>
    void transmit_channel(std::deque<T>& txq, OutOpt<T>& port);

    void receive();

    std::deque<MeshReq> txreq;
    std::deque<MeshRsp> txrsp;
    std::deque<MeshSnp> txsnp;
    std::deque<MeshDat> txdat;
    NodeId m_id;
};

} // namespace ampere::mesh
