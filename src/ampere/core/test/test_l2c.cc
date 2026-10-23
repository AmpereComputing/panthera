//
// Copyright (c) 2018-2019 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//

#include "ampere/archx/addr.h"
#include "ampere/core/l2/l2c.h"
#include "ampere/core/l2/l2c_reqqueue.h"
#include "ampere/core/l2/l2c_reqqueue_prefetch.h"
#include "ampere/core/testing/mock_l2.h"
#include "ampere/core/testing/mock_prefetcher.h"
#include "ampere/mesh/test/mock_mesh_port.h"
#include "ampere/mesh/test/mock_addr_map.h"
#include "ampere/core/testing/mock_indexer.h"
#include "ampere/archx/testing/mock_driver.h"
#include "ampere/sc/testing/test_module.h"
#include "ampere/uarch_common/memop.h"
#include "ampere/uarch_common/memreq.h"
#include "ampere/uarch_common/memresp.h"
#include "gmock/gmock-spec-builders.h"

namespace ampere {
namespace uarch_common {

void PrintTo(const MemReq& req, std::ostream* os)
{
    *os << "(id: " << req->id << ", addr: 0x" << req->phys_addr << ")";
}

void PrintTo(const MemResp& resp, std::ostream* os)
{
    *os << "(id: " << resp->id << ", addr: 0x" << resp->phys_addr << ", value: " << resp->value << ")";
}
} // namespace uarch_common

namespace test_L2C {

using namespace core;
using namespace mesh;
using namespace ::testing;
using mesh::MockMeshPort;
using ::testing::InSequence;

class L2CTest : public TestModule, public mesh::MeshConfigIfc
{
public:
    L2CTest()
        : TestModule({{"~l2c\\.banks\\..*fb_credits_[0-9]+\\.size", 8},
                      {"l2c.cbusy.throttle_max", 64},
                      {"l2c.sam.tgt_lut", "[0]"_json}})
    {
        bind_ports();
    }

    L2CTest(std::initializer_list<std::pair<std::string, sc::json>> settings) : TestModule(settings) { bind_ports(); }

    void bind_ports()
    {
        l2c.immu_request.bind(immu_req_port);
        l2c.immu_response.bind(immu_resp_port);
        l2c.icf_request.bind(icf_req_port);
        l2c.icf_response.bind(icf_resp_port);
        l2c.icf_snoop_request.bind(icf_snoop_port);
        l2c.dmmu_request.bind(dmmu_req_port);
        l2c.dmmu_response.bind(dmmu_resp_port);
        l2c.lsu_read_request.bind(lsu_read_req_port);
        l2c.lsu_read_response.bind(lsu_read_resp_port);
        l2c.lsu_snoop_request.bind(lsu_snoop_request_port);
        l2c.lsu_write_request.bind(lsu_write_req_port);
        l2c.lsu_prefetch_request.bind(lsu_prefetch_req_port);
        l2c.lsu_evict_request.bind(lsu_evict_req_port);
        l2c.lsu_has_barrier_in.bind(lsu_has_barrier_in_port);
        l2c.l2_no_pending_stores_out.bind(l2_barrier_out_port);
        l2c.l2_barrier_out.bind(l2_barrier_out_port);
        l2c.l2_store_release_out.bind(l2_barrier_out_port);
        l2c.l2_store_exclusive_out.bind(l2_barrier_out_port);
        l2c.l2_miss_out.bind(l2_miss_out_port);
        l2c.l2_miss_out_to_icf.bind(l2_miss_out_to_icf_port);
        l2c.l1d_lookup.bind(mock_lookup);
        l2c.l1i_lookup.bind(mock_lookup);
        l2c.oracle_driver.bind(driver);
        l2c.real_driver.bind(driver);

        l2c.mesh_port.tx().req[0].bind(mesh_port.rx.req);
        l2c.mesh_port.tx().rsp[0].bind(mesh_port.rx.rsp);
        l2c.mesh_port.tx().snp[0].bind(mesh_port.rx.snp);
        l2c.mesh_port.tx().dat[0].bind(mesh_port.rx.dat);

        l2c.mesh_port.rx().req[0].bind(mesh_port.tx.req);
        l2c.mesh_port.rx().rsp[0].bind(mesh_port.tx.rsp);
        l2c.mesh_port.rx().snp[0].bind(mesh_port.tx.snp);
        l2c.mesh_port.rx().dat[0].bind(mesh_port.tx.dat);

        l2c.config.bind(*this);
        l2c.sam->system_address_map.bind(system_address_map);
    }

    template<typename... Args>
    std::pair<MemReq, MemResp> make_req_resp(uint64_t id, PhysAddr addr, Args&&... args)
    {
        auto req = make<MemReq>(id, addr, std::forward<Args>(args)...);
        auto resp = make<MemResp>(id, addr, true);
        return {req, resp};
    }

    template<typename... Args>
    std::tuple<MemReq, MemResp, MemResp> make_req_miss_resp(uint64_t id, PhysAddr addr, Args&&... args)
    {
        auto [req, hit_resp] = make_req_resp(id, addr, std::forward<Args>(args)...);
        auto miss_resp = make<MemResp>(id, addr, false);
        return {req, miss_resp, hit_resp};
    }

    int data_bus_width() const override { return 256; }
    int channel(const chi::NodeId id) const override { return 0; }

    L2C l2c{"l2c"};

    using MockMemReqAndVictimReader = MockTaker<uarch_common::Request::Fill>;

    NiceMock<MockTaker<MemReq>> immu_req_port;
    NiceMock<MockPutter<MemResp>> immu_resp_port;
    NiceMock<MockMemReqAndVictimReader> icf_req_port;
    NiceMock<MockPutter<MemResp>> icf_resp_port;
    NiceMock<MockPutter<MemReq>> icf_snoop_port;
    NiceMock<MockTaker<MemReq>> dmmu_req_port;
    NiceMock<MockPutter<MemResp>> dmmu_resp_port;
    NiceMock<MockMemReqAndVictimReader> lsu_read_req_port;
    NiceMock<MockPutter<MemResp>> lsu_read_resp_port;
    NiceMock<MockPutter<MemReq>> lsu_snoop_request_port;
    NiceMock<MockTaker<MemReq>> lsu_write_req_port;
    NiceMock<MockTaker<MemReq>> lsu_prefetch_req_port;
    NiceMock<MockTaker<MemReq>> lsu_evict_req_port;
    NiceMock<MockTaker<bool>> lsu_has_barrier_in_port;
    NiceMock<MockPutter<bool>> l2_barrier_out_port;
    NiceMock<MockPutter<PhysAddr>> l2_miss_out_port;
    NiceMock<MockPutter<PhysAddr>> l2_miss_out_to_icf_port;
    NiceMock<MockCacheLookup> mock_lookup;
    NiceMock<MockMeshPort> mesh_port;
    MockDriver driver;
    MockSystemAddressMap system_address_map{"system_address_map"};

    void SetUp() override
    {
        // Establish some defaults: putters are always puttable, takers are never takeable
        EXPECT_CALL(immu_req_port, can_take()).WillRepeatedly(Return(false));
        EXPECT_CALL(icf_req_port, can_take()).WillRepeatedly(Return(false));
        EXPECT_CALL(dmmu_req_port, can_take()).WillRepeatedly(Return(false));
        EXPECT_CALL(lsu_read_req_port, can_take()).WillRepeatedly(Return(false));
        EXPECT_CALL(lsu_write_req_port, can_take()).WillRepeatedly(Return(false));
        EXPECT_CALL(lsu_has_barrier_in_port, can_take()).WillRepeatedly(Return(false));

        EXPECT_CALL(immu_resp_port, can_put()).WillRepeatedly(Return(true));
        EXPECT_CALL(icf_resp_port, can_put()).WillRepeatedly(Return(true));
        EXPECT_CALL(dmmu_resp_port, can_put()).WillRepeatedly(Return(true));
        EXPECT_CALL(lsu_read_resp_port, can_put()).WillRepeatedly(Return(true));

        EXPECT_CALL(mesh_port.rx.req, can_put()).WillRepeatedly(Return(true));
        EXPECT_CALL(mesh_port.rx.rsp, can_put()).WillRepeatedly(Return(true));
        EXPECT_CALL(mesh_port.rx.dat, can_put()).WillRepeatedly(Return(true));

        EXPECT_CALL(mesh_port.tx.rsp, can_take()).WillRepeatedly(Return(false));
        EXPECT_CALL(mesh_port.tx.snp, can_take()).WillRepeatedly(Return(false));
        EXPECT_CALL(mesh_port.tx.dat, can_take()).WillRepeatedly(Return(false));
    }

    std::function<void(mesh::MeshReq req)> return_data(uint16_t dbid, mesh::chi::FinalState state)
    {
        return [this, dbid, state](mesh::MeshReq req) {
            auto dat1 = make<mesh::MeshDat>(req->TxnID, mesh::DataOpcode::CompData);
            dat1->Resp = mesh::chi::ResponseStatus{mesh::chi::ResponseStatus::Type::Comp, state};
            dat1->DBID = dbid;
            dat1->DataID = 0;

            auto dat2 = make<mesh::MeshDat>(req->TxnID, mesh::DataOpcode::CompData);
            dat2->Resp = mesh::chi::ResponseStatus{mesh::chi::ResponseStatus::Type::Comp, state};
            dat2->DBID = dbid;
            dat2->DataID = 2;

            EXPECT_CALL(mesh_port.tx.dat, can_take())
                .WillOnce(Return(true))
                .WillOnce(Return(true))
                .RetiresOnSaturation();

            EXPECT_CALL(mesh_port.tx.dat, take()).WillOnce(Return(dat1)).WillOnce(Return(dat2)).RetiresOnSaturation();

            EXPECT_CALL(mesh_port.rx.rsp, put(AllOf(HasTxnID(dbid), HasOpcode(mesh::ResponseOpcode::CompAck))))
                .RetiresOnSaturation();
        };
    }

    std::function<void(mesh::MeshReq req)> return_data_sep(uint16_t dbid, mesh::chi::FinalState state)
    {
        return [this, dbid, state](mesh::MeshReq req) {
            auto rsp = make<mesh::MeshRsp>(NodeId{1}, req->SrcID, req->TxnID, mesh::ResponseOpcode::RespSepData);
            rsp->Resp = mesh::chi::ResponseStatus{mesh::chi::ResponseStatus::Type::Comp, state};
            rsp->DBID = dbid;

            EXPECT_CALL(mesh_port.tx.rsp, can_take()).WillOnce(Return(true)).RetiresOnSaturation();

            EXPECT_CALL(mesh_port.tx.rsp, take()).WillOnce(Return(rsp)).RetiresOnSaturation();

            auto dat1 = make<mesh::MeshDat>(req->TxnID, mesh::DataOpcode::DataSepResp);
            dat1->Resp = mesh::chi::ResponseStatus{mesh::chi::ResponseStatus::Type::Comp, state};
            dat1->DBID = dbid + 1;
            dat1->DataID = 0;

            auto dat2 = make<mesh::MeshDat>(req->TxnID, mesh::DataOpcode::DataSepResp);
            dat2->Resp = mesh::chi::ResponseStatus{mesh::chi::ResponseStatus::Type::Comp, state};
            dat2->DBID = dbid + 1;
            dat2->DataID = 2;

            EXPECT_CALL(mesh_port.tx.dat, can_take())
                .WillOnce(Return(false))
                .WillOnce(Return(false))
                .WillOnce(Return(true))
                .WillOnce(Return(true))
                .RetiresOnSaturation();

            EXPECT_CALL(mesh_port.tx.dat, take()).WillOnce(Return(dat1)).WillOnce(Return(dat2)).RetiresOnSaturation();

            EXPECT_CALL(mesh_port.rx.rsp, put(AllOf(HasTxnID(dbid), HasOpcode(mesh::ResponseOpcode::CompAck))))
                .RetiresOnSaturation();
        };
    }
};

class PerfectL2CTest : public L2CTest
{
public:
    PerfectL2CTest()
        : L2CTest({{"~l2c\\.banks\\..*fb_credits_[0-9]+\\.size", 8},
                   {"l2c.cbusy.throttle_max", 64},
                   {"l2c.enable_perfect_mode", true}})
    {
    }
};

/** One bank is useful for testing bank related resource conflicts, like scheduling latencies.
 */
class OneBankL2CTest : public L2CTest
{
public:
    OneBankL2CTest()
        : L2CTest({{"l2c.num_banks", 1},
                   {"~l2c\\.banks\\..*fb_credits_0.size", 8},
                   {"l2c.cbusy.throttle_max", 8},
                   {"l2c.banks.simple_bank_hash", true},
                   {"l2c.sam.tgt_lut", "[0]"_json}})
    {
    }
};

class OneBankFillAtFillL2CTest : public L2CTest
{
public:
    OneBankFillAtFillL2CTest()
        : L2CTest({{"l2c.num_banks", 1},
                   {"l2c.evict_at_fill", true},
                   {"~l2c\\.banks\\..*fb_credits_0.size", 8},
                   {"l2c.cbusy.throttle_max", 8},
                   {"l2c.banks.simple_bank_hash", true},
                   {"l2c.sam.tgt_lut", "[0]"_json}})
    {
    }
};

class OneBankParallelEvictL2CTest : public L2CTest
{
public:
    OneBankParallelEvictL2CTest()
        : L2CTest({{"l2c.num_banks", 1},
                   {"l2c.parallel_eviction", true},
                   {"~l2c\\.banks\\..*fb_credits_0.size", 8},
                   {"l2c.cbusy.throttle_max", 8},
                   {"l2c.banks.simple_bank_hash", true},
                   {"l2c.sam.tgt_lut", "[0]"_json}})
    {
    }
};

class L2CWithoutPWC : public L2CTest
{
public:
    L2CWithoutPWC()
        : L2CTest({{"~l2c\\.banks\\..*fb_credits_[0-9]+\\.size", 8},
                   {"l2c.cbusy.throttle_max", 64},
                   {"l2c.pwc.threshold", 0},
                   {"l2c.sam.tgt_lut", "[0]"_json}})
    {
    }
};

class L2CNoPWCThreshold : public L2CTest
{
public:
    L2CNoPWCThreshold()
        : L2CTest({{"~l2c\\.banks\\..*fb_credits_[0-9]+\\.size", 8},
                   {"l2c.cbusy.throttle_max", 64},
                   {"l2c.pwc.size", 4},
                   {"l2c.pwc.threshold", 4}, // threshold >= size means it will never trigger
                   {"l2c.pwc.can_combine_with_oldest_during_drain", true},
                   {"l2c.pwc.timeout", 512},
                   {"l2c.sam.tgt_lut", "[0]"_json}})
    {
    }
};

class L2CPWCTimeout : public L2CTest
{
public:
    L2CPWCTimeout()
        : L2CTest({{"~l2c\\.banks\\..*fb_credits_[0-9]+\\.size", 8},
                   {"l2c.cbusy.throttle_max", 64},
                   {"l2c.pwc.timeout", 4},
                   {"l2c.sam.tgt_lut", "[0]"_json}})
    {
    }
};

class OneWayL2CTest : public L2CTest
{
public:
    OneWayL2CTest()
        : L2CTest({{"~l2c\\.banks\\..*fb_credits_[0-9]+\\.size", 8},
                   {"l2c.cbusy.throttle_max", 64},
                   {"~l2c\\.banks\\.bank_[0-9]+\\.ways", 1},
                   {"l2c.pwc.timeout", 4},
                   {"l2c.sam.tgt_lut", "[0]"_json}})
    {
    }
};

class OneWayFillAtFillL2CTest : public L2CTest
{
public:
    OneWayFillAtFillL2CTest()
        : L2CTest({{"~l2c\\.banks\\..*fb_credits_[0-9]+\\.size", 8},
                   {"l2c.cbusy.throttle_max", 64},
                   {"~l2c\\.banks\\.bank_[0-9]+\\.ways", 1},
                   {"l2c.evict_at_fill", true},
                   {"l2c.pwc.timeout", 4},
                   {"l2c.sam.tgt_lut", "[0]"_json}})
    {
    }
};

class OneWayParallelEvictL2CTest : public L2CTest
{
public:
    OneWayParallelEvictL2CTest()
        : L2CTest({{"~l2c\\.banks\\..*fb_credits_[0-9]+\\.size", 8},
                   {"l2c.cbusy.throttle_max", 64},
                   {"~l2c\\.banks\\.bank_[0-9]+\\.ways", 1},
                   {"l2c.parallel_eviction", true},
                   {"l2c.pwc.timeout", 4},
                   {"l2c.sam.tgt_lut", "[0]"_json}})
    {
    }
};

class OneBankTwoWaysL2CTest : public L2CTest
{
public:
    OneBankTwoWaysL2CTest()
        : L2CTest({{"l2c.num_banks", 1},
                   {"~l2c\\.banks\\..*fb_credits_0\\.size", 8},
                   {"l2c.cbusy.throttle_max", 8},
                   {"l2c.banks.bank_0.ways", 2},
                   {"l2c.banks.ways_mask", 0b00000001},
                   {"l2c.banks.simple_bank_hash", true},
                   {"l2c.sam.tgt_lut", "[0]"_json}})
    {
    }
};

class OneBankTwoWaysFillAtFillL2CTest : public L2CTest
{
public:
    OneBankTwoWaysFillAtFillL2CTest()
        : L2CTest({{"l2c.num_banks", 1},
                   {"l2c.evict_at_fill", true},
                   {"~l2c\\.banks\\..*fb_credits_0\\.size", 8},
                   {"l2c.cbusy.throttle_max", 8},
                   {"l2c.banks.bank_0.ways", 2},
                   {"l2c.banks.ways_mask", 0b00000001},
                   {"l2c.banks.simple_bank_hash", true},
                   {"l2c.sam.tgt_lut", "[0]"_json}})
    {
    }
};

class OneBankTwoWaysParallelEvictL2CTest : public L2CTest
{
public:
    OneBankTwoWaysParallelEvictL2CTest()
        : L2CTest({{"l2c.num_banks", 1},
                   {"l2c.parallel_eviction", true},
                   {"~l2c\\.banks\\..*fb_credits_0\\.size", 8},
                   {"l2c.cbusy.throttle_max", 8},
                   {"l2c.banks.bank_0.ways", 2},
                   {"l2c.banks.ways_mask", 0b00000001},
                   {"l2c.banks.simple_bank_hash", true},
                   {"l2c.sam.tgt_lut", "[0]"_json}})
    {
    }
};

class OneBankOneWayL2CTest : public L2CTest
{
public:
    OneBankOneWayL2CTest()
        : L2CTest({{"l2c.num_banks", 1},
                   {"~l2c\\.banks\\..*fb_credits_0\\.size", 8},
                   {"l2c.cbusy.throttle_max", 8},
                   {"l2c.banks.bank_0.ways", 2},
                   {"l2c.banks.ways_mask", 0b00000010},
                   {"l2c.banks.simple_bank_hash", true},
                   {"l2c.sam.tgt_lut", "[0]"_json}})
    {
    }
};

class OneWayL2CTestLS : public L2CTest
{
public:
    OneWayL2CTestLS()
        : L2CTest({{"~l2c\\.banks\\..*fb_credits_[0-9]+\\.size", 8},
                   {"l2c.cbusy.throttle_max", 64},
                   {"~l2c\\.banks.bank_[0-9]+\\.ways", 1},
                   {"l2c.pwc.timeout", 4},
                   {"l2c.code_is_likely_shared", true},
                   {"l2c.sam.tgt_lut", "[0]"_json}})
    {
    }
};

class TwoWaysL2CTestLS : public L2CTest
{
public:
    TwoWaysL2CTestLS()
        : L2CTest({{"~l2c\\.banks\\..*fb_credits_[0-9]+\\.size", 8},
                   {"l2c.cbusy.throttle_max", 64},
                   {"~l2c\\.banks.bank_[0-9]+\\.ways", 2},
                   {"l2c.pwc.timeout", 4},
                   {"l2c.code_is_likely_shared", true},
                   {"l2c.sam.tgt_lut", "[0]"_json}})
    {
    }
};

class PrefetcherL2CTest : public L2CTest
{
public:
    PrefetcherL2CTest()
        : L2CTest({{"~l2c\\.banks\\..*fb_credits_[0-9]+\\.size", 8},
                   {"l2c.cbusy.throttle_max", 64},
                   {"l2c.prefetcher.type", "L2P_Dummy"},
                   {"l2c.sam.tgt_lut", "[0]"_json}})
    {
    }

    void SetUp() override
    {
        L2CTest::SetUp();
        prefetcher = dynamic_cast<L2P_Dummy*>(l2c.prefetcher.get());
    }

protected:
    L2P_Dummy* prefetcher;
};

class RequestQueuePrefetchTest : public TestModule
{
public:
    RequestQueuePrefetchTest() : TestModule({{"prefetch_prq.prefetch_favor_high_priority", true}})
    {
        prefetcher.prefetch_request.bind(prefetcher_out);
    }
    L2P_Dummy prefetcher{"prefetcher"};
    l2::RequestQueuePrefetch prefetch_prq{"prefetch_prq", l2::RequestQueueName::PREFETCH, 1, &prefetcher, 6};
    NiceMock<MockPutter<MemReq>> prefetcher_out;
};

TEST_F(RequestQueuePrefetchTest, WrapAroundHole)
{
    auto always_wins_read = [](l2::RequestQueueName queue_name, l2::Op op) -> PipelineArbitration {
        return {true, false, false};
    };

    auto prefetch_reqA = make<MemReq>(0,
                                      0x10000000_pa,
                                      MemOp::Requester::PREFETCH,
                                      MemOp::Type::READ,
                                      8,
                                      true,
                                      MemOp::PrefetchType::NEXTLINE,
                                      MemOp::BarrierType::BAR_NONE);
    auto prefetch_reqB = make<MemReq>(0,
                                      0x10000001_pa,
                                      MemOp::Requester::PREFETCH,
                                      MemOp::Type::READ,
                                      8,
                                      true,
                                      MemOp::PrefetchType::NEXTLINE,
                                      MemOp::BarrierType::BAR_NONE);
    auto prefetch_reqC = make<MemReq>(0,
                                      0x10000002_pa,
                                      MemOp::Requester::PREFETCH,
                                      MemOp::Type::READ,
                                      8,
                                      true,
                                      MemOp::PrefetchType::NEXTLINE,
                                      MemOp::BarrierType::BAR_NONE);
    auto prefetch_reqD = make<MemReq>(0,
                                      0x10000003_pa,
                                      MemOp::Requester::PREFETCH,
                                      MemOp::Type::READ,
                                      8,
                                      true,
                                      MemOp::PrefetchType::NEXTLINE,
                                      MemOp::BarrierType::BAR_NONE);
    auto prefetch_reqE = make<MemReq>(0,
                                      0x10000004_pa,
                                      MemOp::Requester::PREFETCH,
                                      MemOp::Type::READ,
                                      8,
                                      true,
                                      MemOp::PrefetchType::NEXTLINE,
                                      MemOp::BarrierType::BAR_NONE);
    auto prefetch_reqF = make<MemReq>(0,
                                      0x10000005_pa,
                                      MemOp::Requester::PREFETCH,
                                      MemOp::Type::READ,
                                      8,
                                      true,
                                      MemOp::PrefetchType::NEXTLINE,
                                      MemOp::BarrierType::BAR_NONE);
    auto prefetch_opA = make_clone<l2::Op>(prefetch_reqA, l2::Opcode::BLKRD, 0, prefetch_reqA);
    auto prefetch_opB = make_clone<l2::Op>(prefetch_reqB, l2::Opcode::BLKRD, 0, prefetch_reqB);
    auto prefetch_opC = make_clone<l2::Op>(prefetch_reqC, l2::Opcode::BLKRD, 0, prefetch_reqC);
    auto prefetch_opD = make_clone<l2::Op>(prefetch_reqD, l2::Opcode::BLKRD, 0, prefetch_reqD);
    auto prefetch_opE = make_clone<l2::Op>(prefetch_reqE, l2::Opcode::BLKRD, 0, prefetch_reqE);
    auto prefetch_opF = make_clone<l2::Op>(prefetch_reqF, l2::Opcode::BLKRD, 0, prefetch_reqF);

    // Cycle 0 Insert 4 elements
    prefetch_prq.insert(prefetch_opA); // *A
    prefetch_prq.insert(prefetch_opB); // *AB
    prefetch_prq.insert(prefetch_opC); // *BC
    prefetch_prq.insert(prefetch_opD); // *ABCD

    tick(1);
    auto op_r = prefetch_prq.schedule(always_wins_read).op; //  *BCD
    EXPECT_EQ(op_r, prefetch_opA);                          // A

    tick(1);
    prefetch_prq.insert(prefetch_opE);                       //  BCD*E
    prefetch_prq.insert(prefetch_opF);                       //  BCD*EF
    auto op_r1 = prefetch_prq.schedule(always_wins_read).op; //  BCD *F
    EXPECT_EQ(op_r1, prefetch_opE);                          // E

    tick(1);
    auto op_r2 = prefetch_prq.schedule(always_wins_read).op; //  BCD *F
    EXPECT_EQ(op_r2, prefetch_opF);                          // F

    tick(1);
    auto op_r3 = prefetch_prq.schedule(always_wins_read).op; // *BCD
    EXPECT_EQ(op_r3, prefetch_opB);                          // B
}

TEST_F(RequestQueuePrefetchTest, Upgrade)
{
    auto always_wins_read = [](l2::RequestQueueName queue_name, l2::Op op) -> PipelineArbitration {
        return {true, false, false};
    };

    auto prefetch_reqA = make<MemReq>(0,
                                      0x10000000_pa,
                                      MemOp::Requester::PREFETCH,
                                      MemOp::Type::READ,
                                      8,
                                      true,
                                      MemOp::PrefetchType::NEXTLINE,
                                      MemOp::BarrierType::BAR_NONE);
    auto prefetch_reqB = make<MemReq>(0,
                                      0x10000001_pa,
                                      MemOp::Requester::PREFETCH,
                                      MemOp::Type::READ,
                                      8,
                                      true,
                                      MemOp::PrefetchType::NEXTLINE,
                                      MemOp::BarrierType::BAR_NONE);
    auto prefetch_reqC = make<MemReq>(0,
                                      0x10000002_pa,
                                      MemOp::Requester::PREFETCH,
                                      MemOp::Type::READ,
                                      8,
                                      true,
                                      MemOp::PrefetchType::BOP_BEST_OFFSET,
                                      MemOp::BarrierType::BAR_NONE);
    auto prefetch_reqD = make<MemReq>(0,
                                      0x10000003_pa,
                                      MemOp::Requester::PREFETCH,
                                      MemOp::Type::READ,
                                      8,
                                      true,
                                      MemOp::PrefetchType::NEXTLINE,
                                      MemOp::BarrierType::BAR_NONE);
    auto prefetch_reqE = make<MemReq>(0,
                                      0x10000004_pa,
                                      MemOp::Requester::PREFETCH,
                                      MemOp::Type::READ,
                                      8,
                                      true,
                                      MemOp::PrefetchType::NEXTLINE,
                                      MemOp::BarrierType::BAR_NONE);
    auto prefetch_reqF = make<MemReq>(0,
                                      0x10000005_pa,
                                      MemOp::Requester::PREFETCH,
                                      MemOp::Type::READ,
                                      8,
                                      true,
                                      MemOp::PrefetchType::BOP_BEST_OFFSET,
                                      MemOp::BarrierType::BAR_NONE);
    auto prefetch_reqG = make<MemReq>(0,
                                      0x10000006_pa,
                                      MemOp::Requester::PREFETCH,
                                      MemOp::Type::READ,
                                      8,
                                      true,
                                      MemOp::PrefetchType::BOP_BEST_OFFSET,
                                      MemOp::BarrierType::BAR_NONE);
    auto prefetch_reqH = make<MemReq>(0,
                                      0x10000007_pa,
                                      MemOp::Requester::PREFETCH,
                                      MemOp::Type::READ,
                                      8,
                                      true,
                                      MemOp::PrefetchType::BOP_BEST_OFFSET,
                                      MemOp::BarrierType::BAR_NONE);
    auto prefetch_reqI = make<MemReq>(0,
                                      0x10000008_pa,
                                      MemOp::Requester::PREFETCH,
                                      MemOp::Type::READ,
                                      8,
                                      true,
                                      MemOp::PrefetchType::NEXTLINE,
                                      MemOp::BarrierType::BAR_NONE);
    auto prefetch_opA = make_clone<l2::Op>(prefetch_reqA, l2::Opcode::BLKRD, 0, prefetch_reqA);
    auto prefetch_opB = make_clone<l2::Op>(prefetch_reqB, l2::Opcode::BLKRD, 0, prefetch_reqB);
    auto prefetch_opC = make_clone<l2::Op>(prefetch_reqC, l2::Opcode::BLKRD, 0, prefetch_reqC);
    auto prefetch_opD = make_clone<l2::Op>(prefetch_reqD, l2::Opcode::BLKRD, 0, prefetch_reqD);
    auto prefetch_opE = make_clone<l2::Op>(prefetch_reqE, l2::Opcode::BLKRD, 0, prefetch_reqE);
    auto prefetch_opF = make_clone<l2::Op>(prefetch_reqF, l2::Opcode::BLKRD, 0, prefetch_reqF);
    auto prefetch_opG = make_clone<l2::Op>(prefetch_reqG, l2::Opcode::BLKRD, 0, prefetch_reqG);
    auto prefetch_opH = make_clone<l2::Op>(prefetch_reqH, l2::Opcode::BLKRD, 0, prefetch_reqH);
    auto prefetch_opI = make_clone<l2::Op>(prefetch_reqI, l2::Opcode::BLKRD, 0, prefetch_reqI);

    // Cycle 0 Insert 4 elements
    prefetch_prq.insert(prefetch_opA);
    prefetch_prq.insert(prefetch_opB);
    prefetch_prq.insert(prefetch_opC);
    prefetch_prq.insert(prefetch_opD);
    prefetch_prq.insert(prefetch_opE); // *ABCDE

    tick(1);
    auto op_r = prefetch_prq.schedule(always_wins_read).op; // *BCDE
    EXPECT_EQ(op_r, prefetch_opA);                          // A

    tick(1);
    prefetch_prq.insert(prefetch_opF); //  BCDE*F
    prefetch_prq.insert(prefetch_opG); // GBCDE*F
    prefetch_prq.insert(prefetch_opH); // GHCDE*F  - We should override B with H. Prefetch H is higher priority
    prefetch_prq.insert(prefetch_opI); // GHCDE*F  - No override

    auto op_r1 = prefetch_prq.schedule(always_wins_read).op;
    EXPECT_EQ(op_r1, prefetch_opF); // F

    tick(1);
    auto op_r2 = prefetch_prq.schedule(always_wins_read).op;
    EXPECT_EQ(op_r2, prefetch_opG); // G

    tick(1);
    auto op_r3 = prefetch_prq.schedule(always_wins_read).op;
    EXPECT_EQ(op_r3, prefetch_opH); // H

    tick(1);
    auto op_r4 = prefetch_prq.schedule(always_wins_read).op;
    EXPECT_EQ(op_r4, prefetch_opC); // C
}

TEST_F(OneBankTwoWaysL2CTest, ReadFromL2WayOnly)
{
    l2c.preload(0x91111100_pa, l2::LineState::UC); // preload a line in L2

    auto [req, spec_resp, final_resp] = make_req_miss_resp(0, 0x11111100_pa, MemOp::Requester::LSU, MemOp::Type::READ);

    EXPECT_CALL(lsu_read_req_port, can_take()).WillOnce(Return(true)).WillRepeatedly(Return(false));

    EXPECT_CALL(lsu_read_req_port, take()).WillOnce(Return(std::make_pair(req, uarch_common::Request::L1Contents())));

    EXPECT_CALL(mesh_port.rx.req,
                put(AllOf(HasOpcode(mesh::RequestOpcode::ReadNotSharedDirty), HasAddress(0x11111100_pa))))
        .WillOnce(Invoke(return_data_sep(12, mesh::chi::FinalState::SC)));

    {
        InSequence seq;
        EXPECT_CALL(lsu_read_resp_port, put(MemRespMatcher(spec_resp)));
        EXPECT_CALL(lsu_read_resp_port, put(MemRespMatcher(final_resp)));
    }

    auto eviction_response = [this](mesh::MeshReq req) {
        auto rsp = make<MeshRsp>(NodeId{1}, NodeId{0}, req->TxnID, ResponseOpcode::CompDBIDResp);
        rsp->Resp = RespComp_I;

        EXPECT_CALL(mesh_port.tx.rsp, can_take()).WillOnce(Return(true)).RetiresOnSaturation();

        EXPECT_CALL(mesh_port.tx.rsp, take()).WillOnce(Return(rsp));
    };

    EXPECT_CALL(mesh_port.rx.req, put(HasOpcode(mesh::RequestOpcode::WriteEvictFull)))
        .WillOnce(Invoke(eviction_response));

    tick(50);
    // EXPECT_EQ(l2c.peek_state(0x11111100_pa), l2::LineState::SC);
}

// Upon a read request from L1, both L2 and L3 ways should be looked up
TEST_F(OneBankTwoWaysL2CTest, L2MissL3Hit)
{
    l2c.preload_l3(0x91111100_pa, l2::LineState::UC);
    auto [req, resp] = make_req_resp(0, 0x91111100_pa, MemOp::Requester::LSU, MemOp::Type::READ);

    EXPECT_CALL(lsu_read_req_port, can_take()).WillOnce(Return(true)).WillRepeatedly(Return(false));

    EXPECT_CALL(lsu_read_req_port, take()).WillOnce(Return(std::make_pair(req, uarch_common::Request::L1Contents())));

    EXPECT_CALL(lsu_read_resp_port, put(MemRespMatcher(resp)));

    tick(10);
}

// Two ways - one is L2 and other way is L3. This test case is testing Direct L3 Stash flow
// Core receives CopyBackWrData directly from another core.
TEST_F(OneBankTwoWaysL2CTest, FlexFlow1)
{
    l2c.preload(0x91111100_pa, l2::LineState::UC);

    auto snp = make<MeshSnp>(NodeId{1}, NodeId{0}, 5 /* TxnId */, SnoopOpcode::SnpStashShared, 0x11111100_pa);

    // the stash snoop allocates an FB entry. Fbe Id = 0 since this is the first entry allocated.

    EXPECT_CALL(mesh_port.tx.snp, can_take()).WillOnce(Return(true)).WillRepeatedly(Return(false));

    EXPECT_CALL(mesh_port.tx.snp, take()).WillOnce(Return(snp));

    auto copy_back_wr_data = [this](mesh::MeshRsp rsp) {
        auto dat1 =
            make<mesh::MeshDat>(0 /*allocated FB id is the TxnId of CopyBackWrData*/, mesh::DataOpcode::CopyBackWrData);
        dat1->Resp = mesh::chi::RespWriteData_UC;
        dat1->DBID = 12;  // ignored
        dat1->DataID = 0; // ignored

        auto dat2 =
            make<mesh::MeshDat>(0 /*allocated FB id is the TxnId of CopyBackWrData*/, mesh::DataOpcode::CopyBackWrData);
        dat2->Resp = mesh::chi::RespWriteData_UC;
        dat2->DBID = 12;  // ignored
        dat2->DataID = 2; // ignored

        EXPECT_CALL(mesh_port.tx.dat, can_take()).WillOnce(Return(true)).WillOnce(Return(true)).RetiresOnSaturation();
        EXPECT_CALL(mesh_port.tx.dat, take()).WillOnce(Return(dat1)).WillOnce(Return(dat2)).RetiresOnSaturation();
        EXPECT_CALL(mesh_port.rx.rsp,
                    put(AllOf(HasTxnID(5) /* original snp's TxnId */, HasOpcode(mesh::ResponseOpcode::CompAck))))
            .RetiresOnSaturation();
    };

    EXPECT_CALL(mesh_port.rx.rsp, put(HasOpcode(mesh::ResponseOpcode::SnpResp))).WillOnce(Invoke(copy_back_wr_data));

    tick(50);

    EXPECT_EQ(l2c.peek_state(0x11111100_pa), l2::LineState::UC);
    EXPECT_EQ(l2c.peek_state(0x91111100_pa), l2::LineState::UC);
}

// Two ways - one is L2 and other way is L3. This test case is testing Indirect L3 Stash flow
// Indirect: the other core sends CopyBackWrData to PHA, who in turn sends it to the target stash core.
// This scenario seems to never occur.
// The expectations in this test may not be correct.
TEST_F(OneBankTwoWaysL2CTest, FlexFlow1_Indirect)
{
    l2c.preload(0x91111100_pa, l2::LineState::UC);

    auto snp = make<MeshSnp>(NodeId{1}, NodeId{0}, 0, SnoopOpcode::SnpStashShared, 0x11111100_pa);

    EXPECT_CALL(mesh_port.tx.snp, can_take()).WillOnce(Return(true)).WillRepeatedly(Return(false));

    EXPECT_CALL(mesh_port.tx.snp, take()).WillOnce(Return(snp));

    auto comp_data = [this](mesh::MeshRsp rsp) {
        auto dat1 = make<mesh::MeshDat>(rsp->TxnID, mesh::DataOpcode::CompData);
        dat1->Resp = mesh::chi::RespComp_UC;
        dat1->DBID = 12;
        dat1->DataID = 0;

        auto dat2 = make<mesh::MeshDat>(rsp->TxnID, mesh::DataOpcode::CompData);
        dat2->Resp = mesh::chi::RespComp_UC;
        dat2->DBID = 12;
        dat2->DataID = 2;

        EXPECT_CALL(mesh_port.tx.dat, can_take()).WillOnce(Return(true)).WillOnce(Return(true)).RetiresOnSaturation();
        EXPECT_CALL(mesh_port.tx.dat, take()).WillOnce(Return(dat1)).WillOnce(Return(dat2)).RetiresOnSaturation();
        EXPECT_CALL(mesh_port.rx.rsp, put(AllOf(HasTxnID(12), HasOpcode(mesh::ResponseOpcode::CompAck))))
            .RetiresOnSaturation();
    };

    EXPECT_CALL(mesh_port.rx.rsp, put(HasOpcode(mesh::ResponseOpcode::SnpResp))).WillOnce(Invoke(comp_data));

    tick(50);

    EXPECT_EQ(l2c.peek_state(0x11111100_pa), l2::LineState::UC);
    EXPECT_EQ(l2c.peek_state(0x91111100_pa), l2::LineState::UC);
}

// One way - allocated to L3. This test case is testing Direct l3 Stash flow with dirty eviction
TEST_F(OneBankOneWayL2CTest, FlexFlow2)
{
    l2c.preload_l3(0x91111100_pa, l2::LineState::UD);

    auto snp = make<MeshSnp>(NodeId{1}, NodeId{0}, 5 /* TxnId */, SnoopOpcode::SnpStashShared, 0x11111100_pa);

    // the stash snoop allocates an FB entry. Fbe Id = 0 since this is the first entry allocated.

    EXPECT_CALL(mesh_port.tx.snp, can_take()).WillOnce(Return(true)).WillRepeatedly(Return(false));

    EXPECT_CALL(mesh_port.tx.snp, take()).WillOnce(Return(snp));

    auto eviction_response = [this](mesh::MeshReq req) {
        auto rsp = make<MeshRsp>(NodeId{1}, NodeId{0}, req->TxnID, ResponseOpcode::CompDBIDResp);
        rsp->DBID = 399;

        EXPECT_CALL(mesh_port.tx.rsp, can_take()).WillOnce(Return(true)).RetiresOnSaturation();

        EXPECT_CALL(mesh_port.tx.rsp, take()).WillOnce(Return(rsp)).RetiresOnSaturation();

        EXPECT_CALL(mesh_port.rx.dat,
                    put(AllOf(HasOpcode(mesh::DataOpcode::CopyBackWrData), HasTxnID(399), HasPassDirty(true))))
            .Times(2);
    };

    auto copy_back_wr_data = [this, eviction_response](mesh::MeshRsp rsp) {
        auto dat1 =
            make<mesh::MeshDat>(0 /*allocated FB id is the TxnId of CopyBackWrData*/, mesh::DataOpcode::CopyBackWrData);
        dat1->Resp = mesh::chi::RespWriteData_UC;
        dat1->DBID = 12;
        dat1->DataID = 0;

        auto dat2 =
            make<mesh::MeshDat>(0 /*allocated FB id is the TxnId of CopyBackWrData*/, mesh::DataOpcode::CopyBackWrData);
        dat2->Resp = mesh::chi::RespWriteData_UC;
        dat2->DBID = 12;
        dat2->DataID = 2;

        EXPECT_CALL(mesh_port.tx.dat, can_take()).WillOnce(Return(true)).WillOnce(Return(true)).RetiresOnSaturation();
        EXPECT_CALL(mesh_port.tx.dat, take()).WillOnce(Return(dat1)).WillOnce(Return(dat2)).RetiresOnSaturation();
        EXPECT_CALL(mesh_port.rx.rsp,
                    put(AllOf(HasTxnID(5) /* original snp's TxnId */, HasOpcode(mesh::ResponseOpcode::CompAck))))
            .RetiresOnSaturation();

        EXPECT_CALL(mesh_port.rx.req, put(HasOpcode(mesh::RequestOpcode::WriteBackFull)))
            .WillOnce(Invoke(eviction_response));
    };

    EXPECT_CALL(mesh_port.rx.rsp, put(HasOpcode(mesh::ResponseOpcode::SnpResp))).WillOnce(Invoke(copy_back_wr_data));

    tick(50);

    EXPECT_EQ(l2c.peek_state(0x11111100_pa), l2::LineState::UC);
}

TEST_F(OneBankTwoWaysFillAtFillL2CTest, ReadFromL2WayOnly)
{
    l2c.preload(0x91111100_pa, l2::LineState::UC); // preload a line in L2

    auto [req, spec_resp, final_resp] = make_req_miss_resp(0, 0x11111100_pa, MemOp::Requester::LSU, MemOp::Type::READ);

    EXPECT_CALL(lsu_read_req_port, can_take()).WillOnce(Return(true)).WillRepeatedly(Return(false));

    EXPECT_CALL(lsu_read_req_port, take()).WillOnce(Return(std::make_pair(req, uarch_common::Request::L1Contents())));

    EXPECT_CALL(mesh_port.rx.req,
                put(AllOf(HasOpcode(mesh::RequestOpcode::ReadNotSharedDirty), HasAddress(0x11111100_pa))))
        .WillOnce(Invoke(return_data_sep(12, mesh::chi::FinalState::SC)));

    {
        InSequence seq;
        EXPECT_CALL(lsu_read_resp_port, put(MemRespMatcher(spec_resp)));
        EXPECT_CALL(lsu_read_resp_port, put(MemRespMatcher(final_resp)));
    }

    auto eviction_response = [this](mesh::MeshReq req) {
        auto rsp = make<MeshRsp>(NodeId{1}, NodeId{0}, req->TxnID, ResponseOpcode::CompDBIDResp);
        rsp->Resp = RespComp_I;

        EXPECT_CALL(mesh_port.tx.rsp, can_take()).WillOnce(Return(true)).RetiresOnSaturation();

        EXPECT_CALL(mesh_port.tx.rsp, take()).WillOnce(Return(rsp));
    };

    EXPECT_CALL(mesh_port.rx.req, put(HasOpcode(mesh::RequestOpcode::WriteEvictFull)))
        .WillOnce(Invoke(eviction_response));

    tick(50);
    // EXPECT_EQ(l2c.peek_state(0x11111100_pa), l2::LineState::SC);
}

TEST_F(OneBankTwoWaysParallelEvictL2CTest, ReadFromL2WayOnly)
{
    l2c.preload(0x91111100_pa, l2::LineState::UC); // preload a line in L2

    auto [req, spec_resp, final_resp] = make_req_miss_resp(0, 0x11111100_pa, MemOp::Requester::LSU, MemOp::Type::READ);

    EXPECT_CALL(lsu_read_req_port, can_take()).WillOnce(Return(true)).WillRepeatedly(Return(false));

    EXPECT_CALL(lsu_read_req_port, take()).WillOnce(Return(std::make_pair(req, uarch_common::Request::L1Contents())));

    EXPECT_CALL(mesh_port.rx.req,
                put(AllOf(HasOpcode(mesh::RequestOpcode::ReadNotSharedDirty), HasAddress(0x11111100_pa))))
        .WillOnce(Invoke(return_data_sep(12, mesh::chi::FinalState::SC)));

    {
        InSequence seq;
        EXPECT_CALL(lsu_read_resp_port, put(MemRespMatcher(spec_resp)));
        EXPECT_CALL(lsu_read_resp_port, put(MemRespMatcher(final_resp)));
    }

    auto eviction_response = [this](mesh::MeshReq req) {
        auto rsp = make<MeshRsp>(NodeId{1}, NodeId{0}, req->TxnID, ResponseOpcode::CompDBIDResp);
        rsp->Resp = RespComp_I;

        EXPECT_CALL(mesh_port.tx.rsp, can_take()).WillOnce(Return(true)).RetiresOnSaturation();

        EXPECT_CALL(mesh_port.tx.rsp, take()).WillOnce(Return(rsp)).RetiresOnSaturation();
    };

    EXPECT_CALL(mesh_port.rx.req, put(HasOpcode(mesh::RequestOpcode::WriteEvictFull)))
        .WillOnce(Invoke(eviction_response));

    tick(50);
    // EXPECT_EQ(l2c.peek_state(0x11111100_pa), l2::LineState::SC);
}

TEST_F(L2CTest, LSUHitsPreloadedLine)
{
    auto [req, resp] = make_req_resp(0, 0x1000_pa, MemOp::Requester::LSU);
    l2c.preload(0x1000_pa, l2::LineState::UC);

    EXPECT_CALL(lsu_read_req_port, can_take()).WillOnce(Return(true)).WillRepeatedly(Return(false));

    EXPECT_CALL(lsu_read_req_port, take())
        .WillOnce(Return(uarch_common::Request::Fill(req, uarch_common::Request::L1Contents())));

    EXPECT_CALL(lsu_read_resp_port, put(MemRespMatcher(resp)));

    tick(10);
}

TEST_F(L2CWithoutPWC, LSUWriteHitsPreloadedLine)
{
    auto req = make<MemReq>(0, 0x1000_pa, MemOp::Requester::LSU, MemOp::Type::WRITE);
    l2c.preload(0x1000_pa, l2::LineState::UC);

    EXPECT_CALL(lsu_write_req_port, can_take()).WillOnce(Return(true)).WillRepeatedly(Return(false));
    EXPECT_CALL(lsu_write_req_port, peek(0)).WillOnce(ReturnRef(req));
    EXPECT_CALL(lsu_write_req_port, take()).WillOnce(Return(req));

    // Should not generate system request.
    EXPECT_CALL(mesh_port.rx.req, put(_)).Times(0);

    tick(10);
}

TEST_F(L2CPWCTimeout, LSUWriteNotifiesLSUOfPendingWrites)
{
    auto req = make<MemReq>(0, 0x1000_pa, MemOp::Requester::LSU, MemOp::Type::WRITE);
    l2c.preload(0x1000_pa, l2::LineState::UC);

    EXPECT_CALL(lsu_write_req_port, can_take()).WillOnce(Return(true)).WillRepeatedly(Return(false));
    EXPECT_CALL(lsu_write_req_port, peek(0)).WillOnce(ReturnRef(req));
    EXPECT_CALL(lsu_write_req_port, take()).WillOnce(Return(req));

    {
        InSequence seq;
        EXPECT_CALL(l2_barrier_out_port, put(false));
        EXPECT_CALL(l2_barrier_out_port, put(true));
    }

    tick(20);
}

TEST_F(L2CTest, ICFHitsPreloadedLine)
{
    auto [req, resp] = make_req_resp(0, 0x1000_pa, MemOp::Requester::ICF);

    l2c.preload(0x1000_pa, l2::LineState::SC);

    EXPECT_CALL(icf_req_port, can_take()).WillOnce(Return(true)).WillRepeatedly(Return(false));

    EXPECT_CALL(icf_req_port, take())
        .WillOnce(Return(uarch_common::Request::Fill(req, uarch_common::Request::L1Contents())));

    EXPECT_CALL(icf_resp_port, put(MemRespMatcher(resp)));

    tick(10);
}

TEST_F(L2CTest, DMMUHitsPreloadedLine)
{
    auto [req, resp] = make_req_resp(0, 0x1000_pa, MemOp::Requester::DMMU);

    l2c.preload(0x1000_pa, l2::LineState::UC);

    EXPECT_CALL(dmmu_req_port, can_take()).WillOnce(Return(true)).WillRepeatedly(Return(false));

    EXPECT_CALL(dmmu_req_port, take()).WillOnce(Return(req));

    EXPECT_CALL(dmmu_resp_port, put(MemRespMatcher(resp)));

    tick(10);
}

TEST_F(L2CTest, IMMUHitsPreloadedLine)
{
    auto [req, resp] = make_req_resp(0, 0x1000_pa, MemOp::Requester::IMMU);

    l2c.preload(0x1000_pa, l2::LineState::UC);

    EXPECT_CALL(immu_req_port, can_take()).WillOnce(Return(true)).WillRepeatedly(Return(false));

    EXPECT_CALL(immu_req_port, take()).WillOnce(Return(req));

    EXPECT_CALL(immu_resp_port, put(MemRespMatcher(resp)));

    tick(10);
}

TEST_F(L2CTest, ICFReadMiss)
{
    auto [req, resp] = make_req_resp(0, 0x11111110_pa, MemOp::Requester::ICF);

    EXPECT_CALL(icf_req_port, can_take()).WillOnce(Return(true)).WillRepeatedly(Return(false));
    EXPECT_CALL(icf_req_port, take())
        .WillOnce(Return(uarch_common::Request::Fill(req, uarch_common::Request::L1Contents())));

    EXPECT_CALL(mesh_port.rx.req,
                put(AllOf(HasOpcode(mesh::RequestOpcode::ReadNotSharedDirty),
                          HasAddress(0x11111100_pa),
                          HasLikelyShared(true & l2c.code_is_likely_shared))))
        .WillOnce(Invoke(return_data(1, mesh::chi::FinalState::SC)));

    EXPECT_CALL(icf_resp_port, put(MemRespMatcher(resp)));

    tick(20);

    EXPECT_EQ(l2c.peek_state(0x11111100_pa), l2::LineState::SC);
}

TEST_F(L2CTest, ICFReadMissSep)
{
    auto [req, resp] = make_req_resp(0, 0x11111110_pa, MemOp::Requester::ICF);

    EXPECT_CALL(icf_req_port, can_take()).WillOnce(Return(true)).WillRepeatedly(Return(false));
    EXPECT_CALL(icf_req_port, take())
        .WillOnce(Return(uarch_common::Request::Fill(req, uarch_common::Request::L1Contents())));

    EXPECT_CALL(mesh_port.rx.req,
                put(AllOf(HasOpcode(mesh::RequestOpcode::ReadNotSharedDirty),
                          HasAddress(0x11111100_pa),
                          HasLikelyShared(true & l2c.code_is_likely_shared))))
        .WillOnce(Invoke(return_data_sep(1, mesh::chi::FinalState::SC)));

    EXPECT_CALL(icf_resp_port, put(MemRespMatcher(resp)));

    tick(32);

    EXPECT_EQ(l2c.peek_state(0x11111100_pa), l2::LineState::SC);
}

TEST_F(L2CTest, LSUReadMiss)
{
    auto [req, spec_resp, final_resp] = make_req_miss_resp(0, 0x11111110_pa, MemOp::Requester::LSU);

    EXPECT_CALL(lsu_read_req_port, can_take()).WillOnce(Return(true)).WillRepeatedly(Return(false));
    EXPECT_CALL(lsu_read_req_port, take())
        .WillOnce(Return(uarch_common::Request::Fill(req, uarch_common::Request::L1Contents())));

    EXPECT_CALL(
        mesh_port.rx.req,
        put(AllOf(
            HasOpcode(mesh::RequestOpcode::ReadNotSharedDirty), HasAddress(0x11111100_pa), HasLikelyShared(false))))
        .WillOnce(Invoke(return_data(12, mesh::chi::FinalState::SC)));

    {
        InSequence seq;
        EXPECT_CALL(lsu_read_resp_port, put(MemRespMatcher(spec_resp)));
        EXPECT_CALL(lsu_read_resp_port, put(MemRespMatcher(final_resp)));
    }

    tick(100);
}

TEST_F(L2CTest, LSUReadMissSep)
{
    auto [req, spec_resp, final_resp] = make_req_miss_resp(0, 0x11111110_pa, MemOp::Requester::LSU);

    EXPECT_CALL(lsu_read_req_port, can_take()).WillOnce(Return(true)).WillRepeatedly(Return(false));
    EXPECT_CALL(lsu_read_req_port, take())
        .WillOnce(Return(uarch_common::Request::Fill(req, uarch_common::Request::L1Contents())));

    EXPECT_CALL(
        mesh_port.rx.req,
        put(AllOf(
            HasOpcode(mesh::RequestOpcode::ReadNotSharedDirty), HasAddress(0x11111100_pa), HasLikelyShared(false))))
        .WillOnce(Invoke(return_data_sep(12, mesh::chi::FinalState::SC)));

    {
        InSequence seq;
        EXPECT_CALL(lsu_read_resp_port, put(MemRespMatcher(spec_resp)));
        EXPECT_CALL(lsu_read_resp_port, put(MemRespMatcher(final_resp)));
    }

    tick(100);
}

TEST_F(L2CTest, LSUReadUniqueMiss)
{
    auto [req, spec_resp, final_resp] =
        make_req_miss_resp(0, 0x11111110_pa, MemOp::Requester::LSU, MemOp::Type::READUNIQUE);

    EXPECT_CALL(lsu_read_req_port, can_take()).WillOnce(Return(true)).WillRepeatedly(Return(false));
    EXPECT_CALL(lsu_read_req_port, take())
        .WillOnce(Return(uarch_common::Request::Fill(req, uarch_common::Request::L1Contents())));

    EXPECT_CALL(
        mesh_port.rx.req,
        put(AllOf(HasOpcode(mesh::RequestOpcode::ReadUnique), HasAddress(0x11111100_pa), HasLikelyShared(false))))
        .WillOnce(Invoke(return_data(12, mesh::chi::FinalState::UC)));

    {
        InSequence seq;
        EXPECT_CALL(lsu_read_resp_port, put(MemRespMatcher(spec_resp)));
        EXPECT_CALL(lsu_read_resp_port, put(MemRespMatcher(final_resp)));
    }

    tick(100);
}

TEST_F(L2CTest, LSUReadUniqueChunksReturnedOutOfOrder)
{
    auto [req, spec_resp, final_resp] =
        make_req_miss_resp(0, 0x11111110_pa, MemOp::Requester::LSU, MemOp::Type::READUNIQUE);

    EXPECT_CALL(lsu_read_req_port, can_take()).WillOnce(Return(true)).WillRepeatedly(Return(false));
    EXPECT_CALL(lsu_read_req_port, take())
        .WillOnce(Return(uarch_common::Request::Fill(req, uarch_common::Request::L1Contents())));

    auto read_response = [this](mesh::MeshReq req) {
        auto dat1 = make<mesh::MeshDat>(NodeId{1}, NodeId{0}, 0, mesh::DataOpcode::CompData);
        dat1->Resp = mesh::RespComp_UC;
        dat1->DBID = 12;
        dat1->DataID = 2;

        auto dat2 = make<mesh::MeshDat>(NodeId{1}, NodeId{0}, 0, mesh::DataOpcode::CompData);
        dat2->Resp = mesh::RespComp_UC;
        dat1->DBID = 12;
        dat2->DataID = 0;

        EXPECT_CALL(mesh_port.tx.dat, can_take())
            .WillOnce(Return(true))
            .WillOnce(Return(false))
            .WillOnce(Return(false))
            .WillOnce(Return(false))
            .WillOnce(Return(false))
            .WillOnce(Return(true))
            .RetiresOnSaturation();

        EXPECT_CALL(mesh_port.tx.dat, take()).WillOnce(Return(dat1)).WillOnce(Return(dat2)).RetiresOnSaturation();

        EXPECT_CALL(mesh_port.rx.rsp, put(HasOpcode(mesh::ResponseOpcode::CompAck)));
    };

    EXPECT_CALL(
        mesh_port.rx.req,
        put(AllOf(HasOpcode(mesh::RequestOpcode::ReadUnique), HasAddress(0x11111100_pa), HasLikelyShared(false))))
        .WillOnce(Invoke(read_response));

    {
        InSequence seq;
        EXPECT_CALL(lsu_read_resp_port, put(MemRespMatcher(spec_resp)));
        EXPECT_CALL(lsu_read_resp_port, put(MemRespMatcher(final_resp)));
    }

    tick(100);
}

TEST_F(L2CWithoutPWC, LSUWriteToSharedLineUpgrades)
{
    auto req = make<MemReq>(10, 0x1000_pa, MemOp::Requester::LSU, MemOp::Type::WRITE);
    l2c.preload(0x1000_pa, l2::LineState::SC);

    EXPECT_CALL(lsu_write_req_port, can_take()).WillOnce(Return(true)).WillRepeatedly(Return(false));
    EXPECT_CALL(lsu_write_req_port, peek(0)).WillOnce(ReturnRef(req));
    EXPECT_CALL(lsu_write_req_port, take()).WillOnce(Return(req));

    EXPECT_CALL(mesh_port.rx.req, put(AllOf(HasOpcode(mesh::RequestOpcode::ReadUnique), HasAddress(0x1000_pa))))
        .WillOnce(Invoke(return_data(17, mesh::chi::FinalState::UC)));

    tick(20);

    EXPECT_EQ(l2c.peek_state(0x1000_pa), l2::LineState::UD);
}

TEST_F(L2CPWCTimeout, LSUReadMissUniqueFollowedByWriteWillRespin)
{
    auto [req, spec_resp, final_resp] = make_req_miss_resp(0, 0x1000_pa, MemOp::Requester::LSU, MemOp::Type::READ);

    EXPECT_CALL(lsu_read_req_port, can_take()).WillOnce(Return(true)).WillRepeatedly(Return(false));
    EXPECT_CALL(lsu_read_req_port, take())
        .WillOnce(Return(uarch_common::Request::Fill(req, uarch_common::Request::L1Contents())));

    auto issue_write = [this](mesh::MeshReq req) {
        auto store = make<MemReq>(1, 0x1000_pa, MemOp::Requester::LSU, MemOp::Type::WRITE);

        EXPECT_CALL(lsu_write_req_port, can_take()).WillOnce(Return(true)).WillRepeatedly(Return(false));
        EXPECT_CALL(lsu_write_req_port, peek(0)).WillOnce(ReturnRefOfCopy(store));
        EXPECT_CALL(lsu_write_req_port, take()).WillOnce(Return(store));
    };

    {
        InSequence seq;
        EXPECT_CALL(lsu_read_resp_port, put(MemRespMatcher(spec_resp)));
        EXPECT_CALL(mesh_port.rx.req,
                    put(AllOf(HasOpcode(mesh::RequestOpcode::ReadNotSharedDirty), HasAddress(0x1000_pa))))
            .WillOnce(DoAll(Invoke(return_data(0, mesh::chi::FinalState::UC)), Invoke(issue_write)));

        EXPECT_CALL(lsu_read_resp_port, put(MemRespMatcher(spec_resp)));
        EXPECT_CALL(lsu_read_resp_port, put(MemRespMatcher(final_resp)));
    }

    tick(100);
}

TEST_F(L2CPWCTimeout, LSUReadMissSharedFollowedByWriteWillUpgrade)
{
    auto [req, spec_resp, final_resp] = make_req_miss_resp(0, 0x1000_pa, MemOp::Requester::LSU);

    EXPECT_CALL(lsu_read_req_port, can_take()).WillOnce(Return(true)).WillRepeatedly(Return(false));
    EXPECT_CALL(lsu_read_req_port, take())
        .WillOnce(Return(uarch_common::Request::Fill(req, uarch_common::Request::L1Contents())));

    auto return_sc_data = return_data(12, mesh::chi::FinalState::SC);
    auto return_uc_data = return_data(8, mesh::chi::FinalState::UC);

    auto issue_write = [this, return_uc_data](mesh::MeshReq req) {
        auto req2 = make<MemReq>(10, 0x1000_pa, MemOp::Requester::LSU, MemOp::Type::WRITE);

        EXPECT_CALL(lsu_write_req_port, can_take()).WillOnce(Return(true)).WillRepeatedly(Return(false));
        EXPECT_CALL(lsu_write_req_port, peek(0)).WillOnce(ReturnRefOfCopy(req2));
        EXPECT_CALL(lsu_write_req_port, take()).WillOnce(Return(req2));

        EXPECT_CALL(mesh_port.rx.req, put(AllOf(HasOpcode(mesh::RequestOpcode::ReadUnique), HasAddress(0x1000_pa))))
            .WillOnce(Invoke(return_uc_data))
            .RetiresOnSaturation();
    };

    {
        InSequence seq;
        EXPECT_CALL(lsu_read_resp_port, put(MemRespMatcher(spec_resp)));

        EXPECT_CALL(
            mesh_port.rx.req,
            put(AllOf(
                HasOpcode(mesh::RequestOpcode::ReadNotSharedDirty), HasAddress(0x1000_pa), HasLikelyShared(false))))
            .WillOnce(DoAll(Invoke(return_sc_data), Invoke(issue_write)))
            .RetiresOnSaturation();

        EXPECT_CALL(lsu_read_resp_port, put(MemRespMatcher(spec_resp)));
        EXPECT_CALL(lsu_read_resp_port, put(MemRespMatcher(final_resp)));
    }

    tick(100);
}

TEST_F(L2CTest, LSUAtomicLoad)
{
    auto [req, spec_resp, final_resp] = make_req_miss_resp(10,
                                                           0x1000_pa,
                                                           MemOp::Requester::LSU,
                                                           MemOp::Type::WRITE,
                                                           8,
                                                           false,
                                                           MemOp::PrefetchType::NONE,
                                                           MemOp::BAR_NONE,
                                                           MemOp::ATOMIC_LOAD);

    EXPECT_CALL(lsu_write_req_port, can_take()).WillOnce(Return(true)).WillRepeatedly(Return(false));
    EXPECT_CALL(lsu_write_req_port, peek(0)).WillOnce(ReturnRef(req));
    EXPECT_CALL(lsu_write_req_port, take()).WillOnce(Return(req));

    EXPECT_CALL(mesh_port.rx.req, put(AllOf(HasOpcode(mesh::RequestOpcode::ReadUnique), HasAddress(0x1000_pa))))
        .WillOnce(Invoke(return_data(17, mesh::chi::FinalState::UC)));

    {
        InSequence seq;
        EXPECT_CALL(lsu_read_resp_port, put(MemRespMatcher(spec_resp)));
        EXPECT_CALL(lsu_read_resp_port, put(MemRespMatcher(final_resp)));
    }

    tick(50);
}

TEST_F(L2CTest, LSUAtomicLoadWide)
{
    auto [req, spec_resp, final_resp] = make_req_miss_resp(10,
                                                           0x1000_pa,
                                                           MemOp::Requester::LSU,
                                                           MemOp::Type::WRITE,
                                                           8,
                                                           false,
                                                           MemOp::PrefetchType::NONE,
                                                           MemOp::BAR_NONE,
                                                           MemOp::ATOMIC_LOAD_WIDE);
    auto second_req = make<MemReq>(11,
                                   0x1000_pa,
                                   MemOp::Requester::LSU,
                                   MemOp::Type::WRITE,
                                   8,
                                   false,
                                   MemOp::PrefetchType::NONE,
                                   MemOp::BAR_NONE,
                                   MemOp::ATOMIC_LOAD_WIDE);

    {
        InSequence seq;
        EXPECT_CALL(lsu_write_req_port, can_take()).WillOnce(Return(true));
        EXPECT_CALL(lsu_write_req_port, can_take()).Times(20).WillRepeatedly(Return(false));
        EXPECT_CALL(lsu_write_req_port, can_take()).WillOnce(Return(true)).WillRepeatedly(Return(false));
    }

    EXPECT_CALL(lsu_write_req_port, peek(0)).WillOnce(ReturnRef(req)).WillOnce(ReturnRef(second_req));
    EXPECT_CALL(lsu_write_req_port, take()).WillOnce(Return(req)).WillOnce(Return(second_req));

    tick(20);

    EXPECT_CALL(mesh_port.rx.req, put(AllOf(HasOpcode(mesh::RequestOpcode::ReadUnique), HasAddress(0x1000_pa))))
        .WillOnce(Invoke(return_data(17, mesh::chi::FinalState::UC)));

    {
        InSequence seq;
        EXPECT_CALL(lsu_read_resp_port, put(MemRespMatcher(spec_resp)));
        EXPECT_CALL(lsu_read_resp_port, put(MemRespMatcher(final_resp)));
    }

    tick(50);
}

TEST_F(L2CTest, DISABLED_LSUAtomicLoadAfterRead)
{
    auto [read_req, read_spec_resp, read_final_resp] = make_req_miss_resp(9, 0x1000_pa, MemOp::Requester::LSU);
    auto [req, spec_resp, final_resp] = make_req_miss_resp(10,
                                                           0x1000_pa,
                                                           MemOp::Requester::LSU,
                                                           MemOp::Type::WRITE,
                                                           8,
                                                           false,
                                                           MemOp::PrefetchType::NONE,
                                                           MemOp::BAR_NONE,
                                                           MemOp::ATOMIC_LOAD);
    EXPECT_CALL(lsu_read_req_port, can_take()).WillOnce(Return(true)).WillRepeatedly(Return(false));

    EXPECT_CALL(lsu_read_req_port, take())
        .WillOnce(Return(uarch_common::Request::Fill(read_req, uarch_common::Request::L1Contents())));

    {
        InSequence seq;
        EXPECT_CALL(lsu_write_req_port, can_take()).Times(5).WillRepeatedly(Return(false));
        EXPECT_CALL(lsu_write_req_port, can_take()).WillOnce(Return(true)).WillRepeatedly(Return(false));
    }

    tick(5);

    EXPECT_CALL(lsu_write_req_port, peek(0)).WillOnce(ReturnRef(req));
    EXPECT_CALL(lsu_write_req_port, take()).WillOnce(Return(req));

    tick(20);

    EXPECT_CALL(mesh_port.rx.req, put(AllOf(HasOpcode(mesh::RequestOpcode::ReadUnique), HasAddress(0x1000_pa))))
        .WillOnce(Invoke(return_data(17, mesh::chi::FinalState::UC)));

    {
        InSequence seq;
        EXPECT_CALL(lsu_read_resp_port, put(MemRespMatcher(read_spec_resp)));
        EXPECT_CALL(lsu_read_resp_port, put(MemRespMatcher(read_final_resp)));
        EXPECT_CALL(lsu_read_resp_port, put(MemRespMatcher(spec_resp)));
        EXPECT_CALL(lsu_read_resp_port, put(MemRespMatcher(final_resp)));
    }

    tick(50);
}

TEST_F(PerfectL2CTest, ICFReadHitsPerfectL2)
{
    auto req = make<MemReq>(0, 0x11111111_pa, MemOp::Requester::ICF);

    EXPECT_CALL(icf_req_port, can_take()).WillOnce(Return(true)).WillRepeatedly(Return(false));
    EXPECT_CALL(icf_req_port, take())
        .WillOnce(Return(uarch_common::Request::Fill(req, uarch_common::Request::L1Contents())));

    EXPECT_CALL(icf_resp_port, put(MemReqMatcher(req)));

    tick(13);
}

TEST_F(OneBankL2CTest, BackToBackReadsTakeReadLatencyCycles)
{
    l2c.preload(0x1000000_pa, l2::LineState::UC);
    l2c.preload(0x2000000_pa, l2::LineState::UC);

    auto req1 = make<MemReq>(0, 0x1000000_pa, MemOp::Requester::LSU);
    auto req2 = make<MemReq>(1, 0x2000000_pa, MemOp::Requester::LSU);

    EXPECT_CALL(lsu_read_req_port, can_take())
        .WillOnce(Return(true))
        .WillOnce(Return(true))
        .WillRepeatedly(Return(false));

    EXPECT_CALL(lsu_read_req_port, take())
        .WillOnce(Return(uarch_common::Request::Fill(req1, uarch_common::Request::L1Contents())))
        .WillOnce(Return(uarch_common::Request::Fill(req2, uarch_common::Request::L1Contents())));

    Cycle first_cycle{0};
    Cycle second_cycle{0};

    auto get_first_resp_cycle = [this, &first_cycle](MemResp) {
        first_cycle = get_cycle();
    };

    auto get_second_resp_cycle = [this, &second_cycle](MemResp) {
        second_cycle = get_cycle();
    };

    EXPECT_CALL(lsu_read_resp_port, put(_))
        .WillOnce(Invoke(get_first_resp_cycle))
        .WillOnce(Invoke(get_second_resp_cycle));

    tick(20);

    EXPECT_EQ(second_cycle - first_cycle, 4);
}

TEST_F(L2CTest, ICFMissFollowedByHit)
{
    PhysAddr pa(0x11111110);
    auto req1 = make<MemReq>(0, pa, MemOp::Requester::ICF);
    auto req2 = make<MemReq>(1, pa, MemOp::Requester::ICF);

    EXPECT_CALL(icf_req_port, can_take()).WillOnce(Return(true)).WillRepeatedly(Return(false));
    EXPECT_CALL(icf_req_port, take())
        .WillOnce(Return(uarch_common::Request::Fill(req1, uarch_common::Request::L1Contents())));

    EXPECT_CALL(mesh_port.rx.req,
                put(AllOf(HasOpcode(mesh::RequestOpcode::ReadNotSharedDirty),
                          HasAddress(0x11111100_pa),
                          HasLikelyShared(true & l2c.code_is_likely_shared))))
        .WillOnce(Invoke(return_data(1, mesh::chi::FinalState::SC)));

    auto send_second_request = [this, req2](MemResp resp) {
        EXPECT_CALL(icf_req_port, can_take()).WillOnce(Return(true)).RetiresOnSaturation();

        EXPECT_CALL(icf_req_port, take())
            .WillOnce(Return(uarch_common::Request::Fill(req2, uarch_common::Request::L1Contents())))
            .RetiresOnSaturation();
    };

    EXPECT_CALL(icf_resp_port, put(MemReqMatcher(req1))).WillOnce(Invoke(send_second_request));

    EXPECT_CALL(icf_resp_port, put(MemReqMatcher(req2)));

    tick(20);
}

TEST_F(L2CPWCTimeout, TwoStoresToSameLineWillWriteCombine)
{
    auto req1 = make<MemReq>(0, 0x1000_pa, MemOp::Requester::LSU, MemOp::Type::WRITE);
    auto req2 = make<MemReq>(1, 0x1008_pa, MemOp::Requester::LSU, MemOp::Type::WRITE);

    EXPECT_CALL(lsu_write_req_port, can_take())
        .WillOnce(Return(true))
        .WillOnce(Return(true))
        .WillRepeatedly(Return(false));
    EXPECT_CALL(lsu_write_req_port, peek(0)).WillOnce(ReturnRef(req1)).WillOnce(ReturnRef(req2));
    EXPECT_CALL(lsu_write_req_port, take()).WillOnce(Return(req1)).WillOnce(Return(req2));

    auto write_response = [this](mesh::MeshReq req) {
        auto dat1 = make<mesh::MeshDat>(NodeId{1}, NodeId{0}, 0, mesh::DataOpcode::CompData);
        dat1->Resp = mesh::RespComp_UC;
        dat1->DataID = 0;

        auto dat2 = make<mesh::MeshDat>(NodeId{1}, NodeId{0}, 0, mesh::DataOpcode::CompData);
        dat2->Resp = mesh::RespComp_UC;
        dat2->DataID = 2;

        EXPECT_CALL(mesh_port.tx.dat, can_take()).WillOnce(Return(true)).WillOnce(Return(true)).RetiresOnSaturation();

        EXPECT_CALL(mesh_port.tx.dat, take()).WillOnce(Return(dat1)).WillOnce(Return(dat2)).RetiresOnSaturation();

        EXPECT_CALL(mesh_port.rx.rsp, put(HasOpcode(mesh::ResponseOpcode::CompAck)));
    };

    EXPECT_CALL(mesh_port.rx.req, put(HasOpcode(mesh::RequestOpcode::ReadUnique))).WillOnce(Invoke(write_response));

    tick(30);
}

TEST_F(L2CPWCTimeout, FullCachelineStoreWillGenerateMakeUnique)
{
    auto req = make<MemReq>(0, 0x1000_pa, MemOp::Requester::LSU, MemOp::Type::WRITE, 64);

    EXPECT_CALL(lsu_write_req_port, can_take()).WillOnce(Return(true)).WillRepeatedly(Return(false));
    EXPECT_CALL(lsu_write_req_port, peek(0)).WillOnce(ReturnRef(req));
    EXPECT_CALL(lsu_write_req_port, take()).WillOnce(Return(req));

    auto write_response = [this](mesh::MeshReq req) {
        auto rsp = make<MeshRsp>(NodeId{1}, NodeId{0}, 0, ResponseOpcode::Comp);
        rsp->Resp = mesh::RespComp_UC;

        EXPECT_CALL(mesh_port.tx.rsp, can_take()).WillOnce(Return(true)).RetiresOnSaturation();

        EXPECT_CALL(mesh_port.tx.rsp, take()).WillOnce(Return(rsp)).RetiresOnSaturation();

        EXPECT_CALL(mesh_port.rx.rsp, put(HasOpcode(mesh::ResponseOpcode::CompAck)));
    };

    EXPECT_CALL(mesh_port.rx.req, put(HasOpcode(mesh::RequestOpcode::MakeUnique))).WillOnce(Invoke(write_response));

    tick(30);

    EXPECT_EQ(l2c.peek_state(0x1000_pa), l2::LineState::UD);
}

TEST_F(L2CPWCTimeout, OrderedStoreWillNotWriteCombine)
{
    get_environment()->event_stream->enable();

    auto req1 = make<MemReq>(0,
                             0x1000_pa,
                             MemOp::Requester::LSU,
                             MemOp::Type::WRITE,
                             8,
                             false,
                             MemOp::PrefetchType::NONE,
                             MemOp::BarrierType::BAR_DMB,
                             MemOp::ATOMIC_NONE,
                             MemOp::SNOOP_NONE,
                             PageSize::Size::INVALID,
                             0xff);
    auto req2 = make<MemReq>(1,
                             0x1008_pa,
                             MemOp::Requester::LSU,
                             MemOp::Type::WRITE,
                             8,
                             false,
                             MemOp::PrefetchType::NONE,
                             MemOp::BarrierType::BAR_NONE,
                             MemOp::ATOMIC_NONE,
                             MemOp::SNOOP_NONE,
                             PageSize::Size::INVALID,
                             0xff00);

    EXPECT_CALL(lsu_write_req_port, can_take())
        .WillOnce(Return(true))
        .WillOnce(Return(true))
        .WillRepeatedly(Return(false));
    EXPECT_CALL(lsu_write_req_port, peek(0)).WillOnce(ReturnRef(req1)).WillOnce(ReturnRef(req2));
    EXPECT_CALL(lsu_write_req_port, take()).WillOnce(Return(req1)).WillOnce(Return(req2));

    auto write_response = [this](mesh::MeshReq req) {
        auto dat1 = make<MeshDat>(NodeId{1}, NodeId{0}, req->TxnID, DataOpcode::CompData);
        dat1->Resp = mesh::RespComp_UC;
        dat1->DataID = 0;

        auto dat2 = make<MeshDat>(NodeId{1}, NodeId{0}, req->TxnID, DataOpcode::CompData);
        dat2->Resp = mesh::RespComp_UC;
        dat2->DataID = 2;

        EXPECT_CALL(mesh_port.tx.dat, can_take())
            .WillOnce(Return(false))
            .WillOnce(Return(false))
            .WillOnce(Return(false))
            .WillOnce(Return(false))
            .WillOnce(Return(false))
            .WillOnce(Return(false))
            .WillOnce(Return(false))
            .WillOnce(Return(false))
            .WillOnce(Return(true))
            .WillOnce(Return(true))
            .RetiresOnSaturation();

        EXPECT_CALL(mesh_port.tx.dat, take()).WillOnce(Return(dat1)).WillOnce(Return(dat2)).RetiresOnSaturation();

        EXPECT_CALL(mesh_port.rx.rsp, put(HasOpcode(mesh::ResponseOpcode::CompAck))).RetiresOnSaturation();
    };

    EXPECT_CALL(mesh_port.rx.req, put(AllOf(HasOpcode(mesh::RequestOpcode::ReadUnique), HasAddress(0x1000_pa))))
        .WillOnce(Invoke(write_response));

    tick(100);

    EXPECT_EQ(event_count("l2c.access"), 2);
}

TEST_F(L2CPWCTimeout, StoreIsPendingUntilFbDeallocates)
{
    auto req = make<MemReq>(0, 0x1000_pa, MemOp::Requester::LSU, MemOp::Type::WRITE);

    EXPECT_CALL(lsu_write_req_port, can_take()).WillOnce(Return(true)).WillRepeatedly(Return(false));
    EXPECT_CALL(lsu_write_req_port, peek(0)).WillOnce(ReturnRef(req));
    EXPECT_CALL(lsu_write_req_port, take()).WillOnce(Return(req));

    auto write_response = [this](mesh::MeshReq req) {
        auto dat1 = make<MeshDat>(NodeId{1}, NodeId{0}, 0, DataOpcode::CompData);
        dat1->Resp = mesh::RespComp_UC;
        dat1->DataID = 0;

        auto dat2 = make<MeshDat>(NodeId{1}, NodeId{0}, 0, DataOpcode::CompData);
        dat2->Resp = mesh::RespComp_UC;
        dat2->DataID = 2;

        EXPECT_CALL(mesh_port.tx.dat, can_take()).WillOnce(Return(true)).WillOnce(Return(true)).RetiresOnSaturation();

        EXPECT_CALL(mesh_port.tx.dat, take()).WillOnce(Return(dat1)).WillOnce(Return(dat2)).RetiresOnSaturation();

        EXPECT_CALL(mesh_port.rx.rsp, put(HasOpcode(mesh::ResponseOpcode::CompAck)));
    };

    {
        InSequence seq;

        // We shouldn't see "true" again on the barrier port until some time after the ReadUnique returns and the
        // fill buffer deallocates.
        //
        EXPECT_CALL(l2_barrier_out_port, put(false));
        EXPECT_CALL(mesh_port.rx.req, put(HasOpcode(mesh::RequestOpcode::ReadUnique))).WillOnce(Invoke(write_response));
        EXPECT_CALL(l2_barrier_out_port, put(true));
    }

    tick(30);
}

TEST_F(L2CNoPWCThreshold, FifthStoreWillForceDrain)
{
    auto req1 = make<MemReq>(0, 0x1000_pa, MemOp::Requester::LSU, MemOp::Type::WRITE);
    auto req2 = make<MemReq>(1, 0x1040_pa, MemOp::Requester::LSU, MemOp::Type::WRITE);
    auto req3 = make<MemReq>(2, 0x1080_pa, MemOp::Requester::LSU, MemOp::Type::WRITE);
    auto req4 = make<MemReq>(3, 0x10c0_pa, MemOp::Requester::LSU, MemOp::Type::WRITE);
    auto req5 = make<MemReq>(4, 0x1100_pa, MemOp::Requester::LSU, MemOp::Type::WRITE);

    EXPECT_CALL(lsu_write_req_port, can_take())
        .WillOnce(Return(true))
        .WillOnce(Return(true))
        .WillOnce(Return(true))
        .WillOnce(Return(true))
        .WillRepeatedly(Return(false));
    EXPECT_CALL(lsu_write_req_port, peek(0))
        .WillOnce(ReturnRef(req1))
        .WillOnce(ReturnRef(req2))
        .WillOnce(ReturnRef(req3))
        .WillOnce(ReturnRef(req4));
    EXPECT_CALL(lsu_write_req_port, take())
        .WillOnce(Return(req1))
        .WillOnce(Return(req2))
        .WillOnce(Return(req3))
        .WillOnce(Return(req4));

    auto write_response = [this](mesh::MeshReq req) {
        auto dat1 = make<MeshDat>(NodeId{1}, NodeId{0}, 0, DataOpcode::CompData);
        dat1->Resp = mesh::RespComp_UC;
        dat1->DataID = 0;

        auto dat2 = make<MeshDat>(NodeId{1}, NodeId{0}, 0, DataOpcode::CompData);
        dat2->Resp = mesh::RespComp_UC;
        dat2->DataID = 2;

        EXPECT_CALL(mesh_port.tx.dat, can_take()).WillOnce(Return(true)).WillOnce(Return(true)).RetiresOnSaturation();

        EXPECT_CALL(mesh_port.tx.dat, take()).WillOnce(Return(dat1)).WillOnce(Return(dat2)).RetiresOnSaturation();

        EXPECT_CALL(mesh_port.rx.rsp, put(HasOpcode(mesh::ResponseOpcode::CompAck)));
    };

    // Nothing should happen after the first 4 writes. They will fill the PWC and
    // wait for another non-combining store, or a timeout.
    //
    tick(30);

    EXPECT_CALL(lsu_write_req_port, can_take()).WillOnce(Return(true)).RetiresOnSaturation();
    EXPECT_CALL(lsu_write_req_port, peek(0)).WillRepeatedly(ReturnRef(req5));
    EXPECT_CALL(lsu_write_req_port, unsafe_peek(0)).WillRepeatedly(ReturnRef(req5));
    EXPECT_CALL(lsu_write_req_port, unsafe_size()).WillRepeatedly(Return(1));

    EXPECT_CALL(mesh_port.rx.req, put(AllOf(HasOpcode(mesh::RequestOpcode::ReadUnique), HasAddress(0x1000_pa))))
        .WillOnce(Invoke(write_response));

    tick(30);
}

TEST_F(L2CNoPWCThreshold, FifthStoreWontForceDrainIfItCombines)
{
    auto req1 = make<MemReq>(0, 0x1000_pa, MemOp::Requester::LSU, MemOp::Type::WRITE);
    auto req2 = make<MemReq>(1, 0x1040_pa, MemOp::Requester::LSU, MemOp::Type::WRITE);
    auto req3 = make<MemReq>(2, 0x1080_pa, MemOp::Requester::LSU, MemOp::Type::WRITE);
    auto req4 = make<MemReq>(3, 0x10c0_pa, MemOp::Requester::LSU, MemOp::Type::WRITE);
    auto req5 = make<MemReq>(4, 0x1008_pa, MemOp::Requester::LSU, MemOp::Type::WRITE);

    EXPECT_CALL(lsu_write_req_port, can_take()).WillRepeatedly(Return(false));

    EXPECT_CALL(lsu_write_req_port, can_take())
        .WillOnce(Return(true))
        .WillOnce(Return(true))
        .WillOnce(Return(true))
        .WillOnce(Return(true))
        .RetiresOnSaturation();
    EXPECT_CALL(lsu_write_req_port, peek(0))
        .WillOnce(ReturnRef(req1))
        .WillOnce(ReturnRef(req2))
        .WillOnce(ReturnRef(req3))
        .WillOnce(ReturnRef(req4));
    EXPECT_CALL(lsu_write_req_port, take())
        .WillOnce(Return(req1))
        .WillOnce(Return(req2))
        .WillOnce(Return(req3))
        .WillOnce(Return(req4));

    // Nothing should happen after the first 4 writes. They will fill the PWC and
    // wait for another non-combining store, or a timeout.
    //
    tick(30);

    EXPECT_CALL(lsu_write_req_port, can_take()).WillOnce(Return(true)).WillRepeatedly(Return(false));
    EXPECT_CALL(lsu_write_req_port, peek(0)).WillOnce(ReturnRef(req5));
    EXPECT_CALL(lsu_write_req_port, take()).WillOnce(Return(req5));

    tick(30);
}

TEST_F(OneWayL2CTest, ReadEvictsOlderDirtyLine)
{
    PhysAddr pa1(0x11111111);
    PhysAddr pa2(0x91111111);

    auto lsu_streq = make<MemReq>(0, pa1, MemOp::Requester::LSU, MemOp::Type::WRITE);
    auto [lsu_evreq, lsu_spec_evresp, lsu_evresp] =
        make_req_miss_resp(1, pa2, MemOp::Requester::LSU, MemOp::Type::READ);

    EXPECT_CALL(lsu_write_req_port, can_take())
        .WillOnce(Return(true)) // write miss allocates L2 line dirty
        .WillRepeatedly(Return(false));
    EXPECT_CALL(lsu_write_req_port, peek(0)).WillOnce(ReturnRef(lsu_streq));
    EXPECT_CALL(lsu_write_req_port, take()).WillOnce(Return(lsu_streq));

    {
        InSequence seq;
        EXPECT_CALL(lsu_read_req_port, can_take()).Times(14).WillRepeatedly(Return(false));
        EXPECT_CALL(lsu_read_req_port, can_take())
            .WillOnce(Return(true)) // read miss which fills second way
            .WillRepeatedly(Return(false));
    }

    EXPECT_CALL(lsu_read_req_port, take())
        .WillOnce(Return(uarch_common::Request::Fill(lsu_evreq, uarch_common::Request::L1Contents())));

    auto read_response = [this](mesh::MeshReq req) {
        auto dat1 = make<mesh::MeshDat>(req->TxnID, mesh::DataOpcode::CompData);
        dat1->Resp = mesh::chi::RespComp_UC;
        dat1->DBID = 12;
        dat1->DataID = 0;

        auto dat2 = make<mesh::MeshDat>(req->TxnID, mesh::DataOpcode::CompData);
        dat2->Resp = mesh::chi::RespComp_UC;
        dat2->DBID = 12;
        dat2->DataID = 2;

        EXPECT_CALL(mesh_port.tx.dat, can_take()).WillOnce(Return(true)).WillOnce(Return(true)).RetiresOnSaturation();
        EXPECT_CALL(mesh_port.tx.dat, take()).WillOnce(Return(dat1)).WillOnce(Return(dat2)).RetiresOnSaturation();
        EXPECT_CALL(mesh_port.rx.rsp, put(AllOf(HasTxnID(12), HasOpcode(mesh::ResponseOpcode::CompAck))))
            .RetiresOnSaturation();
    };

    auto write_response = [this](mesh::MeshReq req) {
        auto rsp = make<mesh::MeshRsp>(NodeId{1}, NodeId{0}, req->TxnID, mesh::ResponseOpcode::CompDBIDResp);
        rsp->DBID = 300;

        EXPECT_CALL(mesh_port.tx.rsp, can_take()).WillOnce(Return(true)).RetiresOnSaturation();
        EXPECT_CALL(mesh_port.tx.rsp, take()).WillOnce(Return(rsp)).RetiresOnSaturation();
        EXPECT_CALL(mesh_port.rx.dat,
                    put(AllOf(HasOpcode(mesh::DataOpcode::CopyBackWrData), HasTxnID(300), HasPassDirty(true))))
            .Times(2);
    };

    {
        InSequence mem_req_seq;

        EXPECT_CALL(mesh_port.rx.req,
                    put(AllOf(HasOpcode(mesh::RequestOpcode::ReadUnique), HasAddress(pa1.line_address()))))
            .WillOnce(Invoke(read_response));

        EXPECT_CALL(mesh_port.rx.req,
                    put(AllOf(HasOpcode(mesh::RequestOpcode::ReadNotSharedDirty), HasAddress(pa2.line_address()))))
            .WillOnce(Invoke(read_response));

        EXPECT_CALL(mesh_port.rx.req,
                    put(AllOf(HasOpcode(mesh::RequestOpcode::WriteBackFull), HasAddress(pa1.line_address()))))
            .WillOnce(Invoke(write_response));
    }

    {
        InSequence seq;
        EXPECT_CALL(lsu_read_resp_port, put(MemRespMatcher(lsu_spec_evresp)));
        EXPECT_CALL(lsu_read_resp_port, put(MemRespMatcher(lsu_evresp))).RetiresOnSaturation();
    }

    tick(60);
}

/* In this test, we'll seed the L2 with a line, then send in a read to the same set (thus evicting the line), followed
 * directly by another read to the original line. The second read should be satisfiable from the FB allocated for
 * eviction by the first read.
 */
TEST_F(OneWayL2CTest, ReadCanFillFromPendingEviction)
{
    l2c.preload(0x11111100_pa, l2::LineState::UC);

    auto [read1, read1_spec_resp, read1_resp] =
        make_req_miss_resp(0, 0x91111100_pa, MemOp::Requester::LSU, MemOp::Type::READ);
    auto [read2, read2_resp] = make_req_resp(0, 0x11111100_pa, MemOp::Requester::LSU, MemOp::Type::READ);

    EXPECT_CALL(lsu_read_req_port, can_take())
        .WillOnce(Return(true))
        .WillOnce(Return(true))
        .WillRepeatedly(Return(false));
    EXPECT_CALL(lsu_read_req_port, take())
        .WillOnce(Return(uarch_common::Request::Fill(read1, uarch_common::Request::L1Contents())))
        .WillOnce(Return(uarch_common::Request::Fill(read2, uarch_common::Request::L1Contents())));

    auto read_response = [this](mesh::MeshReq req) {
        auto dat1 = make<mesh::MeshDat>(req->TxnID, mesh::DataOpcode::CompData);
        dat1->Resp = mesh::chi::RespComp_UC;
        dat1->DBID = 7;
        dat1->DataID = 0;

        auto dat2 = make<mesh::MeshDat>(req->TxnID, mesh::DataOpcode::CompData);
        dat2->Resp = mesh::chi::RespComp_UC;
        dat2->DBID = 7;
        dat2->DataID = 2;

        EXPECT_CALL(mesh_port.tx.dat, can_take()).WillOnce(Return(true)).WillOnce(Return(true)).RetiresOnSaturation();
        EXPECT_CALL(mesh_port.tx.dat, take()).WillOnce(Return(dat1)).WillOnce(Return(dat2)).RetiresOnSaturation();
        EXPECT_CALL(mesh_port.rx.rsp, put(AllOf(HasTxnID(7), HasOpcode(mesh::ResponseOpcode::CompAck))))
            .RetiresOnSaturation();
    };

    auto eviction_response = [this](mesh::MeshReq req) {
        auto rsp = make<MeshRsp>(NodeId{1}, NodeId{0}, req->TxnID, ResponseOpcode::CompDBIDResp);
        rsp->DBID = 0;

        EXPECT_CALL(mesh_port.tx.rsp, can_take())
            .WillOnce(Return(false)) // This delay allows time for the second read to enter the pipe before we've
            .WillOnce(Return(false)) // completed the fill and deallocate the FB we want to hit.
            .WillOnce(Return(false))
            .WillOnce(Return(false))
            .WillOnce(Return(false))
            .WillOnce(Return(false))
            .WillOnce(Return(false))
            .WillOnce(Return(false))
            .WillOnce(Return(false))
            .WillOnce(Return(false))
            .WillOnce(Return(false))
            .WillOnce(Return(false))
            .WillOnce(Return(true))
            .RetiresOnSaturation();
        EXPECT_CALL(mesh_port.tx.rsp, take()).WillOnce(Return(rsp)).RetiresOnSaturation();
        EXPECT_CALL(mesh_port.rx.dat, put(HasOpcode(mesh::DataOpcode::CopyBackWrData))).Times(2);
    };

    EXPECT_CALL(mesh_port.rx.req,
                put(AllOf(HasOpcode(mesh::RequestOpcode::ReadNotSharedDirty), HasAddress(0x91111100_pa))))
        .WillOnce(Invoke(read_response));

    EXPECT_CALL(mesh_port.rx.req, put(AllOf(HasOpcode(mesh::RequestOpcode::WriteEvictFull), HasAddress(0x11111100_pa))))
        .WillOnce(Invoke(eviction_response));

    {
        InSequence seq;

        EXPECT_CALL(lsu_read_resp_port, put(MemRespMatcher(read1_spec_resp)));

        EXPECT_CALL(lsu_read_resp_port, put(MemRespMatcher(read2_resp))).RetiresOnSaturation();

        EXPECT_CALL(lsu_read_resp_port, put(MemRespMatcher(read1_resp))).RetiresOnSaturation();
    }

    tick(60);
}

/* Test Snoop Clean Invalid on clean line
 *
 *   1. Preload L2 with line in SC
 *   2. Snoop it out with SnpCleanInvalid
 *   3. Attempt a subsequent read to the same line
 *   4. Subsequent read should miss the L2 and go out to the system
 */
TEST_F(L2CTest, SnoopInvalidatesCleanLine)
{
    l2c.preload(0x1000_pa, l2::LineState::SC);

    auto snp = make<MeshSnp>(NodeId{1}, NodeId{0}, 0, SnoopOpcode::SnpCleanInvalid, 0x1000_pa);

    EXPECT_CALL(mesh_port.tx.snp, can_take()).WillOnce(Return(true)).WillRepeatedly(Return(false));

    EXPECT_CALL(mesh_port.tx.snp, take()).WillOnce(Return(snp));

    auto subsequent_read_response = [this](mesh::MeshReq req) {
        auto dat1 = make<mesh::MeshDat>(req->TxnID, mesh::DataOpcode::CompData);
        dat1->Resp = mesh::chi::RespComp_UC;
        dat1->DBID = 12;
        dat1->DataID = 0;

        auto dat2 = make<mesh::MeshDat>(req->TxnID, mesh::DataOpcode::CompData);
        dat2->Resp = mesh::chi::RespComp_UC;
        dat2->DBID = 12;
        dat2->DataID = 2;

        EXPECT_CALL(mesh_port.tx.dat, can_take()).WillOnce(Return(true)).WillOnce(Return(true)).RetiresOnSaturation();
        EXPECT_CALL(mesh_port.tx.dat, take()).WillOnce(Return(dat1)).WillOnce(Return(dat2)).RetiresOnSaturation();
        EXPECT_CALL(mesh_port.rx.rsp, put(AllOf(HasTxnID(12), HasOpcode(mesh::ResponseOpcode::CompAck))))
            .RetiresOnSaturation();
    };

    auto subsequent_read = [this, subsequent_read_response](mesh::MeshRsp rsp) {
        auto req = make<MemReq>(0, 0x1000_pa, MemOp::Requester::LSU);

        EXPECT_CALL(lsu_read_req_port, can_take()).WillOnce(Return(true)).RetiresOnSaturation();
        EXPECT_CALL(lsu_read_req_port, take())
            .WillOnce(Return(uarch_common::Request::Fill(req, uarch_common::Request::L1Contents())))
            .RetiresOnSaturation();

        EXPECT_CALL(mesh_port.rx.req, put(HasOpcode(mesh::RequestOpcode::ReadNotSharedDirty)))
            .WillOnce(Invoke(subsequent_read_response))
            .RetiresOnSaturation();
    };

    EXPECT_CALL(mesh_port.rx.rsp, put(HasOpcode(mesh::ResponseOpcode::SnpResp))).WillOnce(Invoke(subsequent_read));

    tick(30);
}

/* Test Snoop Clean Invalid on dirty line
 *
 *   1. Preload L2 with line in UD
 *   2. Snoop it out with SnpCleanInvalid - should respond with data
 *   3. Attempt a subsequent read to the same line
 *   4. Subsequent read should miss the L2 and go out to the system
 */
TEST_F(L2CTest, SnoopInvalidatesDirtyLineAndReturnsData)
{
    l2c.preload(0x1000_pa, l2::LineState::UD);

    auto snp = make<MeshSnp>(NodeId{1}, NodeId{0}, 0, SnoopOpcode::SnpCleanInvalid, 0x1000_pa);

    EXPECT_CALL(mesh_port.tx.snp, can_take()).WillOnce(Return(true)).WillRepeatedly(Return(false));

    EXPECT_CALL(mesh_port.tx.snp, take()).WillOnce(Return(snp));

    auto subsequent_read_response = [this](mesh::MeshReq req) {
        auto dat1 = make<mesh::MeshDat>(req->TxnID, mesh::DataOpcode::CompData);
        dat1->Resp = mesh::chi::RespComp_UC;
        dat1->DBID = 12;
        dat1->DataID = 0;

        auto dat2 = make<mesh::MeshDat>(req->TxnID, mesh::DataOpcode::CompData);
        dat2->Resp = mesh::chi::RespComp_UC;
        dat2->DBID = 12;
        dat2->DataID = 2;

        EXPECT_CALL(mesh_port.tx.dat, can_take()).WillOnce(Return(true)).WillOnce(Return(true)).RetiresOnSaturation();
        EXPECT_CALL(mesh_port.tx.dat, take()).WillOnce(Return(dat1)).WillOnce(Return(dat2)).RetiresOnSaturation();
        EXPECT_CALL(mesh_port.rx.rsp, put(AllOf(HasTxnID(12), HasOpcode(mesh::ResponseOpcode::CompAck))))
            .RetiresOnSaturation();
    };

    auto subsequent_read = [this, subsequent_read_response](mesh::MeshDat dat) {
        auto req = make<MemReq>(0, 0x1000_pa, MemOp::Requester::LSU);

        EXPECT_CALL(lsu_read_req_port, can_take()).WillOnce(Return(true)).RetiresOnSaturation();
        EXPECT_CALL(lsu_read_req_port, take())
            .WillOnce(Return(uarch_common::Request::Fill(req, uarch_common::Request::L1Contents())))
            .RetiresOnSaturation();

        EXPECT_CALL(mesh_port.rx.req, put(HasOpcode(mesh::RequestOpcode::ReadNotSharedDirty)))
            .WillOnce(Invoke(subsequent_read_response))
            .RetiresOnSaturation();
    };

    EXPECT_CALL(mesh_port.rx.dat, put(HasOpcode(mesh::DataOpcode::SnpRespData)))
        .WillOnce(Return())
        .WillOnce(Invoke(subsequent_read));

    tick(40);
}

/* Test snoop will invalidate LSU.
 *
 *   1. Bring a line into the L2 via the LSU.
 *   2. Snoop it out with SnpCleanInvalid
 *   3. Back invalidation should be generated to the core.
 */
TEST_F(L2CTest, SnoopWillInvalidateLSU)
{
    auto [req, spec_resp, final_resp] = make_req_miss_resp(0, 0x1000_pa, MemOp::Requester::LSU, MemOp::Type::READ);

    EXPECT_CALL(lsu_read_req_port, can_take()).WillOnce(Return(true)).WillRepeatedly(Return(false));

    EXPECT_CALL(lsu_read_req_port, take())
        .WillOnce(Return(uarch_common::Request::Fill(req, uarch_common::Request::L1Contents())));

    EXPECT_CALL(
        mesh_port.rx.req,
        put(AllOf(HasOpcode(mesh::RequestOpcode::ReadNotSharedDirty), HasAddress(0x1000_pa), HasLikelyShared(false))))
        .WillOnce(Invoke(return_data(12, mesh::chi::FinalState::SC)));

    auto subsequent_snoop = [this]() {
        auto snp = make<MeshSnp>(NodeId{1}, NodeId{0}, 0, SnoopOpcode::SnpCleanInvalid, 0x1000_pa);

        EXPECT_CALL(mesh_port.tx.snp, can_take()).WillOnce(Return(true)).WillRepeatedly(Return(false));

        EXPECT_CALL(mesh_port.tx.snp, take()).WillOnce(Return(snp));
    };

    {
        InSequence seq;
        EXPECT_CALL(lsu_read_resp_port, put(MemRespMatcher(spec_resp)));
        EXPECT_CALL(lsu_read_resp_port, put(MemReqMatcher(final_resp))).WillOnce(InvokeWithoutArgs(subsequent_snoop));
    }

    EXPECT_CALL(mesh_port.rx.rsp, put(HasOpcode(mesh::ResponseOpcode::SnpResp)));

    EXPECT_CALL(lsu_snoop_request_port, put(Pointee(Field(&MemReqObj::phys_addr, 0x1000_pa))));

    tick(30);
}

/* Test eviction will invalidate LSU.
 *
 *   1. Bring a line into the L2 via the LSU.
 *   2. Evict it with another line.
 *   3. Back invalidation should be generated to the core.
 */
TEST_F(OneWayL2CTest, EvictionWillInvalidateLSU)
{
    auto [req1, resp1_spec, resp1] = make_req_miss_resp(0, 0x11111100_pa, MemOp::Requester::LSU, MemOp::Type::READ);
    auto [req2, resp2_spec, resp2] = make_req_miss_resp(0, 0x91111100_pa, MemOp::Requester::LSU, MemOp::Type::READ);

    EXPECT_CALL(lsu_read_req_port, can_take()).WillOnce(Return(true)).WillRepeatedly(Return(false));

    EXPECT_CALL(lsu_read_req_port, take())
        .WillOnce(Return(uarch_common::Request::Fill(req1, uarch_common::Request::L1Contents())));

    EXPECT_CALL(mesh_port.rx.req,
                put(AllOf(HasOpcode(mesh::RequestOpcode::ReadNotSharedDirty), HasAddress(0x11111100_pa))))
        .WillOnce(Invoke(return_data(12, mesh::chi::FinalState::SC)));

    auto evicting_read = [this, req = req2]() {
        EXPECT_CALL(lsu_read_req_port, can_take()).WillOnce(Return(true)).RetiresOnSaturation();

        uarch_common::Request::L1Contents l1_contents;
        l1_contents.add(0x11111100_pa);
        EXPECT_CALL(lsu_read_req_port, take()).WillOnce(Return(uarch_common::Request::Fill(req, l1_contents)));

        EXPECT_CALL(mesh_port.rx.req,
                    put(AllOf(HasOpcode(mesh::RequestOpcode::ReadNotSharedDirty), HasAddress(0x91111100_pa))))
            .WillOnce(Invoke(return_data(13, mesh::chi::FinalState::SC)));
    };

    {
        InSequence seq;
        EXPECT_CALL(lsu_read_resp_port, put(MemRespMatcher(resp1_spec)));
        EXPECT_CALL(lsu_read_resp_port, put(MemRespMatcher(resp1))).WillOnce(InvokeWithoutArgs(evicting_read));

        EXPECT_CALL(lsu_read_resp_port, put(MemRespMatcher(resp2_spec)));
        EXPECT_CALL(lsu_read_resp_port, put(MemRespMatcher(resp2)));
    }

    auto eviction_response = [this](mesh::MeshReq req) {
        auto rsp = make<MeshRsp>(NodeId{1}, NodeId{0}, req->TxnID, ResponseOpcode::Comp);
        rsp->Resp = RespComp_I;

        EXPECT_CALL(mesh_port.tx.rsp, can_take()).WillOnce(Return(true)).RetiresOnSaturation();

        EXPECT_CALL(mesh_port.tx.rsp, take()).WillOnce(Return(rsp));
    };

    EXPECT_CALL(mesh_port.rx.req, put(HasOpcode(mesh::RequestOpcode::Evict))).WillOnce(Invoke(eviction_response));

    EXPECT_CALL(lsu_snoop_request_port, put(Pointee(Field(&MemReqObj::phys_addr, 0x11111100_pa))));

    tick(50);
}

/* Test eviction will invalidate ICF.
 *
 *   1. Bring a line into the L2 via the ICF.
 *   2. Evict it with another line.
 *   3. Back invalidation should be generated to the core.
 */
TEST_F(OneWayL2CTestLS, EvictionWillInvalidateICF)
{
    auto [req1, resp1] = make_req_resp(0, 0x11111100_pa, MemOp::Requester::ICF, MemOp::Type::READ);
    auto [req2, resp2_spec, resp2] = make_req_miss_resp(0, 0x91111100_pa, MemOp::Requester::LSU, MemOp::Type::READ);

    EXPECT_CALL(icf_req_port, can_take()).WillOnce(Return(true)).WillRepeatedly(Return(false));

    EXPECT_CALL(icf_req_port, take())
        .WillOnce(Return(uarch_common::Request::Fill(req1, uarch_common::Request::L1Contents())));

    EXPECT_CALL(
        mesh_port.rx.req,
        put(AllOf(
            HasOpcode(mesh::RequestOpcode::ReadNotSharedDirty), HasAddress(0x11111100_pa), HasLikelyShared(true))))
        .WillOnce(Invoke(return_data(12, mesh::chi::FinalState::SC)));

    auto evicting_read = [this, req = req2]() {
        EXPECT_CALL(lsu_read_req_port, can_take()).WillOnce(Return(true)).RetiresOnSaturation();

        EXPECT_CALL(lsu_read_req_port, take())
            .WillOnce(Return(uarch_common::Request::Fill(req, uarch_common::Request::L1Contents())));

        EXPECT_CALL(mesh_port.rx.req,
                    put(AllOf(HasOpcode(mesh::RequestOpcode::ReadNotSharedDirty), HasAddress(0x91111100_pa))))
            .WillOnce(Invoke(return_data(13, mesh::chi::FinalState::SC)));
    };

    EXPECT_CALL(icf_resp_port, put(MemRespMatcher(resp1))).WillOnce(InvokeWithoutArgs(evicting_read));

    {
        InSequence seq;
        EXPECT_CALL(lsu_read_resp_port, put(MemRespMatcher(resp2_spec)));
        EXPECT_CALL(lsu_read_resp_port, put(MemRespMatcher(resp2)));
    }

    auto eviction_response = [this](mesh::MeshReq req) {
        auto rsp = make<mesh::MeshRsp>(NodeId{1}, NodeId{0}, req->TxnID, mesh::ResponseOpcode::Comp);
        rsp->Resp = RespComp_I;

        EXPECT_CALL(mesh_port.tx.rsp, can_take()).WillOnce(Return(true)).RetiresOnSaturation();

        EXPECT_CALL(mesh_port.tx.rsp, take()).WillOnce(Return(rsp));
    };

    EXPECT_CALL(mesh_port.rx.req, put(HasOpcode(mesh::RequestOpcode::Evict))).WillOnce(Invoke(eviction_response));

    EXPECT_CALL(icf_snoop_port, put(Pointee(Field(&MemReqObj::phys_addr, 0x11111100_pa))));

    tick(50);
}

TEST_F(OneWayL2CTestLS, EvictionWillInvalidateICF_LS)
{
    auto req1 = make<MemReq>(0, 0x11111100_pa, MemOp::Requester::ICF, MemOp::Type::READ);
    auto req2 = make<MemReq>(0, 0x91111100_pa, MemOp::Requester::ICF, MemOp::Type::READ);

    EXPECT_CALL(icf_req_port, can_take()).WillOnce(Return(true)).WillRepeatedly(Return(false));

    EXPECT_CALL(icf_req_port, take())
        .WillOnce(Return(uarch_common::Request::Fill(req1, uarch_common::Request::L1Contents())));

    EXPECT_CALL(
        mesh_port.rx.req,
        put(AllOf(
            HasOpcode(mesh::RequestOpcode::ReadNotSharedDirty), HasAddress(0x11111100_pa), HasLikelyShared(true))))
        .WillOnce(Invoke(return_data(12, mesh::chi::FinalState::SC)));

    auto evicting_read = [this, req = req2]() {
        EXPECT_CALL(icf_req_port, can_take()).WillOnce(Return(true)).RetiresOnSaturation();

        uarch_common::Request::L1Contents l1_contents;
        l1_contents.add(0x11111100_pa);
        EXPECT_CALL(icf_req_port, take()).WillOnce(Return(uarch_common::Request::Fill(req, l1_contents)));

        EXPECT_CALL(
            mesh_port.rx.req,
            put(AllOf(
                HasOpcode(mesh::RequestOpcode::ReadNotSharedDirty), HasAddress(0x91111100_pa), HasLikelyShared(true))))
            .WillOnce(Invoke(return_data(13, mesh::chi::FinalState::SC)));
    };

    EXPECT_CALL(icf_resp_port, put(MemReqMatcher(req1))).WillOnce(InvokeWithoutArgs(evicting_read));

    EXPECT_CALL(icf_resp_port, put(MemReqMatcher(req2)));

    auto eviction_response = [this](mesh::MeshReq req) {
        auto rsp = make<mesh::MeshRsp>(NodeId{1}, NodeId{0}, req->TxnID, mesh::ResponseOpcode::Comp);
        rsp->Resp = RespComp_I;

        EXPECT_CALL(mesh_port.tx.rsp, can_take()).WillOnce(Return(true)).RetiresOnSaturation();

        EXPECT_CALL(mesh_port.tx.rsp, take()).WillOnce(Return(rsp));
    };

    EXPECT_CALL(mesh_port.rx.req, put(HasOpcode(mesh::RequestOpcode::Evict))).WillOnce(Invoke(eviction_response));

    EXPECT_CALL(icf_snoop_port, put(Pointee(Field(&MemReqObj::phys_addr, 0x11111100_pa))));

    tick(50);

    EXPECT_EQ(l2c.peek_state(0x91111100_pa), l2::LineState::SC);
}

TEST_F(OneWayFillAtFillL2CTest, ReadEvictsOlderDirtyLine)
{
    PhysAddr pa1(0x11111111);
    PhysAddr pa2(0x91111111);

    auto lsu_streq = make<MemReq>(0, pa1, MemOp::Requester::LSU, MemOp::Type::WRITE);
    auto [lsu_evreq, lsu_spec_evresp, lsu_evresp] =
        make_req_miss_resp(1, pa2, MemOp::Requester::LSU, MemOp::Type::READ);

    EXPECT_CALL(lsu_write_req_port, can_take())
        .WillOnce(Return(true)) // write miss allocates L2 line dirty
        .WillRepeatedly(Return(false));
    EXPECT_CALL(lsu_write_req_port, peek(0)).WillOnce(ReturnRef(lsu_streq));
    EXPECT_CALL(lsu_write_req_port, take()).WillOnce(Return(lsu_streq));

    {
        InSequence seq;
        EXPECT_CALL(lsu_read_req_port, can_take()).Times(14).WillRepeatedly(Return(false));
        EXPECT_CALL(lsu_read_req_port, can_take())
            .WillOnce(Return(true)) // read miss which fills second way
            .WillRepeatedly(Return(false));
    }

    EXPECT_CALL(lsu_read_req_port, take())
        .WillOnce(Return(uarch_common::Request::Fill(lsu_evreq, uarch_common::Request::L1Contents())));

    auto read_response = [this](mesh::MeshReq req) {
        auto dat1 = make<mesh::MeshDat>(req->TxnID, mesh::DataOpcode::CompData);
        dat1->Resp = mesh::chi::RespComp_UC;
        dat1->DBID = 12;
        dat1->DataID = 0;

        auto dat2 = make<mesh::MeshDat>(req->TxnID, mesh::DataOpcode::CompData);
        dat2->Resp = mesh::chi::RespComp_UC;
        dat2->DBID = 12;
        dat2->DataID = 2;

        EXPECT_CALL(mesh_port.tx.dat, can_take()).WillOnce(Return(true)).WillOnce(Return(true)).RetiresOnSaturation();
        EXPECT_CALL(mesh_port.tx.dat, take()).WillOnce(Return(dat1)).WillOnce(Return(dat2)).RetiresOnSaturation();
        EXPECT_CALL(mesh_port.rx.rsp, put(AllOf(HasTxnID(12), HasOpcode(mesh::ResponseOpcode::CompAck))))
            .RetiresOnSaturation();
    };

    auto write_response = [this](mesh::MeshReq req) {
        auto rsp = make<mesh::MeshRsp>(NodeId{1}, NodeId{0}, req->TxnID, mesh::ResponseOpcode::CompDBIDResp);
        rsp->DBID = 300;

        EXPECT_CALL(mesh_port.tx.rsp, can_take()).WillOnce(Return(true)).RetiresOnSaturation();
        EXPECT_CALL(mesh_port.tx.rsp, take()).WillOnce(Return(rsp)).RetiresOnSaturation();
        EXPECT_CALL(mesh_port.rx.dat,
                    put(AllOf(HasOpcode(mesh::DataOpcode::CopyBackWrData), HasTxnID(300), HasPassDirty(true))))
            .Times(2);
    };

    {
        InSequence mem_req_seq;

        EXPECT_CALL(mesh_port.rx.req,
                    put(AllOf(HasOpcode(mesh::RequestOpcode::ReadUnique), HasAddress(pa1.line_address()))))
            .WillOnce(Invoke(read_response));

        EXPECT_CALL(mesh_port.rx.req,
                    put(AllOf(HasOpcode(mesh::RequestOpcode::ReadNotSharedDirty), HasAddress(pa2.line_address()))))
            .WillOnce(Invoke(read_response));

        EXPECT_CALL(mesh_port.rx.req,
                    put(AllOf(HasOpcode(mesh::RequestOpcode::WriteBackFull), HasAddress(pa1.line_address()))))
            .WillOnce(Invoke(write_response));
    }

    {
        InSequence seq;
        EXPECT_CALL(lsu_read_resp_port, put(MemRespMatcher(lsu_spec_evresp)));
        EXPECT_CALL(lsu_read_resp_port, put(MemRespMatcher(lsu_evresp))).RetiresOnSaturation();
    }

    tick(60);
}

/* Test eviction will invalidate LSU.
 *
 *   1. Bring a line into the L2 via the LSU.
 *   2. Evict it with another line.
 *   3. Back invalidation should be generated to the core.
 */
TEST_F(OneWayFillAtFillL2CTest, EvictionWillInvalidateLSU)
{
    auto [req1, resp1_spec, resp1] = make_req_miss_resp(0, 0x11111100_pa, MemOp::Requester::LSU, MemOp::Type::READ);
    auto [req2, resp2_spec, resp2] = make_req_miss_resp(0, 0x91111100_pa, MemOp::Requester::LSU, MemOp::Type::READ);

    EXPECT_CALL(lsu_read_req_port, can_take()).WillOnce(Return(true)).WillRepeatedly(Return(false));

    EXPECT_CALL(lsu_read_req_port, take())
        .WillOnce(Return(uarch_common::Request::Fill(req1, uarch_common::Request::L1Contents())));

    EXPECT_CALL(mesh_port.rx.req,
                put(AllOf(HasOpcode(mesh::RequestOpcode::ReadNotSharedDirty), HasAddress(0x11111100_pa))))
        .WillOnce(Invoke(return_data(12, mesh::chi::FinalState::SC)));

    auto evicting_read = [this, req = req2]() {
        EXPECT_CALL(lsu_read_req_port, can_take()).WillOnce(Return(true)).RetiresOnSaturation();

        uarch_common::Request::L1Contents l1_contents;
        l1_contents.add(0x11111100_pa);
        EXPECT_CALL(lsu_read_req_port, take()).WillOnce(Return(uarch_common::Request::Fill(req, l1_contents)));

        EXPECT_CALL(mesh_port.rx.req,
                    put(AllOf(HasOpcode(mesh::RequestOpcode::ReadNotSharedDirty), HasAddress(0x91111100_pa))))
            .WillOnce(Invoke(return_data(13, mesh::chi::FinalState::SC)));
    };

    {
        InSequence seq;
        EXPECT_CALL(lsu_read_resp_port, put(MemRespMatcher(resp1_spec)));
        EXPECT_CALL(lsu_read_resp_port, put(MemRespMatcher(resp1))).WillOnce(InvokeWithoutArgs(evicting_read));

        EXPECT_CALL(lsu_read_resp_port, put(MemRespMatcher(resp2_spec)));
        EXPECT_CALL(lsu_read_resp_port, put(MemRespMatcher(resp2)));
    }

    auto eviction_response = [this](mesh::MeshReq req) {
        auto rsp = make<MeshRsp>(NodeId{1}, NodeId{0}, req->TxnID, ResponseOpcode::Comp);
        rsp->Resp = RespComp_I;

        EXPECT_CALL(mesh_port.tx.rsp, can_take()).WillOnce(Return(true)).RetiresOnSaturation();

        EXPECT_CALL(mesh_port.tx.rsp, take()).WillOnce(Return(rsp));
    };

    EXPECT_CALL(mesh_port.rx.req, put(HasOpcode(mesh::RequestOpcode::Evict))).WillOnce(Invoke(eviction_response));

    EXPECT_CALL(lsu_snoop_request_port, put(Pointee(Field(&MemReqObj::phys_addr, 0x11111100_pa))));

    tick(50);
}

TEST_F(OneWayParallelEvictL2CTest, ReadEvictsOlderDirtyLine)
{
    PhysAddr pa1(0x11111111);
    PhysAddr pa2(0x91111111);

    auto lsu_streq = make<MemReq>(0, pa1, MemOp::Requester::LSU, MemOp::Type::WRITE);
    auto [lsu_evreq, lsu_spec_evresp, lsu_evresp] =
        make_req_miss_resp(1, pa2, MemOp::Requester::LSU, MemOp::Type::READ);

    EXPECT_CALL(lsu_write_req_port, can_take())
        .WillOnce(Return(true)) // write miss allocates L2 line dirty
        .WillRepeatedly(Return(false));
    EXPECT_CALL(lsu_write_req_port, peek(0)).WillOnce(ReturnRef(lsu_streq));
    EXPECT_CALL(lsu_write_req_port, take()).WillOnce(Return(lsu_streq));

    {
        InSequence seq;
        EXPECT_CALL(lsu_read_req_port, can_take()).Times(14).WillRepeatedly(Return(false));
        EXPECT_CALL(lsu_read_req_port, can_take())
            .WillOnce(Return(true)) // read miss which fills second way
            .WillRepeatedly(Return(false));
    }

    EXPECT_CALL(lsu_read_req_port, take())
        .WillOnce(Return(uarch_common::Request::Fill(lsu_evreq, uarch_common::Request::L1Contents())));

    auto read_response = [this](mesh::MeshReq req) {
        auto dat1 = make<mesh::MeshDat>(req->TxnID, mesh::DataOpcode::CompData);
        dat1->Resp = mesh::chi::RespComp_UC;
        dat1->DBID = 12;
        dat1->DataID = 0;

        auto dat2 = make<mesh::MeshDat>(req->TxnID, mesh::DataOpcode::CompData);
        dat2->Resp = mesh::chi::RespComp_UC;
        dat2->DBID = 12;
        dat2->DataID = 2;

        EXPECT_CALL(mesh_port.tx.dat, can_take()).WillOnce(Return(true)).WillOnce(Return(true)).RetiresOnSaturation();
        EXPECT_CALL(mesh_port.tx.dat, take()).WillOnce(Return(dat1)).WillOnce(Return(dat2)).RetiresOnSaturation();
        EXPECT_CALL(mesh_port.rx.rsp, put(AllOf(HasTxnID(12), HasOpcode(mesh::ResponseOpcode::CompAck))))
            .RetiresOnSaturation();
    };

    auto write_response = [this](mesh::MeshReq req) {
        auto rsp = make<mesh::MeshRsp>(NodeId{1}, NodeId{0}, req->TxnID, mesh::ResponseOpcode::CompDBIDResp);
        rsp->DBID = 300;

        EXPECT_CALL(mesh_port.tx.rsp, can_take()).WillOnce(Return(true)).RetiresOnSaturation();
        EXPECT_CALL(mesh_port.tx.rsp, take()).WillOnce(Return(rsp)).RetiresOnSaturation();
        EXPECT_CALL(mesh_port.rx.dat,
                    put(AllOf(HasOpcode(mesh::DataOpcode::CopyBackWrData), HasTxnID(300), HasPassDirty(true))))
            .Times(2);
    };

    {
        InSequence mem_req_seq;

        EXPECT_CALL(mesh_port.rx.req,
                    put(AllOf(HasOpcode(mesh::RequestOpcode::ReadUnique), HasAddress(pa1.line_address()))))
            .WillOnce(Invoke(read_response));

        EXPECT_CALL(mesh_port.rx.req,
                    put(AllOf(HasOpcode(mesh::RequestOpcode::ReadNotSharedDirty), HasAddress(pa2.line_address()))))
            .WillOnce(Invoke(read_response));

        EXPECT_CALL(mesh_port.rx.req,
                    put(AllOf(HasOpcode(mesh::RequestOpcode::WriteBackFull), HasAddress(pa1.line_address()))))
            .WillOnce(Invoke(write_response));
    }

    {
        InSequence seq;
        EXPECT_CALL(lsu_read_resp_port, put(MemRespMatcher(lsu_spec_evresp)));
        EXPECT_CALL(lsu_read_resp_port, put(MemRespMatcher(lsu_evresp))).RetiresOnSaturation();
    }

    tick(60);
}

/* Test eviction will invalidate LSU.
 *
 *   1. Bring a line into the L2 via the LSU.
 *   2. Evict it with another line.
 *   3. Back invalidation should be generated to the core.
 */
TEST_F(OneWayParallelEvictL2CTest, EvictionWillInvalidateLSU)
{
    auto [req1, resp1_spec, resp1] = make_req_miss_resp(0, 0x11111100_pa, MemOp::Requester::LSU, MemOp::Type::READ);
    auto [req2, resp2_spec, resp2] = make_req_miss_resp(0, 0x91111100_pa, MemOp::Requester::LSU, MemOp::Type::READ);

    EXPECT_CALL(lsu_read_req_port, can_take()).WillOnce(Return(true)).WillRepeatedly(Return(false));

    EXPECT_CALL(lsu_read_req_port, take())
        .WillOnce(Return(uarch_common::Request::Fill(req1, uarch_common::Request::L1Contents())));

    EXPECT_CALL(mesh_port.rx.req,
                put(AllOf(HasOpcode(mesh::RequestOpcode::ReadNotSharedDirty), HasAddress(0x11111100_pa))))
        .WillOnce(Invoke(return_data(12, mesh::chi::FinalState::SC)));

    auto evicting_read = [this, req = req2]() {
        EXPECT_CALL(lsu_read_req_port, can_take()).WillOnce(Return(true)).RetiresOnSaturation();

        uarch_common::Request::L1Contents l1_contents;
        l1_contents.add(0x11111100_pa);
        EXPECT_CALL(lsu_read_req_port, take()).WillOnce(Return(uarch_common::Request::Fill(req, l1_contents)));

        EXPECT_CALL(mesh_port.rx.req,
                    put(AllOf(HasOpcode(mesh::RequestOpcode::ReadNotSharedDirty), HasAddress(0x91111100_pa))))
            .WillOnce(Invoke(return_data(13, mesh::chi::FinalState::SC)));
    };

    {
        InSequence seq;
        EXPECT_CALL(lsu_read_resp_port, put(MemRespMatcher(resp1_spec)));
        EXPECT_CALL(lsu_read_resp_port, put(MemRespMatcher(resp1))).WillOnce(InvokeWithoutArgs(evicting_read));

        EXPECT_CALL(lsu_read_resp_port, put(MemRespMatcher(resp2_spec)));
        EXPECT_CALL(lsu_read_resp_port, put(MemRespMatcher(resp2)));
    }

    auto eviction_response = [this](mesh::MeshReq req) {
        auto rsp = make<MeshRsp>(NodeId{1}, NodeId{0}, req->TxnID, ResponseOpcode::Comp);
        rsp->Resp = RespComp_I;

        EXPECT_CALL(mesh_port.tx.rsp, can_take()).WillOnce(Return(true)).RetiresOnSaturation();

        EXPECT_CALL(mesh_port.tx.rsp, take()).WillOnce(Return(rsp));
    };

    EXPECT_CALL(mesh_port.rx.req, put(HasOpcode(mesh::RequestOpcode::Evict))).WillOnce(Invoke(eviction_response));

    EXPECT_CALL(lsu_snoop_request_port, put(Pointee(Field(&MemReqObj::phys_addr, 0x11111100_pa))));

    tick(50);
}

TEST_F(OneWayL2CTestLS, WriteAfterReadShared_LS)
{
    auto req2 = make<MemReq>(0, 0x11111100_pa, MemOp::Requester::LSU, MemOp::Type::WRITE);

    l2c.preload_with_inclusion_bits(0x11111100_pa, l2::LineState::SC, MemOp::Requester::ICF);

    EXPECT_CALL(lsu_write_req_port, can_take()).WillOnce(Return(true)).WillRepeatedly(Return(false));
    EXPECT_CALL(lsu_write_req_port, peek(0)).WillOnce(ReturnRef(req2));
    EXPECT_CALL(lsu_write_req_port, take()).WillOnce(Return(req2));

    EXPECT_CALL(mesh_port.rx.req, put(AllOf(HasOpcode(mesh::RequestOpcode::ReadUnique), HasAddress(0x11111100_pa))))
        .WillOnce(Invoke(return_data(17, mesh::chi::FinalState::UC)));

    tick(100);
    EXPECT_EQ(l2c.peek_state(0x11111100_pa), l2::LineState::UD);
    EXPECT_EQ(l2c.is_l1d_set(0x11111100_pa), false);
    EXPECT_EQ(l2c.is_l1i_set(0x11111100_pa), false);
}

TEST_F(PrefetcherL2CTest, Drop)
{
    // Prefetch already in l2 or fb is dropped
    auto req = make<MemReq>(0, 0x1000_pa, MemOp::Requester::PREFETCH);
    l2c.preload(0x1000_pa, l2::LineState::SC);

    prefetcher->exercise(req);
    tick(10);

    EXPECT_EQ(prefetcher->drops.size(), 1);
    EXPECT_EQ(prefetcher->drops[0]->phys_addr, 0x1000_pa);
}

TEST_F(PrefetcherL2CTest, PrefetchTrainMissL2)
{
    auto req1 = make<MemReq>(0, 0x11111110_pa, MemOp::Requester::LSU);
    prefetcher->exercise(req1);

    auto return_data = [this](mesh::MeshReq req) {
        auto dat1 = make<mesh::MeshDat>(0, mesh::DataOpcode::CompData);
        dat1->Resp = mesh::chi::RespComp_SC;
        dat1->DBID = 12;
        dat1->DataID = 0;

        auto dat2 = make<mesh::MeshDat>(0, mesh::DataOpcode::CompData);
        dat2->Resp = mesh::chi::RespComp_SC;
        dat2->DBID = 12;
        dat2->DataID = 2;

        EXPECT_CALL(mesh_port.tx.dat, can_take()).WillOnce(Return(true)).WillOnce(Return(true)).RetiresOnSaturation();
        EXPECT_CALL(mesh_port.tx.dat, take()).WillOnce(Return(dat1)).WillOnce(Return(dat2)).RetiresOnSaturation();
    };

    EXPECT_CALL(
        mesh_port.rx.req,
        put(AllOf(
            HasOpcode(mesh::RequestOpcode::ReadNotSharedDirty), HasAddress(0x11111100_pa), HasLikelyShared(false))))
        .WillOnce(Invoke(return_data));

    tick(100);

    EXPECT_EQ(prefetcher->fills.size(), 1);
    EXPECT_EQ(prefetcher->trains.size(), 1);
}

TEST_F(L2CTest, LSUReadMissRetry)
{
    auto [req, spec_resp, final_resp] = make_req_miss_resp(0, 0x11111110_pa, MemOp::Requester::LSU);

    EXPECT_CALL(lsu_read_req_port, can_take()).WillOnce(Return(true)).WillRepeatedly(Return(false));
    EXPECT_CALL(lsu_read_req_port, take())
        .WillOnce(Return(uarch_common::Request::Fill(req, uarch_common::Request::L1Contents())));

    auto respond_with_retry = [this](MeshReq req) {
        auto retry = make<MeshRsp>(NodeId{1}, req->SrcID, req->TxnID, ResponseOpcode::RetryAck);
        retry->PCrdType = 0;

        auto grant = make<MeshRsp>(NodeId{1}, req->SrcID, req->TxnID, ResponseOpcode::PCrdGrant);
        grant->PCrdType = 0;

        EXPECT_CALL(mesh_port.tx.rsp, can_take())
            .WillOnce(Return(true))
            .WillOnce(Return(false))
            .WillOnce(Return(false))
            .WillOnce(Return(false))
            .WillOnce(Return(false))
            .WillOnce(Return(false))
            .WillOnce(Return(false))
            .WillOnce(Return(false))
            .WillOnce(Return(false))
            .WillOnce(Return(true))
            .RetiresOnSaturation();

        EXPECT_CALL(mesh_port.tx.rsp, take()).WillOnce(Return(retry)).WillOnce(Return(grant)).RetiresOnSaturation();
    };

    {
        InSequence seq;

        EXPECT_CALL(lsu_read_resp_port, put(MemRespMatcher(spec_resp)));

        EXPECT_CALL(mesh_port.rx.req,
                    put(AllOf(HasOpcode(mesh::RequestOpcode::ReadNotSharedDirty),
                              HasAddress(0x11111100_pa),
                              HasLikelyShared(false),
                              HasAllowRetry(true))))
            .WillOnce(Invoke(respond_with_retry));

        EXPECT_CALL(mesh_port.rx.req,
                    put(AllOf(HasOpcode(mesh::RequestOpcode::ReadNotSharedDirty),
                              HasAddress(0x11111100_pa),
                              HasLikelyShared(false),
                              HasAllowRetry(false))))
            .WillOnce(Invoke(return_data(12, mesh::chi::FinalState::SC)));

        EXPECT_CALL(lsu_read_resp_port, put(MemRespMatcher(final_resp)));
    }

    tick(100);
}

/** This is a test of the write priority override mechanism. We set up a steady
 *  stream of reads and then ensure that a write still gets through. The reads
 *  are all pre-loaded into the cache so they should hit, while the write should
 *  miss and generate a system request.
 *
 *  It's a little tricky to test that the priority mechanism is really working
 *  from the outside like this. So I've added a companion test where the threshold
 *  for write priority is cranked up high enough that write *shouldn't* get through
 *  in the time allotted. This is "WritesGetStarved", below.
 */
TEST_F(L2CTest, WritesEventuallyTakePriority)
{
    l2c.preload(0x1000_pa, l2::LineState::UC);
    l2c.preload(0x1040_pa, l2::LineState::UC);
    l2c.preload(0x1080_pa, l2::LineState::UC);
    l2c.preload(0x10c0_pa, l2::LineState::UC);

    auto load_addr = 0x1000_pa;
    auto load_id = 1;
    auto make_load = [this, &load_addr, &load_id]() {
        auto req = make<MemReq>(load_id++, load_addr, MemOp::Requester::LSU, MemOp::Type::READ);
        load_addr += 0x40;
        if (load_addr > 0x10c0_pa)
            load_addr = 0x1000_pa;
        return uarch_common::Request::Fill(req, uarch_common::Request::L1Contents());
    };

    EXPECT_CALL(lsu_read_req_port, can_take()).Times(64).WillRepeatedly(Return(true)).RetiresOnSaturation();

    EXPECT_CALL(lsu_read_req_port, take()).Times(64).WillRepeatedly(Invoke(make_load));

    EXPECT_CALL(lsu_write_req_port, can_take()).WillOnce(Return(true)).RetiresOnSaturation();

    auto write_req = make<MemReq>(0, 0x2000_pa, MemOp::Requester::LSU, MemOp::Type::WRITE);

    EXPECT_CALL(lsu_write_req_port, peek(0)).WillOnce(ReturnRef(write_req));
    EXPECT_CALL(lsu_write_req_port, take()).WillOnce(Return(write_req));

    EXPECT_CALL(mesh_port.rx.req, put(AllOf(HasOpcode(mesh::RequestOpcode::ReadUnique), HasAddress(0x2000_pa))))
        .WillOnce(Invoke(return_data(3, mesh::chi::FinalState::UC)));

    tick(100);
}

class ReallySlowWrites : public L2CTest
{
public:
    ReallySlowWrites()
        : L2CTest({{"~l2c\\.banks\\..*fb_credits_[0-9]+\\.size", 8},
                   {"l2c.cbusy.throttle_max", 64},
                   {"l2c.sam.tgt_lut", "[0]"_json},
                   {"l2c.write_priority_age_threshold", 256}})
    {
    }
};

/** This test pairs with "WritesEventuallyTakePiority", above, to try to check that
 *  the write priority mechanism is working. In this case we have a really high
 *  threshold for prioritizing writes so that we can't get one through.
 */
TEST_F(ReallySlowWrites, WritesGetStarved)
{
    l2c.preload(0x1000_pa, l2::LineState::UC);
    l2c.preload(0x1040_pa, l2::LineState::UC);
    l2c.preload(0x1080_pa, l2::LineState::UC);
    l2c.preload(0x10c0_pa, l2::LineState::UC);

    auto load_addr = 0x1000_pa;
    auto load_id = 1;
    auto make_load = [this, &load_addr, &load_id]() {
        auto req = make<MemReq>(load_id++, load_addr, MemOp::Requester::LSU, MemOp::Type::READ);
        load_addr += 0x40;
        if (load_addr > 0x10c0_pa)
            load_addr = 0x1000_pa;
        return uarch_common::Request::Fill(req, uarch_common::Request::L1Contents());
    };

    EXPECT_CALL(lsu_read_req_port, can_take()).Times(64).WillRepeatedly(Return(true)).RetiresOnSaturation();

    EXPECT_CALL(lsu_read_req_port, take()).Times(64).WillRepeatedly(Invoke(make_load));

    EXPECT_CALL(lsu_write_req_port, can_take()).WillOnce(Return(true)).RetiresOnSaturation();

    auto write_req = make<MemReq>(0, 0x2000_pa, MemOp::Requester::LSU, MemOp::Type::WRITE);

    EXPECT_CALL(lsu_write_req_port, peek(0)).WillOnce(ReturnRef(write_req));
    EXPECT_CALL(lsu_write_req_port, take()).WillOnce(Return(write_req));

    EXPECT_CALL(mesh_port.rx.req, put(AllOf(HasOpcode(mesh::RequestOpcode::ReadUnique), HasAddress(0x2000_pa))))
        .Times(0);

    tick(64);
}

} // namespace test_L2C
} // namespace ampere
