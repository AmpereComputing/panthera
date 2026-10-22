//
// Copyright (c) 2020 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//
#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "ampere/mesh/injector/mesh_injector.h"
#include "ampere/mesh/test/mock_addr_map.h"
#include "ampere/mesh/test/mock_mesh_port.h"
#include "ampere/sc/testing/test_module.h"

using namespace ampere::mesh;

class TestMeshInjector : public ampere::sc::TestModule, public MeshConfigIfc
{
protected:
    TestMeshInjector()
        : ampere::sc::TestModule{{"injector.source.type", "UopSourceIS"},
                                 {"injector.source.is", SOURCE_DIRECTORY "/../soc/test/lat_mem_rd_128M.is.xz"},
                                 {"injector.source.decoders", "[\"furiosa\"]"_json},
                                 {"injector.sam.tgt_lut", "[1]"_json},
                                 {"injector.sam.system_address_map.connect", "system_address_map"}}
    {
        bind_ports();
    }

    TestMeshInjector(std::initializer_list<std::pair<std::string, sc::json>> settings)
        : ampere::sc::TestModule(settings)
    {
        bind_ports();
    }

    void SetUp() override { archx::Instruction::set_decoders({"furiosa"}); }

    int data_bus_width() const override { return 256; }
    int channel(const chi::NodeId id) const override { return 0; }

    MeshInjector injector{"injector"};
    MockSystemAddressMap system_address_map{"system_address_map"};

    NiceMock<MockMeshPort> port;

private:
    void bind_ports()
    {
        injector.mesh_port.rx().req[0].bind(port.tx.req);
        injector.mesh_port.rx().rsp[0].bind(port.tx.rsp);
        injector.mesh_port.rx().snp[0].bind(port.tx.snp);
        injector.mesh_port.rx().dat[0].bind(port.tx.dat);

        injector.mesh_port.tx().req[0].bind(port.rx.req);
        injector.mesh_port.tx().rsp[0].bind(port.rx.rsp);
        injector.mesh_port.tx().snp[0].bind(port.rx.snp);
        injector.mesh_port.tx().dat[0].bind(port.rx.dat);

        injector.config.bind(*this);
    }
};

TEST_F(TestMeshInjector, CanCreateMeshInjector) {}

TEST_F(TestMeshInjector, InjectorGeneratesRequest)
{
    EXPECT_CALL(port.rx.req, can_put()).WillRepeatedly(Return(true));
    EXPECT_CALL(port.rx.req, put(_)).Times(1);

    tick(100);
}

TEST_F(TestMeshInjector, InjectorGeneratesSecondRequest)
{
    // The loads in lat_mem_rd are serially dependent, so we won't get a second request
    // until we generate a response for the first load.
    //
    EXPECT_CALL(port.rx.req, can_put()).WillOnce(Return(true)).WillOnce(Return(true)).WillRepeatedly(Return(false));

    auto respond_with_data = [this](MeshReq req) {
        auto dat = make<MeshDat>(NodeId{1}, req->SrcID, req->TxnID, chi::DataOpcode::CompData);
        dat->DBID = req->TxnID;
        dat->Resp = chi::RespComp_UC;
        EXPECT_CALL(port.tx.dat, can_take()).WillOnce(Return(true)).WillRepeatedly(Return(false));
        EXPECT_CALL(port.tx.dat, take()).WillOnce(Return(dat));
    };

    EXPECT_CALL(port.rx.req, put(_)).Times(2).WillOnce(Invoke(respond_with_data)).WillOnce(Return());

    EXPECT_CALL(port.rx.rsp, can_put()).WillOnce(Return(true));
    EXPECT_CALL(port.rx.rsp, put(_)).Times(1);

    tick(100);
}

TEST_F(TestMeshInjector, InjectorHandlesSplitResponse)
{
    // The loads in lat_mem_rd are serially dependent, so we won't get a second request
    // until we generate a response for the first load.
    //
    EXPECT_CALL(port.rx.req, can_put()).WillOnce(Return(true)).WillOnce(Return(true)).WillRepeatedly(Return(false));

    EXPECT_CALL(port.rx.rsp, can_put()).WillRepeatedly(Return(true));

    EXPECT_CALL(port.tx.rsp, can_take()).WillRepeatedly(Return(false));

    EXPECT_CALL(port.tx.dat, can_take()).WillRepeatedly(Return(false));

    // The data and response must both be transmitted(in any order) before a CompAck is seen.
    auto resp_sep_data = [this](MeshReq req) {
        auto rsp = make<MeshRsp>(NodeId{1}, req->SrcID, req->TxnID, chi::ResponseOpcode::RespSepData);
        rsp->Resp = chi::RespComp_UC;

        EXPECT_CALL(port.tx.rsp, can_take()).WillOnce(Return(true)).RetiresOnSaturation();

        EXPECT_CALL(port.tx.rsp, take()).WillOnce(Return(rsp)).RetiresOnSaturation();
    };

    auto data_sep_resp = [this](MeshReq req) {
        auto dat = make<MeshDat>(NodeId{1}, req->SrcID, req->TxnID, chi::DataOpcode::DataSepResp);
        dat->DataID = 0;
        dat->DBID = req->TxnID;

        EXPECT_CALL(port.tx.dat, can_take()).WillOnce(Return(true)).RetiresOnSaturation();

        EXPECT_CALL(port.tx.dat, take()).WillOnce(Return(dat)).RetiresOnSaturation();
    };

    EXPECT_CALL(port.rx.req, put(_))
        .Times(2)
        .WillOnce(DoAll(Invoke(resp_sep_data), Invoke(data_sep_resp)))
        .WillOnce(Return());

    EXPECT_CALL(port.rx.rsp, put(HasOpcode(chi::ResponseOpcode::CompAck)));

    tick(1000);
}

class TestMeshInjectorWithOffset : public TestMeshInjector
{
protected:
    TestMeshInjectorWithOffset()
        : TestMeshInjector{{"injector.source.is", SOURCE_DIRECTORY "/../soc/test/lat_mem_rd_128M.is.xz"},
                           {"injector.sam.system_address_map.connect", "system_address_map"},
                           {"injector.sam.tgt_lut", "[1]"_json},
                           {"injector.source.decoders", "[\"furiosa\"]"_json},
                           {"injector.data_offset", 0x1000000}}
    {
    }
};

TEST_F(TestMeshInjectorWithOffset, InjectorGeneratesRequestWithOffset)
{
    EXPECT_CALL(port.rx.req, can_put()).WillRepeatedly(Return(true));
    EXPECT_CALL(port.rx.req, put(HasAddress(0x139ce2c80_pa))).Times(1);

    tick(100);
}

class TestMeshInjectorWithCode : public TestModule, public MeshConfigIfc
{
protected:
    TestMeshInjectorWithCode()
        : TestModule({{"injector.source.type", "UopSourceProg"},
                      {"injector.source.enable_events_on_start", true},
                      {"injector.sam.system_address_map.connect", "system_address_map"},
                      {"injector.sam.tgt_lut", "[1]"_json},
                      {"injector.source.decoders", "[\"furiosa\"]"_json}})
    {
    }

    int data_bus_width() const override { return 512; }
    int channel(const chi::NodeId id) const override { return 0; }

    void connect_injector()
    {
        injector->mesh_port.rx().req[0].bind(port->tx.req);
        injector->mesh_port.rx().rsp[0].bind(port->tx.rsp);
        injector->mesh_port.rx().snp[0].bind(port->tx.snp);
        injector->mesh_port.rx().dat[0].bind(port->tx.dat);

        injector->mesh_port.tx().req[0].bind(port->rx.req);
        injector->mesh_port.tx().rsp[0].bind(port->rx.rsp);
        injector->mesh_port.tx().snp[0].bind(port->rx.snp);
        injector->mesh_port.tx().dat[0].bind(port->rx.dat);

        injector->config.bind(*this);
    }

    void setup_parameters(json parameters)
    {
        get_environment()->parameters.add_setting("injector.source.parameters", parameters, "test bench");
    }

    void setup(json program)
    {
        get_environment()->parameters.add_setting("injector.source.program", program, "test bench");

        injector = std::make_unique<MeshInjector>("injector");
        port = std::make_unique<MockMeshPort>();

        connect_injector();
    }

    // These are pointers so we can defer constructing them until we've set up the program
    // Parameter appropriately.
    //
    std::unique_ptr<MeshInjector> injector;
    std::unique_ptr<MockMeshPort> port;
    MockSystemAddressMap system_address_map{"system_address_map"};
};

TEST_F(TestMeshInjectorWithCode, InjectorGeneratesRequest)
{
    setup(R"({
"code": [
    {"op": "LDR_64_ldst_pos", "fields": {"imm12": 0, "Rn": 1, "Rt": 2}, "pa": 8192}
]}
)"_json);

    EXPECT_CALL(port->rx.req, can_put()).WillRepeatedly(Return(true));
    EXPECT_CALL(port->rx.req, put(_)).Times(1);

    tick(100);
}

TEST_F(TestMeshInjectorWithCode, ReadAfterWrite)
{
    setup(R"({
"code": [
    {"op": "STR_64_ldst_pos", "fields": {"imm12": 0, "Rn": 1, "Rt": 3}, "pa": 8192},
    {"op": "LDR_64_ldst_pos", "fields": {"imm12": 0, "Rn": 1, "Rt": 2}, "pa": 8192}
]}
)"_json);

    EXPECT_CALL(port->rx.req, can_put()).WillRepeatedly(Return(true));
    EXPECT_CALL(port->rx.req, put(_)).Times(1);

    tick(100);
}

TEST_F(TestMeshInjectorWithCode, NoReadAfterWrite)
{
    setup(R"({
"code": [
    {"op": "STR_64_ldst_pos", "fields": {"imm12": 0, "Rn": 1, "Rt": 3}, "pa": "0x2000"},
    {"op": "LDR_64_ldst_pos", "fields": {"imm12": 0, "Rn": 1, "Rt": 2}, "pa": "0x2040"}
]}
)"_json);

    EXPECT_CALL(port->rx.req, can_put()).WillRepeatedly(Return(true));
    EXPECT_CALL(port->rx.req, put(_)).Times(2);

    tick(100);
}

TEST_F(TestMeshInjectorWithCode, StridedGenerator)
{
    setup(R"({
"generators": [
    {
        "name": "A",
        "type": "strided",
        "start": "0x20000",
        "stride": 64
    }
],
"code": [
    {"op": "LDR_64_ldst_pos", "fields": {"imm12": 0, "Rn": 1, "Rt": 2}, "pa": "A+"},
    {"op": "LDR_64_ldst_pos", "fields": {"imm12": 0, "Rn": 1, "Rt": 2}, "pa": "A+"},
    {"op": "LDR_64_ldst_pos", "fields": {"imm12": 0, "Rn": 1, "Rt": 2}, "pa": "A+"}
]}
)"_json);

    EXPECT_CALL(port->rx.req, can_put()).WillRepeatedly(Return(true));

    {
        InSequence s;

        EXPECT_CALL(port->rx.req, put(Pointee(Field(&MeshReqObj::Addr, 0x20000_pa))));
        EXPECT_CALL(port->rx.req, put(Pointee(Field(&MeshReqObj::Addr, 0x20040_pa))));
        EXPECT_CALL(port->rx.req, put(Pointee(Field(&MeshReqObj::Addr, 0x20080_pa))));
    }

    tick(100);
}

TEST_F(TestMeshInjectorWithCode, StridedGeneratorWithoutAdvance)
{
    setup(R"({
"generators": [
    {
        "name": "A",
        "type": "strided",
        "start": "0x20000",
        "stride": 64
    }
],
"code": [
    {"op": "LDR_64_ldst_pos", "fields": {"imm12": 0, "Rn": 1, "Rt": 2}, "pa": "A"},
    {"op": "LDR_64_ldst_pos", "fields": {"imm12": 0, "Rn": 1, "Rt": 2}, "pa": "A"},
    {"op": "LDR_64_ldst_pos", "fields": {"imm12": 0, "Rn": 1, "Rt": 2}, "pa": "A"}
]}
)"_json);

    EXPECT_CALL(port->rx.req, can_put()).WillRepeatedly(Return(true));

    // Only one goes out because they are all the same cache line
    EXPECT_CALL(port->rx.req, put(Pointee(Field(&MeshReqObj::Addr, 0x20000_pa)))).Times(1);

    tick(100);
}

TEST_F(TestMeshInjectorWithCode, RandomGenerator)
{
    setup(R"({
"generators": [
    {
        "name": "A",
        "type": "random",
        "start": "0x0",
        "stop": "0x10000000",
        "distribution": "uniform",
        "mask": "0x7",
        "seed": 1234
    }
],
"code": [
    {"op": "LDR_64_ldst_pos", "fields": {"imm12": 0, "Rn": 1, "Rt": 2}, "pa": "A+"},
    {"op": "LDR_64_ldst_pos", "fields": {"imm12": 0, "Rn": 1, "Rt": 2}, "pa": "A+"},
    {"op": "LDR_64_ldst_pos", "fields": {"imm12": 0, "Rn": 1, "Rt": 2}, "pa": "A+"}
]}
)"_json);

    EXPECT_CALL(port->rx.req, can_put()).WillRepeatedly(Return(true));

    {
        InSequence s;

        EXPECT_CALL(port->rx.req, put(Pointee(Field(&MeshReqObj::Addr, 0xf27dc40_pa))));
        EXPECT_CALL(port->rx.req, put(Pointee(Field(&MeshReqObj::Addr, 0xd5e800_pa))));
        EXPECT_CALL(port->rx.req, put(Pointee(Field(&MeshReqObj::Addr, 0xf96cec0_pa))));
    }

    tick(100);
}

TEST_F(TestMeshInjectorWithCode, Retry)
{
    setup(R"({
"code": [
    {"op": "LDR_64_ldst_pos", "fields": {"imm12": 0, "Rn": 1, "Rt": 2}, "pa": 8192}
]}
)"_json);

    auto reply_with_retry_ack = [this](MeshReq req) {
        auto retry = trans_make<MeshRsp>(NodeId{1}, req->SrcID, req->TxnID, ResponseOpcode::RetryAck);
        retry->PCrdType = 0;

        auto grant = trans_make<MeshRsp>(NodeId{1}, req->SrcID, req->TxnID, ResponseOpcode::PCrdGrant);
        grant->PCrdType = 0;

        EXPECT_CALL(port->tx.rsp, can_take())
            .WillOnce(Return(true))
            .WillOnce(Return(false)) // spread out the retry and the grant
            .WillOnce(Return(false))
            .WillOnce(Return(false))
            .WillOnce(Return(false))
            .WillOnce(Return(false))
            .WillOnce(Return(false))
            .WillOnce(Return(false))
            .WillOnce(Return(false))
            .WillOnce(Return(true))
            .WillRepeatedly(Return(false));

        EXPECT_CALL(port->tx.rsp, take()).WillOnce(Return(retry)).WillOnce(Return(grant));
    };

    EXPECT_CALL(port->rx.req, can_put()).WillRepeatedly(Return(true));

    {
        InSequence seq;

        EXPECT_CALL(port->rx.req, put(HasAllowRetry(true)))
            .WillOnce(Invoke(reply_with_retry_ack))
            .RetiresOnSaturation();

        EXPECT_CALL(port->rx.req, put(AllOf(HasAddress(8192_pa), HasAllowRetry(false))));
    }

    tick(100);
}

TEST_F(TestMeshInjectorWithCode, DependentAddsGiveIPCOne)
{
    setup(R"({
"code": [
    {"op": "ADD_64_addsub_ext", "fields": {"imm3": 0, "Rd": 4, "Rn": 5, "Rm": 4, "option": 0}}
],
"loop": 100
}
)"_json);

    // Can't just tick(100) because the ClockDummy allows us to keep ticking beyond
    // when the simulation is stopped by running out of instructions. So we need to
    // watch for the stop in order to calculate an accurate IPC.
    //
    while (sc_core::sc_get_status() != sc_core::SC_STOPPED)
        tick(1);

    auto ipc = (double)event_count("injector.instruction_commit") / clock->get_cycle();
    EXPECT_NEAR(ipc, 1.0, 0.1);
}

class TestMeshInjectorWithSmallL2 : public TestMeshInjectorWithCode
{
protected:
    TestMeshInjectorWithSmallL2() : TestMeshInjectorWithCode{}
    {
        auto& params = get_environment()->parameters;
        params.add_setting("injector.decoders", "[\"furiosa\"]"_json, "test bench");
        params.add_setting("injector.cache.size", 64, "test bench"); // one line in L2
        params.add_setting("injector.l3c.size", 64, "test bench");   // one line in L3
        params.add_setting("injector.cache.ways", 1, "test bench");
        params.add_setting("injector.l3c.ways", 1, "test bench");
        params.add_setting("injector.cache.line_size", 64, "test bench");
        params.add_setting("injector.l3c.line_size", 64, "test bench");
    }

    std::function<void(mesh::MeshReq req)> return_data(uint16_t dbid, mesh::chi::FinalState state)
    {
        return [this, dbid, state](mesh::MeshReq req) {
            auto dat1 = trans_make<mesh::MeshDat>(req->TxnID, mesh::DataOpcode::CompData);
            dat1->Resp = mesh::chi::ResponseStatus{mesh::chi::ResponseStatus::Type::Comp, state};
            dat1->DBID = dbid;
            dat1->DataID = 0;

            auto dat2 = trans_make<mesh::MeshDat>(req->TxnID, mesh::DataOpcode::CompData);
            dat2->Resp = mesh::chi::ResponseStatus{mesh::chi::ResponseStatus::Type::Comp, state};
            dat2->DBID = dbid;
            dat2->DataID = 2;

            EXPECT_CALL(port->tx.dat, can_take()).WillOnce(Return(true)).WillOnce(Return(true)).RetiresOnSaturation();

            EXPECT_CALL(port->tx.dat, take()).WillOnce(Return(dat1)).WillOnce(Return(dat2)).RetiresOnSaturation();

            EXPECT_CALL(port->rx.rsp, put(AllOf(HasTxnID(dbid), HasOpcode(mesh::ResponseOpcode::CompAck))))
                .RetiresOnSaturation();
        };
    }
};

TEST_F(TestMeshInjectorWithSmallL2, SecondReadGeneratesCleanEviction)
{
    setup(R"({
"generators": [
    {
        "name": "A",
        "type": "strided",
        "start": "0x20000",
        "stride": 64
    }
],
"code": [
    {"op": "LDR_64_ldst_pos", "fields": {"imm12": 0, "Rn": 1, "Rt": 2}, "pa": "A+"},
    {"op": "LDR_64_ldst_pos", "fields": {"imm12": 0, "Rn": 1, "Rt": 2}, "pa": "A+"}
]}
)"_json);

    EXPECT_CALL(port->tx.dat, can_take()).WillRepeatedly(Return(false));

    EXPECT_CALL(port->rx.rsp, can_put()).WillRepeatedly(Return(true));

    EXPECT_CALL(port->rx.req, can_put()).WillRepeatedly(Return(true));

    EXPECT_CALL(port->rx.req, put(Pointee(Field(&MeshReqObj::Addr, 0x20000_pa))))
        .WillOnce(Invoke(return_data(8, chi::FinalState::UC)))
        .RetiresOnSaturation();

    EXPECT_CALL(port->rx.req, put(Pointee(Field(&MeshReqObj::Addr, 0x20040_pa))))
        .WillOnce(Invoke(return_data(10, chi::FinalState::UC)))
        .RetiresOnSaturation();

    EXPECT_CALL(port->rx.req, put(Pointee(Field(&MeshReqObj::Opcode, chi::RequestOpcode::WriteEvictOrEvict)))).Times(1);

    tick(100);
}

TEST_F(TestMeshInjectorWithSmallL2, SecondWriteGeneratesDirtyEviction)
{
    setup(R"({
"generators": [
    {
        "name": "A",
        "type": "strided",
        "start": "0x20000",
        "stride": 64
    }
],
"code": [
    {"op": "STR_64_ldst_pos", "fields": {"imm12": 0, "Rn": 1, "Rt": 2}, "pa": "A+"},
    {"op": "STR_64_ldst_pos", "fields": {"imm12": 0, "Rn": 1, "Rt": 2}, "pa": "A+"}
]}
)"_json);

    EXPECT_CALL(port->tx.dat, can_take()).WillRepeatedly(Return(false));

    EXPECT_CALL(port->rx.rsp, can_put()).WillRepeatedly(Return(true));

    EXPECT_CALL(port->rx.req, can_put()).WillRepeatedly(Return(true));

    EXPECT_CALL(port->rx.req, put(Pointee(Field(&MeshReqObj::Addr, 0x20000_pa))))
        .WillOnce(Invoke(return_data(8, chi::FinalState::UC)))
        .RetiresOnSaturation();

    EXPECT_CALL(port->rx.req, put(Pointee(Field(&MeshReqObj::Addr, 0x20040_pa))))
        .WillOnce(Invoke(return_data(10, chi::FinalState::UC)))
        .RetiresOnSaturation();

    EXPECT_CALL(port->rx.req, put(Pointee(Field(&MeshReqObj::Opcode, chi::RequestOpcode::WriteBackFull)))).Times(1);

    tick(100);
}

TEST_F(TestMeshInjectorWithSmallL2, ReadFromL2WayOnly)
{
    setup(R"({
    "code": [
        {"op": "LDR_64_ldst_pos", "fields": {"imm12": 0, "Rn": 1, "Rt": 2}, "pa": "0x11111100"}
    ]}
    )"_json);
    injector->preload_in_l2(0x91111100_pa, MeshInjector::CacheEntry::State::UC); // preload a line in L2

    EXPECT_CALL(port->tx.dat, can_take()).WillRepeatedly(Return(false));

    EXPECT_CALL(port->rx.rsp, can_put()).WillRepeatedly(Return(true));

    EXPECT_CALL(port->rx.req, can_put()).WillRepeatedly(Return(true));

    EXPECT_CALL(port->rx.req, put(Pointee(Field(&MeshReqObj::Addr, 0x11111100_pa))))
        .WillOnce(Invoke(return_data(10, chi::FinalState::UC)))
        .RetiresOnSaturation();

    EXPECT_CALL(port->rx.req, put(Pointee(Field(&MeshReqObj::Opcode, chi::RequestOpcode::WriteEvictOrEvict)))).Times(1);

    tick(100);
}

TEST_F(TestMeshInjectorWithSmallL2, FlexCacheStash)
{
    setup(R"({
    "code": [
        {"op": "LDR_64_ldst_pos", "fields": {"imm12": 0, "Rn": 1, "Rt": 2}, "pa": "0x91111100"}
    ]}
    )"_json);
    injector->preload_in_l2(0x91111100_pa, MeshInjector::CacheEntry::State::UC); // preload a line in L2

    EXPECT_CALL(port->tx.dat, can_take()).WillRepeatedly(Return(false));

    auto snp = trans_make<MeshSnp>(NodeId{1}, NodeId{0}, 12, SnoopOpcode::SnpStashShared, 0x2000_pa);

    EXPECT_CALL(port->tx.snp, can_take()).WillOnce(Return(true)).WillRepeatedly(Return(false));

    EXPECT_CALL(port->tx.snp, take()).WillOnce(Return(snp));

    EXPECT_CALL(port->rx.rsp, can_put()).WillOnce(Return(true)).WillRepeatedly(Return(false));

    auto copy_back_wr_data = [this](mesh::MeshRsp rsp) {
        auto dat1 = trans_make<mesh::MeshDat>(rsp->DBID, mesh::DataOpcode::CopyBackWrData);
        dat1->Resp = mesh::chi::RespWriteData_UC;
        dat1->DBID = 12;
        dat1->DataID = 0;

        auto dat2 = trans_make<mesh::MeshDat>(rsp->DBID, mesh::DataOpcode::CopyBackWrData);
        dat2->Resp = mesh::chi::RespWriteData_UC;
        dat2->DBID = 12;
        dat2->DataID = 2;

        EXPECT_CALL(port->tx.dat, can_take()).WillOnce(Return(true)).WillOnce(Return(true)).RetiresOnSaturation();

        EXPECT_CALL(port->tx.dat, take()).WillOnce(Return(dat1)).WillOnce(Return(dat2)).RetiresOnSaturation();

        EXPECT_CALL(port->rx.rsp, can_put()).WillOnce(Return(true)).WillRepeatedly(Return(false));

        EXPECT_CALL(port->rx.rsp, put(AllOf(HasTxnID(12), HasOpcode(mesh::ResponseOpcode::CompAck))))
            .RetiresOnSaturation();
    };

    EXPECT_CALL(port->rx.rsp, put(HasOpcode(chi::ResponseOpcode::SnpResp))).WillOnce(Invoke(copy_back_wr_data));

    tick(100);
}

TEST_F(TestMeshInjectorWithSmallL2, FlexStashCache_Indirect)
{
    setup(R"({
    "code": [
        {"op": "LDR_64_ldst_pos", "fields": {"imm12": 0, "Rn": 1, "Rt": 2}, "pa": "0x91111100"}
    ]}
    )"_json);
    injector->preload_in_l2(0x91111100_pa, MeshInjector::CacheEntry::State::UC); // preload a line in L2

    EXPECT_CALL(port->tx.dat, can_take()).WillRepeatedly(Return(false));

    auto snp = trans_make<MeshSnp>(NodeId{1}, NodeId{0}, 12, SnoopOpcode::SnpStashShared, 0x2000_pa);

    EXPECT_CALL(port->tx.snp, can_take()).WillOnce(Return(true)).WillRepeatedly(Return(false));

    EXPECT_CALL(port->tx.snp, take()).WillOnce(Return(snp));

    EXPECT_CALL(port->rx.rsp, can_put()).WillOnce(Return(true)).WillRepeatedly(Return(false));

    auto copy_back_wr_data = [this](mesh::MeshRsp rsp) {
        auto dat1 = trans_make<mesh::MeshDat>(rsp->DBID, mesh::DataOpcode::CompData);
        dat1->Resp = mesh::chi::RespWriteData_UC;
        dat1->DBID = 12;
        dat1->DataID = 0;

        auto dat2 = trans_make<mesh::MeshDat>(rsp->DBID, mesh::DataOpcode::CompData);
        dat2->Resp = mesh::chi::RespWriteData_UC;
        dat2->DBID = 12;
        dat2->DataID = 2;

        EXPECT_CALL(port->tx.dat, can_take()).WillOnce(Return(true)).WillOnce(Return(true)).RetiresOnSaturation();

        EXPECT_CALL(port->tx.dat, take()).WillOnce(Return(dat1)).WillOnce(Return(dat2)).RetiresOnSaturation();

        EXPECT_CALL(port->rx.rsp, can_put()).WillOnce(Return(true)).WillRepeatedly(Return(false));

        EXPECT_CALL(port->rx.rsp, put(AllOf(HasTxnID(12), HasOpcode(mesh::ResponseOpcode::CompAck))))
            .RetiresOnSaturation();
    };

    EXPECT_CALL(port->rx.rsp, put(HasOpcode(chi::ResponseOpcode::SnpResp))).WillOnce(Invoke(copy_back_wr_data));

    tick(100);
}

// One way - allocated to L3. This test case is testing Direct l3 Stash flow with dirty eviction
TEST_F(TestMeshInjectorWithSmallL2, FlexStashCacheWithEvict)
{
    setup(R"({
    "code": [
        {"op": "LDR_64_ldst_pos", "fields": {"imm12": 0, "Rn": 1, "Rt": 2}, "pa": "0x91111100"}
    ]}
    )"_json);

    injector->preload_in_l3(0x91111100_pa, MeshInjector::CacheEntry::State::UD);

    auto snp = trans_make<MeshSnp>(NodeId{1}, NodeId{0}, 12, SnoopOpcode::SnpStashShared, 0x11111100_pa);

    EXPECT_CALL(port->tx.dat, can_take()).WillRepeatedly(Return(false));

    EXPECT_CALL(port->tx.rsp, can_take()).WillRepeatedly(Return(false));

    EXPECT_CALL(port->tx.snp, can_take()).WillOnce(Return(true)).WillRepeatedly(Return(false));

    EXPECT_CALL(port->tx.snp, take()).WillOnce(Return(snp));

    auto eviction_response = [this](mesh::MeshReq req) {
        auto rsp = trans_make<MeshRsp>(NodeId{1}, NodeId{0}, req->TxnID, ResponseOpcode::CompDBIDResp);
        rsp->DBID = 399;

        EXPECT_CALL(port->tx.rsp, can_take()).WillOnce(Return(true)).RetiresOnSaturation();

        EXPECT_CALL(port->tx.rsp, take()).WillOnce(Return(rsp)).RetiresOnSaturation();

        EXPECT_CALL(port->rx.dat, can_put())
            .WillOnce(Return(true))
            //.WillOnce(Return(true))
            .WillRepeatedly(Return(false));

        EXPECT_CALL(port->rx.dat,
                    put(AllOf(HasOpcode(mesh::DataOpcode::CopyBackWrData), HasTxnID(399), HasPassDirty(true))))
            .Times(1);
    };

    auto copy_back_wr_data = [this, eviction_response](mesh::MeshRsp rsp) {
        auto dat1 = trans_make<mesh::MeshDat>(rsp->DBID, mesh::DataOpcode::CopyBackWrData);
        dat1->Resp = mesh::chi::RespWriteData_UC;
        dat1->DBID = 12;
        dat1->DataID = 0;

        auto dat2 = trans_make<mesh::MeshDat>(rsp->DBID, mesh::DataOpcode::CopyBackWrData);
        dat2->Resp = mesh::chi::RespWriteData_UC;
        dat2->DBID = 12;
        dat2->DataID = 2;

        EXPECT_CALL(port->tx.dat, can_take()).WillOnce(Return(true)).WillOnce(Return(true)).RetiresOnSaturation();

        EXPECT_CALL(port->tx.dat, take()).WillOnce(Return(dat1)).WillOnce(Return(dat2)).RetiresOnSaturation();

        EXPECT_CALL(port->rx.rsp, can_put()).WillOnce(Return(true)).WillRepeatedly(Return(false));

        EXPECT_CALL(port->rx.rsp, put(AllOf(HasTxnID(12), HasOpcode(mesh::ResponseOpcode::CompAck))))
            .RetiresOnSaturation();

        EXPECT_CALL(port->rx.req, can_put()).WillOnce(Return(true)).WillRepeatedly(Return(false));

        EXPECT_CALL(port->rx.req, put(HasOpcode(mesh::RequestOpcode::WriteBackFull)))
            .WillOnce(Invoke(eviction_response));
    };

    EXPECT_CALL(port->rx.rsp, can_put()).WillOnce(Return(true)).WillRepeatedly(Return(false));

    EXPECT_CALL(port->rx.rsp, put(HasOpcode(mesh::ResponseOpcode::SnpResp))).WillOnce(Invoke(copy_back_wr_data));

    tick(50);

    // EXPECT_EQ(injector->peek_state_l3(0x11111100_pa), MeshInjector::CacheEntry::State::UC);
}

class TestProgramParameters : public TestMeshInjectorWithCode
{
protected:
    TestProgramParameters() : TestMeshInjectorWithCode{}
    {
        auto& params = get_environment()->parameters;
        params.add_setting("injector.source.repeat_3", 3, "test bench");
    }
};

TEST_F(TestProgramParameters, TestRepeatParameter)
{
    setup_parameters(R"([
{
    "name": "repeat_3",
    "description": "test parameter",
    "default": 1
}
])"_json);

    setup(R"({
"generators": [
    {
        "name": "A",
        "type": "strided",
        "start": "0x20000",
        "stride": 64
    }
],
"code": [
    {"op": "LDR_64_ldst_pos", "fields": {"imm12": 0, "Rn": 1, "Rt": 2}, "pa": "A+", "repeat": "@repeat_3"}
]}
)"_json);

    EXPECT_CALL(port->rx.req, can_put()).WillRepeatedly(Return(true));
    EXPECT_CALL(port->rx.req, put(_)).Times(3);

    tick(100);
}

TEST_F(TestMeshInjectorWithCode, TestDefaultParameter)
{
    setup_parameters(R"([
{
    "name": "no_setting",
    "description": "test parameter which has no setting",
    "default": 4
}
])"_json);

    setup(R"({
"generators": [
    {
        "name": "A",
        "type": "strided",
        "start": "0x20000",
        "stride": 64
    }
],
"code": [
    {"op": "LDR_64_ldst_pos", "fields": {"imm12": 0, "Rn": 1, "Rt": 2}, "pa": "A+", "repeat": "@no_setting"}
]}
)"_json);

    EXPECT_CALL(port->rx.req, can_put()).WillRepeatedly(Return(true));
    EXPECT_CALL(port->rx.req, put(_)).Times(4);

    tick(100);
}

TEST(TestResponseStatus, I_PD_SetsPassDirty)
{
    auto resp = RespSnoop_I_PD;
    EXPECT_TRUE(resp.pass_dirty());
}
