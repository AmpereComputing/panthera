//
// Copyright (c) 2021 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//
#include "ampere/memory/memory.h"
#include "ampere/mesh/mesh_config.h"
#include "ampere/mesh/test/mock_mesh_port.h"
#include "ampere/sc/testing/test_module.h"

namespace ampere::memory {

class MemoryTest : public sc::TestModule, public mesh::MeshConfigIfc
{
public:
    MemoryTest()
        : sc::TestModule{{{"memory.bw_latency_data", "[{\"write_percentage\": 1.1, \"bw_latency\": [[100, 5]]}]"_json}}}
    {
        bind_ports();
    }

    MemoryTest(std::initializer_list<std::pair<std::string, sc::json>> settings) : sc::TestModule(settings)
    {
        bind_ports();
    }

    void bind_ports()
    {
        auto& mem_mesh_port = memory.get_mesh_port();

        mem_mesh_port.tx().req[0].bind(mesh_port.rx.req);
        mem_mesh_port.tx().rsp[0].bind(mesh_port.rx.rsp);
        mem_mesh_port.tx().snp[0].bind(mesh_port.rx.snp);
        mem_mesh_port.tx().dat[0].bind(mesh_port.rx.dat);

        mem_mesh_port.rx().req[0].bind(mesh_port.tx.req);
        mem_mesh_port.rx().rsp[0].bind(mesh_port.tx.rsp);
        mem_mesh_port.rx().snp[0].bind(mesh_port.tx.snp);
        mem_mesh_port.rx().dat[0].bind(mesh_port.tx.dat);

        memory.config.bind(*this);
    }

    int data_bus_width() const override { return 512; }
    int channel(const chi::NodeId id) const override { return 0; }

    Memory memory{"memory"};
    NiceMock<MockMeshPort> mesh_port;
};

TEST_F(MemoryTest, CanRead)
{
    auto req = make<MeshReq>(NodeId{0}, NodeId{1}, 1, chi::RequestOpcode::ReadNoSnp, 0x1000_pa);
    Cycle comp_cycle{0};

    EXPECT_CALL(mesh_port.tx.req, can_take()).WillOnce(Return(true)).WillRepeatedly(Return(false));

    EXPECT_CALL(mesh_port.tx.req, take()).WillOnce(Return(req));

    EXPECT_CALL(mesh_port.rx.dat, can_put()).WillRepeatedly(Return(true));

    auto save_current_cycle = [this, &comp_cycle] {
        comp_cycle = get_cycle();
    };

    EXPECT_CALL(mesh_port.rx.dat, put(HasOpcode(chi::DataOpcode::CompData)))
        .WillOnce(InvokeWithoutArgs(save_current_cycle));

    tick(20);

    auto min_read_delay = sc_core::sc_time{5, sc_core::SC_NS};
    auto actual_delay = comp_cycle * get_clock()->get_period();
    EXPECT_GE(actual_delay, min_read_delay);
}

TEST_F(MemoryTest, CanWrite)
{
    auto req = make<MeshReq>(NodeId{0}, NodeId{1}, 1, chi::RequestOpcode::WriteNoSnpFull, 0x1000_pa);

    EXPECT_CALL(mesh_port.tx.req, can_take()).WillOnce(Return(true)).WillRepeatedly(Return(false));

    EXPECT_CALL(mesh_port.tx.req, take()).WillOnce(Return(req));

    EXPECT_CALL(mesh_port.rx.dat, can_put()).WillRepeatedly(Return(true));

    auto send_data = [this](MeshRsp rsp) {
        auto dat = make_clone<MeshDat>(rsp, NodeId{0}, NodeId{1}, rsp->DBID, chi::DataOpcode::CopyBackWrData);

        EXPECT_CALL(mesh_port.tx.dat, can_take()).WillOnce(Return(true)).WillRepeatedly(Return(false));

        EXPECT_CALL(mesh_port.tx.dat, take()).WillOnce(Return(dat));
    };

    EXPECT_CALL(mesh_port.rx.rsp, can_put()).WillOnce(Return(true)).WillRepeatedly(Return(false));

    EXPECT_CALL(mesh_port.rx.rsp, put(HasOpcode(chi::ResponseOpcode::CompDBIDResp))).WillOnce(Invoke(send_data));

    tick(20);
}

class MemoryRetryTest : public sc::TestModule, public mesh::MeshConfigIfc
{
public:
    MemoryRetryTest()
        : sc::TestModule{{{"memory.bw_latency_data", "[{\"write_percentage\": 1.1, \"bw_latency\": [[100, 5]]}]"_json},
                          {"memory.max_reads", 1},
                          {"memory.enable_retry", true}}}
    {
        bind_ports();
        get_clock()->run_always(true);
    }

    void bind_ports()
    {
        auto& mem_mesh_port = memory.get_mesh_port();

        mem_mesh_port.tx().req[0].bind(mesh_port.rx.req);
        mem_mesh_port.tx().rsp[0].bind(mesh_port.rx.rsp);
        mem_mesh_port.tx().snp[0].bind(mesh_port.rx.snp);
        mem_mesh_port.tx().dat[0].bind(mesh_port.rx.dat);

        mem_mesh_port.rx().req[0].bind(mesh_port.tx.req);
        mem_mesh_port.rx().rsp[0].bind(mesh_port.tx.rsp);
        mem_mesh_port.rx().snp[0].bind(mesh_port.tx.snp);
        mem_mesh_port.rx().dat[0].bind(mesh_port.tx.dat);

        memory.config.bind(*this);
    }

    int data_bus_width() const override { return 512; }
    int channel(const chi::NodeId id) const override { return 0; }

    Memory memory{"memory"};
    NiceMock<MockMeshPort> mesh_port;
};

TEST_F(MemoryRetryTest, Retry)
{
    auto req1 = make<MeshReq>(NodeId{0}, NodeId{1}, 1, chi::RequestOpcode::ReadNoSnp, 0x1000_pa);
    auto req2 = make<MeshReq>(NodeId{0}, NodeId{1}, 2, chi::RequestOpcode::ReadNoSnp, 0x1000_pa);

    EXPECT_CALL(mesh_port.tx.req, can_take())
        .WillOnce(Return(true))
        .WillOnce(Return(true))
        .WillRepeatedly(Return(false));

    EXPECT_CALL(mesh_port.tx.req, take()).WillOnce(Return(req1)).WillOnce(Return(req2));

    EXPECT_CALL(mesh_port.rx.rsp, can_put()).WillRepeatedly(Return(true));

    auto resend_second_request = [this, req2](MeshRsp rsp) {
        EXPECT_CALL(mesh_port.tx.req, can_take()).WillOnce(Return(true)).RetiresOnSaturation();

        req2->AllowRetry = false;
        req2->PCrdType = rsp->PCrdType;

        EXPECT_CALL(mesh_port.tx.req, take()).WillOnce(Return(req2));
    };

    {
        InSequence seq;
        EXPECT_CALL(mesh_port.rx.rsp, put(AllOf(HasOpcode(chi::ResponseOpcode::RetryAck), HasTxnID(2))));
        EXPECT_CALL(mesh_port.rx.rsp, put(AllOf(HasOpcode(chi::ResponseOpcode::PCrdGrant), HasTxnID(2))))
            .WillOnce(Invoke(resend_second_request));
    }

    EXPECT_CALL(mesh_port.rx.dat, can_put()).WillRepeatedly(Return(true));

    EXPECT_CALL(mesh_port.rx.dat, put(HasTxnID(1)));
    EXPECT_CALL(mesh_port.rx.dat, put(HasTxnID(2)));

    tick(20);
}

class MemoryRequestLatencyAcrossWindowsTest : public sc::TestModule, public mesh::MeshConfigIfc
{
public:
    MemoryRequestLatencyAcrossWindowsTest()
        : sc::TestModule{
              {{"memory.bw_latency_data",
                "[{\"write_percentage\": 1.0, \"bw_latency\": [[0, 19.5], [1.4, 68.56], [1.5, 127.86], [1.3, 367.12]]}]"_json},
               {"memory.use_request_latency_curve", true},
               {"memory.max_reads", 64},
               {"memory.bw_latency_window_count", 1.0},
               {"memory.bw_latency_window", 4.0},
               {"memory.clock_period", "1.0ns"},
               {"memory.enable_retry", false}}}
    {
        bind_ports();
        get_clock()->run_always(true);
    }

    void bind_ports()
    {
        auto& mem_mesh_port = memory.get_mesh_port();

        mem_mesh_port.tx().req[0].bind(mesh_port.rx.req);
        mem_mesh_port.tx().rsp[0].bind(mesh_port.rx.rsp);
        mem_mesh_port.tx().snp[0].bind(mesh_port.rx.snp);
        mem_mesh_port.tx().dat[0].bind(mesh_port.rx.dat);

        mem_mesh_port.rx().req[0].bind(mesh_port.tx.req);
        mem_mesh_port.rx().rsp[0].bind(mesh_port.tx.rsp);
        mem_mesh_port.rx().snp[0].bind(mesh_port.tx.snp);
        mem_mesh_port.rx().dat[0].bind(mesh_port.tx.dat);

        memory.config.bind(*this);
    }

    int data_bus_width() const override { return 512; }
    int channel(const chi::NodeId id) const override { return 0; }

    Memory memory{"memory"};
    NiceMock<MockMeshPort> mesh_port;
};

TEST_F(MemoryRequestLatencyAcrossWindowsTest, BandwidthPeak)
{
    auto req1 = make<MeshReq>(NodeId{0}, NodeId{1}, 1, chi::RequestOpcode::ReadNoSnp, 0x1000_pa);
    auto req2 = make<MeshReq>(NodeId{0}, NodeId{1}, 2, chi::RequestOpcode::ReadNoSnp, 0x2000_pa);
    auto req3 = make<MeshReq>(NodeId{0}, NodeId{1}, 3, chi::RequestOpcode::ReadNoSnp, 0x3000_pa);

    EXPECT_CALL(mesh_port.tx.req, can_take())
        .WillOnce(Return(true))
        .WillOnce(Return(false))
        .WillOnce(Return(true))
        .WillOnce(Return(false))
        .WillOnce(Return(true))
        .WillRepeatedly(Return(false));

    EXPECT_CALL(mesh_port.tx.req, take()).WillOnce(Return(req1)).WillOnce(Return(req2)).WillOnce(Return(req3));

    tick(10);

    float instantaneous_bandwidth = 192.0f / memory.get_latency();

    EXPECT_NEAR(instantaneous_bandwidth, 1.5, 0.1);
}

TEST_F(MemoryRequestLatencyAcrossWindowsTest, BandwidthRegresses)
{
    auto req1 = make<MeshReq>(NodeId{0}, NodeId{1}, 1, chi::RequestOpcode::ReadNoSnp, 0x1000_pa);
    auto req2 = make<MeshReq>(NodeId{0}, NodeId{1}, 2, chi::RequestOpcode::ReadNoSnp, 0x2000_pa);
    auto req3 = make<MeshReq>(NodeId{0}, NodeId{1}, 3, chi::RequestOpcode::ReadNoSnp, 0x3000_pa);
    auto req4 = make<MeshReq>(NodeId{0}, NodeId{1}, 4, chi::RequestOpcode::ReadNoSnp, 0x4000_pa);

    EXPECT_CALL(mesh_port.tx.req, can_take())
        .WillOnce(Return(true))
        .WillOnce(Return(false))
        .WillOnce(Return(true))
        .WillOnce(Return(false))
        .WillOnce(Return(true))
        .WillOnce(Return(false))
        .WillOnce(Return(true))
        .WillRepeatedly(Return(false));

    EXPECT_CALL(mesh_port.tx.req, take())
        .WillOnce(Return(req1))
        .WillOnce(Return(req2))
        .WillOnce(Return(req3))
        .WillOnce(Return(req4));

    tick(10);

    float instantaneous_bandwidth = 256.0f / memory.get_latency();

    EXPECT_LT(instantaneous_bandwidth, 1.41);
}

class MemoryRequestLatencyWithinWindowTest : public sc::TestModule, public mesh::MeshConfigIfc
{
public:
    MemoryRequestLatencyWithinWindowTest()
        : sc::TestModule{
              {{"memory.bw_latency_data",
                "[{\"write_percentage\": 1.0, \"bw_latency\": [[0, 19.5], [1.4, 68.56], [1.5, 127.86], [1.3, 367.12]]}]"_json},
               {"memory.use_request_latency_curve", true},
               {"memory.max_reads", 64},
               {"memory.bw_latency_window_count", 1.0},
               {"memory.bw_latency_window", 16.0},
               {"memory.clock_period", "1.0ns"},
               {"memory.enable_retry", false}}}
    {
        bind_ports();
        get_clock()->run_always(true);
    }

    void bind_ports()
    {
        auto& mem_mesh_port = memory.get_mesh_port();

        mem_mesh_port.tx().req[0].bind(mesh_port.rx.req);
        mem_mesh_port.tx().rsp[0].bind(mesh_port.rx.rsp);
        mem_mesh_port.tx().snp[0].bind(mesh_port.rx.snp);
        mem_mesh_port.tx().dat[0].bind(mesh_port.rx.dat);

        mem_mesh_port.rx().req[0].bind(mesh_port.tx.req);
        mem_mesh_port.rx().rsp[0].bind(mesh_port.tx.rsp);
        mem_mesh_port.rx().snp[0].bind(mesh_port.tx.snp);
        mem_mesh_port.rx().dat[0].bind(mesh_port.tx.dat);

        memory.config.bind(*this);
    }

    int data_bus_width() const override { return 512; }
    int channel(const chi::NodeId id) const override { return 0; }

    Memory memory{"memory"};
    NiceMock<MockMeshPort> mesh_port;
};
TEST_F(MemoryRequestLatencyWithinWindowTest, BandwidthPeak)
{
    auto req1 = make<MeshReq>(NodeId{0}, NodeId{1}, 1, chi::RequestOpcode::ReadNoSnp, 0x1000_pa);
    auto req2 = make<MeshReq>(NodeId{0}, NodeId{1}, 2, chi::RequestOpcode::ReadNoSnp, 0x2000_pa);
    auto req3 = make<MeshReq>(NodeId{0}, NodeId{1}, 3, chi::RequestOpcode::ReadNoSnp, 0x3000_pa);
    auto req4 = make<MeshReq>(NodeId{0}, NodeId{1}, 4, chi::RequestOpcode::ReadNoSnp, 0x4000_pa);

    {
        InSequence seq;
        EXPECT_CALL(mesh_port.tx.req, can_take()).Times(14).WillRepeatedly(Return(false));

        EXPECT_CALL(mesh_port.tx.req, can_take())
            .WillOnce(Return(true))
            .WillOnce(Return(false))
            .WillOnce(Return(true))
            .WillOnce(Return(false))
            .WillOnce(Return(true))
            .WillOnce(Return(false));

        EXPECT_CALL(mesh_port.tx.req, can_take()).Times(14).WillRepeatedly(Return(false));

        EXPECT_CALL(mesh_port.tx.req, can_take()).WillOnce(Return(true)).WillRepeatedly(Return(false));
    }

    EXPECT_CALL(mesh_port.tx.req, take())
        .WillOnce(Return(req1))
        .WillOnce(Return(req2))
        .WillOnce(Return(req3))
        .WillOnce(Return(req4));

    tick(33);

    // The "within" window test has one extra request for 1 of 16 window cycles
    // leading to 64 byte-cycles / 16 cycles -> 4 extra effective request bytes
    float instantaneous_bandwidth = 196.0f / memory.get_latency();

    EXPECT_NEAR(instantaneous_bandwidth, 1.49, 0.1);
}

TEST_F(MemoryRequestLatencyWithinWindowTest, BandwidthRegresses)
{
    auto req1 = make<MeshReq>(NodeId{0}, NodeId{1}, 1, chi::RequestOpcode::ReadNoSnp, 0x1000_pa);
    auto req2 = make<MeshReq>(NodeId{0}, NodeId{1}, 2, chi::RequestOpcode::ReadNoSnp, 0x2000_pa);
    auto req3 = make<MeshReq>(NodeId{0}, NodeId{1}, 3, chi::RequestOpcode::ReadNoSnp, 0x3000_pa);
    auto req4 = make<MeshReq>(NodeId{0}, NodeId{1}, 4, chi::RequestOpcode::ReadNoSnp, 0x4000_pa);
    auto req5 = make<MeshReq>(NodeId{0}, NodeId{1}, 5, chi::RequestOpcode::ReadNoSnp, 0x5000_pa);

    {
        InSequence seq;
        EXPECT_CALL(mesh_port.tx.req, can_take()).Times(13).WillRepeatedly(Return(false));

        EXPECT_CALL(mesh_port.tx.req, can_take())
            .WillOnce(Return(true))
            .WillOnce(Return(false))
            .WillOnce(Return(true))
            .WillOnce(Return(false))
            .WillOnce(Return(true))
            .WillOnce(Return(false))
            .WillOnce(Return(true))
            .WillOnce(Return(false));

        EXPECT_CALL(mesh_port.tx.req, can_take()).Times(14).WillRepeatedly(Return(false));

        EXPECT_CALL(mesh_port.tx.req, can_take()).WillOnce(Return(true)).WillRepeatedly(Return(false));
    }

    EXPECT_CALL(mesh_port.tx.req, take())
        .WillOnce(Return(req1))
        .WillOnce(Return(req2))
        .WillOnce(Return(req3))
        .WillOnce(Return(req4))
        .WillOnce(Return(req5));

    tick(33);

    // The "within" window test has one extra request for 1 of 16 window cycles
    // leading to 64 byte-cycles / 16 cycles -> 4 extra effective request bytes
    float instantaneous_bandwidth = 260.0f / memory.get_latency();

    EXPECT_LT(instantaneous_bandwidth, 1.41);
}

class MemoryCHPTest : public MemoryTest
{
public:
    MemoryCHPTest()
        : MemoryTest({{"memory.use_chp", true},
                      {"memory.bw_latency_data", R"([{"write_percentage": 1.1, "bw_latency": [[100, 5]]}])"_json}})
    {
    }
};

TEST_F(MemoryCHPTest, CanRead)
{
    auto req = make<MeshReq>(NodeId{0}, NodeId{1}, 1, chi::RequestOpcode::ReadNoSnp, 0x1000_pa);
    Cycle comp_cycle{0};

    EXPECT_CALL(mesh_port.tx.req, can_take()).WillOnce(Return(true)).WillRepeatedly(Return(false));

    EXPECT_CALL(mesh_port.tx.req, take()).WillOnce(Return(req));

    EXPECT_CALL(mesh_port.rx.dat, can_put()).WillRepeatedly(Return(true));

    auto save_current_cycle = [this, &comp_cycle] {
        comp_cycle = get_cycle();
    };

    EXPECT_CALL(mesh_port.rx.dat, put(HasOpcode(chi::DataOpcode::CompData)))
        .WillOnce(InvokeWithoutArgs(save_current_cycle));

    tick(20);

    auto min_read_delay = sc_core::sc_time{5, sc_core::SC_NS};
    auto actual_delay = comp_cycle * get_clock()->get_period();
    EXPECT_GE(actual_delay, min_read_delay);
}

} // namespace ampere::memory
