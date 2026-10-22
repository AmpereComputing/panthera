//
// Copyright (c) 2020 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "ampere/mesh/chi/opcodes.enum.h"
#include "ampere/mesh/mesh_node.h"
#include "ampere/sc/testing/test_module.h"

namespace ampere::mesh {

using namespace ampere::mesh;
using namespace ampere::sc;

class MockNode : public Module
{
public:
    explicit MockNode(sc_core::sc_module_name name) : Module(name) {}
    MockNode(sc_core::sc_module_name name, int nreq) : Module(name), con{"con", nreq} {}

    std::vector<MeshReq> transactions_req;
    std::vector<MeshRsp> transactions_resp;

    void send_request(uint8_t x, uint8_t y, uint8_t port, chi::RequestOpcode opcode = chi::RequestOpcode::ReadNoSnp)
    {
        auto transaction = make<MeshReq>(chi::NodeId{0, 0, 0, 0}, chi::NodeId{x, y, port, 0}, 0, opcode, 0_pa);
        transaction->LRID = chi::LocalRoutingId{x, y, port, 0};
        con.tx().req[0]->put(transaction);
    }

    void send_request2(uint8_t x, uint8_t y, uint8_t port, chi::RequestOpcode opcode = chi::RequestOpcode::Evict)
    {
        auto transaction = make<MeshReq>(chi::NodeId{0, 0, 0, 0}, chi::NodeId{x, y, port, 0}, 0, opcode, 0_pa);
        transaction->LRID = chi::LocalRoutingId{x, y, port, 0};
        con.tx().req[1]->put(transaction);
    }

    void send_response(uint8_t x, uint8_t y, uint8_t port)
    {
        auto transaction =
            make<MeshRsp>(chi::NodeId{0, 0, 0, 0}, chi::NodeId{x, y, port, 0}, 0, chi::ResponseOpcode::Comp);
        transaction->LRID = chi::LocalRoutingId{x, y, port, 0};
        con.tx().rsp[0]->put(transaction);
    }

    void clear()
    {
        transactions_req.clear();
        transactions_resp.clear();
    }

    MeshPort con{"con"};

private:
    bool run() override
    {
        while (con.rx().req[0]->can_take()) {
            auto transaction = con.rx().req[0]->take();
            transactions_req.push_back(transaction);
        }
        while (con.rx().rsp[0]->can_take()) {
            auto transaction = con.rx().rsp[0]->take();
            transactions_resp.push_back(transaction);
        }

        return true;
    }
};

class MockPort : public Module
{
public:
    explicit MockPort(sc_core::sc_module_name name) : Module(name) {}

    std::vector<MeshReq> transactions_req;
    std::vector<MeshRsp> transactions_rsp;

    void send_request(uint8_t x, uint8_t y, uint8_t port, chi::RequestOpcode opcode = chi::RequestOpcode::ReadNoSnp)
    {
        auto transaction = make<MeshReq>(chi::NodeId{}, chi::NodeId{x, y, port, 0}, 0, opcode, 0_pa);
        transaction->LRID = chi::LocalRoutingId{x, y, port, 0};
        con.tx().req[0]->put(transaction);
    }

    void send_request(NodeId tgt, uint16_t txn_id, chi::RequestOpcode opcode = chi::RequestOpcode::ReadNoSnp)
    {
        auto transaction = make<MeshReq>(chi::NodeId{0, 0, 0, 0}, tgt, txn_id, opcode, 0_pa);
        con.tx().req[0]->put(transaction);
    }

    void clear()
    {
        transactions_req.clear();
        transactions_rsp.clear();
    }

    MeshPort con{"con"};

private:
    bool run() override
    {
        while (con.rx().req[0]->can_take()) {
            auto transaction = con.rx().req[0]->take();
            transactions_req.push_back(transaction);
        }

        while (con.rx().rsp[0]->can_take()) {
            auto transaction = con.rx().rsp[0]->take();
            transactions_rsp.push_back(transaction);
        }

        return true;
    }
};

class MeshNodeTest : public TestModule, public MeshConfigIfc
{
public:
    MeshNodeTest()
        : TestModule{{{"~node\\.req_[snew]\\.size", 8},
                      {"~node\\.snp_[snew]\\.size", 8},
                      {"~node\\.req_p[0-9]+\\.size", 8},
                      {"~node\\.snp_p[0-9]+\\.size", 8},
                      {"~node\\.rsp[01]_[snew]\\.size", 8},
                      {"~node\\.dat[01]_[snew]\\.size", 8},
                      {"~node\\.rsp[01]_p[0-9]+\\.size", 8},
                      {"~node\\.dat[01]_p[0-9]+\\.size", 8},
                      {"east_req_rx_0.capacity", 8},
                      {"west_req_rx_0.capacity", 8},
                      {"north_req_rx_0.capacity", 8},
                      {"south_req_rx_0.capacity", 8},
                      {"~port[01]_rsp_rx_0\\.capacity", 8},
                      {"node.die_to_die_lrt", R"([
                {"die_id": 1, "entries": [[2, 1, 0, 0], [0, 1, 0, 0], [1, 2, 0, 0], [1, 0, 0, 0]]}
            ])"_json}}}
    {
        connect();
    }

    MeshNodeTest(std::initializer_list<std::pair<std::string, sc::json>> settings) : TestModule{settings} { connect(); }

    void SetUp() override { start_simulation(); }

    int data_bus_width() const override { return 512; }
    int channel(const chi::NodeId id) const override { return 0; }

protected:
    void connect()
    {
        node.ports[WEST].connect(mock_node_west.con, "west");
        node.ports[EAST].connect(mock_node_east.con, "east");
        node.ports[NORTH].connect(mock_node_north.con, "north");
        node.ports[SOUTH].connect(mock_node_south.con, "south");

        node.ports[DIR_COUNT + 0].connect(mock_port_0.con, "port0");
        node.ports[DIR_COUNT + 1].connect(mock_port_1.con, "port1");

        node.config.bind(*this);
    }

    MeshNode node{"node", 1, 1, 2, 0, 0};
    MockNode mock_node_east{"east"};   // Location: 2, 1
    MockNode mock_node_west{"west"};   // Location: 0, 1
    MockNode mock_node_north{"north"}; // Location: 1, 2
    MockNode mock_node_south{"south"}; // Location: 1, 0
    MockPort mock_port_0{"rnf0"};
    MockPort mock_port_1{"rnf1"};
};

TEST_F(MeshNodeTest, CanCreateMeshNode) {}

TEST_F(MeshNodeTest, TrafficPassThruNode)
{
    mock_node_west.send_request(2, 1, 0);
    tick(4);
    EXPECT_EQ(mock_node_east.transactions_req.size(), 1u);

    mock_node_east.send_request(0, 1, 0);
    tick(4);
    EXPECT_EQ(mock_node_west.transactions_req.size(), 1u);

    mock_node_south.send_request(1, 2, 0);
    tick(4);
    EXPECT_EQ(mock_node_north.transactions_req.size(), 1u);

    mock_node_north.send_request(1, 0, 0);
    tick(4);
    EXPECT_EQ(mock_node_south.transactions_req.size(), 1u);
}

TEST_F(MeshNodeTest, TrafficExitsNode)
{
    mock_port_0.send_request(0, 1, 0); // Send West
    tick(4);
    EXPECT_EQ(mock_node_west.transactions_req.size(), 1u);

    mock_port_0.send_request(2, 1, 0); // Send East
    tick(4);
    EXPECT_EQ(mock_node_east.transactions_req.size(), 1u);

    mock_port_1.send_request(1, 0, 0); // Send North
    tick(4);
    EXPECT_EQ(mock_node_south.transactions_req.size(), 1u);

    mock_port_1.send_request(1, 2, 0); // Send South
    tick(4);
    EXPECT_EQ(mock_node_north.transactions_req.size(), 1u);
}

TEST_F(MeshNodeTest, NodeAcceptsTraffic)
{
    mock_node_west.send_response(1, 1, 0);
    tick(4);
    mock_node_east.send_response(1, 1, 0);
    tick(4);
    mock_node_south.send_response(1, 1, 1);
    tick(4);
    mock_node_north.send_response(1, 1, 1);
    tick(4);
    EXPECT_EQ(mock_port_0.transactions_rsp.size(), 2u);
    EXPECT_EQ(mock_port_1.transactions_rsp.size(), 2u);
}

class MeshNodeWithLatencyTest : public MeshNodeTest
{
public:
    MeshNodeWithLatencyTest()
        : MeshNodeTest{{{"~node\\.req_[snew]\\.size", 9},
                        {"~node\\.snp_[snew]\\.size", 8},
                        {"~node\\.req_p[0-9]+\\.size", 9},
                        {"~node\\.snp_p[0-9]+\\.size", 8},
                        {"~node\\.rsp[01]_[snew]\\.size", 8},
                        {"~node\\.dat[01]_[snew]\\.size", 8},
                        {"~node\\.rsp[01]_p[0-9]+\\.size", 8},
                        {"~node\\.dat[01]_p[0-9]+\\.size", 8},
                        {"port0_req_rx_0.latency", 4},
                        {"port0_req_tx_0.latency", 4},
                        {"east_req_tx_0.latency", 4},
                        {"east_req_rx_0.capacity", 8},
                        {"west_req_rx_0.capacity", 8},
                        {"north_req_rx_0.capacity", 8},
                        {"south_req_rx_0.capacity", 8},
                        {"~port[01]_rsp_rx_0\\.capacity", 8}}}
    {
    }

protected:
};

TEST_F(MeshNodeWithLatencyTest, BandwidthTest)
{
    // The idea here is that send_request does not check can_put() before putting.
    // If there is any cycle where we get backed up, this will throw an exception.
    // That should not happen if we have credits vs. latency dialed in correctly.
    //
    for (int i = 0; i < 20; i++) {
        mock_port_0.send_request(2, 1, 0); // Send east
        tick(1);
    }

    tick(12);
    EXPECT_EQ(mock_node_east.transactions_req.size(), 20u);
}

class MeshNodeDecodeDelayTest : public MeshNodeTest
{
public:
    MeshNodeDecodeDelayTest()
        : MeshNodeTest{{{"~node\\.req_[snew]\\.size", 8},
                        {"~node\\.snp_[snew]\\.size", 8},
                        {"~node\\.req_p[0-9]+\\.size", 8},
                        {"~node\\.snp_p[0-9]+\\.size", 8},
                        {"~node\\.rsp[01]_[snew]\\.size", 8},
                        {"~node\\.dat[01]_[snew]\\.size", 8},
                        {"~node\\.rsp[01]_p[0-9]+\\.size", 8},
                        {"~node\\.dat[01]_p[0-9]+\\.size", 8},
                        {"~node\\.req_decode_latency", 4},
                        {"east_req_rx_0.capacity", 8},
                        {"west_req_rx_0.capacity", 8},
                        {"north_req_rx_0.capacity", 8},
                        {"south_req_rx_0.capacity", 8},
                        {"~port[01]_rsp_rx_0\\.capacity", 8}}}
    {
    }
};

TEST_F(MeshNodeDecodeDelayTest, ReqDelay4)
{
    mock_port_0.send_request(0, 1, 0); // Send West

    // mock -> RTR, decode, route, RTR -> mock
    //       1         4      1        1        == 7
    tick(7);
    EXPECT_EQ(mock_node_west.transactions_req.size(), 0u);

    tick(1);
    EXPECT_EQ(mock_node_west.transactions_req.size(), 1u);
}

class MeshNode2RequestChannelsTest : public TestModule, public MeshConfigIfc
{
public:
    MeshNode2RequestChannelsTest()
        : TestModule{{{"~node[12]\\.two_request_channels", true},
                      {"~node[12]\\.req_[snew]\\.size", 8},
                      {"~node[12]\\.req1_[snew]\\.size", 8},
                      {"~node[12]\\.req_p[0-9]+\\.size", 8},
                      {"~port[01]_req_rx_0\\.capacity", 8}}}
    {
        connect();
    }

    void SetUp() override { start_simulation(); }

    int data_bus_width() const override { return 512; }
    int channel(const chi::NodeId id) const override { return 0; }

protected:
    void connect()
    {
        node1.ports[EAST].connect(node2.ports[WEST], "east_to_west");
        node1.ports[DIR_COUNT].connect(mock_port_0.con, "port0");
        node2.ports[DIR_COUNT].connect(mock_port_1.con, "port1");

        node1.config.bind(*this);
        node2.config.bind(*this);
    }

    MeshNode node1{"node1", 1, 1, 1, 0, 0};
    MeshNode node2{"node2", 2, 1, 1, 0, 0};

    MockPort mock_port_0{"rnf0"};
    MockPort mock_port_1{"rnf1"};
};

TEST_F(MeshNode2RequestChannelsTest, UseBothRequestChannels)
{
    get_event_stream()->enable();

    mock_port_0.send_request(2, 1, 0, chi::RequestOpcode::ReadNotSharedDirty);

    tick(1);

    mock_port_0.send_request(2, 1, 0, chi::RequestOpcode::Evict);

    tick(10);

    EXPECT_EQ(mock_port_1.transactions_req.size(), 2u);
    EXPECT_EQ(event_count("node1.send_req"), 1u);
    EXPECT_EQ(event_count("node1.send_req1"), 1u);
}

class MeshNodeCrossDieTgtIdTest : public MeshNodeTest
{
public:
    MeshNodeCrossDieTgtIdTest()
        : MeshNodeTest{{{"~node\\.req_[snew]\\.size", 8},
                        {"~node\\.snp_[snew]\\.size", 8},
                        {"~node\\.req_p[0-9]+\\.size", 8},
                        {"~node\\.snp_p[0-9]+\\.size", 8},
                        {"~node\\.rsp[01]_[snew]\\.size", 8},
                        {"~node\\.dat[01]_[snew]\\.size", 8},
                        {"~node\\.rsp[01]_p[0-9]+\\.size", 8},
                        {"~node\\.dat[01]_p[0-9]+\\.size", 8},
                        {"east_req_rx_0.capacity", 8},
                        {"west_req_rx_0.capacity", 8},
                        {"north_req_rx_0.capacity", 8},
                        {"south_req_rx_0.capacity", 8},
                        {"~port[01]_rsp_rx_0\\.capacity", 8},
                        {"node.die_to_die_lrt", R"([
                {"die_id": 1, "entries": [[2, 1, 0, 0], [0, 1, 0, 0], [1, 2, 0, 0], [1, 0, 0, 0]]}
            ])"_json},
                        {"node.lrt_index_with_tgt_id", true}}}
    {
    }
};

class MeshNodeCrossDieFixedTest : public MeshNodeTest
{
public:
    MeshNodeCrossDieFixedTest()
        : MeshNodeTest{{{"~node\\.req_[snew]\\.size", 8},
                        {"~node\\.snp_[snew]\\.size", 8},
                        {"~node\\.req_p[0-9]+\\.size", 8},
                        {"~node\\.snp_p[0-9]+\\.size", 8},
                        {"~node\\.rsp[01]_[snew]\\.size", 8},
                        {"~node\\.dat[01]_[snew]\\.size", 8},
                        {"~node\\.rsp[01]_p[0-9]+\\.size", 8},
                        {"~node\\.dat[01]_p[0-9]+\\.size", 8},
                        {"east_req_rx_0.capacity", 8},
                        {"west_req_rx_0.capacity", 8},
                        {"north_req_rx_0.capacity", 8},
                        {"south_req_rx_0.capacity", 8},
                        {"~port[01]_rsp_rx_0\\.capacity", 8},
                        {"node.die_to_die_lrt", R"([
                {"die_id": 1, "entries": [[2, 1, 0, 0], [0, 1, 0, 0], [1, 2, 0, 0], [1, 0, 0, 0]]}
            ])"_json},
                        {"node.lrt_index_fixed", "[1, 3]"_json}}}
    {
    }
};

TEST_F(MeshNodeTest, CrossDieUsingTxnID)
{
    for (uint16_t txn_id = 0; txn_id < 8; ++txn_id) {
        mock_port_0.send_request(NodeId{NodeId::DieType::CPU, 0, 1, 0}, txn_id);
        tick(1);
    }

    tick(4);

    ASSERT_EQ(mock_node_east.transactions_req.size(), 2u);
    EXPECT_EQ(mock_node_east.transactions_req.at(0)->TxnID, 0);
    EXPECT_EQ(mock_node_east.transactions_req.at(1)->TxnID, 4);

    ASSERT_EQ(mock_node_west.transactions_req.size(), 2u);
    EXPECT_EQ(mock_node_west.transactions_req.at(0)->TxnID, 1);
    EXPECT_EQ(mock_node_west.transactions_req.at(1)->TxnID, 5);

    ASSERT_EQ(mock_node_north.transactions_req.size(), 2u);
    EXPECT_EQ(mock_node_north.transactions_req.at(0)->TxnID, 2);
    EXPECT_EQ(mock_node_north.transactions_req.at(1)->TxnID, 6);

    ASSERT_EQ(mock_node_south.transactions_req.size(), 2u);
    EXPECT_EQ(mock_node_south.transactions_req.at(0)->TxnID, 3);
    EXPECT_EQ(mock_node_south.transactions_req.at(1)->TxnID, 7);
}

TEST_F(MeshNodeCrossDieTgtIdTest, CrossDieUsingTgtID)
{
    for (uint8_t agent_id = 0; agent_id < 8; ++agent_id) {
        mock_port_0.send_request(NodeId{NodeId::DieType::CPU, 0, 1, agent_id}, 0);
        tick(1);
    }

    tick(4);

    ASSERT_EQ(mock_node_east.transactions_req.size(), 2u);
    EXPECT_EQ(mock_node_east.transactions_req.at(0)->TgtID.agent_id(), 0);
    EXPECT_EQ(mock_node_east.transactions_req.at(1)->TgtID.agent_id(), 4);

    ASSERT_EQ(mock_node_west.transactions_req.size(), 2u);
    EXPECT_EQ(mock_node_west.transactions_req.at(0)->TgtID.agent_id(), 1);
    EXPECT_EQ(mock_node_west.transactions_req.at(1)->TgtID.agent_id(), 5);

    ASSERT_EQ(mock_node_north.transactions_req.size(), 2u);
    EXPECT_EQ(mock_node_north.transactions_req.at(0)->TgtID.agent_id(), 2);
    EXPECT_EQ(mock_node_north.transactions_req.at(1)->TgtID.agent_id(), 6);

    ASSERT_EQ(mock_node_south.transactions_req.size(), 2u);
    EXPECT_EQ(mock_node_south.transactions_req.at(0)->TgtID.agent_id(), 3);
    EXPECT_EQ(mock_node_south.transactions_req.at(1)->TgtID.agent_id(), 7);
}

TEST_F(MeshNodeCrossDieFixedTest, CrossDieFixedByPort)
{
    for (uint16_t txn_id = 0; txn_id < 8;) {
        mock_port_0.send_request(NodeId{NodeId::DieType::CPU, 0, 1, 0}, txn_id++);
        mock_port_1.send_request(NodeId{NodeId::DieType::CPU, 0, 1, 0}, txn_id++);
        tick(1);
    }

    tick(4);

    ASSERT_EQ(mock_node_east.transactions_req.size(), 0u);
    ASSERT_EQ(mock_node_north.transactions_req.size(), 0u);

    ASSERT_EQ(mock_node_west.transactions_req.size(), 4u);
    EXPECT_EQ(mock_node_west.transactions_req.at(0)->TxnID, 0);
    EXPECT_EQ(mock_node_west.transactions_req.at(1)->TxnID, 2);
    EXPECT_EQ(mock_node_west.transactions_req.at(2)->TxnID, 4);
    EXPECT_EQ(mock_node_west.transactions_req.at(3)->TxnID, 6);

    ASSERT_EQ(mock_node_south.transactions_req.size(), 4u);
    EXPECT_EQ(mock_node_south.transactions_req.at(0)->TxnID, 1);
    EXPECT_EQ(mock_node_south.transactions_req.at(1)->TxnID, 3);
    EXPECT_EQ(mock_node_south.transactions_req.at(2)->TxnID, 5);
    EXPECT_EQ(mock_node_south.transactions_req.at(3)->TxnID, 7);
}

} // namespace ampere::mesh

/*
 * vim: sw=4:ts=4:et:si:cindent:cino=\:0,(0,g0,t0:
 */
