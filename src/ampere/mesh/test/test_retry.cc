//
// Copyright (c) 2021 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//

#include <ampere/mesh/chi/opcodes.enum.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "ampere/mesh/mesh_req.h"
#include "ampere/mesh/retry.h"
#include "ampere/sc/testing/test_module.h"
#include "ampere/sc/testing/assertions.h"

using ::testing::HasSubstr;

namespace ampere::mesh {

using namespace archx;

class RetryTest : public sc::TestModule
{
public:
    Retry retry{"retry"};
};

TEST_F(RetryTest, CanDefineCreditTypes)
{
    retry.define_credit_type(0, 12);
    retry.define_credit_type(1, 20);

    EXPECT_EQ(retry.get_pool_entry(0).capacity, 12u);
    EXPECT_EQ(retry.get_pool_entry(1).capacity, 20u);
}

TEST_F(RetryTest, RedefinitionOfCreditTypeIsError)
{
    retry.define_credit_type(0, 12);
    ASSERT_ANY_THROW(retry.define_credit_type(0, 20));
}

TEST_F(RetryTest, RequestWithAllowRetryAndPCrdTypeIsIllegal)
{
    retry.define_credit_type(0, 1);
    auto req = make<MeshReq>(NodeId{0}, NodeId{1}, 0, chi::RequestOpcode::ReadNotSharedDirty, 0x1000_pa);
    req->PCrdType = 3;
    EXPECT_AMPERE_FATAL(retry.process_request(req), HasSubstr("PCrdType must be 0 on a request with AllowRetry"));
}

TEST_F(RetryTest, ReceiveRequestWithCreditsAvailable)
{
    retry.define_credit_type(0, 1);
    auto req = make<MeshReq>(NodeId{0}, NodeId{1}, 0, chi::RequestOpcode::ReadNotSharedDirty, 0x1000_pa);
    EXPECT_TRUE(retry.process_request(req).first);
}

TEST_F(RetryTest, ReceiveRequestWithoutCreditsAvailable)
{
    retry.define_credit_type(0, 0);
    auto req = make<MeshReq>(NodeId{0}, NodeId{1}, 0, chi::RequestOpcode::ReadNotSharedDirty, 0x1000_pa);
    EXPECT_FALSE(retry.process_request(req).first);
}

TEST_F(RetryTest, SecondRequestHasNoCredits)
{
    retry.define_credit_type(0, 1);
    auto req1 = make<MeshReq>(NodeId{0}, NodeId{1}, 0, chi::RequestOpcode::ReadNotSharedDirty, 0x1000_pa);
    auto req2 = make<MeshReq>(NodeId{0}, NodeId{1}, 0, chi::RequestOpcode::ReadNotSharedDirty, 0x2000_pa);

    EXPECT_TRUE(retry.process_request(req1).first);
    EXPECT_FALSE(retry.process_request(req2).first);

    EXPECT_EQ(retry.get_pool_entry(0).reserved, 1u);
}

TEST_F(RetryTest, FreeCreditOnDealloc)
{
    retry.define_credit_type(0, 1);
    auto req = make<MeshReq>(NodeId{0}, NodeId{1}, 0, chi::RequestOpcode::ReadNotSharedDirty, 0x1000_pa);

    EXPECT_TRUE(retry.process_request(req).first);
    EXPECT_EQ(retry.get_pool_entry(0).free, 0u);

    auto requester = retry.dealloc(0);
    EXPECT_FALSE(requester);
    EXPECT_EQ(retry.get_pool_entry(0).free, 1u);
}

TEST_F(RetryTest, SecondRequestAcquiresFreedCredit)
{
    retry.define_credit_type(0, 1);
    auto req1 = make<MeshReq>(NodeId{0}, NodeId{1}, 0, chi::RequestOpcode::ReadNotSharedDirty, 0x1000_pa);
    auto req2 = make<MeshReq>(NodeId{0}, NodeId{1}, 10, chi::RequestOpcode::ReadNotSharedDirty, 0x2000_pa);

    EXPECT_TRUE(retry.process_request(req1).first);
    EXPECT_FALSE(retry.process_request(req2).first);

    auto requester = retry.dealloc(0);
    ASSERT_TRUE(requester);
    EXPECT_EQ(requester->src_id, req2->SrcID);
    EXPECT_EQ(requester->txn_id, req2->TxnID);
    EXPECT_EQ(retry.get_pool_entry(0).issued, 1u);
}

TEST_F(RetryTest, FollowUpRequestWithCreditCanProceed)
{
    retry.define_credit_type(0, 1);
    auto req1 = make<MeshReq>(NodeId{0}, NodeId{1}, 0, chi::RequestOpcode::ReadNotSharedDirty, 0x1000_pa);
    auto req2 = make<MeshReq>(NodeId{0}, NodeId{1}, 10, chi::RequestOpcode::ReadNotSharedDirty, 0x2000_pa);

    EXPECT_TRUE(retry.process_request(req1).first);
    EXPECT_FALSE(retry.process_request(req2).first);

    auto requester = retry.dealloc(0);
    ASSERT_TRUE(requester);
    EXPECT_EQ(requester->src_id, req2->SrcID);
    EXPECT_EQ(requester->txn_id, req2->TxnID);
    EXPECT_EQ(retry.get_pool_entry(0).issued, 1u);

    req2->AllowRetry = false;
    req2->PCrdType = 0;

    EXPECT_TRUE(retry.process_request(req2).first);
}

TEST_F(RetryTest, PCrdReturnFreesCredit)
{
    retry.define_credit_type(0, 1);
    auto req1 = make<MeshReq>(NodeId{0}, NodeId{1}, 0, chi::RequestOpcode::ReadNotSharedDirty, 0x1000_pa);
    auto req2 = make<MeshReq>(NodeId{0}, NodeId{1}, 10, chi::RequestOpcode::ReadNotSharedDirty, 0x2000_pa);

    EXPECT_TRUE(retry.process_request(req1).first);
    EXPECT_FALSE(retry.process_request(req2).first);

    retry.dealloc(0);

    EXPECT_EQ(retry.get_pool_entry(0).issued, 1u);
    EXPECT_EQ(retry.get_pool_entry(0).free, 0u);

    auto credit_return = make<MeshReq>(NodeId{0}, NodeId{1}, 0, chi::RequestOpcode::PCrdReturn, 0_pa);
    credit_return->PCrdType = 0;

    EXPECT_TRUE(retry.process_request(credit_return).first);

    EXPECT_EQ(retry.get_pool_entry(0).issued, 0u);
    EXPECT_EQ(retry.get_pool_entry(0).free, 1u);
}

TEST_F(RetryTest, CantFreeNonIssuedCredit)
{
    retry.define_credit_type(0, 1);
    auto credit_return = make<MeshReq>(NodeId{0}, NodeId{1}, 0, chi::RequestOpcode::PCrdReturn, 0_pa);
    credit_return->PCrdType = 0;

    EXPECT_AMPERE_FATAL(retry.process_request(credit_return), HasSubstr("got a PCrdReturn with no credits issued"));
}

TEST_F(RetryTest, PrefetchTgtDoesNotGetACredit)
{
    retry.define_credit_type(0, 0);
    auto req = make<MeshReq>(NodeId{0}, NodeId{1}, 0, chi::RequestOpcode::PrefetchTgt, 0x1000_pa);

    EXPECT_FALSE(retry.process_request(req).first);  // can't proceed, but...
    EXPECT_EQ(retry.get_pool_entry(0).reserved, 0u); // no reserved credits
}

class RetryWithAlternateCreditType : public Retry
{
public:
    using Retry::Retry;

protected:
    int request_to_pcrdtype(const MeshReq& req) override { return 3; }
};

class RetryWithAlternateCreditTypeTest : public sc::TestModule
{
public:
    RetryWithAlternateCreditType retry{"retry"};
};

TEST_F(RetryWithAlternateCreditTypeTest, PCrdReturnFreesCredit)
{
    retry.define_credit_type(3, 1);
    auto req1 = make<MeshReq>(NodeId{0}, NodeId{1}, 0, chi::RequestOpcode::ReadNotSharedDirty, 0x1000_pa);
    auto req2 = make<MeshReq>(NodeId{0}, NodeId{1}, 10, chi::RequestOpcode::ReadNotSharedDirty, 0x2000_pa);

    auto [can_process, pcrd_type] = retry.process_request(req1);
    EXPECT_TRUE(can_process);
    EXPECT_EQ(pcrd_type, 3);

    auto [can_process2, pcrd_type2] = retry.process_request(req2);
    EXPECT_FALSE(can_process2);
    EXPECT_EQ(pcrd_type2, 3);

    retry.dealloc(3);

    EXPECT_EQ(retry.get_pool_entry(3).issued, 1u);
    EXPECT_EQ(retry.get_pool_entry(3).free, 0u);

    auto credit_return = make<MeshReq>(NodeId{0}, NodeId{1}, 0, chi::RequestOpcode::PCrdReturn, 0_pa);
    credit_return->PCrdType = 3;

    auto [can_process3, pcrd_type3] = retry.process_request(credit_return);
    EXPECT_TRUE(can_process3);
    EXPECT_EQ(pcrd_type3, 3);

    EXPECT_EQ(retry.get_pool_entry(3).issued, 0u);
    EXPECT_EQ(retry.get_pool_entry(3).free, 1u);
}

} // namespace ampere::mesh
