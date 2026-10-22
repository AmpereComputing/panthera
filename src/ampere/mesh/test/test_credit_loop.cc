//
// Copyright (c) 2025 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//
#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "ampere/mesh/cpx.h"
#include "ampere/mesh/mesh_config.h"
#include "ampere/mesh/mesh_module.h"
#include "ampere/mesh/test/dummy_requester.h"
#include "ampere/mesh/test/mock_addr_map.h"
#include "ampere/mesh/test/mock_mesh_port.h"
#include "ampere/sc/testing/test_module.h"

using namespace ampere::mesh;
using namespace ampere::sc;

class DummyCore : public MeshModule
{
public:
    DummyCore(const sc_core::sc_module_name& name, Clock* clock) : MeshModule{name, clock} {}

    MeshPort mesh_port{"mesh_port"};

    bool run() override
    {
        if (!sent_request) {
            if (mesh_port.tx().req[0]->can_put()) {
                auto req = trans_make<MeshReq>(NodeId{NodeId::DieType::CPU, 0, 0, 0},
                                               NodeId{NodeId::DieType::CPU, 0, 0, 1},
                                               0,
                                               chi::RequestOpcode::ReadShared,
                                               0xaced0000_pa);
                mesh_port.tx().req[0]->put(req);
                sent_request = true;
            }
        } else {
            ++clock_count;
            if (mesh_port.tx().req[0]->can_put())
                return false;
        }

        return true;
    }

    bool sent_request{false};
    int clock_count{0};
};

class TestCreditLoop : public TestModule, public MeshConfigIfc
{
protected:
    TestCreditLoop()
        : TestModule{
              {"cpx.n", 1},
              {"cpx.dev_0.req_in.size", 1},
              {"~core2cpx.*rx.*\\.delay", "1.333ns"},
              {"~core2cpx.*tx.*\\.delay", "1.166ns"},
          }
    {
        core.mesh_port.connect(cpx.device_port[0], "core2cpx");
        cpx.mesh_port.tx().req[0].bind(rtr.rx.req);
        cpx.mesh_port.tx().rsp[0].bind(rtr.rx.rsp);
        cpx.mesh_port.tx().rsp[1].bind(rtr.rx.rsp1);
        cpx.mesh_port.tx().snp[0].bind(rtr.rx.snp);
        cpx.mesh_port.tx().dat[0].bind(rtr.rx.dat);
        cpx.mesh_port.rx().req[0].bind(rtr.tx.req);
        cpx.mesh_port.rx().rsp[0].bind(rtr.tx.rsp);
        cpx.mesh_port.rx().rsp[1].bind(rtr.tx.rsp1);
        cpx.mesh_port.rx().snp[0].bind(rtr.tx.snp);
        cpx.mesh_port.rx().dat[0].bind(rtr.tx.dat);

        core.config.bind(*this);
        cpx.config.bind(*this);
    }

    Clock mesh_clock{"mesh_clock", 500_ps};
    Clock core_clock{"core_clock", 333_ps};

    DummyCore core{"core", &core_clock};
    CPX cpx{"cpx", &mesh_clock};
    MockMeshPort rtr;

    int data_bus_width() const override { return 512; }
    int channel(const NodeId id) const override { return 0; }
};

TEST_F(TestCreditLoop, MeasureCoreToMeshRequestLoop)
{
    EXPECT_CALL(rtr.rx.req, can_put).WillRepeatedly(Return(true));

    EXPECT_CALL(rtr.rx.req, put(_));

    sc_core::sc_start(10_ns);
    EXPECT_EQ(core.clock_count, 10);
}

class DummyReceiverCore : public MeshModule
{
public:
    DummyReceiverCore(const sc_core::sc_module_name& name, Clock* clock) : MeshModule{name, clock} {}

    MeshPort mesh_port{"mesh_port"};

    bool run() override
    {
        if (mesh_port.rx().snp[0]->can_take()) {
            mesh_port.rx().snp[0]->take();
            received_at.push_back(get_cycle());
        }

        return true;
    }

    float mean_interval() const
    {
        Cycle last_at = 0;
        float interval_sum = 0;
        float interval_count = 0;

        for (auto at : received_at) {
            if (last_at != 0) {
                interval_sum += static_cast<float>(at - last_at);
                interval_count++;
            }
            last_at = at;
        }

        return interval_sum / interval_count;
    }

    std::vector<Cycle> received_at;
};

class DummySenderMesh : public MeshModule
{
public:
    DummySenderMesh(const sc_core::sc_module_name& name, Clock* clock) : MeshModule{name, clock} {}

    MeshPort mesh_port{"mesh_port"};

    bool run() override
    {
        if (mesh_port.tx().snp[0]->can_put()) {
            auto snp = trans_make<MeshSnp>(NodeId{NodeId::DieType::CPU, 0, 0, 0},
                                           NodeId{NodeId::DieType::CPU, 0, 0, 1},
                                           0,
                                           chi::SnoopOpcode::SnpStashShared,
                                           0xaced0000_pa);
            mesh_port.tx().snp[0]->put(snp);
        }

        if (++count > 10)
            return false;

        return true;
    }

    int count{0};
};

class TestMeshToCoreCreditLoop : public TestModule, public MeshConfigIfc
{
protected:
    TestMeshToCoreCreditLoop()
        : TestModule{{"cpx.n", 1},
                     {"cpx.mesh_snp_in.size", 1},
                     {"~core2cpx.*rx.*\\.delay", "1.333ns"},
                     {"~core2cpx.*tx.*\\.delay", "1.166ns"},
                     {"~core2cpx_snp_tx_0.capacity", 1},
                     {"~mesh2cpx.*tx.*\\.latency", 0}}
    {
        core.mesh_port.connect(cpx.device_port[0], "core2cpx");
        rtr.mesh_port.connect(cpx.mesh_port, "mesh2cpx");

        core.config.bind(*this);
        cpx.config.bind(*this);
        rtr.config.bind(*this);
    }

    Clock mesh_clock{"mesh_clock", 500_ps};
    Clock core_clock{"core_clock", 333_ps};

    DummyReceiverCore core{"core", &core_clock};
    CPX cpx{"cpx", &mesh_clock};
    DummySenderMesh rtr{"rtr", &mesh_clock};

    int data_bus_width() const override { return 512; }
    int channel(const NodeId id) const override { return 0; }
};

TEST_F(TestMeshToCoreCreditLoop, MeasureMeshToCoreRequestLoop)
{
    sc_core::sc_start(30_ns);
    EXPECT_NEAR(core.mean_interval(), 9.0, 0.5);
}
