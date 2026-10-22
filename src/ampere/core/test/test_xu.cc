//
// Copyright (c) 2018-2019 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//

#include "ampere/core/xu.h"
#include "ampere/core/testing/uops.h"
#include "ampere/sc/testing/test_module.h"
#include "ampere/core/testing/mock_flush.h"

namespace ampere {
namespace xu {

using namespace core;
using namespace ::testing;

class XUTest : public TestModule
{
protected:
    XU xu{"xu"};
    Consumer<core::Uop> mispredicts;
    Consumer<core::Uop> indirect_mispredicts;

    void SetUp() override
    {
        xu.mispredict_port.bind(mispredicts);
        xu.mispredict_indirect_port.bind(indirect_mispredicts);
        archx::Instruction::set_decoders({"furiosa"});
    }
};

TEST_F(XUTest, testOneUop)
{
    auto uop = create_nop_uop();
    xu.issue(uop);
    tick(1);
    auto exec_uops = xu.execute();
    ASSERT_EQ(uop, exec_uops.at(0));
}

TEST_F(XUTest, testBranchUop)
{
    auto uop = create_branch_uop();
    auto instr = uop->instructions.front();
    instr->fetch->update_prediction(
        {BranchType::UNCONDITIONAL_DIRECT, instr->archx_instruction->program_counter(), instr->next_pc()});
    xu.issue(uop);
    tick(1);
    auto exec_uops = xu.execute();
    ASSERT_EQ(uop, exec_uops.at(0));
    ASSERT_EQ(0, mispredicts.items.size());
}

TEST_F(XUTest, testMispredictedBranchUop)
{
    auto uop = create_cond_branch_uop();
    auto instr = uop->instructions.front();
    instr->fetch->update_prediction({BranchType::NONE, instr->archx_instruction->program_counter(), instr->next_pc()});
    xu.issue(uop);
    tick(1);
    auto exec_uops = xu.execute();
    ASSERT_EQ(uop, exec_uops.at(0));
    ASSERT_EQ(1, mispredicts.items.size());
}

TEST_F(XUTest, testTwoCycleLatency)
{
    auto two_cyc_uop = create_core_uop(0b01001110'00'10100'0010'1'10'00000'00000); // AESD_B_LV

    tick(1);
    ASSERT_EQ(two_cyc_uop->latency(), 2);
    ASSERT_TRUE(xu.can_issue(two_cyc_uop));
    xu.issue(two_cyc_uop);

    tick(2);
    auto exec_uops = xu.execute();
    ASSERT_EQ(two_cyc_uop, exec_uops.at(0));
}

TEST_F(XUTest, testBusy)
{
    auto two_cyc_uop = create_core_uop(0b01001110'00'10100'0010'1'10'00000'00000); // AESD_B_LV
    std::vector<core::Uop> uops;

    ASSERT_FALSE(xu.busy());

    xu.issue(two_cyc_uop);
    ASSERT_TRUE(xu.busy());

    tick(1);
    uops = xu.execute();
    ASSERT_TRUE(xu.busy());

    tick(1);
    uops = xu.execute();
    ASSERT_FALSE(xu.busy());
    ASSERT_EQ(two_cyc_uop, uops.at(0));
}

} // namespace xu
} // namespace ampere

/*
 * vim: sw=4:ts=4:et:si:cindent:cino=\:0,(0,g0,t0:
 */
