//
// Copyright (c) 2021 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//
#include "ampere/memory/chp.h"
#include "ampere/mesh/test/mock_mesh_port.h"
#include "ampere/sc/testing/test_module.h"

namespace ampere::memory {

MATCHER_P(HIFOpcode, opcode, "")
{
    if (arg->op == opcode)
        return true;

    *result_listener << "opcode mismatch: expected " << opcode << " got " << arg->op;
    return false;
}

MATCHER_P(HIFAddr, addr, "")
{
    if (arg->addr == addr)
        return true;

    *result_listener << "address mismatch: expected " << addr << " got " << arg->addr;
    return false;
}

class CHPTest : public sc::TestModule, public mesh::MeshConfigIfc
{
public:
    CHPTest() : sc::TestModule{}
    {
        get_clock()->run_always(true);

        auto& mem_mesh_port = chp.get_mesh_port();

        mem_mesh_port.tx().req[0].bind(mesh_port.rx.req);
        mem_mesh_port.tx().rsp[0].bind(mesh_port.rx.rsp);
        mem_mesh_port.tx().snp[0].bind(mesh_port.rx.snp);
        mem_mesh_port.tx().dat[0].bind(mesh_port.rx.dat);

        mem_mesh_port.rx().req[0].bind(mesh_port.tx.req);
        mem_mesh_port.rx().rsp[0].bind(mesh_port.tx.rsp);
        mem_mesh_port.rx().snp[0].bind(mesh_port.tx.snp);
        mem_mesh_port.rx().dat[0].bind(mesh_port.tx.dat);

        chp.config.bind(*this);

        chp.hif_rd_cmd.bind(hif_rd_cmd_port);
        chp.hif_wr_cmd.bind(hif_wr_cmd_port);
        chp.hif_dat.bind(hif_dat_port);

        // Default for mock ports: putters can always put, takers can never take
        //
        EXPECT_CALL(mesh_port.rx.rsp, can_put()).WillRepeatedly(Return(true));
        EXPECT_CALL(mesh_port.rx.dat, can_put()).WillRepeatedly(Return(true));

        EXPECT_CALL(mesh_port.tx.req, can_take()).WillRepeatedly(Return(false));
        EXPECT_CALL(mesh_port.tx.dat, can_take()).WillRepeatedly(Return(false));

        EXPECT_CALL(hif_rd_cmd_port, can_put()).WillRepeatedly(Return(true));
        EXPECT_CALL(hif_wr_cmd_port, can_put()).WillRepeatedly(Return(true));

        EXPECT_CALL(hif_dat_port, can_take()).WillRepeatedly(Return(false));
    }

    int data_bus_width() const override { return 512; }
    int channel(const chi::NodeId id) const override { return 0; }

    CHP chp{"chp"};
    MockMeshPort mesh_port;
    MockPutter<HIFCmd> hif_rd_cmd_port;
    MockPutter<HIFCmd> hif_wr_cmd_port;
    MockTaker<HIFDat> hif_dat_port;
};

TEST_F(CHPTest, SingleReadNoSnpSep)
{
    auto req = make<MeshReq>(NodeId{0}, NodeId{1}, 1, chi::RequestOpcode::ReadNoSnpSep, 0x1000_pa);
    req->Order = chi::RequestOrder::RequestAccepted;

    EXPECT_CALL(mesh_port.tx.req, can_take()).WillOnce(Return(true)).WillRepeatedly(Return(false));

    EXPECT_CALL(mesh_port.tx.req, take()).WillOnce(Return(req));

    EXPECT_CALL(hif_rd_cmd_port, can_put()).WillRepeatedly(Return(true));

    auto return_data = [this](HIFCmd rd_cmd) {
        EXPECT_CALL(hif_dat_port, can_take).WillOnce(Return(false)).WillOnce(Return(true)).RetiresOnSaturation();

        EXPECT_CALL(hif_dat_port, take).WillOnce(Return(rd_cmd->id)).RetiresOnSaturation();
    };

    EXPECT_CALL(hif_rd_cmd_port, put(AllOf(HIFOpcode(HIFOp::RD), HIFAddr(0x1000_pa)))).WillOnce(Invoke(return_data));

    EXPECT_CALL(mesh_port.rx.rsp, can_put()).WillRepeatedly(Return(true));

    EXPECT_CALL(mesh_port.rx.dat, can_put()).WillRepeatedly(Return(true));

    EXPECT_CALL(mesh_port.rx.rsp, put(HasOpcode(chi::ResponseOpcode::ReadReceipt)));
    EXPECT_CALL(mesh_port.rx.dat, put(HasOpcode(chi::DataOpcode::DataSepResp)));

    tick(20);
}

TEST_F(CHPTest, SingleReadNoSnp)
{
    auto req = make<MeshReq>(NodeId{0}, NodeId{1}, 1, chi::RequestOpcode::ReadNoSnp, 0x1000_pa);

    EXPECT_CALL(mesh_port.tx.req, can_take()).WillOnce(Return(true)).WillRepeatedly(Return(false));

    EXPECT_CALL(mesh_port.tx.req, take()).WillOnce(Return(req));

    EXPECT_CALL(hif_rd_cmd_port, can_put()).WillRepeatedly(Return(true));

    auto return_data = [this](HIFCmd rd_cmd) {
        EXPECT_CALL(hif_dat_port, can_take).WillOnce(Return(false)).WillOnce(Return(true)).RetiresOnSaturation();

        EXPECT_CALL(hif_dat_port, take).WillOnce(Return(rd_cmd->id)).RetiresOnSaturation();
    };

    EXPECT_CALL(hif_rd_cmd_port, put(AllOf(HIFOpcode(HIFOp::RD), HIFAddr(0x1000_pa)))).WillOnce(Invoke(return_data));

    EXPECT_CALL(mesh_port.rx.rsp, can_put()).WillRepeatedly(Return(true));

    EXPECT_CALL(mesh_port.rx.dat, can_put()).WillRepeatedly(Return(true));

    EXPECT_CALL(mesh_port.rx.dat, put(HasOpcode(chi::DataOpcode::CompData)));

    tick(20);
}

TEST_F(CHPTest, SingleWriteNoSnpFull)
{
    auto req = make<MeshReq>(NodeId{0}, NodeId{1}, 1, chi::RequestOpcode::WriteNoSnpFull, 0x1000_pa);

    EXPECT_CALL(mesh_port.tx.req, can_take()).WillOnce(Return(true)).WillRepeatedly(Return(false));

    EXPECT_CALL(mesh_port.tx.req, take()).WillOnce(Return(req));

    auto pull_data = [this](MeshRsp rsp) {
        EXPECT_CALL(mesh_port.tx.dat, can_take()).WillOnce(Return(true)).RetiresOnSaturation();

        auto dat = make<MeshDat>(NodeId{0}, NodeId{1}, rsp->DBID, chi::DataOpcode::NonCopyBackWrData);

        EXPECT_CALL(mesh_port.tx.dat, take()).WillOnce(Return(dat)).RetiresOnSaturation();
    };

    EXPECT_CALL(mesh_port.rx.rsp, put).WillOnce(Invoke(pull_data)).RetiresOnSaturation();

    EXPECT_CALL(hif_wr_cmd_port, put(AllOf(HIFOpcode(HIFOp::WR), HIFAddr(0x1000_pa))));

    tick(8);
}

TEST_F(CHPTest, SingleWriteNoSnpPtl)
{
    auto req = make<MeshReq>(NodeId{0}, NodeId{1}, 1, chi::RequestOpcode::WriteNoSnpPtl, 0x1000_pa);

    EXPECT_CALL(mesh_port.tx.req, can_take()).WillOnce(Return(true)).WillRepeatedly(Return(false));

    EXPECT_CALL(mesh_port.tx.req, take()).WillOnce(Return(req));

    auto pull_data = [this](MeshRsp rsp) {
        EXPECT_CALL(mesh_port.tx.dat, can_take()).WillOnce(Return(true)).RetiresOnSaturation();

        auto dat = make<MeshDat>(NodeId{0}, NodeId{1}, rsp->DBID, chi::DataOpcode::NonCopyBackWrData);

        EXPECT_CALL(mesh_port.tx.dat, take()).WillOnce(Return(dat)).RetiresOnSaturation();
    };

    auto return_data = [this](HIFCmd rd_cmd) {
        EXPECT_CALL(hif_dat_port, can_take).WillOnce(Return(false)).WillOnce(Return(true)).RetiresOnSaturation();

        EXPECT_CALL(hif_dat_port, take).WillOnce(Return(rd_cmd->id)).RetiresOnSaturation();
    };

    EXPECT_CALL(mesh_port.rx.rsp, put).WillOnce(Invoke(pull_data)).RetiresOnSaturation();

    {
        InSequence seq;

        EXPECT_CALL(hif_rd_cmd_port, put(AllOf(HIFOpcode(HIFOp::RD), HIFAddr(0x1000_pa))))
            .WillOnce(Invoke(return_data));
        EXPECT_CALL(hif_wr_cmd_port, put(AllOf(HIFOpcode(HIFOp::WR), HIFAddr(0x1000_pa))));
    }

    tick(20);
}

TEST_F(CHPTest, ReadAfterWrite)
{
    auto wr = make<MeshReq>(NodeId{0}, NodeId{1}, 1, chi::RequestOpcode::WriteNoSnpFull, 0x1000_pa);
    auto rd = make<MeshReq>(NodeId{0}, NodeId{1}, 2, chi::RequestOpcode::ReadNoSnp, 0x1000_pa);

    EXPECT_CALL(mesh_port.tx.req, can_take())
        .WillOnce(Return(true))
        .WillOnce(Return(true))
        .WillRepeatedly(Return(false));

    EXPECT_CALL(mesh_port.tx.req, take()).WillOnce(Return(wr)).WillOnce(Return(rd));

    auto pull_data = [this](MeshRsp rsp) {
        // Delay the data pull response so that the read would have time to
        // be sent to the HIF interface if the RAW ordering is not working.
        EXPECT_CALL(mesh_port.tx.dat, can_take())
            .WillOnce(Return(false))
            .WillOnce(Return(false))
            .WillOnce(Return(false))
            .WillOnce(Return(false))
            .WillOnce(Return(true))
            .RetiresOnSaturation();

        auto dat = make<MeshDat>(NodeId{0}, NodeId{1}, rsp->DBID, chi::DataOpcode::NonCopyBackWrData);

        EXPECT_CALL(mesh_port.tx.dat, take()).WillOnce(Return(dat)).RetiresOnSaturation();
    };

    EXPECT_CALL(mesh_port.rx.rsp, put).WillOnce(Invoke(pull_data)).RetiresOnSaturation();

    {
        InSequence seq;
        EXPECT_CALL(hif_wr_cmd_port, put(AllOf(HIFOpcode(HIFOp::WR), HIFAddr(0x1000_pa))));
        EXPECT_CALL(hif_rd_cmd_port, put(AllOf(HIFOpcode(HIFOp::RD), HIFAddr(0x1000_pa))));
    }

    tick(10);
}

TEST_F(CHPTest, PrefetchTgt)
{
    auto pf = make<MeshReq>(NodeId{0}, NodeId{1}, 1, chi::RequestOpcode::PrefetchTgt, 0x1000_pa);
    auto rd = make<MeshReq>(NodeId{0}, NodeId{1}, 2, chi::RequestOpcode::ReadNoSnp, 0x1000_pa);

    EXPECT_CALL(mesh_port.tx.req, can_take()).WillOnce(Return(true)).RetiresOnSaturation();

    EXPECT_CALL(mesh_port.tx.req, take()).WillOnce(Return(pf));

    auto return_data = [this, rd](HIFCmd rd_cmd) {
        EXPECT_CALL(hif_dat_port, can_take).WillOnce(Return(false)).WillOnce(Return(true)).RetiresOnSaturation();

        EXPECT_CALL(hif_dat_port, take).WillOnce(Return(rd_cmd->id)).RetiresOnSaturation();

        EXPECT_CALL(mesh_port.tx.req, can_take())
            .WillOnce(Return(false))
            .WillOnce(Return(false))
            .WillOnce(Return(false))
            .WillOnce(Return(true))
            .RetiresOnSaturation();

        EXPECT_CALL(mesh_port.tx.req, take()).WillOnce(Return(rd));
    };

    EXPECT_CALL(hif_rd_cmd_port, put(AllOf(HIFOpcode(HIFOp::RD), HIFAddr(0x1000_pa)))).WillOnce(Invoke(return_data));

    EXPECT_CALL(
        mesh_port.rx.dat,
        put(AllOf(HasOpcode(chi::DataOpcode::CompData), HasTxnID(2), HasDatSource(DataSource::PrefetchTgtUseful))));

    tick(20);
}

TEST_F(CHPTest, PrefetchTgtPending)
{
    auto pf = make<MeshReq>(NodeId{0}, NodeId{1}, 1, chi::RequestOpcode::PrefetchTgt, 0x1000_pa);
    auto rd = make<MeshReq>(NodeId{0}, NodeId{1}, 2, chi::RequestOpcode::ReadNoSnp, 0x1000_pa);

    EXPECT_CALL(mesh_port.tx.req, can_take()).WillOnce(Return(true)).WillOnce(Return(true)).RetiresOnSaturation();

    EXPECT_CALL(mesh_port.tx.req, take()).WillOnce(Return(pf)).WillOnce(Return(rd));

    auto return_data = [this, rd](HIFCmd rd_cmd) {
        EXPECT_CALL(hif_dat_port, can_take).WillOnce(Return(false)).WillOnce(Return(true)).RetiresOnSaturation();

        EXPECT_CALL(hif_dat_port, take).WillOnce(Return(rd_cmd->id)).RetiresOnSaturation();
    };

    EXPECT_CALL(hif_rd_cmd_port, put(AllOf(HIFOpcode(HIFOp::RD), HIFAddr(0x1000_pa)))).WillOnce(Invoke(return_data));

    EXPECT_CALL(
        mesh_port.rx.dat,
        put(AllOf(HasOpcode(chi::DataOpcode::CompData), HasTxnID(2), HasDatSource(DataSource::PrefetchTgtUseful))));

    tick(20);
}

TEST_F(CHPTest, ReadPriority)
{
    auto rd_plat = make<MeshReq>(NodeId{0}, NodeId{1}, 1, chi::RequestOpcode::ReadNoSnp, 0x1000_pa);
    rd_plat->QoS = Qos::PLATINUM;

    auto rd_gold = make<MeshReq>(NodeId{0}, NodeId{1}, 2, chi::RequestOpcode::ReadNoSnp, 0x2000_pa);
    rd_gold->QoS = Qos::GOLD;

    auto rd_iron = make<MeshReq>(NodeId{0}, NodeId{1}, 3, chi::RequestOpcode::ReadNoSnp, 0x3000_pa);
    rd_iron->QoS = Qos::IRON;

    EXPECT_CALL(mesh_port.tx.req, can_take())
        .WillOnce(Return(true))
        .WillOnce(Return(true))
        .WillOnce(Return(true))
        .WillRepeatedly(Return(false));

    // Supply requests in reverse priority order. We'll delay the ability of these
    // requests to schedule by using the HIF port so they build up, at which point
    // they should pop out in priority order.
    //
    EXPECT_CALL(mesh_port.tx.req, take()).WillOnce(Return(rd_iron)).WillOnce(Return(rd_gold)).WillOnce(Return(rd_plat));

    EXPECT_CALL(hif_rd_cmd_port, can_put())
        .WillOnce(Return(false))
        .WillOnce(Return(false))
        .WillOnce(Return(false))
        .WillOnce(Return(false))
        .RetiresOnSaturation();

    {
        InSequence seq;
        EXPECT_CALL(hif_rd_cmd_port, put(AllOf(HIFOpcode(HIFOp::RD), HIFAddr(0x1000_pa))));
        EXPECT_CALL(hif_rd_cmd_port, put(AllOf(HIFOpcode(HIFOp::RD), HIFAddr(0x2000_pa))));
        EXPECT_CALL(hif_rd_cmd_port, put(AllOf(HIFOpcode(HIFOp::RD), HIFAddr(0x3000_pa))));
    }

    tick(10);
}

} // namespace ampere::memory
