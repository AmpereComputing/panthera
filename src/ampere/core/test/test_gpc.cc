//
// Copyright (c) 2018-2019 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//

#include "ampere/archx/instruction_simple.h"
#include "ampere/archx/testing/mock_driver.h"
#include "ampere/core/gpc.h"
#include "ampere/core/instruction.h"
#include "ampere/core/testing/uop_generator.h"
#include "ampere/core/testing/uop_consumer.h"
#include "ampere/sc/testing/clock_dummy.h"
#include "ampere/core/testing/mock_flush.h"
#include "ampere/sc/testing/test_module.h"
#include "ampere/core/testing/uops.h"

namespace ampere {
namespace gpctest {

using namespace core;
using namespace ::testing;

class GPCTest : public TestModule
{
protected:
    GPC gpc{"gpc"};
    MockDriver driver;
    NiceMock<MockTaker<core::Uop>> dispatch;
    NiceMock<MockTaker<core::Uop>> uop_mispredict[8];
    NiceMock<MockTaker<core::Uop>> uop_indirect_mispredict[8];
    NiceMock<MockTaker<core::Uop>> uop_raw_scrub;
    NiceMock<MockTaker<core::Uop>> commit;
    NiceMock<MockFlushReader> flush_reader;
    NiceMock<MockPutter<core::Uop>> uop_return;
    NiceMock<MockTaker<core::Uop>> idr_flush;
    NiceMock<MockPutter<uint>> retired_writer;
    NiceMock<MockPutter<uint64_t>> uint64_writer;
    NiceMock<MockPutter<bool>> bool_writer;
    NiceMock<MockFlushWriter> flush_writer;

    GPCTest() : TestModule{{"gpc.commit_freeze_cycles", 1}} {}

    void SetUp() override
    {
        gpc.driver_port.bind(driver);
        gpc.dispatch_port.bind(dispatch);
        gpc.commit_port.bind(commit);
        gpc.loads_committed_port.bind(retired_writer);
        gpc.stores_committed_port.bind(retired_writer);
        gpc.lsu_next_to_commit_port.bind(uint64_writer);
        gpc.branches_committed_port.bind(retired_writer);
        gpc.instructions_committed_port.bind(retired_writer);
        gpc.flush_port.bind(flush_writer);
        gpc.flush_read_port.bind(flush_reader);
        gpc.rob_empty_out.bind(bool_writer);
        gpc.flush_req_from_idr.bind(idr_flush);
        for (int i = 0; i < gpc.num_execution_units; i++) {
            gpc.mispredict_port[i].bind(uop_mispredict[i]);
            gpc.mispredict_indirect_port[i].bind(uop_indirect_mispredict[i]);
        }
        gpc.raw_scrub_port.bind(uop_raw_scrub);
        gpc.idr_uop_return.bind(uop_return);
        archx::Instruction::set_decoders({"furiosa"});
    }
};

TEST_F(GPCTest, testDispatch)
{
    auto uop = create_nop_uop();
    uop->alloc_id = 7;

    EXPECT_CALL(dispatch, can_take()).WillOnce(Return(true)).WillRepeatedly(Return(false));
    EXPECT_CALL(dispatch, take()).WillOnce(Return(uop));
    tick();
}

TEST_F(GPCTest, testDispatchAndCommit)
{
    auto uop = create_nop_uop();
    uop->alloc_id = 7;
    get_environment()->event_stream->enable();

    EXPECT_CALL(dispatch, can_take()).WillOnce(Return(true)).WillRepeatedly(Return(false));
    EXPECT_CALL(dispatch, take()).WillOnce(Return(uop));
    EXPECT_CALL(driver, commit(_)).Times(1);

    tick();
    uop->complete = true;
    tick();

    ASSERT_EQ(1, event_count("gpc.uop_commit"));
}

TEST_F(GPCTest, testOneMispredict)
{
    auto uop = create_branch_uop();
    uop->alloc_id = 7;

    EXPECT_CALL(dispatch, can_take()).WillOnce(Return(true)).WillRepeatedly(Return(false));
    EXPECT_CALL(dispatch, take()).WillOnce(Return(uop));

    EXPECT_CALL(uop_mispredict[0], can_take()).WillOnce(Return(false)).WillOnce(Return(true)).WillOnce(Return(false));
    EXPECT_CALL(uop_mispredict[0], take()).WillOnce(Return(uop));
    EXPECT_CALL(flush_writer, put(Pointee(Field(&FlushObj::alloc_id, uop->alloc_id))));
    tick(2);
}

TEST_F(GPCTest, testSimultaneousMispredicts)
{
    auto a = create_branch_uop();
    auto b = create_branch_uop();
    a->alloc_id = 12;
    b->alloc_id = 11;

    EXPECT_CALL(dispatch, can_take()).WillOnce(Return(true)).WillOnce(Return(true)).WillRepeatedly(Return(false));
    EXPECT_CALL(dispatch, take()).WillOnce(Return(b)).WillOnce(Return(a));

    EXPECT_CALL(uop_mispredict[0], can_take()).WillOnce(Return(false)).WillOnce(Return(true)).WillOnce(Return(false));
    EXPECT_CALL(uop_mispredict[1], can_take()).WillOnce(Return(false)).WillOnce(Return(true)).WillOnce(Return(false));

    EXPECT_CALL(uop_mispredict[0], take()).WillOnce(Return(a));
    EXPECT_CALL(uop_mispredict[1], take()).WillOnce(Return(b));

    EXPECT_CALL(flush_writer, put(Pointee(Field(&FlushObj::alloc_id, b->alloc_id))));
    tick(2);
}

TEST_F(GPCTest, testBrainDead)
{
    auto uop = create_nop_uop();
    uop->alloc_id = 7;
    uop->complete = true;

    EXPECT_CALL(dispatch, can_take()).WillOnce(Return(true)).WillRepeatedly(Return(false));
    EXPECT_CALL(dispatch, take()).WillOnce(Return(uop));
    EXPECT_CALL(driver, commit(_)).Times(1);

    ASSERT_ANY_THROW(tick(110000));
}

} // namespace gpctest
} // namespace ampere

/*
 * vim: sw=4:ts=4:et:si:cindent:cino=\:0,(0,g0,t0:
 */
