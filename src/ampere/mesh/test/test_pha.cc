//
// Copyright (c) 2022 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//
#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "ampere/mesh/cpx.h"
#include "ampere/mesh/mesh_node.h"
#include "ampere/mesh/pha.h"
#include "ampere/mesh/test/dummy_memory.h"
#include "ampere/mesh/test/dummy_requester.h"
#include "ampere/mesh/test/mock_addr_map.h"
#include "ampere/mesh/test/mock_mesh_port.h"
#include "ampere/sc/testing/test_interfaces.h"
#include "ampere/sc/testing/test_module.h"

using namespace ampere::mesh;
using namespace ampere::archx;

class TestPHAWithMemorySmallTracker : public ampere::sc::TestModule, public MeshConfigIfc
{
protected:
    TestPHAWithMemorySmallTracker()
        : sc::TestModule{{"~node\\.req_p[0-9]+\\.size", 8},
                         {"~node\\.snp_p[0-9]+\\.size", 8},
                         {"~node\\.rsp[01]_p[0-9]+\\.size", 8},
                         {"~node\\.dat[01]_p[0-9]+\\.size", 8},
                         {"cpx.n", 1},
                         {"cpx.nrsp", 2},
                         {"~cpx\\.dev_[0-9]+\\.req_in\\.size", 8},
                         {"~cpx\\.dev_[0-9]+\\.snp_in\\.size", 8},
                         {"~cpx\\.dev_[0-9]+\\.rsp[01]_in\\.size", 8},
                         {"~cpx\\.dev_[0-9]+\\.dat[01]_in\\.size", 8},
                         {"~cpx\\.mesh_.*_in\\.size", 8},
                         {"pha_proxy.node_id", "{\"agent_id\": 3}"_json},
                         {"pha_proxy.sam.type", "PXHA_SAM"},
                         {"pha_proxy.sam.system_address_map.connect", "system_address_map"},
                         {"pha_proxy.rsam.system_address_map.connect", "system_address_map"},
                         {"pha_proxy.prefetch_tgt_sam.system_address_map.connect", "system_address_map"},
                         {"pha_proxy.sam.pmha_tgt", 4},
                         {"pha_proxy.cbusy.throttle_start", 96},
                         {"pha_primary.node_id", "{\"agent_id\": 4}"_json},
                         {"pha_primary.sam.type", "PMHA_SAM"},
                         {"pha_primary.sam.system_address_map.connect", "system_address_map"},
                         {"pha_primary.rsam.system_address_map.connect", "system_address_map"},
                         {"pha_primary.sam.mcu_tgt", 2},
                         {"pha_primary.cbusy.throttle_start", 96},
                         {"~pha_pr.*\\.num_snp_entries", 1},
                         {"~pha_pr.*\\.num_sfv_entries", 1},
                         {"~pha_pr.*\\.num_platinum_entries", 0},
                         {"~pha_pr.*\\.num_gold_entries", 0},
                         {"~pha_pr.*\\.num_iron_entries", 4},
                         {"~pha_pr.*\\.snoop_filter\\.sets", 1},
                         {"~pha_pr.*\\.snoop_filter\\.ways", 4},
                         {"node.local_lut",
                          std::map<uint8_t, LocalRoutingId>{{0, LocalRoutingId{0, 0, 0, 0}},
                                                            {1, LocalRoutingId{0, 0, 4, 0}},
                                                            {2, LocalRoutingId{0, 0, 3, 0}},
                                                            {3, LocalRoutingId{0, 0, 1, 0}},
                                                            {4, LocalRoutingId{0, 0, 2, 0}}}},
                         {"~core_cpx_.*_tx_[01]\\.capacity", 8},
                         {"~pxha_mp_.*_rx_[01]\\.capacity", 8},
                         {"~pmha_mp_.*_rx_[01]\\.capacity", 8},
                         {"~mem_mp_.*_rx_[01]\\.capacity", 8},
                         {"~stash_mp_.*_rx_[01]\\.capacity", 8}}
    {
        node.ports[DIR_COUNT + 0].connect(cpx.mesh_port, "cpx_mp");
        core.mesh_port.connect(cpx.device_port[0], "core_cpx");
        node.ports[DIR_COUNT + 1].connect(pha_proxy.get_mesh_port(), "pxha_mp");
        node.ports[DIR_COUNT + 2].connect(pha_primary.get_mesh_port(), "pmha_mp");
        node.ports[DIR_COUNT + 3].connect(mem.mesh_port, "mem_mp");
        node.ports[DIR_COUNT + 4].connect(stash_core.mesh_port, "stash_mp");

        node.config.bind(*this);
        cpx.config.bind(*this);
        pha_proxy.config.bind(*this);
        pha_primary.config.bind(*this);

        core.announcer.bind(announcer);
        stash_core.announcer.bind(stash_announcer);
    }

    NodeId core_nid{NodeId::DieType::CPU, 0, 0, 0};
    NodeId stash_nid{NodeId::DieType::CPU, 0, 0, 1};
    NodeId mem_nid{NodeId::DieType::CPU, 0, 0, 2};
    NodeId pxha_nid{NodeId::DieType::CPU, 0, 0, 3};
    NodeId pmha_nid{NodeId::DieType::CPU, 0, 0, 4};

    MeshNode node{"node", 0, 0, 5, 0, 0};
    CPX cpx{"cpx"};
    DummyRequester core{"core", core_nid};              // Position [0,0,0,0]
    PxHA pha_proxy{"pha_proxy"};                        // Position [0,0,1,0]
    PmHA pha_primary{"pha_primary"};                    // Position [0,0,2,0]
    DummyMemory mem{"mem", mem_nid};                    // Position [0,0,3,0]
    DummyRequester stash_core{"stash_core", stash_nid}; // Position [0,0,4,0]
    MockRequesterAnnouncer announcer;
    MockRequesterAnnouncer stash_announcer;
    MockSystemAddressMap system_address_map{"system_address_map"};

    int data_bus_width() const override { return 512; }
    int channel(const chi::NodeId id) const override { return 0; }
    std::vector<chi::NodeId> stash_nodes(uint8_t die_id) const override
    {
        return {NodeId{NodeId::DieType::CPU, 0, 0, 1}};
    }
};

class TestPHASmallPmhaSf : public ampere::sc::TestModule, public MeshConfigIfc
{
protected:
    TestPHASmallPmhaSf()
        : sc::TestModule{{"~node\\.req_p[0-9]+\\.size", 8},
                         {"~node\\.snp_p[0-9]+\\.size", 8},
                         {"~node\\.rsp[01]_p[0-9]+\\.size", 8},
                         {"~node\\.dat[01]_p[0-9]+\\.size", 8},
                         {"pha_proxy.node_id", "{\"agent_id\": 3}"_json},
                         {"pha_proxy.sam.type", "PXHA_SAM"},
                         {"pha_proxy.sam.system_address_map.connect", "system_address_map"},
                         {"pha_proxy.rsam.system_address_map.connect", "system_address_map"},
                         {"pha_proxy.prefetch_tgt_sam.system_address_map.connect", "system_address_map"},
                         {"pha_proxy.sam.pmha_tgt", 4},
                         {"pha_proxy.cbusy.throttle_start", 96},
                         {"pha_primary.node_id", "{\"agent_id\": 4}"_json},
                         {"pha_primary.sam.type", "PMHA_SAM"},
                         {"pha_primary.sam.system_address_map.connect", "system_address_map"},
                         {"pha_primary.rsam.system_address_map.connect", "system_address_map"},
                         {"pha_primary.sam.mcu_tgt", 2},
                         {"pha_primary.cbusy.throttle_start", 96},
                         {"~pha_pr.*\\.num_snp_entries", 1},
                         {"~pha_pr.*\\.num_sfv_entries", 1},
                         {"~pha_pr.*\\.num_platinum_entries", 0},
                         {"~pha_pr.*\\.num_gold_entries", 0},
                         {"~pha_pr.*\\.num_iron_entries", 4},
                         {"~pha_pr.*\\.snoop_filter\\.sets", 1},
                         {"~pha_pr.*\\.snoop_filter\\.ways", 4},
                         {"~pha_primary\\.snoop_filter\\.ways", 1},
                         {"node.local_lut",
                          std::map<uint8_t, LocalRoutingId>{{0, LocalRoutingId{0, 0, 0, 0}},
                                                            {1, LocalRoutingId{0, 0, 4, 0}},
                                                            {2, LocalRoutingId{0, 0, 3, 0}},
                                                            {3, LocalRoutingId{0, 0, 1, 0}},
                                                            {4, LocalRoutingId{0, 0, 2, 0}}}},
                         {"~pxha_mp_.*_rx_[01]\\.capacity", 8},
                         {"~pmha_mp_.*_rx_[01]\\.capacity", 8},
                         {"~core_mp_.*_rx_[01]\\.capacity", 8},
                         {"~mem_mp_.*_rx_[01]\\.capacity", 8},
                         {"~stash_mp_.*_rx_[01]\\.capacity", 8}}
    {
        node.ports[DIR_COUNT + 0].connect(core.mesh_port, "core_mp");
        node.ports[DIR_COUNT + 1].connect(pha_proxy.get_mesh_port(), "pxha_mp");
        node.ports[DIR_COUNT + 2].connect(pha_primary.get_mesh_port(), "pmha_mp");
        node.ports[DIR_COUNT + 3].connect(mem.mesh_port, "mem_mp");
        node.ports[DIR_COUNT + 4].connect(stash_core.mesh_port, "stash_mp");

        node.config.bind(*this);
        pha_proxy.config.bind(*this);
        pha_primary.config.bind(*this);

        core.announcer.bind(announcer);
        stash_core.announcer.bind(stash_announcer);
    }

    NodeId core_nid{NodeId::DieType::CPU, 0, 0, 0};
    NodeId stash_nid{NodeId::DieType::CPU, 0, 0, 1};
    NodeId mem_nid{NodeId::DieType::CPU, 0, 0, 2};
    NodeId pxha_nid{NodeId::DieType::CPU, 0, 0, 3};
    NodeId pmha_nid{NodeId::DieType::CPU, 0, 0, 4};

    MeshNode node{"node", 0, 0, 5, 0, 0};
    DummyRequester core{"core", core_nid};              // Position [0,0,0,0]
    PxHA pha_proxy{"pha_proxy"};                        // Position [0,0,1,0]
    PmHA pha_primary{"pha_primary"};                    // Position [0,0,2,0]
    DummyMemory mem{"mem", mem_nid};                    // Position [0,0,3,0]
    DummyRequester stash_core{"stash_core", stash_nid}; // Position [0,0,4,0]
    MockRequesterAnnouncer announcer;
    MockRequesterAnnouncer stash_announcer;
    MockSystemAddressMap system_address_map{"system_address_map"};

    int data_bus_width() const override { return 512; }
    int channel(const chi::NodeId id) const override { return 0; }
    std::vector<chi::NodeId> stash_nodes(uint8_t die_id) const override
    {
        return {NodeId{NodeId::DieType::CPU, 0, 0, 1}};
    }
};

class TestIORWithMemory : public ampere::sc::TestModule, public MeshConfigIfc
{
protected:
    TestIORWithMemory()
        : sc::TestModule{
              {"~node\\.req_p[0-9]+\\.size", 8},
              {"~node\\.snp_p[0-9]+\\.size", 8},
              {"~node\\.rsp[01]_p[0-9]+\\.size", 8},
              {"~node\\.dat[01]_p[0-9]+\\.size", 8},
              {"pha_primary.node_id", "{\"agent_id\": 1}"_json},
              {"pha_primary.sam.type", "PMHA_SAM"},
              {"pha_primary.sam.system_address_map.connect", "system_address_map"},
              {"pha_primary.rsam.system_address_map.connect", "system_address_map"},
              {"pha_primary.sam.mcu_tgt", 2},
              {"pha_primary.cbusy.throttle_start", 96},
              {"node.local_lut",
               std::map<uint8_t, LocalRoutingId>{
                   {0, LocalRoutingId{0, 0, 0, 0}}, {1, LocalRoutingId{0, 0, 1, 0}}, {2, LocalRoutingId{0, 0, 2, 0}}}},
              {"~pmha_mp_.*_rx_[01]\\.capacity", 8},
              {"~core_mp_.*_rx_[01]\\.capacity", 8},
              {"~mem_mp_.*_rx_[01]\\.capacity", 8}}
    {
        node.ports[DIR_COUNT + 0].connect(core.mesh_port, "core_mp");
        node.ports[DIR_COUNT + 1].connect(pha_primary.get_mesh_port(), "pmha_mp");
        node.ports[DIR_COUNT + 2].connect(mem.mesh_port, "mem_mp");

        node.config.bind(*this);
        pha_primary.config.bind(*this);

        core.announcer.bind(announcer);
    }

    NodeId ior_nid{NodeId::DieType::CPU, 0, 0, 0};
    NodeId pmha_nid{NodeId::DieType::CPU, 0, 0, 1};
    NodeId mem_nid{NodeId::DieType::CPU, 0, 0, 2};

    DummyRequester core{"core", ior_nid}; // Position [0,0,0,0]: AgentId: 0
    PmHA pha_primary{"pha_primary"};      // Position [0,0,1,0], AgentId: 1
    DummyMemory mem{"mem", mem_nid};      // Position [0,0,2,0], AgentId: 2
    MeshNode node{"node", 0, 0, 3, 0, 0};
    MockRequesterAnnouncer announcer;
    MockSystemAddressMap system_address_map{"system_address_map"};

    int data_bus_width() const override { return 512; }
    int channel(const chi::NodeId id) const override { return 0; }
};

class TestPHAWithMemory : public ampere::sc::TestModule, public MeshConfigIfc
{
protected:
    TestPHAWithMemory()
        : sc::TestModule{{"~node\\.req_p[0-9]+\\.size", 8},
                         {"~node\\.snp_p[0-9]+\\.size", 8},
                         {"~node\\.rsp[01]_p[0-9]+\\.size", 8},
                         {"~node\\.dat[01]_p[0-9]+\\.size", 8},
                         {"pha_proxy.node_id", "{\"agent_id\": 3}"_json},
                         {"pha_proxy.sam.type", "PXHA_SAM"},
                         {"pha_proxy.sam.system_address_map.connect", "system_address_map"},
                         {"pha_proxy.rsam.system_address_map.connect", "system_address_map"},
                         {"pha_proxy.prefetch_tgt_sam.system_address_map.connect", "system_address_map"},
                         {"pha_proxy.sam.pmha_tgt", 4},
                         {"pha_proxy.cbusy.throttle_start", 96},
                         {"pha_primary.node_id", "{\"agent_id\": 4}"_json},
                         {"pha_primary.sam.type", "PMHA_SAM"},
                         {"pha_primary.sam.system_address_map.connect", "system_address_map"},
                         {"pha_primary.rsam.system_address_map.connect", "system_address_map"},
                         {"pha_primary.sam.mcu_tgt", 2},
                         {"pha_primary.cbusy.throttle_start", 96},
                         {"node.local_lut",
                          std::map<uint8_t, LocalRoutingId>{{0, LocalRoutingId{0, 0, 0, 0}},
                                                            {1, LocalRoutingId{0, 0, 4, 0}},
                                                            {2, LocalRoutingId{0, 0, 3, 0}},
                                                            {3, LocalRoutingId{0, 0, 1, 0}},
                                                            {4, LocalRoutingId{0, 0, 2, 0}}}},
                         {"~pmha_mp_.*_rx_[01]\\.capacity", 8},
                         {"~pxha_mp_.*_rx_[01]\\.capacity", 8},
                         {"~core_mp_.*_rx_[01]\\.capacity", 8},
                         {"~mem_mp_.*_rx_[01]\\.capacity", 8},
                         {"~stash_mp_.*_rx_[01]\\.capacity", 8}}
    {
        node.ports[DIR_COUNT + 0].connect(core.mesh_port, "core_mp");
        node.ports[DIR_COUNT + 1].connect(pha_proxy.get_mesh_port(), "pxha_mp");
        node.ports[DIR_COUNT + 2].connect(pha_primary.get_mesh_port(), "pmha_mp");
        node.ports[DIR_COUNT + 3].connect(mem.mesh_port, "mem_mp");
        node.ports[DIR_COUNT + 4].connect(stash_core.mesh_port, "stash_mp");

        node.config.bind(*this);
        pha_proxy.config.bind(*this);
        pha_primary.config.bind(*this);

        core.announcer.bind(announcer);
        stash_core.announcer.bind(stash_announcer);
    }

    NodeId core_nid{NodeId::DieType::CPU, 0, 0, 0};
    NodeId stash_nid{NodeId::DieType::CPU, 0, 0, 1};
    NodeId mem_nid{NodeId::DieType::CPU, 0, 0, 2};
    NodeId pxha_nid{NodeId::DieType::CPU, 0, 0, 3};
    NodeId pmha_nid{NodeId::DieType::CPU, 0, 0, 4};

    MeshNode node{"node", 0, 0, 5, 0, 0};
    DummyRequester core{"core", core_nid};              // Position [0,0,0,0]: AgentId: 0
    PxHA pha_proxy{"pha_proxy"};                        // Position [0,0,1,0], AgentId: 3
    PmHA pha_primary{"pha_primary"};                    // Position [0,0,2,0], AgentId: 4
    DummyMemory mem{"mem", mem_nid};                    // Position [0,0,3,0], AgentId: 2
    DummyRequester stash_core{"stash_core", stash_nid}; // Position [0,0,4,0], AgentId: 1
    MockRequesterAnnouncer announcer;
    MockRequesterAnnouncer stash_announcer;
    MockSystemAddressMap system_address_map{"system_address_map"};

    int data_bus_width() const override { return 512; }
    int channel(const chi::NodeId id) const override { return 0; }
    std::vector<chi::NodeId> stash_nodes(uint8_t die_id) const override
    {
        return {NodeId{NodeId::DieType::CPU, 0, 0, 1}};
    }
};

TEST_F(TestIORWithMemory, ReadOnceWithOrder)
{
    core.send_request(pmha_nid, 4, RequestOpcode::ReadOnce, 0xaced0000_pa, RequestOrder::RequestOrder);

    auto send_comp_ack = [this](DataOpcode opcode, uint16_t txn, uint16_t dbid) {
        core.send_response(pmha_nid, dbid, ResponseOpcode::CompAck);
    };

    EXPECT_CALL(announcer, response(ResponseOpcode::ReadReceipt, 4, _));
    EXPECT_CALL(announcer, data(DataOpcode::CompData, 4, _)).WillOnce(Invoke(send_comp_ack));
    tick(100);
}

TEST_F(TestIORWithMemory, WriteUniquePtl)
{
    core.send_request(pmha_nid, 4, RequestOpcode::WriteUniquePtl, 0xaced0000_pa, RequestOrder::RequestOrder);

    auto send_data_comp_ack = [this](ResponseOpcode opcode, uint16_t txn, uint16_t dbid) {
        auto dat = make<MeshDat>(ior_nid, pmha_nid, dbid, DataOpcode::NonCopyBackWrData);
        dat->Resp = RespWriteData_UD_PD;
        core.send_data(dat);
        tick(100);
        core.send_response(pmha_nid, dbid, ResponseOpcode::CompAck);
    };

    EXPECT_CALL(announcer, response(ResponseOpcode::CompDBIDResp, 4, _)).WillOnce(Invoke(send_data_comp_ack));
    tick(100);
}

TEST_F(TestPHAWithMemory, ReadUnique)
{
    core.send_request(pxha_nid, 4, RequestOpcode::ReadUnique, 0xaced0000_pa);

    auto send_comp_ack = [this](ResponseOpcode opcode, uint16_t txn, uint16_t dbid) {
        core.send_response(pxha_nid, dbid, ResponseOpcode::CompAck);
    };

    EXPECT_CALL(announcer, response(ResponseOpcode::RespSepData, 4, _)).WillOnce(Invoke(send_comp_ack));
    EXPECT_CALL(announcer, data(DataOpcode::DataSepResp, 4, _));
    tick(100);
}

TEST_F(TestPHAWithMemory, ReadUniqueWithQoS)
{
    mem.save_requests();
    auto req = make<MeshReq>(core_nid, pxha_nid, 4, RequestOpcode::ReadUnique, 0xaced0000_pa);
    req->QoS = chi::Qos::Level::PLATINUM;
    core.send_request(req);

    auto send_comp_ack = [this](ResponseOpcode opcode, uint16_t txn, uint16_t dbid) {
        core.send_response(pxha_nid, dbid, ResponseOpcode::CompAck);
    };

    EXPECT_CALL(announcer, response(ResponseOpcode::RespSepData, 4, _)).WillOnce(Invoke(send_comp_ack));
    EXPECT_CALL(announcer, data(DataOpcode::DataSepResp, 4, _));
    tick(100);

    ASSERT_EQ(mem.get_saved_requests().size(), 1);
    EXPECT_TRUE(mem.get_saved_requests().at(0)->QoS.is_platinum());
}

TEST_F(TestPHAWithMemory, ReadNotSharedDirty)
{
    core.send_request(pxha_nid, 7, RequestOpcode::ReadNotSharedDirty, 0xaced0000_pa);

    auto send_comp_ack = [this](ResponseOpcode opcode, uint16_t txn, uint16_t dbid) {
        core.send_response(pxha_nid, dbid, ResponseOpcode::CompAck);
    };

    EXPECT_CALL(announcer, response(ResponseOpcode::RespSepData, 7, _)).WillOnce(Invoke(send_comp_ack));
    EXPECT_CALL(announcer, data(DataOpcode::DataSepResp, 7, _));
    tick(100);

    auto pxha_snoop_status = pha_proxy.get_snoop_status(0xaced0000_pa);
    ASSERT_TRUE(pxha_snoop_status);
    ASSERT_EQ((*pxha_snoop_status)->count(), 0u);

    auto containing_core_id = (*pxha_snoop_status)->primary_snoop_target();
    ASSERT_TRUE(containing_core_id);
    EXPECT_EQ(*containing_core_id, core_nid);

    auto pmha_snoop_status = pha_primary.get_snoop_status(0xaced0000_pa);
    ASSERT_TRUE(pmha_snoop_status);
    ASSERT_EQ((*pmha_snoop_status)->count(), 0u);

    auto containing_proxy_id = (*pmha_snoop_status)->primary_snoop_target();
    ASSERT_TRUE(containing_proxy_id);
    EXPECT_EQ(*containing_proxy_id, pxha_nid);
}

TEST_F(TestPHASmallPmhaSf, CleanInvalid)
{
    core.send_request(pxha_nid, 7, RequestOpcode::ReadNotSharedDirty, 0xaced0000_pa);
    core.send_request(pxha_nid, 8, RequestOpcode::ReadNotSharedDirty, 0xacee0000_pa);

    auto send_comp_ack = [this](ResponseOpcode opcode, uint16_t txn, uint16_t dbid) {
        core.send_response(pxha_nid, dbid, ResponseOpcode::CompAck);
    };

    auto send_snp_resp = [this](SnoopOpcode opcode, uint16_t txn) {
        core.send_response(pxha_nid, txn, ResponseOpcode::SnpResp, 0, RespSnoop_I);
    };

    EXPECT_CALL(announcer, response(ResponseOpcode::RespSepData, 7, _)).WillOnce(Invoke(send_comp_ack));
    EXPECT_CALL(announcer, data(DataOpcode::DataSepResp, 7, _));
    EXPECT_CALL(announcer, snoop(SnoopOpcode::SnpCleanInvalid, _)).WillOnce(Invoke(send_snp_resp));

    EXPECT_CALL(announcer, response(ResponseOpcode::RespSepData, 8, _)).WillOnce(Invoke(send_comp_ack));
    EXPECT_CALL(announcer, data(DataOpcode::DataSepResp, 8, _));
    tick(100);

    auto pxha_snoop_status = pha_proxy.get_snoop_status(0xaced0000_pa);
    ASSERT_FALSE(pxha_snoop_status);

    auto pmha_snoop_status = pha_primary.get_snoop_status(0xaced0000_pa);
    ASSERT_FALSE(pmha_snoop_status);
}

TEST_F(TestPHAWithMemory, WriteBackFullToL3)
{
    /* Stolen from ReadUnique test
     * Pull in Line to Core L2 to allow a snoop filter hit */
    core.send_request(pxha_nid, 4, RequestOpcode::ReadUnique, 0xaced0000_pa);

    auto send_comp_ack = [this](ResponseOpcode opcode, uint16_t txn, uint16_t dbid) {
        core.send_response(pxha_nid, dbid, ResponseOpcode::CompAck);
    };
    EXPECT_CALL(announcer, response(ResponseOpcode::RespSepData, 4, _)).WillOnce(Invoke(send_comp_ack));
    EXPECT_CALL(announcer, data(DataOpcode::DataSepResp, 4, _));

    /* WriteBackFull Flow */
    core.send_request(pxha_nid, 13, RequestOpcode::WriteBackFull, 0xaced0000_pa);

    auto send_snoop_response = [this](SnoopOpcode opcode, uint16_t txn) {
        auto rsp = make<MeshRsp>(stash_nid, pxha_nid, txn, ResponseOpcode::SnpResp);
        rsp->Resp = RespSnoop_I; // I don't really get why this stash response is I, but that agrees with what the PHA
                                 // FSM expects
        rsp->DatPull = DataPull::Read;
        rsp->DBID = 14;
        stash_core.send_response(rsp);
    };
    auto send_comp_ack2 = [this](ResponseOpcode opcode, uint16_t txn, uint16_t dbid) {
        core.send_response(pxha_nid, 1, ResponseOpcode::CompAck);
    };

    EXPECT_CALL(stash_announcer, snoop(SnoopOpcode::SnpStashShared, _)).WillOnce(Invoke(send_snoop_response));
    EXPECT_CALL(announcer, response(ResponseOpcode::CompDBIDResp, 13, 14)).WillOnce(Invoke(send_comp_ack2));
    tick(100);

    auto snoop_status = pha_proxy.get_snoop_status(0xaced0000_pa);
    ASSERT_TRUE(snoop_status);
    ASSERT_EQ((*snoop_status)->count(), 0u);

    auto containing_core_id = (*snoop_status)->primary_snoop_target();
    ASSERT_TRUE(containing_core_id);
    EXPECT_EQ(*containing_core_id, stash_nid);
}

TEST_F(TestPHAWithMemory, WriteBackFullToMemory)
{
    /* Stolen from ReadUnique test
     * Pull in Line to Core L2 to allow a snoop filter hit */
    core.send_request(pxha_nid, 4, RequestOpcode::ReadUnique, 0xaced0000_pa);

    auto send_comp_ack = [this](ResponseOpcode opcode, uint16_t txn, uint16_t dbid) {
        core.send_response(pxha_nid, dbid, ResponseOpcode::CompAck);
    };
    EXPECT_CALL(announcer, response(ResponseOpcode::RespSepData, 4, _))
        .WillOnce(Invoke(send_comp_ack))
        .RetiresOnSaturation();
    EXPECT_CALL(announcer, data(DataOpcode::DataSepResp, 4, _));

    /* WriteBackFull Flow */
    auto req = make<MeshReq>(core_nid, pxha_nid, 13, RequestOpcode::WriteBackFull, 0xaced0000_pa);
    req->RSVDC = chi::ReserveCustomerUseReq(chi::RsvdcReq::CopyBackL2L3);
    core.send_request(req);

    auto send_copyback = [this](ResponseOpcode opcode, uint16_t txn, uint16_t dbid) {
        auto dat = make<MeshDat>(core_nid, pxha_nid, dbid, DataOpcode::CopyBackWrData);
        dat->Resp = RespWriteData_UD_PD;
        core.send_data(dat);
    };

    EXPECT_CALL(announcer, response(ResponseOpcode::CompDBIDResp, 13, _)).WillOnce(Invoke(send_copyback));
    tick(100);

    bool memory_write = mem.has_write(0xaced0000_pa);
    EXPECT_TRUE(memory_write);
}

TEST_F(TestPHAWithMemory, WriteEvictOrEvict)
{
    /* Stolen from ReadUnique test
     * Pull in Line to Core L2 to allow a snoop filter hit */
    core.send_request(pxha_nid, 4, RequestOpcode::ReadUnique, 0xaced0000_pa);

    auto send_comp_ack = [this](ResponseOpcode opcode, uint16_t txn, uint16_t dbid) {
        core.send_response(pxha_nid, dbid, ResponseOpcode::CompAck);
    };
    EXPECT_CALL(announcer, response(ResponseOpcode::RespSepData, 4, _)).WillOnce(Invoke(send_comp_ack));
    EXPECT_CALL(announcer, data(DataOpcode::DataSepResp, 4, _));

    /* WriteEvictOrEvict Flow */
    core.send_request(pxha_nid, 13, RequestOpcode::WriteEvictOrEvict, 0xaced0000_pa);

    auto send_snoop_response = [this](SnoopOpcode opcode, uint16_t txn) {
        auto rsp = make<MeshRsp>(stash_nid, pxha_nid, txn, ResponseOpcode::SnpResp);
        rsp->Resp = RespSnoop_I;
        rsp->DatPull = DataPull::Read;
        rsp->DBID = 14;
        stash_core.send_response(rsp);
    };
    auto send_comp_ack2 = [this](ResponseOpcode opcode, uint16_t txn, uint16_t dbid) {
        core.send_response(pxha_nid, 1, ResponseOpcode::CompAck);
    };

    EXPECT_CALL(stash_announcer, snoop(SnoopOpcode::SnpStashShared, _)).WillOnce(Invoke(send_snoop_response));
    EXPECT_CALL(announcer, response(ResponseOpcode::CompDBIDResp, 13, 14)).WillOnce(Invoke(send_comp_ack2));
    tick(1000);

    auto snoop_status = pha_proxy.get_snoop_status(0xaced0000_pa);
    ASSERT_TRUE(snoop_status);
    ASSERT_EQ((*snoop_status)->count(), 0u);

    auto containing_core_id = (*snoop_status)->primary_snoop_target();
    ASSERT_TRUE(containing_core_id);
    EXPECT_EQ(*containing_core_id, stash_nid);
}

TEST_F(TestPHAWithMemory, WriteEvictOrEvictStashReject)
{
    /* Stolen from ReadUnique test
     * Pull in Line to Core L2 to allow a snoop filter hit */
    core.send_request(pxha_nid, 4, RequestOpcode::ReadUnique, 0xaced0000_pa);

    auto send_comp_ack = [this](ResponseOpcode opcode, uint16_t txn, uint16_t dbid) {
        core.send_response(pxha_nid, dbid, ResponseOpcode::CompAck);
    };
    EXPECT_CALL(announcer, response(ResponseOpcode::RespSepData, 4, _)).WillOnce(Invoke(send_comp_ack));
    EXPECT_CALL(announcer, data(DataOpcode::DataSepResp, 4, _));

    /* WriteEvictOrEvict Flow */
    core.send_request(pxha_nid, 13, RequestOpcode::WriteEvictOrEvict, 0xaced0000_pa);

    auto send_snoop_response = [this](SnoopOpcode opcode, uint16_t txn) {
        auto rsp = make<MeshRsp>(stash_nid, pxha_nid, txn, ResponseOpcode::SnpResp);
        rsp->Resp = RespSnoop_I;
        rsp->DatPull = DataPull::NoRead;
        stash_core.send_response(rsp);
    };
    auto send_comp_ack2 = [this](ResponseOpcode opcode, uint16_t txn, uint16_t dbid) {
        core.send_response(pxha_nid, dbid, ResponseOpcode::CompAck);
    };

    EXPECT_CALL(stash_announcer, snoop(SnoopOpcode::SnpStashShared, _)).WillOnce(Invoke(send_snoop_response));
    EXPECT_CALL(announcer, response(ResponseOpcode::Comp, 13, _)).WillOnce(Invoke(send_comp_ack2));
    tick(1000);

    auto snoop_status = pha_proxy.get_snoop_status(0xaced0000_pa);
    ASSERT_FALSE(snoop_status);
}

TEST_F(TestPHAWithMemory, WriteEvictFull)
{
    /* Stolen from ReadUnique test
     * Pull in Line to Core L2 to allow a snoop filter hit */
    core.send_request(pxha_nid, 4, RequestOpcode::ReadUnique, 0xaced0000_pa);

    auto send_comp_ack = [this](ResponseOpcode opcode, uint16_t txn, uint16_t dbid) {
        core.send_response(pxha_nid, dbid, ResponseOpcode::CompAck);
    };
    EXPECT_CALL(announcer, response(ResponseOpcode::RespSepData, 4, _)).WillOnce(Invoke(send_comp_ack));
    EXPECT_CALL(announcer, data(DataOpcode::DataSepResp, 4, _));

    /* WriteEvictFull Flow */
    core.send_request(pxha_nid, 13, RequestOpcode::WriteEvictFull, 0xaced0000_pa);

    auto send_snoop_response = [this](SnoopOpcode opcode, uint16_t txn) {
        auto rsp = make<MeshRsp>(stash_nid, pxha_nid, txn, ResponseOpcode::SnpResp);
        rsp->Resp = RespSnoop_I;
        rsp->DatPull = DataPull::Read;
        rsp->DBID = 14;
        stash_core.send_response(rsp);
    };
    auto send_comp_ack2 = [this](ResponseOpcode opcode, uint16_t txn, uint16_t dbid) {
        core.send_response(pxha_nid, 1, ResponseOpcode::CompAck);
    };

    EXPECT_CALL(stash_announcer, snoop(SnoopOpcode::SnpStashShared, _)).WillOnce(Invoke(send_snoop_response));
    EXPECT_CALL(announcer, response(ResponseOpcode::CompDBIDResp, 13, 14)).WillOnce(Invoke(send_comp_ack2));
    tick(1000);

    auto snoop_status = pha_proxy.get_snoop_status(0xaced0000_pa);
    ASSERT_TRUE(snoop_status);
    ASSERT_EQ((*snoop_status)->count(), 0u);

    auto containing_core_id = (*snoop_status)->primary_snoop_target();
    ASSERT_TRUE(containing_core_id);
    EXPECT_EQ(*containing_core_id, stash_nid);
}

TEST_F(TestPHAWithMemory, WriteEvictFullStashNotAccepted)
{
    /* Stolen from ReadUnique test
     * Pull in Line to Core L2 to allow a snoop filter hit */
    core.send_request(pxha_nid, 4, RequestOpcode::ReadUnique, 0xaced0000_pa);

    auto send_comp_ack = [this](ResponseOpcode opcode, uint16_t txn, uint16_t dbid) {
        core.send_response(pxha_nid, dbid, ResponseOpcode::CompAck);
    };
    EXPECT_CALL(announcer, response(ResponseOpcode::RespSepData, 4, _))
        .WillOnce(Invoke(send_comp_ack))
        .RetiresOnSaturation();
    EXPECT_CALL(announcer, data(DataOpcode::DataSepResp, 4, _)).RetiresOnSaturation();

    /* WriteEvictFull Flow */
    core.send_request(pxha_nid, 13, RequestOpcode::WriteEvictFull, 0xaced0000_pa);

    auto send_snoop_response = [this](SnoopOpcode opcode, uint16_t txn) {
        auto rsp = make<MeshRsp>(stash_nid, pxha_nid, txn, ResponseOpcode::SnpResp);
        rsp->Resp = RespSnoop_I;
        stash_core.send_response(rsp);
    };
    auto send_copyback = [this](ResponseOpcode opcode, uint16_t txn, uint16_t dbid) {
        core.send_data(pxha_nid, txn, DataOpcode::CopyBackWrData, dbid, RespWriteData_UC);
    };

    EXPECT_CALL(stash_announcer, snoop(SnoopOpcode::SnpStashShared, _)).WillOnce(Invoke(send_snoop_response));
    EXPECT_CALL(announcer, response(ResponseOpcode::CompDBIDResp, 13, _)).WillOnce(Invoke(send_copyback));
    tick(1000);

    auto snoop_status = pha_proxy.get_snoop_status(0xaced0000_pa);
    ASSERT_FALSE(snoop_status);
}

TEST_F(TestPHAWithMemory, CleanInvalid)
{
    core.send_request(pxha_nid, 11, RequestOpcode::ReadNotSharedDirty, 0x100000_pa);
    core.send_request(pxha_nid, 12, RequestOpcode::ReadNotSharedDirty, 0x200000_pa);
    core.send_request(pxha_nid, 13, RequestOpcode::ReadNotSharedDirty, 0x300000_pa);
    core.send_request(pxha_nid, 14, RequestOpcode::ReadNotSharedDirty, 0x400000_pa);

    auto send_comp_ack = [this](ResponseOpcode opcode, uint16_t txn, uint16_t dbid) {
        core.send_response(pxha_nid, dbid, ResponseOpcode::CompAck);
    };

    auto send_final_read = [this](DataOpcode opcode, uint16_t txn, uint16_t dbid) {
        tick(100);
        core.send_request(pxha_nid, 15, RequestOpcode::ReadNotSharedDirty, 0x500000_pa);
    };

    auto send_snoop_response = [this](SnoopOpcode opcode, uint16_t txn) {
        core.send_response(pxha_nid, txn, ResponseOpcode::SnpResp, 0, RespSnoop_I);
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

    EXPECT_CALL(announcer, response(ResponseOpcode::RespSepData, 15, _)).WillOnce(Invoke(send_comp_ack));

    EXPECT_CALL(announcer, data(DataOpcode::DataSepResp, 15, _));
    EXPECT_CALL(announcer, snoop(SnoopOpcode::SnpCleanInvalid, _)).WillOnce(Invoke(send_snoop_response));
    tick(100);
}

TEST_F(TestPHAWithMemory, Evict)
{
    core.send_request(pxha_nid, 4, RequestOpcode::ReadUnique, 0xaced0000_pa);

    auto send_comp_ack = [this](ResponseOpcode opcode, uint16_t txn, uint16_t dbid) {
        core.send_response(pxha_nid, dbid, ResponseOpcode::CompAck);
    };

    auto send_eviction = [this](DataOpcode opcode, uint16_t txn, uint16_t dbid) {
        core.send_request(pxha_nid, 19, RequestOpcode::Evict, 0xaced0000_pa);
    };

    EXPECT_CALL(announcer, response(ResponseOpcode::RespSepData, 4, _)).WillOnce(Invoke(send_comp_ack));
    EXPECT_CALL(announcer, data(DataOpcode::DataSepResp, 4, _)).WillOnce(Invoke(send_eviction));
    EXPECT_CALL(announcer, response(ResponseOpcode::Comp, 19, _));
    tick(100);

    auto snoop_status = pha_proxy.get_snoop_status(0xaced0000_pa);
    EXPECT_FALSE(snoop_status);
}

TEST_F(TestPHAWithMemory, MakeUnique)
{
    core.send_request(pxha_nid, 7, RequestOpcode::ReadNotSharedDirty, 0xaced0000_pa);

    auto send_comp_ack = [this](ResponseOpcode opcode, uint16_t txn, uint16_t dbid) {
        core.send_response(pxha_nid, dbid, ResponseOpcode::CompAck);
    };

    EXPECT_CALL(announcer, response(ResponseOpcode::RespSepData, 7, _)).WillOnce(Invoke(send_comp_ack));
    EXPECT_CALL(announcer, data(DataOpcode::DataSepResp, 7, _));
    tick(100);

    auto core_snoop_response = [this](SnoopOpcode opcode, uint16_t txn) {
        core.send_response(pxha_nid, txn, ResponseOpcode::SnpResp, 0, RespSnoop_I);
    };

    core.send_request(pxha_nid, 8, RequestOpcode::MakeUnique, 0xaced0000_pa);

    EXPECT_CALL(announcer, snoop(SnoopOpcode::SnpMakeInvalid, _))
        .WillOnce(Invoke(core_snoop_response))
        .RetiresOnSaturation();

    EXPECT_CALL(announcer, response(ResponseOpcode::Comp, 8, _)).WillOnce(Invoke(send_comp_ack)); // TODO: Test UC???
    tick(100);
}

TEST_F(TestPHAWithMemory, ReadShared)
{
    core.send_request(pxha_nid, 7, RequestOpcode::ReadShared, 0xaced0000_pa);

    auto send_comp_ack = [this](ResponseOpcode opcode, uint16_t txn, uint16_t dbid) {
        core.send_response(pxha_nid, dbid, ResponseOpcode::CompAck);
    };

    EXPECT_CALL(announcer, response(ResponseOpcode::RespSepData, 7, _)).WillOnce(Invoke(send_comp_ack));
    EXPECT_CALL(announcer, data(DataOpcode::DataSepResp, 7, _));
    tick(100);
}

// Read same address from two cores.
// PxHA should send out a Snoop for the second read request
// Snoop filter should be updated with second core as PST and first core in the core vector list
TEST_F(TestPHAWithMemorySmallTracker, SnpFwd)
{
    core.send_request(pxha_nid, 4, RequestOpcode::ReadUnique, 0xaced0000_pa);

    auto send_comp_ack = [this](ResponseOpcode opcode, uint16_t txn, uint16_t dbid) {
        core.send_response(pxha_nid, dbid, ResponseOpcode::CompAck);
    };
    EXPECT_CALL(announcer, response(ResponseOpcode::RespSepData, 4, _))
        .WillOnce(Invoke(send_comp_ack))
        .RetiresOnSaturation();
    EXPECT_CALL(announcer, data(DataOpcode::DataSepResp, 4, _));

    tick(100);

    stash_core.send_request(pxha_nid, 5, RequestOpcode::ReadNotSharedDirty, 0xaced0000_pa);

    auto snp_resp = [this](SnoopOpcode opcode, uint16_t txn) {
        auto rsp = make<MeshRsp>(core_nid, pxha_nid, txn, ResponseOpcode::SnpRespFwded);
        rsp->Resp = chi::RespSnoop_SC;
        rsp->FwdState = chi::RespComp_SC;
        core.send_response(rsp);
        auto comp_rsp = make<MeshRsp>(stash_nid, pxha_nid, txn, ResponseOpcode::CompAck);
        stash_core.send_response(comp_rsp);
    };

    EXPECT_CALL(announcer, snoop(SnoopOpcode::SnpCleanFwd, _)).WillOnce(Invoke(snp_resp)).RetiresOnSaturation();

    tick(500);

    auto snoop_status = pha_proxy.get_snoop_status(0xaced0000_pa);
    ASSERT_TRUE(snoop_status);
    ASSERT_EQ((*snoop_status)->count(), 1u);

    auto containing_core_id = (*snoop_status)->primary_snoop_target();
    ASSERT_TRUE(containing_core_id);
    EXPECT_EQ(*containing_core_id, stash_nid);
}

// Read same address from two cores and fill in the snoop filter
// Issue another read to invalidate a snoop filter and send clean invalids to
// valid cores.
TEST_F(TestPHAWithMemory, SnpInv)
{
    core.send_request(pxha_nid, 11, RequestOpcode::ReadUnique, 0x100000_pa);
    core.send_request(pxha_nid, 12, RequestOpcode::ReadUnique, 0x200000_pa);
    core.send_request(pxha_nid, 13, RequestOpcode::ReadUnique, 0x300000_pa);
    core.send_request(pxha_nid, 14, RequestOpcode::ReadUnique, 0x400000_pa);

    auto send_comp_ack = [this](ResponseOpcode opcode, uint16_t txn, uint16_t dbid) {
        core.send_response(pxha_nid, dbid, ResponseOpcode::CompAck);
    };

    EXPECT_CALL(announcer, response(ResponseOpcode::RespSepData, _, _))
        .Times(4)
        .WillRepeatedly(Invoke(send_comp_ack))
        .RetiresOnSaturation();

    auto send_4_reads = [this](DataOpcode opcode, uint16_t txn, uint16_t dbid) {
        tick(500);
        stash_core.send_request(pxha_nid, 15, RequestOpcode::ReadNotSharedDirty, 0x100000_pa);
        stash_core.send_request(pxha_nid, 16, RequestOpcode::ReadNotSharedDirty, 0x200000_pa);
        stash_core.send_request(pxha_nid, 17, RequestOpcode::ReadNotSharedDirty, 0x300000_pa);
        stash_core.send_request(pxha_nid, 18, RequestOpcode::ReadNotSharedDirty, 0x400000_pa);
    };

    EXPECT_CALL(announcer, data(DataOpcode::DataSepResp, _, _))
        .WillOnce(Return())
        .WillOnce(Return())
        .WillOnce(Return())
        .WillOnce(Invoke(send_4_reads));

    auto snp_resp = [this](SnoopOpcode opcode, uint16_t txn) {
        auto rsp = make<MeshRsp>(core_nid, pxha_nid, txn, ResponseOpcode::SnpRespFwded);
        rsp->Resp = chi::RespSnoop_SC;
        rsp->FwdState = chi::RespComp_SC;
        core.send_response(rsp);
        auto comp_rsp = make<MeshRsp>(stash_nid, pxha_nid, txn, ResponseOpcode::CompAck);
        stash_core.send_response(comp_rsp);
    };

    EXPECT_CALL(announcer, snoop(SnoopOpcode::SnpCleanFwd, _))
        .Times(4)
        .WillRepeatedly(Invoke(snp_resp))
        .RetiresOnSaturation();

    tick(500);

    auto snoop_status = pha_proxy.get_snoop_status(0x100000_pa);
    ASSERT_TRUE(snoop_status);
    EXPECT_EQ((*snoop_status)->count(), 1u);

    auto containing_core_id = (*snoop_status)->primary_snoop_target();
    ASSERT_TRUE(containing_core_id);
    EXPECT_EQ(*containing_core_id, stash_nid);

    core.send_request(pxha_nid, 19, RequestOpcode::ReadNotSharedDirty, 0x500000_pa);

    EXPECT_CALL(announcer, response(ResponseOpcode::RespSepData, 19, _)).WillOnce(Invoke(send_comp_ack));

    EXPECT_CALL(announcer, data(DataOpcode::DataSepResp, 19, _));

    auto core_snoop_response = [this](SnoopOpcode opcode, uint16_t txn) {
        core.send_response(pxha_nid, txn, ResponseOpcode::SnpResp, 0, RespSnoop_I);
    };

    auto stash_core_snoop_response = [this](SnoopOpcode opcode, uint16_t txn) {
        core.send_response(pxha_nid, txn, ResponseOpcode::SnpResp, 0, RespSnoop_I);
    };

    EXPECT_CALL(announcer, snoop(SnoopOpcode::SnpCleanInvalid, _))
        .WillOnce(Invoke(core_snoop_response))
        .RetiresOnSaturation();

    EXPECT_CALL(stash_announcer, snoop(SnoopOpcode::SnpCleanInvalid, _))
        .WillOnce(Invoke(stash_core_snoop_response))
        .RetiresOnSaturation();

    tick(500);

    auto snoop_status_1 = pha_proxy.get_snoop_status(0x500000_pa);
    ASSERT_TRUE(snoop_status_1);
    EXPECT_EQ((*snoop_status_1)->count(), 0u);

    auto containing_core_id_1 = (*snoop_status_1)->primary_snoop_target();
    ASSERT_TRUE(containing_core_id_1);
    EXPECT_EQ(*containing_core_id_1, core_nid);
}

// Read same address from two cores.
// PxHA should send out a Snoop for the second read request
// Write to the same address from first core (CleanUnique)
// PxHA should invalidate the cache copy from the second core.
TEST_F(TestPHAWithMemory, CleanUnique)
{
    core.send_request(pxha_nid, 4, RequestOpcode::ReadUnique, 0xaced0000_pa);

    auto send_comp_ack = [this](ResponseOpcode opcode, uint16_t txn, uint16_t dbid) {
        core.send_response(pxha_nid, dbid, ResponseOpcode::CompAck);
    };
    EXPECT_CALL(announcer, response(ResponseOpcode::RespSepData, 4, _))
        .WillOnce(Invoke(send_comp_ack))
        .RetiresOnSaturation();
    EXPECT_CALL(announcer, data(DataOpcode::DataSepResp, 4, _));

    tick(100);

    stash_core.send_request(pxha_nid, 5, RequestOpcode::ReadNotSharedDirty, 0xaced0000_pa);

    auto snp_resp = [this](SnoopOpcode opcode, uint16_t txn) {
        auto rsp = make<MeshRsp>(core_nid, pxha_nid, txn, ResponseOpcode::SnpRespFwded);
        rsp->Resp = chi::RespSnoop_SC;
        rsp->FwdState = chi::RespComp_SC;
        core.send_response(rsp);
        stash_core.send_response(pxha_nid, txn, ResponseOpcode::CompAck);
    };

    EXPECT_CALL(announcer, snoop(SnoopOpcode::SnpCleanFwd, _)).WillOnce(Invoke(snp_resp)).RetiresOnSaturation();

    tick(500);

    auto snoop_status = pha_proxy.get_snoop_status(0xaced0000_pa);
    ASSERT_TRUE(snoop_status);
    ASSERT_EQ((*snoop_status)->count(), 1u);
    auto containing_core_id = (*snoop_status)->primary_snoop_target();
    ASSERT_TRUE(containing_core_id);
    EXPECT_EQ(*containing_core_id, stash_nid);

    core.send_request(pxha_nid, 6, RequestOpcode::CleanUnique, 0xaced0000_pa);

    auto stash_snoop_response = [this](SnoopOpcode opcode, uint16_t txn) {
        stash_core.send_response(pxha_nid, txn, ResponseOpcode::SnpResp, 0, RespSnoop_I);
    };

    EXPECT_CALL(stash_announcer, snoop(SnoopOpcode::SnpCleanInvalid, _))
        .WillOnce(Invoke(stash_snoop_response))
        .RetiresOnSaturation();

    EXPECT_CALL(announcer, response(ResponseOpcode::Comp, 6, _)).WillOnce(Invoke(send_comp_ack));

    tick(500);

    auto snoop_status_1 = pha_proxy.get_snoop_status(0xaced0000_pa);
    ASSERT_TRUE(snoop_status_1);
    ASSERT_EQ((*snoop_status_1)->count(), 0u);
    auto containing_core_id_1 = (*snoop_status_1)->primary_snoop_target();
    ASSERT_TRUE(containing_core_id_1);
    EXPECT_EQ(*containing_core_id_1, core_nid);
}

// Read an address in UC state.
// Read same address from another core in UC state.
TEST_F(TestPHAWithMemory, ReadUniqueTwoCores)
{
    core.send_request(pxha_nid, 4, RequestOpcode::ReadUnique, 0xaced0000_pa);

    auto send_comp_ack = [this](ResponseOpcode opcode, uint16_t txn, uint16_t dbid) {
        core.send_response(pxha_nid, dbid, ResponseOpcode::CompAck);
    };
    EXPECT_CALL(announcer, response(ResponseOpcode::RespSepData, 4, _))
        .WillOnce(Invoke(send_comp_ack))
        .RetiresOnSaturation();
    EXPECT_CALL(announcer, data(DataOpcode::DataSepResp, 4, _));

    stash_core.send_request(pxha_nid, 5, RequestOpcode::ReadUnique, 0xaced0000_pa);

    auto core_snoop_response = [this](SnoopOpcode opcode, uint16_t txn) {
        auto rsp = make<MeshRsp>(core_nid, pxha_nid, txn, ResponseOpcode::SnpRespFwded);
        rsp->Resp = chi::RespSnoop_I;
        rsp->FwdState = chi::RespComp_UC;
        core.send_response(rsp);
        auto comp_rsp = make<MeshRsp>(stash_nid, pxha_nid, txn, ResponseOpcode::CompAck);
        stash_core.send_response(comp_rsp);
    };

    EXPECT_CALL(announcer, snoop(SnoopOpcode::SnpUniqueFwd, _))
        .WillOnce(Invoke(core_snoop_response))
        .RetiresOnSaturation();

    tick(500);

    auto snoop_status = pha_proxy.get_snoop_status(0xaced0000_pa);
    ASSERT_TRUE(snoop_status);
    ASSERT_EQ((*snoop_status)->count(), 0u);
    auto containing_core_id = (*snoop_status)->primary_snoop_target();
    ASSERT_TRUE(containing_core_id);
    EXPECT_EQ(*containing_core_id, stash_nid);
}

// Read address
// Write Address into stash core
// Read Address Not in SD.  This makes the SF track 2 copies.  1 stash, 2 us.
// Do ReadUnique which causes flows that trigger Greater than one copy
// TEST_F(TestPHAWithMemory, ReadG1CopyFromStash)
//{
//    /* Stolen from ReadUnique test
//     * Pull in Line to Core L2 to allow a snoop filter hit */
//    core.send_request(pxha_nid, 4, RequestOpcode::ReadUnique, 0xaced0000_pa);
//
//    auto send_comp_ack = [this](ResponseOpcode opcode, uint16_t txn, uint16_t dbid) {
//        core.send_response(pxha_nid, dbid, ResponseOpcode::CompAck);
//    };
//    EXPECT_CALL(announcer, response(ResponseOpcode::RespSepData, 4,
//    _)).WillOnce(Invoke(send_comp_ack)).RetiresOnSaturation(); EXPECT_CALL(announcer, data(DataOpcode::DataSepResp, 4,
//    _));
//
//
//    /* WriteEvictFull Flow */
//    core.send_request(pxha_nid, 13, RequestOpcode::WriteEvictFull, 0xaced0000_pa);
//
//    auto send_snoop_response = [this](SnoopOpcode opcode, uint16_t txn) {
//        auto rsp = make<MeshRsp>(stash_nid, pxha_nid, txn, ResponseOpcode::SnpResp);
//        rsp->DatPull = DataPull::Read;
//        rsp->DBID = 14;
//        stash_core.send_response(rsp);
//    };
//    auto send_comp_ack2 = [this](ResponseOpcode opcode, uint16_t txn, uint16_t dbid) {
//        core.send_response(pxha_nid, 1, ResponseOpcode::CompAck);
//    };
//
//    EXPECT_CALL(stash_announcer, snoop(SnoopOpcode::SnpStashShared,
//    _)).WillOnce(Invoke(send_snoop_response)).RetiresOnSaturation(); EXPECT_CALL(announcer,
//    response(ResponseOpcode::CompDBIDResp, 13, 14)).WillOnce(Invoke(send_comp_ack2)); tick(1000);
//
//    auto snoop_status = pha_proxy.get_snoop_status(0xaced0000_pa);
//    ASSERT_TRUE(snoop_status);
//    ASSERT_EQ((*snoop_status)->count(), 0u);
//
//    auto containing_core_id = (*snoop_status)->primary_snoop_target();
//    ASSERT_TRUE(containing_core_id);
//    EXPECT_EQ(*containing_core_id, stash_nid);
//
//
//    /* Send Stash to core flow */
//    auto send_stash = [this](SnoopOpcode opcode, uint16_t txn) {
//        /* Send response back to pxha */
//        auto rsp = make<MeshRsp>(stash_nid, pxha_nid, txn, ResponseOpcode::SnpRespFwded);
//        rsp->Resp = chi::ResponseStatus{chi::FinalState::SC, false};
//        rsp->FwdState = chi::ResponseStatus{chi::FinalState::SC, false};
//        stash_core.send_response(rsp);
//
//        core.send_response(pxha_nid, txn, ResponseOpcode::CompAck);
//    };
//
//    /* Request stash line back to our core */
//    core.send_request(pxha_nid, 7, RequestOpcode::ReadNotSharedDirty, 0xaced0000_pa);
//    EXPECT_CALL(stash_announcer, snoop(SnoopOpcode::SnpCleanFwd,
//    _)).WillOnce(Invoke(send_stash)).RetiresOnSaturation(); tick(1000);
//
//    auto pst = (*snoop_status)->primary_snoop_target();
//    ASSERT_TRUE(snoop_status);
//    EXPECT_EQ((*snoop_status)->count(), 1u);
//    ASSERT_TRUE(pst);
//    EXPECT_EQ(*pst, core_nid);
//    EXPECT_TRUE((*snoop_status)->core_is_valid(stash_nid));
//
//    /* Pull in Line to Core L2 to allow a snoop filter hit */
//    core.send_request(pxha_nid, 11, RequestOpcode::ReadUnique, 0xaced0000_pa);
//
//    auto send_snoop_response_stash_invalidate = [this](SnoopOpcode opcode, uint16_t txn) {
//        auto rsp = make<MeshRsp>(stash_nid, pxha_nid, txn, ResponseOpcode::SnpResp);
//        rsp->Resp = chi::ResponseStatus{chi::FinalState::I, false};
//        stash_core.send_response(rsp);
//    };
//    auto send_snoop_response_core_invalidate = [this](SnoopOpcode opcode, uint16_t txn) {
//        auto rsp = make<MeshRsp>(core_nid, pxha_nid, txn, ResponseOpcode::SnpResp);
//        rsp->Resp = chi::ResponseStatus{chi::FinalState::I, false};
//        core.send_response(rsp);
//    };
//
//    /* Invalidate our copy of the cacheline */
//    EXPECT_CALL(stash_announcer, snoop(SnoopOpcode::SnpUnique,
//    _)).WillOnce(Invoke(send_snoop_response_stash_invalidate)).RetiresOnSaturation(); EXPECT_CALL(announcer,
//    snoop(SnoopOpcode::SnpUnique, _)).WillOnce(Invoke(send_snoop_response_core_invalidate)).RetiresOnSaturation();
//
//    /* We have a copy we must send it back to the HomeNode */
//    EXPECT_CALL(announcer, data(DataOpcode::CompData, 11, _));
//
//    tick(1000);
//}

TEST_F(TestPHAWithMemorySmallTracker, RecycleReadUnique)
{
    /* Send 4 Reads into pha.  This will fill the tracker table */
    core.send_request(pxha_nid, 11, RequestOpcode::ReadNotSharedDirty, 0x100000_pa);
    core.send_request(pxha_nid, 12, RequestOpcode::ReadNotSharedDirty, 0x200000_pa);
    core.send_request(pxha_nid, 13, RequestOpcode::ReadNotSharedDirty, 0x300000_pa);
    core.send_request(pxha_nid, 14, RequestOpcode::ReadNotSharedDirty, 0x400000_pa);

    auto send_comp_ack = [this](ResponseOpcode opcode, uint16_t txn, uint16_t dbid) {
        core.send_response(pxha_nid, dbid, ResponseOpcode::CompAck);
    };

    auto send_snoop_response = [this](SnoopOpcode opcode, uint16_t txn) {
        core.send_response(pxha_nid, txn, ResponseOpcode::SnpResp, 0, RespSnoop_I);
    };

    EXPECT_CALL(announcer, response(ResponseOpcode::RespSepData, _, _))
        .Times(5)
        .WillRepeatedly(Invoke(send_comp_ack))
        .RetiresOnSaturation();

    EXPECT_CALL(announcer, data(DataOpcode::DataSepResp, _, _)).Times(5).WillRepeatedly(Return());

    tick(100);

    core.send_request(pxha_nid, 15, RequestOpcode::ReadNotSharedDirty, 0x500000_pa);

    EXPECT_CALL(announcer, snoop(SnoopOpcode::SnpCleanInvalid, _)).WillOnce(Invoke(send_snoop_response));
    tick(100);
}

TEST_F(TestPHAWithMemorySmallTracker, Retry)
{
    /* Send 5 Reads into pha.  This will fill the tracker table and cause a RetryAck */
    core.send_request(pxha_nid, 11, RequestOpcode::ReadNotSharedDirty, 0x100000_pa);
    core.send_request(pxha_nid, 12, RequestOpcode::ReadNotSharedDirty, 0x200000_pa);
    core.send_request(pxha_nid, 13, RequestOpcode::ReadNotSharedDirty, 0x300000_pa);
    core.send_request(pxha_nid, 14, RequestOpcode::ReadNotSharedDirty, 0x400000_pa);
    core.send_request(pxha_nid, 15, RequestOpcode::ReadNotSharedDirty, 0x500000_pa);

    auto send_comp_ack = [this](ResponseOpcode opcode, uint16_t txn, uint16_t dbid) {
        core.send_response(pxha_nid, dbid, ResponseOpcode::CompAck);
    };

    auto send_snoop_response = [this](SnoopOpcode opcode, uint16_t txn) {
        core.send_response(pxha_nid, txn, ResponseOpcode::SnpResp, 0, RespSnoop_I);
    };

    auto resend_retried_request = [this](ResponseOpcode opcode, uint16_t txn, uint16_t dbid) {
        core.send_request_retry(pxha_nid, 15, RequestOpcode::ReadNotSharedDirty, 0x500000_pa);
    };

    EXPECT_CALL(announcer, response(ResponseOpcode::RespSepData, _, _))
        .Times(5)
        .WillRepeatedly(Invoke(send_comp_ack))
        .RetiresOnSaturation();

    EXPECT_CALL(announcer, data(DataOpcode::DataSepResp, _, _)).Times(5).WillRepeatedly(Return());

    EXPECT_CALL(announcer, response(ResponseOpcode::RetryAck, 15, _)).WillOnce(Return()).RetiresOnSaturation();

    EXPECT_CALL(announcer, response(ResponseOpcode::PCrdGrant, _, _))
        .WillOnce(Invoke(resend_retried_request))
        .RetiresOnSaturation();

    EXPECT_CALL(announcer, snoop(SnoopOpcode::SnpCleanInvalid, _)).WillOnce(Invoke(send_snoop_response));

    tick(100);
}

class TestPHATracker : public ampere::sc::TestModule
{
protected:
    TestPHATracker() : sc::TestModule{} {}

    PHATracker tracker{"tracker", 10, nullptr};
};

TEST_F(TestPHATracker, ScheduleOldest)
{
    auto req1 = make<MeshReq>(NodeId(), NodeId(), 1, RequestOpcode::ReadUnique, 0x10000_pa);
    auto req2 = make<MeshReq>(NodeId(), NodeId(), 2, RequestOpcode::ReadUnique, 0x20000_pa);
    auto req3 = make<MeshReq>(NodeId(), NodeId(), 3, RequestOpcode::ReadUnique, 0x30000_pa);
    auto req4 = make<MeshReq>(NodeId(), NodeId(), 4, RequestOpcode::ReadUnique, 0x40000_pa);
    auto req5 = make<MeshReq>(NodeId(), NodeId(), 5, RequestOpcode::ReadUnique, 0x50000_pa);

    auto e1 = tracker.allocate(req1, pha_fsm::PHA_t::Pxha, 0);
    auto e2 = tracker.allocate(req2, pha_fsm::PHA_t::Pxha, 0);
    auto e3 = tracker.allocate(req3, pha_fsm::PHA_t::Pxha, 0);
    auto e4 = tracker.allocate(req4, pha_fsm::PHA_t::Pxha, 0);
    auto e5 = tracker.allocate(req5, pha_fsm::PHA_t::Pxha, 0);

    tracker.ready_to_schedule(*e1);
    tracker.ready_to_schedule(*e2);
    tracker.ready_to_schedule(*e3);
    tracker.ready_to_schedule(*e4);
    tracker.ready_to_schedule(*e5);

    auto r1 = tracker.get_next_ready_entry();
    auto r2 = tracker.get_next_ready_entry();
    auto r3 = tracker.get_next_ready_entry();
    auto r4 = tracker.get_next_ready_entry();
    auto r5 = tracker.get_next_ready_entry();

    EXPECT_EQ(r1->request->TxnID, 1);
    EXPECT_EQ(r2->request->TxnID, 2);
    EXPECT_EQ(r3->request->TxnID, 3);
    EXPECT_EQ(r4->request->TxnID, 4);
    EXPECT_EQ(r5->request->TxnID, 5);
}

TEST_F(TestPHATracker, ScheduleAlternatePlatinumwithIron)
{
    auto req1 = make<MeshReq>(NodeId(), NodeId(), 1, RequestOpcode::ReadUnique, 0x10000_pa);
    auto req2 = make<MeshReq>(NodeId(), NodeId(), 2, RequestOpcode::ReadUnique, 0x20000_pa);
    auto req3 = make<MeshReq>(NodeId(), NodeId(), 3, RequestOpcode::ReadUnique, 0x30000_pa);

    auto req_p1 = make<MeshReq>(NodeId(), NodeId(), 10, RequestOpcode::ReadUnique, 0xa0000_pa);
    auto req_p2 = make<MeshReq>(NodeId(), NodeId(), 11, RequestOpcode::ReadUnique, 0xb0000_pa);
    req_p1->QoS = chi::Qos(chi::Qos::Level::PLATINUM);
    req_p2->QoS = chi::Qos(chi::Qos::Level::PLATINUM);

    auto e1 = tracker.allocate(req1, pha_fsm::PHA_t::Pxha, 0);
    auto e2 = tracker.allocate(req2, pha_fsm::PHA_t::Pxha, 0);
    auto e3 = tracker.allocate(req3, pha_fsm::PHA_t::Pxha, 0);
    auto e_p1 = tracker.allocate(req_p1, pha_fsm::PHA_t::Pxha, 0);
    auto e_p2 = tracker.allocate(req_p2, pha_fsm::PHA_t::Pxha, 0);

    tracker.ready_to_schedule(*e1);
    tracker.ready_to_schedule(*e2);
    tracker.ready_to_schedule(*e3);
    tracker.ready_to_schedule(*e_p1);
    tracker.ready_to_schedule(*e_p2);

    auto r1 = tracker.get_next_ready_entry();
    auto r2 = tracker.get_next_ready_entry();
    auto r3 = tracker.get_next_ready_entry();
    auto r4 = tracker.get_next_ready_entry();
    auto r5 = tracker.get_next_ready_entry();

    EXPECT_EQ(r1->request->TxnID, 10);
    EXPECT_EQ(r2->request->TxnID, 1);
    EXPECT_EQ(r3->request->TxnID, 11);
    EXPECT_EQ(r4->request->TxnID, 2);
    EXPECT_EQ(r5->request->TxnID, 3);
}

TEST_F(TestPHATracker, ScheduleSfvBeforeAnyRequest)
{
    auto req1 = make<MeshReq>(NodeId(), NodeId(), 1, RequestOpcode::ReadUnique, 0x10000_pa);
    auto req2 = make<MeshReq>(NodeId(), NodeId(), 2, RequestOpcode::ReadUnique, 0x20000_pa);
    auto req3 = make<MeshReq>(NodeId(), NodeId(), 3, RequestOpcode::ReadUnique, 0x30000_pa);
    auto req4 = make<MeshReq>(NodeId(), NodeId(), 4, RequestOpcode::ReadUnique, 0x40000_pa);

    auto e1 = tracker.allocate(req1, pha_fsm::PHA_t::Pxha, 0);
    auto e2 = tracker.allocate(req2, pha_fsm::PHA_t::Pxha, 0);
    auto e3 = tracker.allocate(req3, pha_fsm::PHA_t::Pxha, 0);
    auto e4 = tracker.allocate(req4, pha_fsm::PHA_t::Pxha, 0);
    e3->input.SfVictim = true; // PHA sets this when the allocation is sfv, simulate that here
    e4->input.SfVictim = true; // PHA sets this when the allocation is sfv, simulate that here

    tracker.ready_to_schedule(*e1);
    tracker.ready_to_schedule(*e2);
    tracker.ready_to_schedule(*e3);
    tracker.ready_to_schedule(*e4);

    auto r1 = tracker.get_next_ready_entry();
    auto r2 = tracker.get_next_ready_entry();
    auto r3 = tracker.get_next_ready_entry();
    auto r4 = tracker.get_next_ready_entry();

    EXPECT_EQ(r1->request->TxnID, 3);
    EXPECT_EQ(r2->request->TxnID, 4);
    EXPECT_EQ(r3->request->TxnID, 1);
    EXPECT_EQ(r4->request->TxnID, 2);
}

TEST_F(TestPHATracker, ScheduleAlternateSnpwithRequests)
{
    auto req1 = make<MeshReq>(NodeId(), NodeId(), 1, RequestOpcode::ReadUnique, 0x10000_pa);
    auto req2 = make<MeshReq>(NodeId(), NodeId(), 2, RequestOpcode::ReadUnique, 0x20000_pa);
    auto req3 = make<MeshReq>(NodeId(), NodeId(), 3, RequestOpcode::ReadUnique, 0x30000_pa);
    auto snp_p1 = make<MeshReq>(NodeId(), NodeId(), 10, RequestOpcode::ReadUnique, 0xa0000_pa);
    auto snp_p2 = make<MeshReq>(NodeId(), NodeId(), 11, RequestOpcode::ReadUnique, 0xb0000_pa);

    auto e1 = tracker.allocate(req1, pha_fsm::PHA_t::Pxha, 0);
    auto e2 = tracker.allocate(req2, pha_fsm::PHA_t::Pxha, 0);
    auto e3 = tracker.allocate(req3, pha_fsm::PHA_t::Pxha, 0);
    auto e_p1 = tracker.allocate(snp_p1, pha_fsm::PHA_t::Pxha, 0);
    auto e_p2 = tracker.allocate(snp_p2, pha_fsm::PHA_t::Pxha, 0);
    e_p1->input.FlowType =
        pha_fsm::FlowType_t::FLOW_SNP; /* phatracker determines if a allocate is a snp based on pha flow type */
    e_p2->input.FlowType =
        pha_fsm::FlowType_t::FLOW_SNP; /* phatracker determines if a allocate is a snp based on pha flow type */

    tracker.ready_to_schedule(*e1);
    tracker.ready_to_schedule(*e2);
    tracker.ready_to_schedule(*e3);
    tracker.ready_to_schedule(*e_p1);
    tracker.ready_to_schedule(*e_p2);

    auto r1 = tracker.get_next_ready_entry();
    auto r2 = tracker.get_next_ready_entry();
    auto r3 = tracker.get_next_ready_entry();
    auto r4 = tracker.get_next_ready_entry();
    auto r5 = tracker.get_next_ready_entry();

    EXPECT_EQ(r1->request->TxnID, 10);
    EXPECT_EQ(r2->request->TxnID, 1);
    EXPECT_EQ(r3->request->TxnID, 11);
    EXPECT_EQ(r4->request->TxnID, 2);
    EXPECT_EQ(r5->request->TxnID, 3);
}

TEST_F(TestPHATracker, ScheduleAllocateAllTypes)
{
    auto req1 = make<MeshReq>(NodeId(), NodeId(), 1, RequestOpcode::ReadUnique, 0x10000_pa);
    auto req2 = make<MeshReq>(NodeId(), NodeId(), 2, RequestOpcode::ReadUnique, 0x20000_pa);
    auto snp1 = make<MeshReq>(NodeId(), NodeId(), 4, RequestOpcode::ReadUnique, 0x40000_pa);
    auto snp2 = make<MeshReq>(NodeId(), NodeId(), 5, RequestOpcode::ReadUnique, 0x50000_pa);
    auto snp3 = make<MeshReq>(NodeId(), NodeId(), 3, RequestOpcode::ReadUnique, 0x30000_pa);
    auto req_p1 = make<MeshReq>(NodeId(), NodeId(), 6, RequestOpcode::ReadUnique, 0x60000_pa);
    auto req_p2 = make<MeshReq>(NodeId(), NodeId(), 7, RequestOpcode::ReadUnique, 0x70000_pa);
    auto req_p3 = make<MeshReq>(NodeId(), NodeId(), 8, RequestOpcode::ReadUnique, 0x80000_pa);
    req_p1->QoS = chi::Qos(chi::Qos::Level::PLATINUM);
    req_p2->QoS = chi::Qos(chi::Qos::Level::PLATINUM);
    req_p3->QoS = chi::Qos(chi::Qos::Level::PLATINUM);
    auto sfv1 = make<MeshReq>(NodeId(), NodeId(), 9, RequestOpcode::ReadUnique, 0x90000_pa);
    auto sfv2 = make<MeshReq>(NodeId(), NodeId(), 10, RequestOpcode::ReadUnique, 0xa0000_pa);

    auto e1 = tracker.allocate(req1, pha_fsm::PHA_t::Pxha, 0);
    auto e2 = tracker.allocate(req2, pha_fsm::PHA_t::Pxha, 0);
    auto e_snp1 = tracker.allocate(snp1, pha_fsm::PHA_t::Pxha, 0);
    auto e_snp2 = tracker.allocate(snp2, pha_fsm::PHA_t::Pxha, 0);
    auto e_snp3 = tracker.allocate(snp3, pha_fsm::PHA_t::Pxha, 0);
    e_snp1->input.FlowType =
        pha_fsm::FlowType_t::FLOW_SNP; /* phatracker determines if a allocate is a snp based on pha flow type */
    e_snp2->input.FlowType =
        pha_fsm::FlowType_t::FLOW_SNP; /* phatracker determines if a allocate is a snp based on pha flow type */
    e_snp3->input.FlowType =
        pha_fsm::FlowType_t::FLOW_SNP; /* phatracker determines if a allocate is a snp based on pha flow type */
    auto e_plat1 = tracker.allocate(req_p1, pha_fsm::PHA_t::Pxha, 0);
    auto e_plat2 = tracker.allocate(req_p2, pha_fsm::PHA_t::Pxha, 0);
    auto e_plat3 = tracker.allocate(req_p3, pha_fsm::PHA_t::Pxha, 0);
    auto e_sfv1 = tracker.allocate(sfv1, pha_fsm::PHA_t::Pxha, 0);
    auto e_sfv2 = tracker.allocate(sfv2, pha_fsm::PHA_t::Pxha, 0);
    e_sfv1->input.SfVictim = true; // PHA sets this when the allocation is sfv, simulate that here
    e_sfv2->input.SfVictim = true; // PHA sets this when the allocation is sfv, simulate that here

    tracker.ready_to_schedule(*e1);
    tracker.ready_to_schedule(*e2);
    tracker.ready_to_schedule(*e_snp1);
    tracker.ready_to_schedule(*e_snp2);
    tracker.ready_to_schedule(*e_snp3);
    tracker.ready_to_schedule(*e_plat1);
    tracker.ready_to_schedule(*e_plat2);
    tracker.ready_to_schedule(*e_plat3);
    tracker.ready_to_schedule(*e_sfv1);
    tracker.ready_to_schedule(*e_sfv2);

    auto r1 = tracker.get_next_ready_entry();
    auto r2 = tracker.get_next_ready_entry();
    auto r3 = tracker.get_next_ready_entry();
    auto r4 = tracker.get_next_ready_entry();
    auto r5 = tracker.get_next_ready_entry();
    auto r6 = tracker.get_next_ready_entry();
    auto r7 = tracker.get_next_ready_entry();
    auto r8 = tracker.get_next_ready_entry();
    auto r9 = tracker.get_next_ready_entry();
    auto r10 = tracker.get_next_ready_entry();

    EXPECT_EQ(r1->request->TxnID, 4);
    EXPECT_EQ(r2->request->TxnID, 9);
    EXPECT_EQ(r3->request->TxnID, 5);
    EXPECT_EQ(r4->request->TxnID, 10);
    EXPECT_EQ(r5->request->TxnID, 3);
    EXPECT_EQ(r6->request->TxnID, 6);
    EXPECT_EQ(r7->request->TxnID, 1);
    EXPECT_EQ(r8->request->TxnID, 7);
    EXPECT_EQ(r9->request->TxnID, 2);
    EXPECT_EQ(r10->request->TxnID, 8);
}

class TestPHAPrefetchTgt : public ampere::sc::TestModule, public MeshConfigIfc
{
protected:
    TestPHAPrefetchTgt()
        : sc::TestModule{{"~node\\.req_p[0-9]+\\.size", 8},
                         {"~node\\.snp_p[0-9]+\\.size", 8},
                         {"~node\\.rsp[01]_p[0-9]+\\.size", 8},
                         {"~node\\.dat[01]_p[0-9]+\\.size", 8},

                         {"pha_proxy.node_id", "{\"agent_id\": 3}"_json},
                         {"pha_proxy.sam.type", "PXHA_SAM"},
                         {"pha_proxy.sam.system_address_map.connect", "system_address_map"},
                         {"pha_proxy.sam.pmha_tgt", 4},
                         {"pha_proxy.rsam.system_address_map.connect", "system_address_map"},
                         {"pha_proxy.prefetch_tgt_sam.system_address_map.connect", "system_address_map"},
                         {"pha_proxy.prefetch_tgt_sam.type", "PMHA_SAM"},
                         {"pha_proxy.prefetch_tgt_sam.system_address_map.connect", "system_address_map"},
                         {"pha_proxy.prefetch_tgt_sam.mcu_tgt", 2},
                         {"pha_proxy.cbusy.throttle_start", 96},
                         {"pha_proxy.enable_prefetch_target", true},

                         {"pha_primary.node_id", "{\"agent_id\": 4}"_json},
                         {"pha_primary.sam.type", "PMHA_SAM"},
                         {"pha_primary.sam.system_address_map.connect", "system_address_map"},
                         {"pha_primary.sam.mcu_tgt", 2},
                         {"pha_primary.rsam.system_address_map.connect", "system_address_map"},

                         {"node.local_lut",
                          std::map<uint8_t, LocalRoutingId>{{0, LocalRoutingId{0, 0, 0, 0}},
                                                            {1, LocalRoutingId{0, 0, 4, 0}},
                                                            {2, LocalRoutingId{0, 0, 3, 0}},
                                                            {3, LocalRoutingId{0, 0, 1, 0}},
                                                            {4, LocalRoutingId{0, 0, 2, 0}}}},
                         {"~pmha_mp_.*_rx_[01]\\.capacity", 8},
                         {"~pxha_mp_.*_rx_[01]\\.capacity", 8},
                         {"~core_mp_.*_rx_[01]\\.capacity", 8},
                         {"~mem_mp_.*_rx_[01]\\.capacity", 8},
                         {"~stash_mp_.*_rx_[01]\\.capacity", 8}}
    {
        node.ports[DIR_COUNT + 0].connect(core.mesh_port, "core_mp");
        node.ports[DIR_COUNT + 1].connect(pha_proxy.get_mesh_port(), "pxha_mp");
        node.ports[DIR_COUNT + 2].connect(pha_primary.get_mesh_port(), "pmha_mp");
        node.ports[DIR_COUNT + 3].connect(mem.mesh_port, "mem_mp");
        node.ports[DIR_COUNT + 4].connect(stash_core.mesh_port, "stash_mp");

        node.config.bind(*this);
        pha_proxy.config.bind(*this);
        pha_primary.config.bind(*this);

        core.announcer.bind(announcer);
        stash_core.announcer.bind(stash_announcer);
    }

    NodeId core_nid{NodeId::DieType::CPU, 0, 0, 0};
    NodeId stash_nid{NodeId::DieType::CPU, 0, 0, 1};
    NodeId mem_nid{NodeId::DieType::CPU, 0, 0, 2};
    NodeId pxha_nid{NodeId::DieType::CPU, 0, 0, 3};
    NodeId pmha_nid{NodeId::DieType::CPU, 0, 0, 4};

    MeshNode node{"node", 0, 0, 5, 0, 0};
    DummyRequester core{"core", core_nid};              // Position [0,0,0,0]: AgentId: 0
    PxHA pha_proxy{"pha_proxy"};                        // Position [0,0,1,0], AgentId: 3
    PmHA pha_primary{"pha_primary"};                    // Position [0,0,2,0], AgentId: 4
    DummyMemory mem{"mem", mem_nid};                    // Position [0,0,3,0], AgentId: 2
    DummyRequester stash_core{"stash_core", stash_nid}; // Position [0,0,4,0], AgentId: 1
    MockSystemAddressMap system_address_map{"system_address_map"};
    MockRequesterAnnouncer announcer;
    MockRequesterAnnouncer stash_announcer;

    int data_bus_width() const override { return 512; }
    int channel(const chi::NodeId id) const override { return 0; }
    std::vector<chi::NodeId> stash_nodes(uint8_t die_id) const override
    {
        return {NodeId{NodeId::DieType::CPU, 0, 0, 1}};
    }
};

TEST_F(TestPHAPrefetchTgt, IdleSystemPrefetchTgt)
{
    core.send_request(pxha_nid, 7, RequestOpcode::ReadNotSharedDirty, 0xaced0000_pa);

    auto send_comp_ack = [this](ResponseOpcode opcode, uint16_t txn, uint16_t dbid) {
        core.send_response(pxha_nid, dbid, ResponseOpcode::CompAck);
    };

    EXPECT_CALL(announcer, response(ResponseOpcode::RespSepData, 7, _)).WillOnce(Invoke(send_comp_ack));
    EXPECT_CALL(announcer, data(DataOpcode::DataSepResp, 7, _));
    tick(100);

    auto pxha_snoop_status = pha_proxy.get_snoop_status(0xaced0000_pa);
    ASSERT_TRUE(pxha_snoop_status);
    ASSERT_EQ((*pxha_snoop_status)->count(), 0u);

    auto containing_core_id = (*pxha_snoop_status)->primary_snoop_target();
    ASSERT_TRUE(containing_core_id);
    EXPECT_EQ(*containing_core_id, core_nid);

    auto pmha_snoop_status = pha_primary.get_snoop_status(0xaced0000_pa);
    ASSERT_TRUE(pmha_snoop_status);
    ASSERT_EQ((*pmha_snoop_status)->count(), 0u);

    auto containing_proxy_id = (*pmha_snoop_status)->primary_snoop_target();
    ASSERT_TRUE(containing_proxy_id);
    EXPECT_EQ(*containing_proxy_id, pxha_nid);

    bool prefetch_tgt = mem.has_prefetch_tgt(0xaced0000_pa);
    EXPECT_TRUE(prefetch_tgt);
}
