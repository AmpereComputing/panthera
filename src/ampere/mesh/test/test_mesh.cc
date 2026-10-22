//
// Copyright (c) 2020 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "ampere/mesh/mesh.h"
#include "ampere/mesh/cpx.h"
#include "ampere/mesh/test/dummy_requester.h"
#include "ampere/sc/testing/test_module.h"

namespace ampere::mesh {

using namespace ampere::mesh;
using namespace ampere::sc;

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

class MeshRoutingTest : public TestModule, public MeshConfigIfc
{
public:
    MeshRoutingTest()
        : TestModule{
              {{"mesh.size_x", 3},
               {"mesh.size_y", 3},
               {"~mesh\\.rtr_X[0-9]+_Y[0-9]+\\.req_[snew]\\.size", 8},
               {"~mesh\\.rtr_X[0-9]+_Y[0-9]+\\.snp_[snew]\\.size", 8},
               {"~mesh\\.rtr_X[0-9]+_Y[0-9]+\\.req_p[0-9]+\\.size", 8},
               {"~mesh\\.rtr_X[0-9]+_Y[0-9]+\\.snp_p[0-9]+\\.size", 8},
               {"~mesh\\.rtr_X[0-9]+_Y[0-9]+\\.rsp[01]_[snew]\\.size", 8},
               {"~mesh\\.rtr_X[0-9]+_Y[0-9]+\\.dat[01]_[snew]\\.size", 8},
               {"~mesh\\.rtr_X[0-9]+_Y[0-9]+\\.rsp[01]_p[0-9]+\\.size", 8},
               {"~mesh\\.rtr_X[0-9]+_Y[0-9]+\\.dat[01]_p[0-9]+\\.size", 8},
               {"d0_req_rx_0.capacity", 8},
               {"d1_req_rx_0.capacity", 8},
               {"~mesh\\.rtr_X[0-9]+_Y[0-9]+_req_rx_0.capacity", 8},
               {"~mesh\\.rtr_X[0-9]+_Y[0-9]+\\.local_lut",
                std::map<uint8_t, LocalRoutingId>{{0, LocalRoutingId{0, 0, 0, 0}}, {1, LocalRoutingId{2, 2, 0, 0}}}}}}
    {
        connect();
    }

    MeshRoutingTest(std::initializer_list<std::pair<std::string, sc::json>> settings) : TestModule{settings}
    {
        connect();
    }

    void SetUp() override { start_simulation(); }

    int data_bus_width() const override { return 512; }
    int channel(const chi::NodeId id) const override { return 0; }

protected:
    void connect()
    {
        mesh.get_device_port(0, 0, 0).connect(dev0.mesh_port, "d0");
        mesh.get_device_port(2, 2, 0).connect(dev1.con, "d1");

        mesh.config.bind(*this);
    }

    Mesh mesh{"mesh", 0, 0};

    DummyRequester dev0{"dev0", NodeId{0, 0, 0, 0}};
    MockPort dev1{"dev1"};
};

TEST_F(MeshRoutingTest, TestXFirstRouting)
{
    get_event_stream()->enable();
    dev0.send_request(NodeId{2, 2, 0, 0}, 0, chi::RequestOpcode::ReadUnique, 0xaced00_pa);
    tick(20);
    EXPECT_EQ(event_count("mesh.rtr_X0_Y0.recv_req"), 1u);
    EXPECT_EQ(event_count("mesh.rtr_X1_Y0.recv_req"), 1u);
    EXPECT_EQ(event_count("mesh.rtr_X2_Y0.recv_req"), 1u);
    EXPECT_EQ(event_count("mesh.rtr_X2_Y1.recv_req"), 1u);
    EXPECT_EQ(event_count("mesh.rtr_X2_Y2.recv_req"), 1u);
    EXPECT_EQ(dev1.transactions_req.size(), 1u);
}

class MeshRoutingTestYFirst : public MeshRoutingTest
{
public:
    MeshRoutingTestYFirst()
        : MeshRoutingTest(
              {{"mesh.size_x", 3},
               {"mesh.size_y", 3},
               {"~mesh\\.rtr_X[0-9]+_Y[0-9]+\\.req_[snew]\\.size", 8},
               {"~mesh\\.rtr_X[0-9]+_Y[0-9]+\\.snp_[snew]\\.size", 8},
               {"~mesh\\.rtr_X[0-9]+_Y[0-9]+\\.req_p[0-9]+\\.size", 8},
               {"~mesh\\.rtr_X[0-9]+_Y[0-9]+\\.snp_p[0-9]+\\.size", 8},
               {"~mesh\\.rtr_X[0-9]+_Y[0-9]+\\.rsp[01]_[snew]\\.size", 8},
               {"~mesh\\.rtr_X[0-9]+_Y[0-9]+\\.dat[01]_[snew]\\.size", 8},
               {"~mesh\\.rtr_X[0-9]+_Y[0-9]+\\.rsp[01]_p[0-9]+\\.size", 8},
               {"~mesh\\.rtr_X[0-9]+_Y[0-9]+\\.dat[01]_p[0-9]+\\.size", 8},
               {"d0_req_rx_0.capacity", 8},
               {"d1_req_rx_0.capacity", 8},
               {"~mesh\\.rtr_X[0-9]+_Y[0-9]+_req_rx_0.capacity", 8},
               {"~mesh\\.rtr_X[0-9]+_Y[0-9]+\\.local_lut",
                std::map<uint8_t, LocalRoutingId>{{0, LocalRoutingId{0, 0, 0, 0}}, {1, LocalRoutingId{2, 2, 0, 0}}}},
               {"~mesh\\.rtr_X[0-9]+_Y[0-9]+\\.routing_order", "yx"}})
    {
    }
};

TEST_F(MeshRoutingTestYFirst, TestYFirstRouting)
{
    get_event_stream()->enable();
    dev0.send_request(NodeId{2, 2, 0, 0}, 0, chi::RequestOpcode::ReadUnique, 0xaced00_pa);
    tick(20);
    EXPECT_EQ(event_count("mesh.rtr_X0_Y0.recv_req"), 1u);
    EXPECT_EQ(event_count("mesh.rtr_X0_Y1.recv_req"), 1u);
    EXPECT_EQ(event_count("mesh.rtr_X0_Y2.recv_req"), 1u);
    EXPECT_EQ(event_count("mesh.rtr_X1_Y2.recv_req"), 1u);
    EXPECT_EQ(event_count("mesh.rtr_X2_Y2.recv_req"), 1u);
    EXPECT_EQ(dev1.transactions_req.size(), 1u);
}

class MeshXYZRoutingTest : public TestModule, public MeshConfigIfc
{
public:
    MeshXYZRoutingTest()
        : TestModule{{{"mesh.size_x", 4},
                      {"mesh.size_y", 4},
                      {"mesh.size_z", 4},
                      {"~mesh\\.rtr_X[0-9]+_Y[0-9]+_Z[0-9]+\\.req_[snewud]\\.size", 8},
                      {"~mesh\\.rtr_X[0-9]+_Y[0-9]+_Z[0-9]+\\.snp_[snewud]\\.size", 8},
                      {"~mesh\\.rtr_X[0-9]+_Y[0-9]+_Z[0-9]+\\.req_p[0-9]+\\.size", 8},
                      {"~mesh\\.rtr_X[0-9]+_Y[0-9]+_Z[0-9]+\\.snp_p[0-9]+\\.size", 8},
                      {"~mesh\\.rtr_X[0-9]+_Y[0-9]+_Z[0-9]+\\.rsp[01]_[snewud]\\.size", 8},
                      {"~mesh\\.rtr_X[0-9]+_Y[0-9]+_Z[0-9]+\\.dat[01]_[snewud]\\.size", 8},
                      {"~mesh\\.rtr_X[0-9]+_Y[0-9]+_Z[0-9]+\\.rsp[01]_p[0-9]+\\.size", 8},
                      {"~mesh\\.rtr_X[0-9]+_Y[0-9]+_Z[0-9]+\\.dat[01]_p[0-9]+\\.size", 8},
                      {"d0_req_rx_0.capacity", 8},
                      {"d1_req_rx_0.capacity", 8},
                      {"~mesh\\.rtr_X[0-9]+_Y[0-9]+_Z[0-9]+_req_rx_0.capacity", 8},
                      {"~mesh\\.rtr_X[0-9]+_Y[0-9]+_Z[0-9]+\\.local_lut",
                       std::map<uint8_t, LocalRoutingId>{{0, LocalRoutingId{0, 0, 0, 0, 0}},
                                                         {1, LocalRoutingId{2, 2, 3, 0, 0}}}},
                      {"~mesh\\.rtr_X[0-9]+_Y[0-9]+_Z[0-9]+\\.routing_order", "xyz"}}}
    {
        connect();
    }

    MeshXYZRoutingTest(std::initializer_list<std::pair<std::string, sc::json>> settings) : TestModule{settings}
    {
        connect();
    }

    void SetUp() override { start_simulation(); }

    int data_bus_width() const override { return 512; }
    int channel(const chi::NodeId id) const override { return 0; }

protected:
    void connect()
    {
        mesh.get_device_port(0, 0, 0, 0).connect(dev0.mesh_port, "d0");
        mesh.get_device_port(2, 2, 3, 0).connect(dev1.con, "d1");

        mesh.config.bind(*this);
    }

    Mesh mesh{"mesh", 0, 0};

    DummyRequester dev0{"dev0", NodeId{NodeId::DieType::CPU, 0, 0, 0}};
    MockPort dev1{"dev1"};
};

TEST_F(MeshXYZRoutingTest, TestXYZRouting)
{
    get_event_stream()->enable();
    dev0.send_request(NodeId{NodeId::DieType::CPU, 0, 0, 1}, 0, chi::RequestOpcode::ReadUnique, 0xaced00_pa);
    tick(20);
    EXPECT_EQ(event_count("mesh.rtr_X0_Y0_Z0.recv_req"), 1U);
    EXPECT_EQ(event_count("mesh.rtr_X1_Y0_Z0.recv_req"), 1U);
    EXPECT_EQ(event_count("mesh.rtr_X2_Y0_Z0.recv_req"), 1U);
    EXPECT_EQ(event_count("mesh.rtr_X2_Y1_Z0.recv_req"), 1U);
    EXPECT_EQ(event_count("mesh.rtr_X2_Y2_Z0.recv_req"), 1U);
    EXPECT_EQ(event_count("mesh.rtr_X2_Y2_Z1.recv_req"), 1U);
    EXPECT_EQ(event_count("mesh.rtr_X2_Y2_Z2.recv_req"), 1U);
    EXPECT_EQ(event_count("mesh.rtr_X2_Y2_Z3.recv_req"), 1U);
    EXPECT_EQ(dev1.transactions_req.size(), 1U);
}

// Z first mechansim will not work since Z transitions will happen only at destination router
/*
class MeshZXYRoutingTest : public MeshXYZRoutingTest
{
public:
    MeshZXYRoutingTest()
        : MeshXYZRoutingTest{{{"mesh.size_x", 4},
                              {"mesh.size_y", 4},
                              {"mesh.size_z", 4},
                              {"~mesh\\.rtr_X[0-9]+_Y[0-9]+_Z[0-9]+\\.req_[snewud]\\.size", 8},
                              {"~mesh\\.rtr_X[0-9]+_Y[0-9]+_Z[0-9]+\\.snp_[snewud]\\.size", 8},
                              {"~mesh\\.rtr_X[0-9]+_Y[0-9]+_Z[0-9]+\\.req_p[0-9]+\\.size", 8},
                              {"~mesh\\.rtr_X[0-9]+_Y[0-9]+_Z[0-9]+\\.snp_p[0-9]+\\.size", 8},
                              {"~mesh\\.rtr_X[0-9]+_Y[0-9]+_Z[0-9]+\\.rsp[01]_[snewud]\\.size", 8},
                              {"~mesh\\.rtr_X[0-9]+_Y[0-9]+_Z[0-9]+\\.dat[01]_[snewud]\\.size", 8},
                              {"~mesh\\.rtr_X[0-9]+_Y[0-9]+_Z[0-9]+\\.rsp[01]_p[0-9]+\\.size", 8},
                              {"~mesh\\.rtr_X[0-9]+_Y[0-9]+_Z[0-9]+\\.dat[01]_p[0-9]+\\.size", 8},
                              {"d0_req_rx_0.capacity", 8},
                              {"d1_req_rx_0.capacity", 8},
                              {"~mesh\\.rtr_X[0-9]+_Y[0-9]+_Z[0-9]+_req_rx_0.capacity", 8},
                              {"~mesh\\.rtr_X[0-9]+_Y[0-9]+_Z[0-9]+\\.local_lut", "[[0,0,0,0,0],[2,2,3,0,0]]"_json},
                              {"~mesh\\.rtr_X[0-9]+_Y[0-9]+_Z[0-9]+\\.routing_order", "zxy"}}}
    {
    }
};

TEST_F(MeshZXYRoutingTest, TestZXYRouting)
{
    get_event_stream()->enable();
    dev0.send_request(NodeId{NodeId::DieType::CPU, 0, 0, 1}, 0, chi::RequestOpcode::ReadUnique, 0xaced00_pa);
    tick(20);
    EXPECT_EQ(event_count("mesh.rtr_X0_Y0_Z0.recv_req"), 1U);
    EXPECT_EQ(event_count("mesh.rtr_X0_Y0_Z1.recv_req"), 1U);
    EXPECT_EQ(event_count("mesh.rtr_X0_Y0_Z2.recv_req"), 1U);
    EXPECT_EQ(event_count("mesh.rtr_X0_Y0_Z3.recv_req"), 1U);
    EXPECT_EQ(event_count("mesh.rtr_X1_Y0_Z3.recv_req"), 1U);
    EXPECT_EQ(event_count("mesh.rtr_X2_Y0_Z3.recv_req"), 1U);
    EXPECT_EQ(event_count("mesh.rtr_X2_Y1_Z3.recv_req"), 1U);
    EXPECT_EQ(event_count("mesh.rtr_X2_Y2_Z3.recv_req"), 1U);
    EXPECT_EQ(dev1.transactions_req.size(), 1U);
}

class MeshXZYRoutingTest : public MeshXYZRoutingTest
{
public:
    MeshXZYRoutingTest()
        : MeshXYZRoutingTest{{{"mesh.size_x", 4},
                              {"mesh.size_y", 4},
                              {"mesh.size_z", 4},
                              {"~mesh\\.rtr_X[0-9]+_Y[0-9]+_Z[0-9]+\\.req_[snewud]\\.size", 8},
                              {"~mesh\\.rtr_X[0-9]+_Y[0-9]+_Z[0-9]+\\.snp_[snewud]\\.size", 8},
                              {"~mesh\\.rtr_X[0-9]+_Y[0-9]+_Z[0-9]+\\.req_p[0-9]+\\.size", 8},
                              {"~mesh\\.rtr_X[0-9]+_Y[0-9]+_Z[0-9]+\\.snp_p[0-9]+\\.size", 8},
                              {"~mesh\\.rtr_X[0-9]+_Y[0-9]+_Z[0-9]+\\.rsp[01]_[snewud]\\.size", 8},
                              {"~mesh\\.rtr_X[0-9]+_Y[0-9]+_Z[0-9]+\\.dat[01]_[snewud]\\.size", 8},
                              {"~mesh\\.rtr_X[0-9]+_Y[0-9]+_Z[0-9]+\\.rsp[01]_p[0-9]+\\.size", 8},
                              {"~mesh\\.rtr_X[0-9]+_Y[0-9]+_Z[0-9]+\\.dat[01]_p[0-9]+\\.size", 8},
                              {"d0_req_rx_0.capacity", 8},
                              {"d1_req_rx_0.capacity", 8},
                              {"~mesh\\.rtr_X[0-9]+_Y[0-9]+_Z[0-9]+_req_rx_0.capacity", 8},
                              {"~mesh\\.rtr_X[0-9]+_Y[0-9]+_Z[0-9]+\\.local_lut", "[[0,0,0,0,0],[2,2,3,0,0]]"_json},
                              {"~mesh\\.rtr_X[0-9]+_Y[0-9]+_Z[0-9]+\\.routing_order", "xzy"}}}
    {
    }
};

TEST_F(MeshXZYRoutingTest, TestXZYRouting)
{
    get_event_stream()->enable();
    dev0.send_request(NodeId{NodeId::DieType::CPU, 0, 0, 1}, 0, chi::RequestOpcode::ReadUnique, 0xaced00_pa);
    tick(20);
    EXPECT_EQ(event_count("mesh.rtr_X0_Y0_Z0.recv_req"), 1U);
    EXPECT_EQ(event_count("mesh.rtr_X1_Y0_Z0.recv_req"), 1U);
    EXPECT_EQ(event_count("mesh.rtr_X2_Y0_Z0.recv_req"), 1U);
    EXPECT_EQ(event_count("mesh.rtr_X2_Y0_Z1.recv_req"), 1U);
    EXPECT_EQ(event_count("mesh.rtr_X2_Y0_Z2.recv_req"), 1U);
    EXPECT_EQ(event_count("mesh.rtr_X2_Y0_Z3.recv_req"), 1U);
    EXPECT_EQ(event_count("mesh.rtr_X2_Y1_Z3.recv_req"), 1U);
    EXPECT_EQ(event_count("mesh.rtr_X2_Y2_Z3.recv_req"), 1U);
    EXPECT_EQ(dev1.transactions_req.size(), 1U);
}
*/

class MeshTXYZRoutingTest : public MeshXYZRoutingTest
{
public:
    MeshTXYZRoutingTest()
        : MeshXYZRoutingTest{{{"mesh.size_x", 4},
                              {"mesh.size_y", 4},
                              {"mesh.size_z", 4},
                              {"~mesh\\.rtr_X[0-9]+_Y[0-9]+_Z[0-9]+\\.req_[snewud]\\.size", 8},
                              {"~mesh\\.rtr_X[0-9]+_Y[0-9]+_Z[0-9]+\\.snp_[snewud]\\.size", 8},
                              {"~mesh\\.rtr_X[0-9]+_Y[0-9]+_Z[0-9]+\\.req_p[0-9]+\\.size", 8},
                              {"~mesh\\.rtr_X[0-9]+_Y[0-9]+_Z[0-9]+\\.snp_p[0-9]+\\.size", 8},
                              {"~mesh\\.rtr_X[0-9]+_Y[0-9]+_Z[0-9]+\\.rsp[01]_[snewud]\\.size", 8},
                              {"~mesh\\.rtr_X[0-9]+_Y[0-9]+_Z[0-9]+\\.dat[01]_[snewud]\\.size", 8},
                              {"~mesh\\.rtr_X[0-9]+_Y[0-9]+_Z[0-9]+\\.rsp[01]_p[0-9]+\\.size", 8},
                              {"~mesh\\.rtr_X[0-9]+_Y[0-9]+_Z[0-9]+\\.dat[01]_p[0-9]+\\.size", 8},
                              {"d0_req_rx_0.capacity", 8},
                              {"d1_req_rx_0.capacity", 8},
                              {"~mesh\\.rtr_X[0-9]+_Y[0-9]+_Z[0-9]+_req_rx_0.capacity", 8},
                              {"~mesh\\.rtr_X[0-9]+_Y[0-9]+_Z[0-9]+\\.local_lut",
                               std::map<uint8_t, LocalRoutingId>{{64, LocalRoutingId{0, 0, 0, 0, 3}},
                                                                 {192, LocalRoutingId{0, 1, 0, 0, 3}}}},
                              {"~mesh\\.rtr_X[0-9]+_Y[0-9]+_Z[0-9]+\\.routing_order", "txyz"},
                              {"~mesh\\.rtr_X[0-9]+_Y[0-9]+_Z[0-9]+\\.assign_transport_z", true}}}
    {
    }
};

TEST_F(MeshTXYZRoutingTest, TestTXYZRouting)
{
    get_event_stream()->enable();
    dev0.send_request(NodeId{NodeId::DieType::CPU, 0, 0, 297}, 0, chi::RequestOpcode::ReadUnique, 0xaced00_pa);
    tick(20);
    EXPECT_EQ(event_count("mesh.rtr_X0_Y0_Z0.recv_req"), 1U);
    EXPECT_EQ(event_count("mesh.rtr_X0_Y0_Z1.recv_req"), 1U);
    EXPECT_EQ(event_count("mesh.rtr_X0_Y0_Z2.recv_req"), 1U);
    EXPECT_EQ(event_count("mesh.rtr_X1_Y0_Z2.recv_req"), 1U);
    EXPECT_EQ(event_count("mesh.rtr_X2_Y0_Z2.recv_req"), 1U);
    EXPECT_EQ(event_count("mesh.rtr_X2_Y1_Z2.recv_req"), 1U);
    EXPECT_EQ(event_count("mesh.rtr_X2_Y2_Z2.recv_req"), 1U);
    EXPECT_EQ(event_count("mesh.rtr_X2_Y2_Z3.recv_req"), 1U);
    EXPECT_EQ(dev1.transactions_req.size(), 1U);
}

class MeshCpxTest : public TestModule, public MeshConfigIfc
{
public:
    MeshCpxTest()
        : TestModule(
              {{"cpx.n", 2},
               {"cpx.nrsp", 2},
               {"~node\\.req_p[0-9]+\\.size", 8},
               {"~node\\.snp_p[0-9]+\\.size", 8},
               {"~node\\.rsp[01]_p[0-9]+\\.size", 8},
               {"~node\\.dat[01]_p[0-9]+\\.size", 8},
               {"cpx.dev_0.snp_in.size", 8},  // cpools
               {"cpx.mesh_snp_in.size", 8},   // cpools
               {"cpx0_snp_tx_0.latency", 1},  // connection
               {"cpx0_snp_rx_0.latency", 0},  // connection
               {"dev0_snp_tx_0.latency", 1},  // connection
               {"dev0_snp_rx_0.latency", 0},  // connection
               {"dev1_snp_tx_0.latency", 1},  // connection
               {"dev1_snp_rx_0.latency", 0},  // connection
               {"dev1_snp_tx_0.capacity", 8}, // connection
               {"node.local_lut", std::map<uint8_t, LocalRoutingId>{{0, LocalRoutingId{0}}, {1, LocalRoutingId{1}}}}})
    {
        connect();
    }

    void SetUp() override
    {
        get_environment()->parameters.check_all_matched();
        start_simulation();
    }

    int data_bus_width() const override { return 512; }
    int channel(const chi::NodeId id) const override { return 0; }

protected:
    void connect()
    {
        node.ports[DIR_COUNT + 0].connect(cpx.mesh_port, "cpx0");
        dev0.mesh_port.connect(cpx.device_port[0], "dev0"); // cpx -> dev0
        dev1.mesh_port.connect(cpx.device_port[1], "dev1"); // cpx -> dev1

        node.config.bind(*this);
        cpx.config.bind(*this);
        dev1.announcer.bind(announcer);
    }

    MeshNode node{"node", 0, 0, 1, 0, 0};
    CPX cpx{"cpx"};
    DummyRequester dev0{"dev0", NodeId{0, 0, 0, 0}};
    DummyRequester dev1{"dev1", NodeId{0, 0, 0, 1}};
    MockRequesterAnnouncer announcer;
};

TEST_F(MeshCpxTest, CPX_Latency)
{
    // CPX should be able to pass traffic from device to router in 1 cycles.
    // CPX should be able to pass traffic from router to device in 2 cycles
    get_event_stream()->enable();
    dev0.send_snoop(NodeId{0, 0, 0, 1}, 0, chi::SnoopOpcode::Evict, 0xaced0000_pa);
    tick(1); // Cycle 0, CPX takes from device and puts into Router node
    tick(1); // Cycle 1, arrive is called before depart, so this cycle the router takes in the request from the router
             // node
    EXPECT_EQ(event_count("node.recv_snp"), 1);
    EXPECT_CALL(announcer, snoop(SnoopOpcode::Evict, 0));
    tick(3);
}

} // namespace ampere::mesh
