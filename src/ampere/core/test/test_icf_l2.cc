//
// Copyright (c) 2018-2019 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//

#include "ampere/core/icf_l2.h"
#include "ampere/core/testing/mock_l2.h"
#include "ampere/sc/testing/test_module.h"

namespace bfs = boost::filesystem;

namespace ampere {
namespace icf_l2 {

using namespace core;
using namespace ::testing;
using ::testing::InSequence;

class ICF_L2Test : public TestModule
{
public:
    void SetUp() override
    {
        icf_l2.icf_request.bind(icfreq_port);
        icf_l2.icf_response.bind(icfresp_port);
        icf_l2.memory_request.bind(memreq_port);
        icf_l2.memory_response.bind(memresp_port);
    }

    ICF_L2 icf_l2{"icf_l2"};

    NiceMock<MockMemReqReader> icfreq_port;
    NiceMock<MockMemReqWriter> icfresp_port;
    NiceMock<MockMemReqWriter> memreq_port;
    NiceMock<MockMemReqReader> memresp_port;
};

TEST_F(ICF_L2Test, TestOneRequest)
{
    PhysAddr pa(0x11111111);
    auto memreq = make<MemReq>(0, pa);

    EXPECT_CALL(icfreq_port, can_take()).WillOnce(Return(true));
    EXPECT_CALL(icfreq_port, take()).WillOnce(Return(memreq));
    EXPECT_CALL(memreq_port, put(Pointee(Field(&MemReqObj::phys_addr, pa))));
    EXPECT_CALL(memresp_port, can_take()).WillOnce(Return(false));

    icf_l2.run();
}

TEST_F(ICF_L2Test, TestOneRequestToMemory)
{
    PhysAddr pa(0x11111111);
    auto memreq = make<MemReq>(0, pa);

    EXPECT_CALL(icfreq_port, can_take()).WillOnce(Return(true)).WillOnce(Return(false));
    EXPECT_CALL(icfreq_port, take()).WillOnce(Return(memreq));
    EXPECT_CALL(memreq_port, put(Pointee(Field(&MemReqObj::phys_addr, pa))));
    EXPECT_CALL(memresp_port, can_take()).WillOnce(Return(false)).WillOnce(Return(true));
    EXPECT_CALL(memresp_port, take()).WillOnce(Return(memreq));
    EXPECT_CALL(icfresp_port, put(Pointee(Field(&MemReqObj::phys_addr, pa))));

    icf_l2.run();
    icf_l2.run();
}

TEST_F(ICF_L2Test, TestOneMissOneHit)
{
    PhysAddr pa(0x11111111);
    auto memreq1 = make<MemReq>(0, pa);
    auto memreq2 = make<MemReq>(1, pa);

    EXPECT_CALL(icfreq_port, can_take()).WillOnce(Return(true)).WillOnce(Return(false)).WillOnce(Return(true));
    EXPECT_CALL(icfreq_port, take()).WillOnce(Return(memreq1)).WillOnce(Return(memreq2));
    EXPECT_CALL(memreq_port, put(Pointee(Field(&MemReqObj::phys_addr, pa))));
    EXPECT_CALL(memresp_port, can_take()).WillOnce(Return(false)).WillOnce(Return(true)).WillOnce(Return(false));
    EXPECT_CALL(memresp_port, take()).WillOnce(Return(memreq1));
    EXPECT_CALL(icfresp_port, put(Pointee(Field(&MemReqObj::phys_addr, pa)))).Times(2);

    icf_l2.run();
    icf_l2.run();
    icf_l2.run();
}

} // namespace icf_l2
} // namespace ampere

/*
 * vim: sw=4:ts=4:et:si:cindent:cino=\:0,(0,g0,t0:
 */
