//
// Copyright (c) 2019 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//

#include "ampere/archx/addr.h"
#include "ampere/archx/instruction_simple.h"
#include "ampere/core/testing/uops.h"
#include "ampere/core/testing/uop_generator.h"
#include "ampere/sc/testing/test_module.h"

namespace ampere::core {

using namespace sc;

struct UopGenModule : public sc::Module
{
    explicit UopGenModule(const sc_core::sc_module_name& name) : sc::Module(name) {}

    Out<Uop> out{"out"};
    UopGenerator ug;

    CreditPool rob{"rob"};
    CreditPool lob{"lob"};
    CreditPool sob{"sob"};

    bool run() override
    {
        if (get_cycle() < 3) {
            Uop u = ug.take();

            u->robid = rob.acquire();
            u->lobid = lob.acquire();
            u->sobid = sob.acquire();

            out->put(u);
        }

        return true;
    }
};

struct UopClearModule : public sc::Module
{
    explicit UopClearModule(const sc_core::sc_module_name& name) : sc::Module(name) {}

    In<Uop> in{"in"};

    bool run() override
    {
        if (get_cycle() == 3)
            in->clear();
        return true;
    }
};

struct ParamSetup
{
    ParamSetup()
    {
        auto& params = Environment::get_root()->parameters;

        params.add_setting("ugen.rob.size", 10, "unit test");
        params.add_setting("ugen.lob.size", 10, "unit test");
        params.add_setting("ugen.sob.size", 10, "unit test");
    }
};

struct UopTest : public TestModule
{
    ParamSetup param_setup;
    UopGenModule ugen{"ugen"};
    UopClearModule uclear{"uclear"};

    void SetUp() override
    {
        connect(ugen.out, uclear.in).capacity(10);
        archx::Instruction::set_decoders({"furiosa"});
    }
};

TEST_F(UopTest, CreditsAreReturnedOnClear)
{
    tick(3);
    EXPECT_EQ(ugen.rob.get_count(), 7);
    EXPECT_EQ(ugen.lob.get_count(), 7);
    EXPECT_EQ(ugen.sob.get_count(), 7);
    tick(2);
    EXPECT_EQ(ugen.rob.get_count(), 10);
    EXPECT_EQ(ugen.lob.get_count(), 10);
    EXPECT_EQ(ugen.sob.get_count(), 10);
}

TEST_F(UopTest, TestFlushContextSwitch)
{
    auto uop1 = create_core_uop(0b1101011'0'100'11111'0000'0'0'11111'00000); // ERET
    auto uop2 = create_core_uop(0b11010100'000'0000000000011011'000'10);     // HVC #27
    auto uop3 = create_nop_uop();
    auto uop4 = create_branch_uop();

    // ERET and HVC ought to both return their own flush types
    EXPECT_TRUE(uop1->requires_flush().has_value());
    EXPECT_EQ(uop1->requires_flush().value(), archx::FlushType::ERET);
    EXPECT_TRUE(uop2->requires_flush().has_value());
    EXPECT_EQ(uop2->requires_flush().value(), archx::FlushType::HVC);

    // A no-op should not usually flush, but it should if it has a PC
    // discontinuity indicating an interrupt, fault, or other asynchronous
    // exception
    EXPECT_FALSE(uop3->requires_flush().has_value());
    std::shared_ptr<InstructionSimple> nop_instr =
        std::dynamic_pointer_cast<InstructionSimple>(uop3->instructions[0]->archx_instruction);
    nop_instr->set_next_pc(0xf00ba4_pc);
    EXPECT_TRUE(uop3->requires_flush().has_value());
    EXPECT_EQ(uop3->requires_flush().value(), archx::FlushType::NOT_KNOWN);

    // A branch should not return anything here, even if mispredicted, because
    // branch mispredictions are detected by the XU using logic like
    // `if (uop->is_branch() && uop->is_mispredicted_branch())`.
    // The GPC uses `uop->requires_flush()`, to detect non-branch-mispredict
    // types of flushes.
    EXPECT_TRUE(uop4->is_mispredicted_branch());
    EXPECT_FALSE(uop4->requires_flush().has_value());
}

} // namespace ampere::core

