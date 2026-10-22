//
// Copyright (c) 2022 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "ampere/mesh/rdi.h"
#include "ampere/sc/testing/test_module.h"

namespace ampere::mesh {

using namespace rdi;

struct RDIPackingTest : public sc::Test
{};

TEST_F(RDIPackingTest, PackOneRequest)
{
    auto& params = Environment::get_root()->parameters;
    params.add_setting("packer.rules",
                       R"([
        {"Req": 1}
    ])"_json,
                       "test bench");

    IncomingMsgVec incoming = {{MsgType::REQ, 12}};

    LinkState link_state;

    Packer packer{"packer", 1};
    auto packing = packer.pack(incoming, link_state);

    EXPECT_EQ(packing.new_state[0].current.s.reqs, 1u);

    auto [msg, link] = packing.packing_list[0];
    EXPECT_EQ(msg.type, MsgType::REQ);
    EXPECT_EQ(msg.tag, 12);
    EXPECT_EQ(link, 0);
}

TEST_F(RDIPackingTest, PackTwoRequests)
{
    auto& params = Environment::get_root()->parameters;
    params.add_setting("packer.rules",
                       R"([
        {"Req": 2}
    ])"_json,
                       "test bench");

    IncomingMsgVec incoming = {
        {MsgType::REQ, 12},
        {MsgType::REQ, 13},
    };

    LinkState link_state;

    Packer packer{"packer", 1};
    auto packing = packer.pack(incoming, link_state);

    EXPECT_EQ(packing.new_state[0].current.s.reqs, 2u);

    auto [msg, link] = packing.packing_list[0];
    EXPECT_EQ(msg.type, MsgType::REQ);
    EXPECT_EQ(msg.tag, 12);

    std::tie(msg, link) = packing.packing_list[1];
    EXPECT_EQ(msg.type, MsgType::REQ);
    EXPECT_EQ(msg.tag, 13);
}

TEST_F(RDIPackingTest, FailToPackThreeRequests)
{
    auto& params = Environment::get_root()->parameters;
    params.add_setting("packer.rules",
                       R"([
        {"Req": 2}
    ])"_json,
                       "test bench");

    IncomingMsgVec incoming = {
        {MsgType::REQ, 12},
        {MsgType::REQ, 13},
        {MsgType::REQ, 14},
    };

    LinkState link_state;

    Packer packer{"packer", 1};
    auto packing = packer.pack(incoming, link_state);

    EXPECT_EQ(packing.new_state[0].current.s.reqs, 2u);

    auto [msg, link] = packing.packing_list[0];
    EXPECT_EQ(msg.type, MsgType::REQ);
    EXPECT_EQ(msg.tag, 12);

    std::tie(msg, link) = packing.packing_list[1];
    EXPECT_EQ(msg.type, MsgType::REQ);
    EXPECT_EQ(msg.tag, 13);
}

TEST_F(RDIPackingTest, PackThreeRequestsOnThreeLinks)
{
    auto& params = Environment::get_root()->parameters;
    params.add_setting("packer.rules",
                       R"([
        {"Req": 1}
    ])"_json,
                       "test bench");

    IncomingMsgVec incoming = {
        {MsgType::REQ, 12},
        {MsgType::REQ, 13},
        {MsgType::REQ, 14},
    };

    LinkState link_state;

    Packer packer{"packer", 3};
    auto packing = packer.pack(incoming, link_state);

    EXPECT_EQ(packing.new_state[0].current.s.reqs, 1u);
    EXPECT_EQ(packing.new_state[1].current.s.reqs, 1u);
    EXPECT_EQ(packing.new_state[2].current.s.reqs, 1u);

    auto [msg, link] = packing.packing_list[0];
    EXPECT_EQ(link, 0);

    std::tie(msg, link) = packing.packing_list[1];
    EXPECT_EQ(link, 1);

    std::tie(msg, link) = packing.packing_list[2];
    EXPECT_EQ(link, 2);
}

TEST_F(RDIPackingTest, PackThreeDifferentTypesOnThreeLinks)
{
    auto& params = Environment::get_root()->parameters;
    params.add_setting("packer.rules",
                       R"([
        {"Req": 1, "Links": [2]},
        {"Rsp": 1, "Links": [1]},
        {"Dat": 1, "Links": [0]}
    ])"_json,
                       "test bench");

    IncomingMsgVec incoming = {
        {MsgType::REQ, 42},
        {MsgType::RSP, 96},
        {MsgType::DAT, 10},
    };

    LinkState link_state;

    Packer packer{"packer", 3};
    auto packing = packer.pack(incoming, link_state);

    EXPECT_EQ(packing.new_state[0].current.s.dats, 1u);
    EXPECT_EQ(packing.new_state[1].current.s.rsps, 1u);
    EXPECT_EQ(packing.new_state[2].current.s.reqs, 1u);

    auto [msg, link] = packing.packing_list[0];
    EXPECT_EQ(msg.type, MsgType::REQ);
    EXPECT_EQ(link, 2);

    std::tie(msg, link) = packing.packing_list[1];
    EXPECT_EQ(msg.type, MsgType::RSP);
    EXPECT_EQ(link, 1);

    std::tie(msg, link) = packing.packing_list[2];
    EXPECT_EQ(msg.type, MsgType::DAT);
    EXPECT_EQ(link, 0);
}

class Sender : public sc::Module
{
public:
    explicit Sender(const sc_core::sc_module_name& name) : sc::Module{name} {}
    MeshPort out{"out", 1, 2, 1, 1};

    bool run() override
    {
        if (!req_fifo.empty() && out.tx().req[0]->can_put()) {
            auto opcode = req_fifo.front();
            req_fifo.pop();
            out.tx().req[0]->put(make<MeshReq>(chi::NodeId{0}, chi::NodeId{1}, 0, opcode, 0x1000_pa));
        }

        if (!rsp0_fifo.empty() && out.tx().rsp[0]->can_put()) {
            auto opcode = rsp0_fifo.front();
            rsp0_fifo.pop();
            out.tx().rsp[0]->put(make<MeshRsp>(chi::NodeId{0}, chi::NodeId{1}, 0, opcode));
        }

        if (!rsp1_fifo.empty() && out.tx().rsp[1]->can_put()) {
            auto opcode = rsp1_fifo.front();
            rsp1_fifo.pop();
            out.tx().rsp[1]->put(make<MeshRsp>(chi::NodeId{0}, chi::NodeId{1}, 0, opcode));
        }

        if (!dat_fifo.empty() && out.tx().dat[0]->can_put()) {
            auto opcode = dat_fifo.front();
            dat_fifo.pop();
            out.tx().dat[0]->put(make<MeshDat>(chi::NodeId{0}, chi::NodeId{1}, 0, opcode));
        }

        return true;
    }

    std::queue<chi::RequestOpcode> req_fifo;
    std::queue<chi::ResponseOpcode> rsp0_fifo;
    std::queue<chi::ResponseOpcode> rsp1_fifo;
    std::queue<chi::DataOpcode> dat_fifo;
};

class Receiver : public sc::Module
{
public:
    explicit Receiver(const sc_core::sc_module_name& name) : sc::Module{name}
    {
        in.rx().req[0].set_credit_pool(&credits);
        in.rx().rsp[0].set_credit_pool(&credits);
        if (in.nrsp == 2)
            in.rx().rsp[1].set_credit_pool(&credits);
        in.rx().snp[0].set_credit_pool(&credits);
        in.rx().dat[0].set_credit_pool(&credits);
        in.rx().dat[0].set_credit_pool(&credits);
    }
    MeshPort in{"in", 1, 2, 1, 1};
    CreditPool credits{"credits"};

    std::deque<std::pair<chi::RequestOpcode, Cycle>> req_rcvd;
    std::deque<std::pair<chi::ResponseOpcode, Cycle>> rsp0_rcvd;
    std::deque<std::pair<chi::ResponseOpcode, Cycle>> rsp1_rcvd;
    std::deque<std::pair<chi::DataOpcode, Cycle>> dat_rcvd;

    bool run() override
    {
        if (in.rx().req[0]->can_take_with_credit()) {
            auto [pkt, credit] = in.rx().req[0]->take_with_credit();
            req_rcvd.emplace_back(pkt->Opcode, get_cycle());
            in.rx().req[0]->put_credit(std::move(credit));
        }

        if (in.rx().rsp[0]->can_take_with_credit()) {
            auto [pkt, credit] = in.rx().rsp[0]->take_with_credit();
            rsp0_rcvd.emplace_back(pkt->Opcode, get_cycle());
            in.rx().rsp[0]->put_credit(std::move(credit));
        }

        if (in.nrsp == 2 && in.rx().rsp[1]->can_take_with_credit()) {
            auto [pkt, credit] = in.rx().rsp[1]->take_with_credit();
            rsp1_rcvd.emplace_back(pkt->Opcode, get_cycle());
            in.rx().rsp[1]->put_credit(std::move(credit));
        }

        if (in.rx().dat[0]->can_take_with_credit()) {
            auto [pkt, credit] = in.rx().dat[0]->take_with_credit();
            dat_rcvd.emplace_back(pkt->Opcode, get_cycle());
            in.rx().dat[0]->put_credit(std::move(credit));
        }

        return true;
    }
};

struct RDITest : sc::IntegrationTest, public MeshConfigIfc
{
    RDI rdi{"rdi"};
    Sender sender{"sender"};
    Receiver receiver{"receiver"};

    RDITest()
        : sc::IntegrationTest{{{"receiver.credits.size", 100},
                               {"rdi.a_in_req.fifo.size", 4},
                               {"rdi.a_in_rsp0.fifo.size", 4},
                               {"rdi.a_in_rsp1.fifo.size", 4},
                               {"rdi.a_in_snp.fifo.size", 4},
                               {"rdi.a_in_dat.fifo.size", 4},
                               {"rdi.a_out_req_fifo_size", 32},
                               {"rdi.packer.rules", R"([
                {"Req": 1, "Crd": 1, "Links": [2]},
                {"Rsp": 2, "Crd": 2, "Links": [1]},
                {"Dat": 1, "Links": [0]}
              ])"_json}}}
    {
        sender.out.connect(rdi.a);
        rdi.b.connect(receiver.in);
        rdi.config.bind(*this);
    }

    void SetUp() override { warmup_for(0_ns); }

    // MeshConfigIfc
    int data_bus_width() const override { return 512; }
    int channel(const chi::NodeId id) const override { return 0; }
};

TEST_F(RDITest, CanBuildRDI) {}

TEST_F(RDITest, CanSendOneRequest)
{
    sender.req_fifo.push(chi::RequestOpcode::ReadShared);
    run_for(30_ns);
    ASSERT_EQ(receiver.req_rcvd.size(), 1u);
    EXPECT_EQ(receiver.req_rcvd.front().second, 13);
}

TEST_F(RDITest, CanSustainOneRequestPerCycle)
{
    for (int i = 0; i < 30; ++i)
        sender.req_fifo.push(chi::RequestOpcode::ReadShared);

    // run_for(150_ns);
    run_for(1000_ns);
    ASSERT_EQ(receiver.req_rcvd.size(), 30u);

    auto last_recv_cycle = receiver.req_rcvd.front().second;
    receiver.req_rcvd.pop_front();

    while (!receiver.req_rcvd.empty()) {
        auto [opcode, recv_cycle] = receiver.req_rcvd.front();
        receiver.req_rcvd.pop_front();
        EXPECT_EQ(recv_cycle, last_recv_cycle + 1);
        last_recv_cycle = recv_cycle;
    }
}

TEST_F(RDITest, DataTakesThreeFlits)
{
    for (int i = 0; i < 10; ++i)
        sender.dat_fifo.push(chi::DataOpcode::CopyBackWrData);

    run_for(150_ns);
    ASSERT_EQ(receiver.dat_rcvd.size(), 10u);

    auto last_recv_cycle = receiver.dat_rcvd.front().second;
    receiver.dat_rcvd.pop_front();

    while (!receiver.dat_rcvd.empty()) {
        auto [opcode, recv_cycle] = receiver.dat_rcvd.front();
        receiver.dat_rcvd.pop_front();
        EXPECT_EQ(recv_cycle, last_recv_cycle + 3);
        last_recv_cycle = recv_cycle;
    }
}

TEST_F(RDITest, CanSend1Req2Rsp1DatSimultaneously)
{
    sender.req_fifo.push(chi::RequestOpcode::ReadShared);
    sender.rsp0_fifo.push(chi::ResponseOpcode::CompAck);
    sender.rsp1_fifo.push(chi::ResponseOpcode::Comp);
    sender.dat_fifo.push(chi::DataOpcode::CopyBackWrData);

    run_for(150_ns);
    ASSERT_EQ(receiver.req_rcvd.size(), 1u);
    ASSERT_EQ(receiver.rsp0_rcvd.size(), 1u);
    ASSERT_EQ(receiver.rsp1_rcvd.size(), 1u);
    ASSERT_EQ(receiver.dat_rcvd.size(), 1u);

    auto req_recv_cycle = receiver.req_rcvd.front().second;
    auto rsp0_recv_cycle = receiver.rsp0_rcvd.front().second;
    auto rsp1_recv_cycle = receiver.rsp1_rcvd.front().second;
    auto dat_recv_cycle = receiver.dat_rcvd.front().second;

    EXPECT_EQ(rsp0_recv_cycle, req_recv_cycle);
    EXPECT_EQ(rsp1_recv_cycle, req_recv_cycle);
    EXPECT_EQ(dat_recv_cycle, req_recv_cycle);
}

} // namespace ampere::mesh

