//
// Copyright (c) 2020 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//
#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "ampere/mesh/home_node.h"
#include "ampere/mesh/test/dummy_memory.h"
#include "ampere/mesh/test/dummy_mesh_node.h"
#include "ampere/mesh/test/dummy_requester.h"
#include "ampere/mesh/test/mock_addr_map.h"
#include "ampere/mesh/test/mock_mesh_port.h"
#include "ampere/sc/testing/test_interfaces.h"
#include "ampere/sc/testing/test_module.h"

using namespace ampere::mesh;
using namespace ampere::archx;

using ::testing::_;
using ::testing::Field;
using ::testing::InSequence;
using ::testing::Invoke;
using ::testing::NiceMock;
using ::testing::Pointee;
using ::testing::Return;
using ::testing::SaveArg;

class TestHomeNode : public ampere::sc::TestModule, public MeshConfigIfc
{
protected:
    TestHomeNode()
        : ampere::sc::TestModule{{"hn.sam.system_address_map.connect", "system_address_map"},
                                 {"hn.sam.tgt_lut", "[[0,0,0,0]]"_json}}
    {
        hn.mesh_port.rx().req[0].bind(port.tx.req);
        hn.mesh_port.rx().rsp[0].bind(port.tx.rsp);
        hn.mesh_port.rx().snp[0].bind(port.tx.snp);
        hn.mesh_port.rx().dat[0].bind(port.tx.dat);

        hn.mesh_port.tx().req[0].bind(port.rx.req);
        hn.mesh_port.tx().rsp[0].bind(port.rx.rsp);
        hn.mesh_port.tx().snp[0].bind(port.rx.snp);
        hn.mesh_port.tx().dat[0].bind(port.rx.dat);

        hn.config.bind(*this);
    }

    int data_bus_width() const override { return 512; }
    int channel(const chi::NodeId id) const override { return 0; }

    HomeNode hn{"hn"};
    NiceMock<MockMeshPort> port;
    MockSystemAddressMap system_address_map{"system_address_map"};
};

TEST_F(TestHomeNode, CanSendRequest)
{
    auto req = make<MeshReq>(NodeId{0}, NodeId{1}, 0, chi::RequestOpcode::ReadUnique, 0xaced000_pa);

    EXPECT_CALL(port.tx.req, can_take()).WillOnce(Return(true)).WillRepeatedly(Return(false));

    EXPECT_CALL(port.tx.req, take()).WillOnce(Return(req));

    tick(4);
}

TEST_F(TestHomeNode, CanSendRequestAndGetResponse)
{
    auto req = make<MeshReq>(NodeId{0}, NodeId{1}, 0, chi::RequestOpcode::ReadUnique, 0xaced000_pa);

    EXPECT_CALL(port.tx.req, can_take()).WillOnce(Return(true)).WillRepeatedly(Return(false));
    EXPECT_CALL(port.tx.req, take()).WillOnce(Return(req));

    EXPECT_CALL(port.rx.req, can_put()).WillOnce(Return(true)).WillRepeatedly(Return(false));
    EXPECT_CALL(port.rx.rsp, can_put()).WillOnce(Return(true)).WillRepeatedly(Return(false));

    EXPECT_CALL(port.tx.rsp, can_take()).WillRepeatedly(Return(false));

    auto respond = [this](MeshReq req) {
        auto receipt = make<MeshRsp>(req->TgtID, req->SrcID, req->TxnID, chi::ResponseOpcode::ReadReceipt);

        EXPECT_CALL(port.tx.rsp, can_take()).WillOnce(Return(true)).RetiresOnSaturation();

        EXPECT_CALL(port.tx.rsp, take()).WillOnce(Return(receipt)).RetiresOnSaturation();
    };

    EXPECT_CALL(port.rx.req, put(HasOpcode(chi::RequestOpcode::ReadNoSnpSep))).WillOnce(Invoke(respond));

    auto send_comp_ack = [this](MeshRsp rsp) {
        auto compack = make<MeshRsp>(NodeId{0}, rsp->SrcID, rsp->TxnID, chi::ResponseOpcode::CompAck);

        EXPECT_CALL(port.tx.rsp, can_take()).WillOnce(Return(true)).RetiresOnSaturation();

        EXPECT_CALL(port.tx.rsp, take()).WillOnce(Return(compack)).RetiresOnSaturation();
    };

    EXPECT_CALL(port.rx.rsp, put(HasOpcode(chi::ResponseOpcode::RespSepData))).WillOnce(Invoke(send_comp_ack));

    tick(100);

    EXPECT_EQ(hn.tracker_occupancy(), 0u);
}

TEST_F(TestHomeNode, CanGetRetryAndReSend)
{
    auto req = make<MeshReq>(NodeId{0}, NodeId{1}, 0, chi::RequestOpcode::ReadUnique, 0xaced000_pa);

    EXPECT_CALL(port.tx.req, can_take()).WillOnce(Return(true)).WillRepeatedly(Return(false));
    EXPECT_CALL(port.tx.req, take()).WillOnce(Return(req));

    EXPECT_CALL(port.rx.req, can_put()).WillRepeatedly(Return(true));
    EXPECT_CALL(port.rx.rsp, can_put()).WillRepeatedly(Return(true));

    EXPECT_CALL(port.tx.rsp, can_take()).WillRepeatedly(Return(false));

    auto respond_with_retry = [this](MeshReq req) {
        auto retry = make<MeshRsp>(req->TgtID, req->SrcID, req->TxnID, chi::ResponseOpcode::RetryAck);
        retry->PCrdType = 0;

        auto pcrdgrant = make<MeshRsp>(req->TgtID, req->SrcID, req->TxnID, chi::ResponseOpcode::PCrdGrant);
        pcrdgrant->PCrdType = 0;

        // Respond immediately with RetryAck, but also set up a delayed PCrdGrant
        EXPECT_CALL(port.tx.rsp, can_take())
            .WillOnce(Return(true))
            .WillOnce(Return(false))
            .WillOnce(Return(false))
            .WillOnce(Return(false))
            .WillOnce(Return(false))
            .WillOnce(Return(false))
            .WillOnce(Return(false))
            .WillOnce(Return(false))
            .WillOnce(Return(true))
            .RetiresOnSaturation();

        EXPECT_CALL(port.tx.rsp, take()).WillOnce(Return(retry)).WillOnce(Return(pcrdgrant)).RetiresOnSaturation();
    };

    auto respond = [this](MeshReq req) {
        auto receipt = make<MeshRsp>(req->TgtID, req->SrcID, req->TxnID, chi::ResponseOpcode::ReadReceipt);

        EXPECT_CALL(port.tx.rsp, can_take()).WillOnce(Return(true)).RetiresOnSaturation();

        EXPECT_CALL(port.tx.rsp, take()).WillOnce(Return(receipt)).RetiresOnSaturation();
    };

    EXPECT_CALL(port.rx.req, put(HasOpcode(chi::RequestOpcode::ReadNoSnpSep)))
        .WillOnce(Invoke(respond_with_retry))
        .WillOnce(Invoke(respond));

    auto send_comp_ack = [this](MeshRsp rsp) {
        auto compack = make<MeshRsp>(NodeId{0}, rsp->SrcID, rsp->TxnID, chi::ResponseOpcode::CompAck);

        EXPECT_CALL(port.tx.rsp, can_take()).WillOnce(Return(true)).RetiresOnSaturation();

        EXPECT_CALL(port.tx.rsp, take()).WillOnce(Return(compack)).RetiresOnSaturation();
    };

    EXPECT_CALL(port.rx.rsp, put(HasOpcode(chi::ResponseOpcode::RespSepData))).WillOnce(Invoke(send_comp_ack));

    tick(100);

    EXPECT_EQ(hn.tracker_occupancy(), 0u);
}

class TestHomeNodeWithMemory : public ampere::sc::TestModule, public MeshConfigIfc
{
protected:
    TestHomeNodeWithMemory()
        : sc::TestModule{{"hn.node_id", "[0, 0, 0, 1]"_json},
                         {"hn.snoop_filter.sets", 1},
                         {"hn.snoop_filter.ways", 4},
                         {"xp.ports", 3},
                         {"hn.sam.system_address_map.connect", "system_address_map"},
                         {"hn.sam.tgt_lut", "[[0,0,0,2]]"_json},
                         {"~fifo_[0-9]+\\.capacity", 8}}
    {
        xp.mesh_ports[0].connect(core.mesh_port);
        xp.mesh_ports[1].connect(hn.mesh_port);
        xp.mesh_ports[2].connect(mem.mesh_port);

        core.announcer.bind(announcer);
        hn.config.bind(*this);
    }

    int data_bus_width() const override { return 512; }
    int channel(const chi::NodeId id) const override { return 0; }

    HomeNode hn{"hn"};
    MockSystemAddressMap system_address_map{"system_address_map"};
    DummyRequester core{"core", NodeId{0}};
    DummyMeshNode xp{"xp"};
    DummyMemory mem{"mem", NodeId{2}};
    NiceMock<MockRequesterAnnouncer> announcer;
};

TEST_F(TestHomeNodeWithMemory, ReadUnique)
{
    core.send_request(NodeId{1}, 4, RequestOpcode::ReadUnique, 0xaced0000_pa);
    EXPECT_CALL(announcer, response(ResponseOpcode::RespSepData, 4, _));
    EXPECT_CALL(announcer, data(DataOpcode::DataSepResp, 4, _));
    tick(10);
}

TEST_F(TestHomeNodeWithMemory, ReadNotSharedDirty)
{
    core.send_request(NodeId{1}, 7, RequestOpcode::ReadNotSharedDirty, 0xaced0000_pa);
    EXPECT_CALL(announcer, response(ResponseOpcode::RespSepData, 7, _));
    EXPECT_CALL(announcer, data(DataOpcode::DataSepResp, 7, _));
    tick(10);
}

TEST_F(TestHomeNodeWithMemory, ReadNotSharedDirtyLS)
{
    core.send_request_ls(NodeId{1}, 7, RequestOpcode::ReadNotSharedDirty, 0xaced0000_pa);
    EXPECT_CALL(announcer, data(DataOpcode::CompData, 7, _));
    tick(20);
}

TEST_F(TestHomeNodeWithMemory, WriteBackFull)
{
    core.send_request(NodeId{1}, 4, RequestOpcode::ReadUnique, 0xaced0000_pa);

    auto send_comp_ack = [this](DataOpcode opcode, uint16_t txn, uint16_t dbid) {
        core.send_response(NodeId{1}, dbid, ResponseOpcode::CompAck);
    };

    auto send_write_back = [this](DataOpcode opcode, uint16_t txn, uint16_t dbid) {
        core.send_request(NodeId{1}, 21, RequestOpcode::WriteBackFull, 0xaced0000_pa);
    };

    auto respond_with_data = [this](ResponseOpcode opcode, uint16_t txn, uint16_t dbid) {
        core.send_data(NodeId{1}, 21, DataOpcode::CopyBackWrData, dbid, RespWriteData_UD_PD);
    };

    EXPECT_CALL(announcer, response(ResponseOpcode::RespSepData, 4, _));

    EXPECT_CALL(announcer, data(DataOpcode::DataSepResp, 4, _))
        .WillOnce(DoAll(Invoke(send_comp_ack), Invoke(send_write_back)));

    EXPECT_CALL(announcer, response(ResponseOpcode::CompDBIDResp, 21, _)).WillOnce(Invoke(respond_with_data));

    tick(20);
}

TEST_F(TestHomeNodeWithMemory, WriteEvictFull)
{
    core.send_request(NodeId{1}, 4, RequestOpcode::ReadUnique, 0xaced0000_pa);

    auto send_comp_ack = [this](DataOpcode opcode, uint16_t txn, uint16_t dbid) {
        core.send_response(NodeId{1}, dbid, ResponseOpcode::CompAck);
    };

    auto send_write_evict = [this](DataOpcode opcode, uint16_t txn, uint16_t dbid) {
        core.send_request(NodeId{1}, 10, RequestOpcode::WriteEvictFull, 0xaced0000_pa);
    };

    auto respond_with_data = [this](ResponseOpcode opcode, uint16_t txn, uint16_t dbid) {
        core.send_data(NodeId{1}, 10, DataOpcode::CopyBackWrData, dbid, RespWriteData_UC);
    };

    EXPECT_CALL(announcer, response(ResponseOpcode::RespSepData, 4, _));

    EXPECT_CALL(announcer, data(DataOpcode::DataSepResp, 4, _))
        .WillOnce(DoAll(Invoke(send_comp_ack), Invoke(send_write_evict)));

    EXPECT_CALL(announcer, response(ResponseOpcode::CompDBIDResp, 10, _)).WillOnce(Invoke(respond_with_data));

    tick(100);

    EXPECT_TRUE(mem.has_write(0xaced0000_pa));
}

TEST_F(TestHomeNodeWithMemory, Evict)
{
    core.send_request(NodeId{1}, 4, RequestOpcode::ReadUnique, 0xaced0000_pa);

    auto send_comp_ack = [this](DataOpcode opcode, uint16_t txn, uint16_t dbid) {
        core.send_response(NodeId{1}, dbid, ResponseOpcode::CompAck);
    };

    auto send_eviction = [this](DataOpcode opcode, uint16_t txn, uint16_t dbid) {
        core.send_request(NodeId{1}, 19, RequestOpcode::Evict, 0xaced0000_pa);
    };

    EXPECT_CALL(announcer, response(ResponseOpcode::RespSepData, 4, _));

    EXPECT_CALL(announcer, data(DataOpcode::DataSepResp, 4, _))
        .WillOnce(DoAll(Invoke(send_comp_ack), Invoke(send_eviction)));

    EXPECT_CALL(announcer, response(ResponseOpcode::Comp, 19, _));

    tick(20);
}

TEST_F(TestHomeNodeWithMemory, MakeUnique)
{
    core.send_request(NodeId{1}, 4, RequestOpcode::ReadNotSharedDirty, 0xaced0000_pa);

    auto send_comp_ack = [this](DataOpcode opcode, uint16_t txn, uint16_t dbid) {
        core.send_response(NodeId{1}, dbid, ResponseOpcode::CompAck);
    };

    auto send_make_unique = [this](DataOpcode opcode, uint16_t txn, uint16_t dbid) {
        core.send_request(NodeId{1}, 21, RequestOpcode::MakeUnique, 0xaced0000_pa);
    };

    EXPECT_CALL(announcer, response(ResponseOpcode::RespSepData, 4, _));

    EXPECT_CALL(announcer, data(DataOpcode::DataSepResp, 4, _))
        .WillOnce(DoAll(Invoke(send_comp_ack), Invoke(send_make_unique)));

    EXPECT_CALL(announcer, response(ResponseOpcode::Comp, 21, _));

    tick(20);
}

TEST_F(TestHomeNodeWithMemory, MakeUniqueCold)
{
    core.send_request(NodeId{1}, 17, RequestOpcode::MakeUnique, 0xaced0000_pa);

    auto send_comp_ack = [this](ResponseOpcode opcode, uint16_t txn, uint16_t dbid) {
        core.send_response(NodeId{1}, dbid, ResponseOpcode::CompAck);
    };

    EXPECT_CALL(announcer, response(ResponseOpcode::Comp, 17, _)).WillOnce(Invoke(send_comp_ack));

    tick(20);
}

TEST_F(TestHomeNodeWithMemory, Clean)
{
    core.send_request(NodeId{1}, 1, RequestOpcode::ReadNotSharedDirty, 0x100000_pa);
    core.send_request(NodeId{1}, 2, RequestOpcode::ReadNotSharedDirty, 0x200000_pa);
    core.send_request(NodeId{1}, 3, RequestOpcode::ReadNotSharedDirty, 0x300000_pa);
    core.send_request(NodeId{1}, 4, RequestOpcode::ReadNotSharedDirty, 0x400000_pa);

    auto send_comp_ack = [this](ResponseOpcode opcode, uint16_t txn, uint16_t dbid) {
        core.send_response(NodeId{1}, dbid, ResponseOpcode::CompAck);
    };

    auto send_final_read = [this](DataOpcode opcode, uint16_t txn, uint16_t dbid) {
        core.send_request(NodeId{1}, 5, RequestOpcode::ReadNotSharedDirty, 0x500000_pa);
    };

    auto send_snoop_response = [this](SnoopOpcode opcode, uint16_t txn) {
        core.send_response(NodeId{1}, txn, ResponseOpcode::SnpResp, 0, RespSnoop_I);
    };

    EXPECT_CALL(announcer, response(ResponseOpcode::RespSepData, _, _))
        .Times(4)
        .WillRepeatedly(Invoke(send_comp_ack))
        .RetiresOnSaturation();

    EXPECT_CALL(announcer, data(DataOpcode::DataSepResp, _, _))
        .WillOnce(Return())
        .WillOnce(Return())
        .WillOnce(Return())
        .WillOnce(Invoke(send_final_read));

    EXPECT_CALL(announcer, response(ResponseOpcode::RespSepData, 5, _)).WillOnce(Invoke(send_comp_ack));

    EXPECT_CALL(announcer, data(DataOpcode::DataSepResp, 5, _));

    EXPECT_CALL(announcer, snoop(SnoopOpcode::SnpCleanInvalid, _)).WillOnce(Invoke(send_snoop_response));

    tick(100);
}

TEST_F(TestHomeNodeWithMemory, CleanWithSnpRespData)
{
    core.send_request(NodeId{1}, 1, RequestOpcode::ReadNotSharedDirty, 0x100000_pa);
    core.send_request(NodeId{1}, 2, RequestOpcode::ReadNotSharedDirty, 0x200000_pa);
    core.send_request(NodeId{1}, 3, RequestOpcode::ReadNotSharedDirty, 0x300000_pa);
    core.send_request(NodeId{1}, 4, RequestOpcode::ReadNotSharedDirty, 0x400000_pa);

    auto send_comp_ack = [this](ResponseOpcode opcode, uint16_t txn, uint16_t dbid) {
        core.send_response(NodeId{1}, dbid, ResponseOpcode::CompAck);
    };

    auto send_final_read = [this](DataOpcode opcode, uint16_t txn, uint16_t dbid) {
        core.send_request(NodeId{1}, 5, RequestOpcode::ReadNotSharedDirty, 0x500000_pa);
    };

    auto send_snoop_response = [this](SnoopOpcode opcode, uint16_t txn) {
        core.send_data(NodeId{1}, txn, DataOpcode::SnpRespData, 0, RespSnoop_I);
    };

    EXPECT_CALL(announcer, response(ResponseOpcode::RespSepData, _, _))
        .Times(4)
        .WillRepeatedly(Invoke(send_comp_ack))
        .RetiresOnSaturation();

    EXPECT_CALL(announcer, data(DataOpcode::DataSepResp, _, _))
        .WillOnce(Return())
        .WillOnce(Return())
        .WillOnce(Return())
        .WillOnce(Invoke(send_final_read));

    EXPECT_CALL(announcer, response(ResponseOpcode::RespSepData, 5, _)).WillOnce(Invoke(send_comp_ack));

    EXPECT_CALL(announcer, data(DataOpcode::DataSepResp, 5, _));

    EXPECT_CALL(announcer, snoop(SnoopOpcode::SnpCleanInvalid, _)).WillOnce(Invoke(send_snoop_response));

    tick(100);
}

TEST_F(TestHomeNodeWithMemory, RequestsToSameLineAllComplete)
{
    core.send_request(NodeId{1}, 1, RequestOpcode::ReadNotSharedDirty, 0x100000_pa);
    core.send_request(NodeId{1}, 2, RequestOpcode::ReadNotSharedDirty, 0x100000_pa);
    core.send_request(NodeId{1}, 3, RequestOpcode::ReadNotSharedDirty, 0x100000_pa);
    core.send_request(NodeId{1}, 4, RequestOpcode::ReadNotSharedDirty, 0x100000_pa);

    auto send_comp_ack = [this](ResponseOpcode opcode, uint16_t txn, uint16_t dbid) {
        core.send_response(NodeId{1}, dbid, ResponseOpcode::CompAck);
    };

    EXPECT_CALL(announcer, data(DataOpcode::DataSepResp, _, _)).Times(4);

    {
        InSequence seq;

        EXPECT_CALL(announcer, response(ResponseOpcode::RespSepData, 1, _)).WillOnce(Invoke(send_comp_ack));
        EXPECT_CALL(announcer, response(ResponseOpcode::RespSepData, 2, _)).WillOnce(Invoke(send_comp_ack));
        EXPECT_CALL(announcer, response(ResponseOpcode::RespSepData, 3, _)).WillOnce(Invoke(send_comp_ack));
        EXPECT_CALL(announcer, response(ResponseOpcode::RespSepData, 4, _)).WillOnce(Invoke(send_comp_ack));
    }

    tick(100);
}

TEST_F(TestHomeNodeWithMemory, ReadUniqueFollowedByReadNotSharedDirty)
{
    core.send_request(NodeId{1}, 4, RequestOpcode::ReadUnique, 0xaced0000_pa);

    auto send_comp_ack = [this](ResponseOpcode opcode, uint16_t txn, uint16_t dbid) {
        core.send_response(NodeId{1}, dbid, ResponseOpcode::CompAck);
    };

    auto send_readnotsd = [this](ResponseOpcode opcode, uint16_t txn, uint16_t dbid) {
        core.send_request(NodeId{1}, 7, RequestOpcode::ReadNotSharedDirty, 0xaced0000_pa);
    };

    EXPECT_CALL(announcer, data(DataOpcode::DataSepResp, 4, _));

    EXPECT_CALL(announcer, response(ResponseOpcode::RespSepData, 4, _))
        .WillOnce(DoAll(Invoke(send_comp_ack), Invoke(send_readnotsd)));

    EXPECT_CALL(announcer, data(DataOpcode::DataSepResp, 7, _));
    EXPECT_CALL(announcer, response(ResponseOpcode::RespSepData, 7, _));

    tick(30);
}

class TestHomeNodeWithSLC : public ampere::sc::TestModule, public MeshConfigIfc
{
protected:
    TestHomeNodeWithSLC()
        : sc::TestModule{{"hn.node_id", "[0, 0, 0, 1]"_json},
                         {"hn.enable_slc", true},
                         {"hn.snoop_filter.sets", 1},
                         {"hn.snoop_filter.ways", 4},
                         {"xp.ports", 3},
                         {"hn.slc.sets", 1},
                         {"hn.slc.ways", 4},
                         {"hn.sam.system_address_map.connect", "system_address_map"},
                         {"hn.sam.tgt_lut", "[[0,0,0,2]]"_json},
                         {"~fifo_[0-9]+\\.capacity", 8}}
    {
        xp.mesh_ports[0].connect(core.mesh_port);
        xp.mesh_ports[1].connect(hn.mesh_port);
        xp.mesh_ports[2].tx().req[0].bind(mem.rx.req);
        xp.mesh_ports[2].tx().rsp[0].bind(mem.rx.rsp);
        xp.mesh_ports[2].tx().snp[0].bind(mem.rx.snp);
        xp.mesh_ports[2].tx().dat[0].bind(mem.rx.dat);
        xp.mesh_ports[2].rx().req[0].bind(mem.tx.req);
        xp.mesh_ports[2].rx().rsp[0].bind(mem.tx.rsp);
        xp.mesh_ports[2].rx().snp[0].bind(mem.tx.snp);
        xp.mesh_ports[2].rx().dat[0].bind(mem.tx.dat);

        core.announcer.bind(announcer);
        hn.config.bind(*this);
    }

    int data_bus_width() const override { return 512; }
    int channel(const chi::NodeId id) const override { return 0; }

    HomeNode hn{"hn"};
    MockSystemAddressMap system_address_map{"system_address_map"};
    DummyRequester core{"core", NodeId{0}};
    DummyMeshNode xp{"xp"};
    NiceMock<MockMeshPort> mem;
    NiceMock<MockRequesterAnnouncer> announcer;
};

TEST_F(TestHomeNodeWithSLC, HitSLC)
{
    hn.preload_slc(0xbeef000_pa, NewSLC::State::CLEAN);
    core.send_request(NodeId{1}, 20, RequestOpcode::ReadUnique, 0xbeef000_pa);

    auto send_comp_ack = [this](DataOpcode opcode, uint16_t txn, uint16_t dbid) {
        core.send_response(NodeId{1}, dbid, ResponseOpcode::CompAck);
    };

    EXPECT_CALL(announcer, data(DataOpcode::CompData, 20, _)).WillOnce(Invoke(send_comp_ack));

    tick(20);
}

/* Test sequence:
 *
 *   (1) Read data from memory (this is so our subsequent eviction makes sense)
 *   (2) Evict from the core, which should fill the SLC
 *   (3) Follow up with another read to get data from the SLC
 *
 *     Core                   Hn                  Memory
 *     ==+==                  =+=                 ===+===
 *       | --ReadUnique------> |                     |
 *       |                  (miss) --ReadNoSnpSep--> |
 *       |                     | <---ReadReceipt---- |
 *       | <---RespSepData---- |                     |
 *       | <-------------------------DataSepResp---- |
 *       | --CompAck---------> |
 *       |                     |
 *       | --WriteEvictFull--> |
 *       | <---CompDBID------- |
 *       | ----CompData------> |
 *       |                  (fill)
 *       |                     |
 *       | --ReadUnique------> |
 *       |                   (hit)
 *       | <--CompData-------- |
 *       | --CompAck---------> |
 */
TEST_F(TestHomeNodeWithSLC, FillSLC)
{
    core.send_request(NodeId{1}, 4, RequestOpcode::ReadUnique, 0xaced0000_pa);

    auto send_comp_ack = [this](ResponseOpcode opcode, uint16_t txn, uint16_t dbid) {
        core.send_response(NodeId{1}, dbid, ResponseOpcode::CompAck);
    };

    auto send_comp_ack2 = [this](DataOpcode opcode, uint16_t txn, uint16_t dbid) {
        core.send_response(NodeId{1}, dbid, ResponseOpcode::CompAck);
    };

    auto send_write_evict = [this](DataOpcode opcode, uint16_t txn, uint16_t dbid) {
        core.send_request(NodeId{1}, 10, RequestOpcode::WriteEvictFull, 0xaced0000_pa);
    };

    auto respond_with_data = [this](ResponseOpcode opcode, uint16_t txn, uint16_t dbid) {
        core.send_data(NodeId{1}, 10, DataOpcode::CopyBackWrData, dbid, RespWriteData_UC);
    };

    auto send_subsequent_read = [this](ResponseOpcode opcode, uint16_t txn, uint16_t dbid) {
        // Second read *should* come from the SLC
        core.send_request(NodeId{1}, 18, RequestOpcode::ReadNotSharedDirty, 0xaced0000_pa);
    };

    auto send_mem_comp = [this](MeshReq req) {
        auto rsp = make<MeshRsp>(NodeId{2}, req->SrcID, req->TxnID, ResponseOpcode::ReadReceipt);
        EXPECT_CALL(mem.tx.rsp, can_take()).WillOnce(Return(true)).RetiresOnSaturation();
        EXPECT_CALL(mem.tx.rsp, take()).WillOnce(Return(rsp)).RetiresOnSaturation();

        auto dat = make<MeshDat>(NodeId{2}, req->ReturnNID, req->ReturnTxnID, chi::DataOpcode::DataSepResp);
        dat->HomeNID = req->SrcID;
        dat->DBID = req->TxnID;
        EXPECT_CALL(mem.tx.dat, can_take()).WillOnce(Return(true)).RetiresOnSaturation();
        EXPECT_CALL(mem.tx.dat, take()).WillOnce(Return(dat)).RetiresOnSaturation();
    };

    EXPECT_CALL(mem.tx.rsp, can_take()).WillRepeatedly(Return(false));

    EXPECT_CALL(mem.tx.dat, can_take()).WillRepeatedly(Return(false));
    EXPECT_CALL(mem.rx.req, can_put()).WillRepeatedly(Return(true));
    EXPECT_CALL(mem.rx.req, put(_)).WillOnce(Invoke(send_mem_comp));

    EXPECT_CALL(announcer, response(ResponseOpcode::RespSepData, 4, _)).WillOnce(Invoke(send_comp_ack));

    EXPECT_CALL(announcer, data(DataOpcode::DataSepResp, 4, _)).WillOnce(Invoke(send_write_evict));

    EXPECT_CALL(announcer, response(ResponseOpcode::CompDBIDResp, 10, _))
        .WillOnce(DoAll(Invoke(respond_with_data), Invoke(send_subsequent_read)));

    EXPECT_CALL(announcer, data(DataOpcode::CompData, 18, _)).WillOnce(Invoke(send_comp_ack2));

    tick(200);
}

TEST_F(TestHomeNodeWithSLC, EvictFromSLC)
{
    hn.preload_slc(0x100000_pa, NewSLC::State::DIRTY);
    hn.preload_slc(0x200000_pa, NewSLC::State::DIRTY);
    hn.preload_slc(0x300000_pa, NewSLC::State::DIRTY);
    hn.preload_slc(0x400000_pa, NewSLC::State::DIRTY);

    core.send_request(NodeId{1}, 4, RequestOpcode::ReadUnique, 0xaced0000_pa);

    auto send_comp_ack = [this](ResponseOpcode opcode, uint16_t txn, uint16_t dbid) {
        core.send_response(NodeId{1}, dbid, ResponseOpcode::CompAck);
    };

    auto send_write_evict = [this](DataOpcode opcode, uint16_t txn, uint16_t dbid) {
        core.send_request(NodeId{1}, 10, RequestOpcode::WriteEvictFull, 0xaced0000_pa);
    };

    auto respond_with_data = [this](ResponseOpcode opcode, uint16_t txn, uint16_t dbid) {
        core.send_data(NodeId{1}, 10, DataOpcode::CopyBackWrData, dbid, RespWriteData_UC);
    };

    auto send_mem_comp = [this](MeshReq req) {
        auto rsp = make<MeshRsp>(NodeId{2}, req->SrcID, req->TxnID, ResponseOpcode::ReadReceipt);
        EXPECT_CALL(mem.tx.rsp, can_take()).WillOnce(Return(true)).RetiresOnSaturation();
        EXPECT_CALL(mem.tx.rsp, take()).WillOnce(Return(rsp)).RetiresOnSaturation();

        auto dat = make<MeshDat>(NodeId{2}, req->ReturnNID, req->ReturnTxnID, chi::DataOpcode::DataSepResp);
        dat->HomeNID = req->SrcID;
        dat->DBID = req->TxnID;
        EXPECT_CALL(mem.tx.dat, can_take()).WillOnce(Return(true)).RetiresOnSaturation();
        EXPECT_CALL(mem.tx.dat, take()).WillOnce(Return(dat)).RetiresOnSaturation();
    };

    EXPECT_CALL(announcer, response(ResponseOpcode::RespSepData, 4, _)).WillOnce(Invoke(send_comp_ack));

    EXPECT_CALL(announcer, data(DataOpcode::DataSepResp, 4, _)).WillOnce(Invoke(send_write_evict));

    EXPECT_CALL(announcer, response(ResponseOpcode::CompDBIDResp, 10, _)).WillOnce(Invoke(respond_with_data));

    EXPECT_CALL(mem.tx.rsp, can_take()).WillRepeatedly(Return(false));
    EXPECT_CALL(mem.tx.dat, can_take()).WillRepeatedly(Return(false));
    EXPECT_CALL(mem.rx.req, can_put()).WillRepeatedly(Return(true));
    EXPECT_CALL(mem.rx.req, put(HasOpcode(RequestOpcode::ReadNoSnpSep))).WillOnce(Invoke(send_mem_comp));
    EXPECT_CALL(mem.rx.req, put(HasOpcode(RequestOpcode::WriteNoSnpFull))).Times(1);

    tick(100);
}

class TestHomeNodeWithSLCAndMemory : public ampere::sc::TestModule, public MeshConfigIfc
{
protected:
    TestHomeNodeWithSLCAndMemory()
        : sc::TestModule{{"hn.node_id", "[0, 0, 0, 1]"_json},
                         {"hn.enable_slc", true},
                         {"hn.snoop_filter.sets", 1},
                         {"hn.snoop_filter.ways", 4},
                         {"xp.ports", 3},
                         {"hn.slc.sets", 1},
                         {"hn.slc.ways", 4},
                         {"hn.sam.system_address_map.connect", "system_address_map"},
                         {"hn.sam.tgt_lut", "[[0,0,0,2]]"_json},
                         {"~fifo_[0-9]+\\.capacity", 8}}
    {
        xp.mesh_ports[0].connect(core.mesh_port);
        xp.mesh_ports[1].connect(hn.mesh_port);
        xp.mesh_ports[2].connect(mem.mesh_port);

        core.announcer.bind(announcer);
        hn.config.bind(*this);
    }

    int data_bus_width() const override { return 512; }
    int channel(const chi::NodeId id) const override { return 0; }

    HomeNode hn{"hn"};
    MockSystemAddressMap system_address_map{"system_address_map"};
    DummyRequester core{"core", NodeId{0}};
    DummyMeshNode xp{"xp"};
    DummyMemory mem{"mem", NodeId{2}};
    NiceMock<MockRequesterAnnouncer> announcer;
};

/* Test sequence (mimics a bug):
 *
 *  (1) Read from memory with ReadUnique
 *  (2) Evict with WriteEvictFull
 *  (3) Read again with ReadUnique
 *  (4) Read again with ReadNotSharedDirty
 */
TEST_F(TestHomeNodeWithSLCAndMemory, EvictAndReRequest)
{
    core.send_request(NodeId{1}, 4, RequestOpcode::ReadUnique, 0xaced0000_pa);

    auto send_comp_ack = [this](ResponseOpcode opcode, uint16_t txn, uint16_t dbid) {
        core.send_response(NodeId{1}, dbid, ResponseOpcode::CompAck);
    };

    auto send_comp_ack2 = [this](DataOpcode opcode, uint16_t txn, uint16_t dbid) {
        core.send_response(NodeId{1}, dbid, ResponseOpcode::CompAck);
    };

    auto send_evict = [this](DataOpcode opcode, uint16_t txn, uint16_t dbid) {
        core.send_request(NodeId{1}, 7, RequestOpcode::WriteEvictFull, 0xaced0000_pa);
    };

    auto respond_with_data = [this](ResponseOpcode opcode, uint16_t txn, uint16_t dbid) {
        core.send_data(NodeId{1}, 7, DataOpcode::CopyBackWrData, dbid, RespWriteData_UC);
    };

    auto send_second_readunique = [this]() {
        core.send_request(NodeId{1}, 9, RequestOpcode::ReadUnique, 0xaced0000_pa);
    };

    auto send_readnotsd = [this](DataOpcode opcode, uint16_t txn, uint16_t dbid) {
        core.send_request(NodeId{1}, 11, RequestOpcode::ReadNotSharedDirty, 0xaced0000_pa);
    };

    EXPECT_CALL(announcer, response(ResponseOpcode::RespSepData, 4, _)).WillOnce(Invoke(send_comp_ack));

    EXPECT_CALL(announcer, data(DataOpcode::DataSepResp, 4, _)).WillOnce(Invoke(send_evict));

    EXPECT_CALL(announcer, response(ResponseOpcode::CompDBIDResp, _, _))
        .WillOnce(DoAll(Invoke(respond_with_data), InvokeWithoutArgs(send_second_readunique)));

    EXPECT_CALL(announcer, data(DataOpcode::CompData, 9, _))
        .WillOnce(DoAll(Invoke(send_comp_ack2), Invoke(send_readnotsd)));

    EXPECT_CALL(announcer, response(ResponseOpcode::RespSepData, 11, _)).WillOnce(Invoke(send_comp_ack));

    EXPECT_CALL(announcer, data(DataOpcode::DataSepResp, 11, _));

    tick(100);
}

class TestHomeNodeWithOneTrackerEntry : public ampere::sc::TestModule, public MeshConfigIfc
{
protected:
    TestHomeNodeWithOneTrackerEntry()
        : sc::TestModule{{"hn.node_id", "[0, 0, 0, 1]"_json},
                         {"hn.snoop_filter.sets", 1},
                         {"hn.snoop_filter.ways", 4},
                         {"hn.tracker.size", 1},
                         {"xp.ports", 3},
                         {"hn.sam.system_address_map.connect", "system_address_map"},
                         {"hn.sam.tgt_lut", "[[0,0,0,2]]"_json},
                         {"~fifo_[0-9]+\\.capacity", 8}}
    {
        xp.mesh_ports[0].connect(core.mesh_port);
        xp.mesh_ports[1].connect(hn.mesh_port);
        xp.mesh_ports[2].connect(mem.mesh_port);

        core.announcer.bind(announcer);

        hn.config.bind(*this);
    }

    HomeNode hn{"hn"};
    MockSystemAddressMap system_address_map{"system_address_map"};
    DummyRequester core{"core", NodeId{0}};
    DummyMeshNode xp{"xp"};
    DummyMemory mem{"mem", NodeId{2}};
    NiceMock<MockRequesterAnnouncer> announcer;

    int data_bus_width() const override { return 512; }
    int channel(const chi::NodeId id) const override { return 0; }
};

TEST_F(TestHomeNodeWithOneTrackerEntry, SecondRequestGetsRetryAck)
{
    core.send_request(NodeId{1}, 4, RequestOpcode::ReadUnique, 0x1000_pa);
    core.send_request(NodeId{1}, 5, RequestOpcode::ReadUnique, 0x2000_pa);

    auto send_comp_ack = [this](ResponseOpcode opcode, uint16_t txn, uint16_t dbid) {
        core.send_response(NodeId{1}, dbid, ResponseOpcode::CompAck);
    };

    auto resend_second_request = [this](ResponseOpcode, uint64_t txn, uint16_t dbid) {
        core.send_credited_request(NodeId{1}, 5, RequestOpcode::ReadUnique, 0x2000_pa, 0 /* PCrdType */);
    };

    EXPECT_CALL(announcer, response(ResponseOpcode::RespSepData, 4, _)).WillOnce(Invoke(send_comp_ack));

    EXPECT_CALL(announcer, data(DataOpcode::DataSepResp, 4, _));

    EXPECT_CALL(announcer, response(ResponseOpcode::RetryAck, 5, _));
    EXPECT_CALL(announcer, response(ResponseOpcode::PCrdGrant, 5, _)).WillOnce(Invoke(resend_second_request));

    EXPECT_CALL(announcer, response(ResponseOpcode::RespSepData, 5, _)).WillOnce(Invoke(send_comp_ack));

    EXPECT_CALL(announcer, data(DataOpcode::DataSepResp, 5, _));

    tick(30);
}
