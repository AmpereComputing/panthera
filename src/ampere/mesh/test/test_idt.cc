//
// Copyright (c) 2020 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//
#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "ampere/mesh/home_node.h"
#include "ampere/mesh/mesh_node.h"
#include "ampere/mesh/idt.h"
#include "ampere/mesh/test/dummy_memory.h"
#include "ampere/mesh/test/dummy_requester.h"
#include "ampere/mesh/test/mock_addr_map.h"
#include "ampere/mesh/test/mock_mesh_port.h"
#include "ampere/sc/testing/test_interfaces.h"
#include "ampere/sc/testing/test_module.h"

using namespace ampere::mesh;
using namespace ampere::archx;

class TestHNWithIDT : public sc::IntegrationTest, public MeshConfigIfc
{
protected:
    TestHNWithIDT()
        : sc::IntegrationTest{
              {"~cpu_node\\.req_p[0-9]+\\.size", 8},
              {"~cpu_node\\.snp_p[0-9]+\\.size", 8},
              {"~cpu_node\\.rsp[01]_p[0-9]+\\.size", 8},
              {"~cpu_node\\.dat[01]_p[0-9]+\\.size", 8},
              {"~io_node\\.req_p[0-9]+\\.size", 8},
              {"~io_node\\.snp_p[0-9]+\\.size", 8},
              {"~io_node\\.rsp[01]_p[0-9]+\\.size", 8},
              {"~io_node\\.dat[01]_p[0-9]+\\.size", 8},
              {"idt.a_to_b_idt.rcbusy.requester_lut", "[-8, -1, 1, 8]"_json},
              {"idt.a_to_b_idt.rcbusy.throttle",
               "[256, 240, 224, 208, 192, 176, 160, 144, 128, 112, 96, 80, 64, 48, 32, 16]"_json},
              {"idt.a_to_b_idt.cbusy.c_threshold_high", 240},
              {"idt.a_to_b_idt.cbusy.c_threshold_mid", 192},
              {"idt.a_to_b_idt.cbusy.c_threshold_low", 128},
              {"idt.b_to_a_idt.rcbusy.requester_lut", "[-8, -1, 1, 8]"_json},
              {"idt.b_to_a_idt.rcbusy.throttle",
               "[256, 240, 224, 208, 192, 176, 160, 144, 128, 112, 96, 80, 64, 48, 32, 16]"_json},
              {"idt.b_to_a_idt.cbusy.c_threshold_high", 240},
              {"idt.b_to_a_idt.cbusy.c_threshold_mid", 192},
              {"idt.b_to_a_idt.cbusy.c_threshold_low", 128},
              {"idt.rdi.a_in_req.fifo.size", 4},
              {"idt.rdi.a_in_rsp0.fifo.size", 4},
              {"idt.rdi.a_in_rsp1.fifo.size", 4},
              {"idt.rdi.a_in_snp.fifo.size", 4},
              {"idt.rdi.a_in_dat.fifo.size", 4},
              {"idt.rdi.b_in_req.fifo.size", 4},
              {"idt.rdi.b_in_rsp0.fifo.size", 4},
              {"idt.rdi.b_in_rsp1.fifo.size", 4},
              {"idt.rdi.b_in_snp.fifo.size", 4},
              {"idt.rdi.b_in_dat.fifo.size", 4},
              {"idt.rdi.packer.rules", R"([
                  {"Req": 1, "Crd": 1, "Links": [2]},
                  {"Rsp": 2, "Crd": 2, "Links": [1]},
                  {"Dat": 1, "Links": [0]}
                ])"_json},
              {"idt.a_to_b_idt.rdi_in_req.size", 8},
              {"idt.a_to_b_idt.rdi_in_rsp.size", 8},
              {"idt.a_to_b_idt.rdi_in_dat.size", 8},
              {"idt.a_to_b_idt.idt_in_req.size", 8},
              {"idt.a_to_b_idt.idt_in_rsp.size", 8},
              {"idt.a_to_b_idt.idt_in_dat.size", 8},
              {"idt.b_to_a_idt.rdi_in_req.size", 8},
              {"idt.b_to_a_idt.rdi_in_rsp.size", 8},
              {"idt.b_to_a_idt.rdi_in_dat.size", 8},
              {"idt.b_to_a_idt.idt_in_req.size", 8},
              {"idt.b_to_a_idt.idt_in_rsp.size", 8},
              {"idt.b_to_a_idt.idt_in_dat.size", 8},
              {"idt.rdi.delay", "1ns"},
              {"hn.sam.tgt_lut", "[[0, 0, 1, 0]]"_json},
              {"hn.sam.system_address_map.connect", "system_address_map"},
              {"hn.node_id", NodeId{NodeId::DieType::IO, 0, 1, 1}},
              {"cpu_node.local_lut",
               std::vector<LocalRoutingId>{
                   LocalRoutingId{0, 0, 0, 0} // Core agent id = 0
               }},
              {"cpu_node.die_to_die_lrt",
               "[{\"die_id\": 1, \"entries\": [[0,0,1,0], [0,0,1,0], [0,0,1,0], [0,0,1,0]]}]"_json},
              {"io_node.local_lut",
               std::vector<LocalRoutingId>{
                   LocalRoutingId{0, 0, 0, 0},
                   LocalRoutingId{0, 0, 0, 0}, // Homenode agent id = 1
                   LocalRoutingId{0, 0, 1, 0}  // Memory agent id = 2
               }},
              {"io_node.die_to_die_lrt",
               "[{\"die_id\": 0, \"entries\": [[0,0,2,0], [0,0,2,0], [0,0,2,0], [0,0,2,0]]}]"_json},
              {"~to_hn_.*_rx_[01]\\.capacity", 8},
              {"~to_mem_.*_rx_[01]\\.capacity", 8},
              {"~to_core_.*_rx_[01]\\.capacity", 8},
          }
    {
        connect_ports();
        get_environment()->parameters.check_all_matched();
    }
    TestHNWithIDT(std::initializer_list<std::pair<std::string, sc::json>> settings) : sc::IntegrationTest(settings)
    {
        connect_ports();
        get_environment()->parameters.check_all_matched();
    }

    MeshNode cpu_node{"cpu_node", 0, 0, 2, 0, 0};
    MeshNode io_node{"io_node", 0, 0, 3, 0, 1};
    DummyRequester core{"core", NodeId{NodeId::DieType::CPU, 0, 0, 0}}; // LRID [0,0,0,0]   Requester is in die 0
    IDT idt{"idt"};
    HomeNode hn{"hn"};                                            // LRID [0,0,0,0]   HN and Mem is in die 1
    DummyMemory mem{"mem", NodeId{NodeId::DieType::IO, 0, 1, 2}}; // LRID [0,0,1,0]

    MockRequesterAnnouncer announcer;
    MockSystemAddressMap system_address_map{"system_address_map"};

    int data_bus_width() const override { return 512; }
    int channel(const chi::NodeId id) const override { return 0; }

private:
    void connect_ports()
    {
        cpu_node.ports[DIR_COUNT + 0].connect(core.mesh_port, "to_core");
        cpu_node.ports[DIR_COUNT + 1].connect(idt.a, "to_idt_a");
        io_node.ports[DIR_COUNT + 0].connect(hn.get_mesh_port(), "to_hn");
        io_node.ports[DIR_COUNT + 1].connect(mem.mesh_port, "to_mem");
        io_node.ports[DIR_COUNT + 2].connect(idt.b, "to_idt_b");

        io_node.config.bind(*this);
        cpu_node.config.bind(*this);
        hn.config.bind(*this);
        idt.config.bind(*this);

        idt.set_a_to_b_lrid(NodeId{0, 0, 2, 0}, 1);
        idt.set_b_to_a_lrid(NodeId{0, 0, 1, 0}, 0);

        core.announcer.bind(announcer);
    }
};

class TestHNWithIDTOneEntry : public TestHNWithIDT
{
protected:
    TestHNWithIDTOneEntry()
        : TestHNWithIDT{
              {"~cpu_node\\.req_p[0-9]+\\.size", 8},
              {"~cpu_node\\.snp_p[0-9]+\\.size", 8},
              {"~cpu_node\\.rsp[01]_p[0-9]+\\.size", 8},
              {"~cpu_node\\.dat[01]_p[0-9]+\\.size", 8},
              {"~io_node\\.req_p[0-9]+\\.size", 8},
              {"~io_node\\.snp_p[0-9]+\\.size", 8},
              {"~io_node\\.rsp[01]_p[0-9]+\\.size", 8},
              {"~io_node\\.dat[01]_p[0-9]+\\.size", 8},
              {"idt.a_to_b_idt.rcbusy.requester_lut", "[-8, -1, 1, 8]"_json},
              {"idt.a_to_b_idt.rcbusy.throttle",
               "[256, 240, 224, 208, 192, 176, 160, 144, 128, 112, 96, 80, 64, 48, 32, 16]"_json},
              {"idt.a_to_b_idt.cbusy.c_threshold_high", 240},
              {"idt.a_to_b_idt.cbusy.c_threshold_mid", 192},
              {"idt.a_to_b_idt.cbusy.c_threshold_low", 128},
              {"idt.b_to_a_idt.rcbusy.requester_lut", "[-8, -1, 1, 8]"_json},
              {"idt.b_to_a_idt.rcbusy.throttle",
               "[256, 240, 224, 208, 192, 176, 160, 144, 128, 112, 96, 80, 64, 48, 32, 16]"_json},
              {"idt.b_to_a_idt.cbusy.c_threshold_high", 240},
              {"idt.b_to_a_idt.cbusy.c_threshold_mid", 192},
              {"idt.b_to_a_idt.cbusy.c_threshold_low", 128},
              {"idt.rdi.a_in_req.fifo.size", 4},
              {"idt.rdi.a_in_rsp0.fifo.size", 4},
              {"idt.rdi.a_in_rsp1.fifo.size", 4},
              {"idt.rdi.a_in_snp.fifo.size", 4},
              {"idt.rdi.a_in_dat.fifo.size", 4},
              {"idt.rdi.b_in_req.fifo.size", 4},
              {"idt.rdi.b_in_rsp0.fifo.size", 4},
              {"idt.rdi.b_in_rsp1.fifo.size", 4},
              {"idt.rdi.b_in_snp.fifo.size", 4},
              {"idt.rdi.b_in_dat.fifo.size", 4},
              {"idt.rdi.packer.rules", R"([
                  {"Req": 1, "Crd": 1, "Links": [2]},
                  {"Rsp": 2, "Crd": 2, "Links": [1]},
                  {"Dat": 1, "Links": [0]}
                ])"_json},
              {"idt.a_to_b_idt.rdi_in_req.size", 8},
              {"idt.a_to_b_idt.rdi_in_rsp.size", 8},
              {"idt.a_to_b_idt.rdi_in_dat.size", 8},
              {"idt.a_to_b_idt.idt_in_req.size", 8},
              {"idt.a_to_b_idt.idt_in_rsp.size", 8},
              {"idt.a_to_b_idt.idt_in_dat.size", 8},
              {"idt.b_to_a_idt.rdi_in_req.size", 8},
              {"idt.b_to_a_idt.rdi_in_rsp.size", 8},
              {"idt.b_to_a_idt.rdi_in_dat.size", 8},
              {"idt.b_to_a_idt.idt_in_req.size", 8},
              {"idt.b_to_a_idt.idt_in_rsp.size", 8},
              {"idt.b_to_a_idt.idt_in_dat.size", 8},
              {"idt.rdi.delay", "1ns"},
              {"idt.a_to_b_idt.rtrk_size", 1},
              {"idt.b_to_a_idt.rtrk_size", 1},
              {"hn.node_id", NodeId{NodeId::DieType::IO, 0, 1, 1}},
              {"hn.sam.tgt_lut", "[[0, 0, 1, 0]]"_json},
              {"hn.sam.system_address_map.connect", "system_address_map"},
              {"cpu_node.local_lut",
               std::vector<LocalRoutingId>{
                   LocalRoutingId{0, 0, 0, 0} // Core agent id = 0
               }},
              {"cpu_node.die_to_die_lrt",
               "[{\"die_id\": 1, \"entries\": [[0,0,1,0], [0,0,1,0], [0,0,1,0], [0,0,1,0]]}]"_json},
              {"io_node.local_lut",
               std::vector<LocalRoutingId>{
                   LocalRoutingId{0, 0, 0, 0},
                   LocalRoutingId{0, 0, 0, 0}, // Homenode agent id = 1
                   LocalRoutingId{0, 0, 1, 0}  // Memory agent id = 2
               }},
              {"io_node.die_to_die_lrt",
               "[{\"die_id\": 0, \"entries\": [[0,0,2,0], [0,0,2,0], [0,0,2,0], [0,0,2,0]]}]"_json},
              {"~to_hn_.*_rx_[01]\\.capacity", 8},
              {"~to_mem_.*_rx_[01]\\.capacity", 8},
              {"~to_core_.*_rx_[01]\\.capacity", 8},
          }
    {
    }
};

TEST_F(TestHNWithIDT, ReadNotSharedDirty)
{
    core.send_request(NodeId{NodeId::DieType::IO, 0, 1, 1}, 4, RequestOpcode::ReadNotSharedDirty, 0xaced0000_pa);

    auto send_comp_ack = [this](ResponseOpcode opcode, uint16_t txn, uint16_t dbid) {
        core.send_response(NodeId{NodeId::DieType::IO, 0, 1, 1}, dbid, ResponseOpcode::CompAck);
    };

    EXPECT_CALL(announcer, response(ResponseOpcode::RespSepData, 4, _)).WillOnce(Invoke(send_comp_ack));
    EXPECT_CALL(announcer, data(DataOpcode::DataSepResp, 4, _));
    run_for(100_ns);
}

TEST_F(TestHNWithIDT, ReadUnique)
{
    core.send_request(NodeId{NodeId::DieType::IO, 0, 1, 1}, 0, RequestOpcode::ReadUnique, 0xaced0000_pa);

    auto send_comp_ack = [this](ResponseOpcode opcode, uint16_t txn, uint16_t dbid) {
        core.send_response(NodeId{NodeId::DieType::IO, 0, 1, 1}, dbid, ResponseOpcode::CompAck);
    };

    EXPECT_CALL(announcer, response(ResponseOpcode::RespSepData, 0, _)).WillOnce(Invoke(send_comp_ack));
    EXPECT_CALL(announcer, data(DataOpcode::DataSepResp, 0, _));
    run_for(100_ns);
}

TEST_F(TestHNWithIDT, WriteBackFull)
{
    core.send_request(NodeId{NodeId::DieType::IO, 0, 1, 1}, 4, RequestOpcode::ReadUnique, 0xaced0000_pa);

    auto send_comp_ack = [this](DataOpcode opcode, uint16_t txn, uint16_t dbid) {
        core.send_response(NodeId{NodeId::DieType::IO, 0, 1, 1}, dbid, ResponseOpcode::CompAck);
    };

    auto send_write_back = [this](DataOpcode opcode, uint16_t txn, uint16_t dbid) {
        core.send_request(NodeId{NodeId::DieType::IO, 0, 1, 1}, 21, RequestOpcode::WriteBackFull, 0xaced0000_pa);
    };

    auto respond_with_data = [this](ResponseOpcode opcode, uint16_t txn, uint16_t dbid) {
        core.send_data(NodeId{NodeId::DieType::IO, 0, 1, 1}, 21, DataOpcode::CopyBackWrData, dbid, RespWriteData_UD_PD);
    };

    EXPECT_CALL(announcer, response(ResponseOpcode::RespSepData, 4, _));

    EXPECT_CALL(announcer, data(DataOpcode::DataSepResp, 4, _))
        .WillOnce(DoAll(Invoke(send_comp_ack), Invoke(send_write_back)));

    EXPECT_CALL(announcer, response(ResponseOpcode::CompDBIDResp, 21, _)).WillOnce(Invoke(respond_with_data));

    run_for(100_ns);
}

TEST_F(TestHNWithIDT, WriteEvictFull)
{
    core.send_request(NodeId{NodeId::DieType::IO, 0, 1, 1}, 4, RequestOpcode::ReadUnique, 0xaced0000_pa);

    auto send_comp_ack = [this](DataOpcode opcode, uint16_t txn, uint16_t dbid) {
        core.send_response(NodeId{NodeId::DieType::IO, 0, 1, 1}, dbid, ResponseOpcode::CompAck);
    };

    auto send_write_evict = [this](DataOpcode opcode, uint16_t txn, uint16_t dbid) {
        core.send_request(NodeId{NodeId::DieType::IO, 0, 1, 1}, 10, RequestOpcode::WriteEvictFull, 0xaced0000_pa);
    };

    auto respond_with_data = [this](ResponseOpcode opcode, uint16_t txn, uint16_t dbid) {
        core.send_data(NodeId{NodeId::DieType::IO, 0, 1, 1}, 10, DataOpcode::CopyBackWrData, dbid, RespWriteData_UC);
    };

    EXPECT_CALL(announcer, response(ResponseOpcode::RespSepData, 4, _));

    EXPECT_CALL(announcer, data(DataOpcode::DataSepResp, 4, _))
        .WillOnce(DoAll(Invoke(send_comp_ack), Invoke(send_write_evict)));

    EXPECT_CALL(announcer, response(ResponseOpcode::CompDBIDResp, 10, _)).WillOnce(Invoke(respond_with_data));

    run_for(100_ns);

    EXPECT_TRUE(mem.has_write(0xaced0000_pa));
}

TEST_F(TestHNWithIDTOneEntry, Retry)
{
    // set rtrk_size as 1 TODO
    core.send_request(NodeId{NodeId::DieType::IO, 0, 1, 1}, 4, RequestOpcode::ReadNotSharedDirty, 0xaced0000_pa);
    core.send_request(NodeId{NodeId::DieType::IO, 0, 1, 1}, 8, RequestOpcode::ReadNotSharedDirty, 0xaded0000_pa);

    auto send_req = [this](ResponseOpcode opcode, uint16_t txn, uint16_t dbid) {
        core.send_request_retry(
            NodeId{NodeId::DieType::IO, 0, 1, 1}, 8, RequestOpcode::ReadNotSharedDirty, 0xaded0000_pa);
    };

    auto send_comp_ack_1 = [this](ResponseOpcode opcode, uint16_t txn, uint16_t dbid) {
        core.send_response(NodeId{NodeId::DieType::IO, 0, 1, 1}, dbid, ResponseOpcode::CompAck);
    };

    auto send_comp_ack_2 = [this](ResponseOpcode opcode, uint16_t txn, uint16_t dbid) {
        core.send_response(NodeId{NodeId::DieType::IO, 0, 1, 1}, dbid, ResponseOpcode::CompAck);
    };

    EXPECT_CALL(announcer, response(ResponseOpcode::RetryAck, 8, _));

    EXPECT_CALL(announcer, response(ResponseOpcode::RespSepData, 4, _)).WillOnce(Invoke(send_comp_ack_1));
    EXPECT_CALL(announcer, data(DataOpcode::DataSepResp, 4, _));

    EXPECT_CALL(announcer, response(ResponseOpcode::PCrdGrant, 8, _)).WillOnce(Invoke(send_req));

    EXPECT_CALL(announcer, response(ResponseOpcode::RespSepData, 8, _)).WillOnce(Invoke(send_comp_ack_2));
    EXPECT_CALL(announcer, data(DataOpcode::DataSepResp, 8, _));
    run_for(100_ns);
}
