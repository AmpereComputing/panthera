//
// Copyright (c) 2018-2019 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <boost/filesystem/operations.hpp>
#include <nlohmann/json.hpp>

#include "ampere/archx/addr.h"
#include "ampere/core/idr.h"
#include "ampere/archx/driver_is.h"
#include "ampere/core/ixu_defs.h"
#include "ampere/core/testing/instruction_generator.h"
#include "ampere/core/testing/mock_flush.h"
#include "ampere/archx/testing/mock_driver.h"
#include "ampere/core/testing/uop_consumer.h"
#include "ampere/event_stream/testing/event_stream_mock.h"
#include "ampere/sc/testing/clock_dummy.h"
#include "ampere/sc/testing/test_module.h"

using nlohmann::json;

namespace bfs = boost::filesystem;

namespace ampere {
namespace idr {

using namespace core;
using namespace event_stream;

using ::testing::_;
using ::testing::AllOf;
using ::testing::AtLeast;
using ::testing::ByMove;
using ::testing::Expectation;
using ::testing::Field;
using ::testing::Invoke;
using ::testing::NiceMock;
using ::testing::Pointee;
using ::testing::Return;
using ::testing::ReturnPointee;

TEST(IDRSchedulerDefinitionTest, ToScheduler)
{
    EXPECT_EQ(Scheduler(Sch::A, 0).scheduler(), 0);
    EXPECT_EQ(Scheduler(Sch::B, 0).scheduler(), 1);
    EXPECT_EQ(Scheduler(Sch::J, 0).scheduler(), 2);
    EXPECT_EQ(Scheduler(Sch::K, 0).scheduler(), 3);
    EXPECT_EQ(Scheduler(Sch::M, 0).scheduler(), 4);
    EXPECT_EQ(Scheduler(Sch::N, 0).scheduler(), 5);

    EXPECT_EQ(Scheduler(Sch::A, 1).scheduler(), 6);
    EXPECT_EQ(Scheduler(Sch::B, 1).scheduler(), 7);
    EXPECT_EQ(Scheduler(Sch::J, 1).scheduler(), 8);
    EXPECT_EQ(Scheduler(Sch::K, 1).scheduler(), 9);
    EXPECT_EQ(Scheduler(Sch::M, 1).scheduler(), 10);
    EXPECT_EQ(Scheduler(Sch::N, 1).scheduler(), 11);

    EXPECT_EQ(Scheduler(Sch::X, 0).scheduler(), 12);
    EXPECT_EQ(Scheduler(Sch::Y, 0).scheduler(), 13);

    EXPECT_EQ(Scheduler(Sch::X, 1).scheduler(), 14);
    EXPECT_EQ(Scheduler(Sch::Y, 1).scheduler(), 15);
}

TEST(IDRSchedulerDefinitionTest, FromSched)
{
    EXPECT_EQ(Scheduler::from_sched(0), Scheduler(Sch::A, 0));
    EXPECT_EQ(Scheduler::from_sched(1), Scheduler(Sch::B, 0));
    EXPECT_EQ(Scheduler::from_sched(2), Scheduler(Sch::J, 0));
    EXPECT_EQ(Scheduler::from_sched(3), Scheduler(Sch::K, 0));
    EXPECT_EQ(Scheduler::from_sched(4), Scheduler(Sch::M, 0));
    EXPECT_EQ(Scheduler::from_sched(5), Scheduler(Sch::N, 0));

    EXPECT_EQ(Scheduler::from_sched(6), Scheduler(Sch::A, 1));
    EXPECT_EQ(Scheduler::from_sched(7), Scheduler(Sch::B, 1));
    EXPECT_EQ(Scheduler::from_sched(8), Scheduler(Sch::J, 1));
    EXPECT_EQ(Scheduler::from_sched(9), Scheduler(Sch::K, 1));
    EXPECT_EQ(Scheduler::from_sched(10), Scheduler(Sch::M, 1));
    EXPECT_EQ(Scheduler::from_sched(11), Scheduler(Sch::N, 1));

    EXPECT_EQ(Scheduler::from_sched(12), Scheduler(Sch::X, 0));
    EXPECT_EQ(Scheduler::from_sched(13), Scheduler(Sch::Y, 0));

    EXPECT_EQ(Scheduler::from_sched(14), Scheduler(Sch::X, 1));
    EXPECT_EQ(Scheduler::from_sched(15), Scheduler(Sch::Y, 1));
}

TEST(IDRSchedulerDefinitionTest, ToName)
{
    EXPECT_EQ(Scheduler(Sch::A, 0).name(), "A0");
    EXPECT_EQ(Scheduler(Sch::A, 1).name(), "A1");
    EXPECT_EQ(Scheduler(Sch::B, 0).name(), "B0");
    EXPECT_EQ(Scheduler(Sch::B, 1).name(), "B1");
    EXPECT_EQ(Scheduler(Sch::J, 0).name(), "J0");
    EXPECT_EQ(Scheduler(Sch::J, 1).name(), "J1");
    EXPECT_EQ(Scheduler(Sch::K, 0).name(), "K0");
    EXPECT_EQ(Scheduler(Sch::K, 1).name(), "K1");
    EXPECT_EQ(Scheduler(Sch::M, 0).name(), "M0");
    EXPECT_EQ(Scheduler(Sch::M, 1).name(), "M1");
    EXPECT_EQ(Scheduler(Sch::N, 0).name(), "N0");
    EXPECT_EQ(Scheduler(Sch::N, 1).name(), "N1");

    EXPECT_EQ(Scheduler(Sch::X, 0).name(), "X0");
    EXPECT_EQ(Scheduler(Sch::X, 1).name(), "X1");
    EXPECT_EQ(Scheduler(Sch::Y, 0).name(), "Y0");
    EXPECT_EQ(Scheduler(Sch::Y, 1).name(), "Y1");
}

TEST(IDRSchedulerDefinitionTest, ToExtendedName)
{
    EXPECT_EQ(Scheduler(Sch::A, 0).extended_name(), "A_P0");
    EXPECT_EQ(Scheduler(Sch::A, 1).extended_name(), "A_P1");
    EXPECT_EQ(Scheduler(Sch::B, 0).extended_name(), "B_P0");
    EXPECT_EQ(Scheduler(Sch::B, 1).extended_name(), "B_P1");
    EXPECT_EQ(Scheduler(Sch::J, 0).extended_name(), "J_P0");
    EXPECT_EQ(Scheduler(Sch::J, 1).extended_name(), "J_P1");
    EXPECT_EQ(Scheduler(Sch::K, 0).extended_name(), "K_P0");
    EXPECT_EQ(Scheduler(Sch::K, 1).extended_name(), "K_P1");
    EXPECT_EQ(Scheduler(Sch::M, 0).extended_name(), "M_P0");
    EXPECT_EQ(Scheduler(Sch::M, 1).extended_name(), "M_P1");
    EXPECT_EQ(Scheduler(Sch::N, 0).extended_name(), "N_P0");
    EXPECT_EQ(Scheduler(Sch::N, 1).extended_name(), "N_P1");

    EXPECT_EQ(Scheduler(Sch::X, 0).extended_name(), "X_P0");
    EXPECT_EQ(Scheduler(Sch::X, 1).extended_name(), "X_P1");
    EXPECT_EQ(Scheduler(Sch::Y, 0).extended_name(), "Y_P0");
    EXPECT_EQ(Scheduler(Sch::Y, 1).extended_name(), "Y_P1");
}

TEST(IDRPipeDefinitionTest, ToPipe)
{
    EXPECT_EQ(expand_pipe(PipeType::A, 0), 0);
    EXPECT_EQ(expand_pipe(PipeType::B, 0), 1);
    EXPECT_EQ(expand_pipe(PipeType::J, 0), 2);
    EXPECT_EQ(expand_pipe(PipeType::K, 0), 3);
    EXPECT_EQ(expand_pipe(PipeType::S, 0), 4);
    EXPECT_EQ(expand_pipe(PipeType::C, 0), 5);
    EXPECT_EQ(expand_pipe(PipeType::D, 0), 6);
    EXPECT_EQ(expand_pipe(PipeType::E, 0), 7);
    EXPECT_EQ(expand_pipe(PipeType::F, 0), 8);

    EXPECT_EQ(expand_pipe(PipeType::A, 1), 9);
    EXPECT_EQ(expand_pipe(PipeType::B, 1), 10);
    EXPECT_EQ(expand_pipe(PipeType::J, 1), 11);
    EXPECT_EQ(expand_pipe(PipeType::K, 1), 12);
    EXPECT_EQ(expand_pipe(PipeType::S, 1), 13);
    EXPECT_EQ(expand_pipe(PipeType::C, 1), 14);
    EXPECT_EQ(expand_pipe(PipeType::D, 1), 15);
    EXPECT_EQ(expand_pipe(PipeType::E, 1), 16);
    EXPECT_EQ(expand_pipe(PipeType::F, 1), 17);

    EXPECT_EQ(expand_pipe(PipeType::X, 0), 18);
    EXPECT_EQ(expand_pipe(PipeType::Y, 0), 19);
    EXPECT_EQ(expand_pipe(PipeType::Z, 0), 20);
    EXPECT_EQ(expand_pipe(PipeType::U, 0), 21);
    EXPECT_EQ(expand_pipe(PipeType::V, 0), 22);
    EXPECT_EQ(expand_pipe(PipeType::W, 0), 23);

    EXPECT_EQ(expand_pipe(PipeType::X, 1), 24);
    EXPECT_EQ(expand_pipe(PipeType::Y, 1), 25);
    EXPECT_EQ(expand_pipe(PipeType::Z, 1), 26);
    EXPECT_EQ(expand_pipe(PipeType::U, 1), 27);
    EXPECT_EQ(expand_pipe(PipeType::V, 1), 28);
    EXPECT_EQ(expand_pipe(PipeType::W, 1), 29);

    EXPECT_EQ(expand_pipe(PipeType::BS, 0), 30);
    EXPECT_EQ(expand_pipe(PipeType::BS, 1), 30);
}

TEST(IDRPipeDefinitionTest, FromPipe)
{
    EXPECT_EQ(split_pipe(0), std::make_pair(PipeType::A, 0U));
    EXPECT_EQ(split_pipe(1), std::make_pair(PipeType::B, 0U));
    EXPECT_EQ(split_pipe(2), std::make_pair(PipeType::J, 0U));
    EXPECT_EQ(split_pipe(3), std::make_pair(PipeType::K, 0U));
    EXPECT_EQ(split_pipe(4), std::make_pair(PipeType::S, 0U));
    EXPECT_EQ(split_pipe(5), std::make_pair(PipeType::C, 0U));
    EXPECT_EQ(split_pipe(6), std::make_pair(PipeType::D, 0U));
    EXPECT_EQ(split_pipe(7), std::make_pair(PipeType::E, 0U));
    EXPECT_EQ(split_pipe(8), std::make_pair(PipeType::F, 0U));

    EXPECT_EQ(split_pipe(9), std::make_pair(PipeType::A, 1U));
    EXPECT_EQ(split_pipe(10), std::make_pair(PipeType::B, 1U));
    EXPECT_EQ(split_pipe(11), std::make_pair(PipeType::J, 1U));
    EXPECT_EQ(split_pipe(12), std::make_pair(PipeType::K, 1U));
    EXPECT_EQ(split_pipe(13), std::make_pair(PipeType::S, 1U));
    EXPECT_EQ(split_pipe(14), std::make_pair(PipeType::C, 1U));
    EXPECT_EQ(split_pipe(15), std::make_pair(PipeType::D, 1U));
    EXPECT_EQ(split_pipe(16), std::make_pair(PipeType::E, 1U));
    EXPECT_EQ(split_pipe(17), std::make_pair(PipeType::F, 1U));

    EXPECT_EQ(split_pipe(18), std::make_pair(PipeType::X, 0U));
    EXPECT_EQ(split_pipe(19), std::make_pair(PipeType::Y, 0U));
    EXPECT_EQ(split_pipe(20), std::make_pair(PipeType::Z, 0U));
    EXPECT_EQ(split_pipe(21), std::make_pair(PipeType::U, 0U));
    EXPECT_EQ(split_pipe(22), std::make_pair(PipeType::V, 0U));
    EXPECT_EQ(split_pipe(23), std::make_pair(PipeType::W, 0U));

    EXPECT_EQ(split_pipe(24), std::make_pair(PipeType::X, 1U));
    EXPECT_EQ(split_pipe(25), std::make_pair(PipeType::Y, 1U));
    EXPECT_EQ(split_pipe(26), std::make_pair(PipeType::Z, 1U));
    EXPECT_EQ(split_pipe(27), std::make_pair(PipeType::U, 1U));
    EXPECT_EQ(split_pipe(28), std::make_pair(PipeType::V, 1U));
    EXPECT_EQ(split_pipe(29), std::make_pair(PipeType::W, 1U));

    EXPECT_EQ(split_pipe(30), std::make_pair(PipeType::BS, 0U));
}

TEST(IDRPipeDefinitionTest, ToString)
{
    EXPECT_EQ(pipe_name(expand_pipe(PipeType::A, 0)), "A_P0");
    EXPECT_EQ(pipe_name(expand_pipe(PipeType::B, 0)), "B_P0");
    EXPECT_EQ(pipe_name(expand_pipe(PipeType::J, 0)), "J_P0");
    EXPECT_EQ(pipe_name(expand_pipe(PipeType::K, 0)), "K_P0");
    EXPECT_EQ(pipe_name(expand_pipe(PipeType::S, 0)), "S_P0");
    EXPECT_EQ(pipe_name(expand_pipe(PipeType::C, 0)), "C_P0");
    EXPECT_EQ(pipe_name(expand_pipe(PipeType::D, 0)), "D_P0");
    EXPECT_EQ(pipe_name(expand_pipe(PipeType::E, 0)), "E_P0");
    EXPECT_EQ(pipe_name(expand_pipe(PipeType::F, 0)), "F_P0");

    EXPECT_EQ(pipe_name(expand_pipe(PipeType::A, 1)), "A_P1");
    EXPECT_EQ(pipe_name(expand_pipe(PipeType::B, 1)), "B_P1");
    EXPECT_EQ(pipe_name(expand_pipe(PipeType::J, 1)), "J_P1");
    EXPECT_EQ(pipe_name(expand_pipe(PipeType::K, 1)), "K_P1");
    EXPECT_EQ(pipe_name(expand_pipe(PipeType::S, 1)), "S_P1");
    EXPECT_EQ(pipe_name(expand_pipe(PipeType::C, 1)), "C_P1");
    EXPECT_EQ(pipe_name(expand_pipe(PipeType::D, 1)), "D_P1");
    EXPECT_EQ(pipe_name(expand_pipe(PipeType::E, 1)), "E_P1");
    EXPECT_EQ(pipe_name(expand_pipe(PipeType::F, 1)), "F_P1");

    EXPECT_EQ(pipe_name(expand_pipe(PipeType::X, 0)), "X_P0");
    EXPECT_EQ(pipe_name(expand_pipe(PipeType::Y, 0)), "Y_P0");
    EXPECT_EQ(pipe_name(expand_pipe(PipeType::Z, 0)), "Z_P0");
    EXPECT_EQ(pipe_name(expand_pipe(PipeType::U, 0)), "U_P0");
    EXPECT_EQ(pipe_name(expand_pipe(PipeType::V, 0)), "V_P0");
    EXPECT_EQ(pipe_name(expand_pipe(PipeType::W, 0)), "W_P0");

    EXPECT_EQ(pipe_name(expand_pipe(PipeType::X, 1)), "X_P1");
    EXPECT_EQ(pipe_name(expand_pipe(PipeType::Y, 1)), "Y_P1");
    EXPECT_EQ(pipe_name(expand_pipe(PipeType::Z, 1)), "Z_P1");
    EXPECT_EQ(pipe_name(expand_pipe(PipeType::U, 1)), "U_P1");
    EXPECT_EQ(pipe_name(expand_pipe(PipeType::V, 1)), "V_P1");
    EXPECT_EQ(pipe_name(expand_pipe(PipeType::W, 1)), "W_P1");

    EXPECT_EQ(pipe_name(expand_pipe(PipeType::BS, 0)), "BS");
    EXPECT_EQ(pipe_name(expand_pipe(PipeType::BS, 1)), "BS");
}

class IDRTest : public Test
{
public:
    inline static auto swizzle_table = R"""([
    [ [1, "0000", false, false], ["NONE"  , false, false] ],
    [ [1, "0000", false, true ], ["NONE"  , false, false] ],
    [ [1, "0000", true , false], ["NONE"  , false, false] ],
    [ [1, "0000", true , true ], ["NONE"  , false, false] ],
    [ [1, "0001", false, false], ["FULL"  , true , true ] ],
    [ [1, "0001", false, true ], ["NONE"  , true , false] ],
    [ [1, "0100", true , false], ["NONE"  , true , true ] ],
    [ [1, "0100", true , true ], ["FULL"  , true , false] ],
    [ [2, "0000", true , false], ["NONE"  , false, false] ],
    [ [2, "0000", true , true ], ["NONE"  , false, false] ],
    [ [2, "0001", true , false], ["FULL"  , true , true ] ],
    [ [2, "0001", true , true ], ["NONE"  , true , false] ],
    [ [2, "0100", true , false], ["NONE"  , true , true ] ],
    [ [2, "0100", true , true ], ["FULL"  , true , false] ],
    [ [2, "0101", true , false], ["NONE"  , false, false] ],
    [ [2, "0101", true , true ], ["NONE"  , false, false] ],
    [ [3, "0000", false, false], ["NONE"  , false, false] ],
    [ [3, "0000", false, true ], ["NONE"  , false, false] ],
    [ [3, "0000", true , false], ["NONE"  , false, false] ],
    [ [3, "0000", true , true ], ["NONE"  , false, false] ],
    [ [3, "0001", false, false], ["FULL"  , true , true ] ],
    [ [3, "0001", false, true ], ["NONE"  , true , false] ],
    [ [3, "0001", true , false], ["FULL"  , true , true ] ],
    [ [3, "0001", true , true ], ["NONE"  , true , false] ],
    [ [3, "0010", false, false], ["MIDDLE", true , true ] ],
    [ [3, "0010", false, true ], ["NONE"  , true , false] ],
    [ [3, "0100", true , false], ["NONE"  , true , true ] ],
    [ [3, "0100", true , true ], ["MIDDLE", true , false] ],
    [ [3, "0011", false, false], ["MIDDLE", false, false] ],
    [ [3, "0011", false, true ], ["MIDDLE", false, false] ],
    [ [3, "0101", true , false], ["NONE"  , false, false] ],
    [ [3, "0101", true , true ], ["NONE"  , false, false] ],
    [ [3, "0100", false, false], ["NONE"  , true , true ] ],
    [ [3, "0100", false, true ], ["MIDDLE", true , false] ],
    [ [3, "1000", true , false], ["NONE"  , true , true ] ],
    [ [3, "1000", true , true ], ["FULL"  , true , false] ],
    [ [3, "0101", false, false], ["NONE"  , false, false] ],
    [ [3, "0101", false, true ], ["NONE"  , false, false] ],
    [ [3, "1001", true , false], ["NONE"  , false, false] ],
    [ [3, "1001", true , true ], ["NONE"  , false, false] ],
    [ [3, "0110", false, false], ["NONE"  , false, false] ],
    [ [3, "0110", false, true ], ["NONE"  , false, false] ],
    [ [3, "1100", true , false], ["MIDDLE", false, false] ],
    [ [3, "1100", true , true ], ["MIDDLE", false, false] ],
    [ [3, "0111", false, false], ["FULL"  , true , true ] ],
    [ [3, "0111", false, true ], ["NONE"  , true , false] ],
    [ [3, "1101", true , false], ["NONE"  , true , true ] ],
    [ [3, "1101", true , true ], ["FULL"  , true , false] ],
    [ [4, "0000", false, false], ["NONE"  , false, false] ],
    [ [4, "0000", false, true ], ["NONE"  , false, false] ],
    [ [4, "0001", false, false], ["FULL"  , true , true ] ],
    [ [4, "0001", false, true ], ["NONE"  , true , false] ],
    [ [4, "0010", false, false], ["MIDDLE", true , true ] ],
    [ [4, "0010", false, true ], ["NONE"  , true , false] ],
    [ [4, "0011", false, false], ["MIDDLE", false, false] ],
    [ [4, "0011", false, true ], ["MIDDLE", false, false] ],
    [ [4, "0100", false, false], ["NONE"  , true , true ] ],
    [ [4, "0100", false, true ], ["MIDDLE", true , false] ],
    [ [4, "0101", false, false], ["NONE"  , false, false] ],
    [ [4, "0101", false, true ], ["NONE"  , false, false] ],
    [ [4, "0110", false, false], ["NONE"  , false, false] ],
    [ [4, "0110", false, true ], ["NONE"  , false, false] ],
    [ [4, "0111", false, false], ["FULL"  , true , true ] ],
    [ [4, "0111", false, true ], ["NONE"  , true , false] ],
    [ [4, "1000", false, false], ["NONE"  , true , true ] ],
    [ [4, "1000", false, true ], ["FULL"  , true , false] ],
    [ [4, "1001", false, false], ["NONE"  , false, false] ],
    [ [4, "1001", false, true ], ["NONE"  , false, false] ],
    [ [4, "1010", false, false], ["NONE"  , false, false] ],
    [ [4, "1010", false, true ], ["NONE"  , false, false] ],
    [ [4, "1011", false, false], ["FULL"  , true , true ] ],
    [ [4, "1011", false, true ], ["NONE"  , true , false] ],
    [ [4, "1100", false, false], ["MIDDLE", false, false] ],
    [ [4, "1100", false, true ], ["MIDDLE", false, false] ],
    [ [4, "1101", false, false], ["NONE"  , true , true ] ],
    [ [4, "1101", false, true ], ["FULL"  , true , false] ],
    [ [4, "1110", false, false], ["NONE"  , true , true ] ],
    [ [4, "1110", false, true ], ["FULL"  , true , false] ],
    [ [4, "1111", false, false], ["NONE"  , false, false] ],
    [ [4, "1111", false, true ], ["NONE"  , false, false] ]
])"""_json;

    explicit IDRTest()
    {
        get_environment()->parameters.add_settings(
            {{"top.idr.rob.size", 100},
             {"~top.idr.sch.*\\.size", 100},
             {"top.idr.rf_int.size", 50},
             {"top.idr.rf_flag.size", 20},
             {"top.idr.swizzle", true},
             {"top.idr.swizzle_table", swizzle_table},
             {"top.idr.load_balance", true},
             {"top.idr.pipe_balancing", true},
             {"top.idr.pipe_balancing_num", 4},
             {"top.idr.rf_fp.size", 100},
             {"top.idr.int_scheduler_assignment.type", "BinaryIDRSchedulerAssignment"},
             {"top.idr.int_scheduler_assignment.first_scheduler", "A"},
             {"top.idr.int_scheduler_assignment.second_scheduler", "B"},
             {"top.idr.fp_scheduler_assignment.type", "BinaryIDRSchedulerAssignment"},
             {"top.idr.fp_scheduler_assignment.first_scheduler", "X"},
             {"top.idr.fp_scheduler_assignment.second_scheduler", "Y"},
             {"top.idr.fp_scheduler_assignment.use_partition_for_default", "true"},
             {"top.dispatch.bandwidth", 4},
             {"top.gpc_dispatch.bandwidth", 4},
             {"top.rob_uop_return.bandwidth", 4}, // ROB, PRF, FLAG RF per uop * 4 uops
             {"top.ixu_credit_return.bandwidth", 4},
             {"top.lsu_credit_return.bandwidth", 4},
             {"top.instruction_fifo.size", 8},
             {"top.feq.size", 8}},
            "test bench");

        event_announcer = new NiceMock<MockEventAnnouncer>{};
        archx::Instruction::set_decoders({"furiosa"});
        InstructionSimple::set_decoder_uarch(UopUarch::FURIOSA);
    }

    ~IDRTest() override { delete event_announcer; }

protected:
    MockEventAnnouncer* event_announcer;

    void set(const std::string& name, const ampere::sc::json& value)
    {
        get_environment()->parameters.add_setting(name, value, "test bench");
    }
};

class UopCollector : public Module
{
public:
    explicit UopCollector(sc_core::sc_module_name name) : Module(name) {}

    In<core::Uop> in{"dispatch"};
    In<core::Uop> in_gpc{"gpc_dispatch"};
    In<core::Uop> bob_alloc{"bob_alloc"};

    Out<core::Uop> rob_uop_return{"rob_uop_return"};
    Out<Credit> ixu_credit_return{"ixu_credit_return"};
    Out<Credit> lsu_credit_return{"lsu_credit_return"};
    Out<Credit> bob_credit_return{"bob_credit_return"};

    std::vector<core::Uop> uops;

    bool freeze_credit_return{false};

    void clear()
    {
        release_credits();
        uops.clear();
    }

private:
    bool run() override
    {
        while (in->can_take()) {
            auto uop = in->take();
            uops.push_back(uop);

            if (!freeze_credit_return)
                return_credits(uop);
        }

        while (in_gpc->can_take())
            in_gpc->take();

        while (bob_alloc->can_take()) {
            auto uop = bob_alloc->take();
            bob_credit_return->put(std::move(uop->bobid));
        }

        return false;
    }

    void return_credits(core::Uop& uop)
    {
        rob_uop_return->put(uop);
        ixu_credit_return->put(std::move(uop->schid));

        if (uop->lobid)
            lsu_credit_return->put(std::move(uop->lobid));

        if (uop->sobid)
            lsu_credit_return->put(std::move(uop->sobid));
    }

    void release_credits()
    {
        for (auto& uop : uops) {
            release_credit(uop->robid);
            release_credit(uop->schid);
            if (uop->prfid)
                release_credit(uop->prfid);
            if (uop->prfid2)
                release_credit(uop->prfid2);
            if (uop->flagsid)
                release_credit(uop->flagsid);
            if (uop->lobid)
                release_credit(uop->lobid);
            if (uop->sobid)
                release_credit(uop->sobid);
        }
    }
};

using MockPMUReader = MockReader<bool>;

class Top : public Module
{
public:
    Top(const sc_core::sc_module_name& name) : Module(name)
    {
        idr.in.bind(ig);
        idr.flush.bind(flush);
        idr.instruction_credit_return.bind(instruction_credit_return);
        idr.pmu_oldest_load_tlb_miss.bind(pmu_tlb_in_mock);
        idr.pmu_oldest_load_cache_miss.bind(pmu_cache_in_mock);
        idr.pmu_oldest_load_ofb_full.bind(pmu_ofb_full_in_mock);
        idr.pmu_oldest_load_mem_miss.bind(pmu_mem_in_mock);
        idr.pmu_oldest_ldst_valid.bind(pmu_ldst_valid_in_mock);
        idr.pmu_oldest_ldst_atomic.bind(pmu_ldst_atomic_in_mock);
        idr.pmu_oldest_load_st_wait.bind(pmu_load_st_wait_in_mock);
        idr.feq_credit_return.bind(feq_credit_releaser);
        idr.frontend_stall.bind(frontend_stall);
        idr.lsu_mem_rob_preview_port.bind(uop_preview_rob);
        idr.rob_empty_in.bind(rob_empty_in_mock);
        idr.lob_balance_in.bind(lob_balance_in_mock);

        idr.driver_port.bind(driver);
        idr.pac_mode.driver_port.bind(driver);

        connect(idr.out, uc.in).name("dispatch");
        connect(idr.gpc_dispatch_port, uc.in_gpc).name("gpc_dispatch");
        connect(idr.bob_alloc, uc.bob_alloc).name("bob_alloc");
        connect(uc.rob_uop_return, idr.rob_uop_return).name("rob_uop_return");
        connect(uc.ixu_credit_return, idr.ixu_credit_return).name("ixu_credit_return");
        connect(uc.lsu_credit_return, idr.lsu_credit_return).name("lsu_credit_return");
        connect(uc.bob_credit_return, idr.bob_credit_return).name("bob_credit_return");
        connect(pmu_sched_stall_xu_busy, idr.pmu_sched_stall_xu_busy).name("pmu_sched_to_idr_xu_busy");
        connect(pmu_sched_stall_ilock, idr.pmu_sched_stall_ilock).name("pmu_sched_to_idr_ilock");
        idr.pmu_frontend_cache_miss.bind(pmu_frontend_cache_miss);
        idr.pmu_frontend_tlb_miss.bind(pmu_frontend_tlb_miss);
        idr.pmu_frontend_l2_miss.bind(pmu_frontend_l2_miss);
        idr.pmu_frontend_feq_empty.bind(pmu_frontend_feq_empty);
        idr.pmu_frontend_mem_stall.bind(pmu_frontend_mem_stall);
        idr.pmu_btc_clear.bind(pmu_btc_clear);
        idr.pmu_btb_clear.bind(pmu_btb_clear);
        idr.flush_req_to_gpc.bind(flush_req_to_gpc);
    }

    IDR idr{"idr"};
    InstructionGenerator ig;
    UopCollector uc{"uc"};
    NiceMock<MockFlushReader> flush;
    CreditReleaser instruction_credit_return;
    CreditReleaser feq_credit_releaser;
    NiceMock<MockDriver> driver;
    NiceMock<MockPMUReader> pmu_tlb_in_mock;
    NiceMock<MockPMUReader> pmu_cache_in_mock;
    NiceMock<MockPMUReader> pmu_ofb_full_in_mock;
    NiceMock<MockPMUReader> pmu_mem_in_mock;
    NiceMock<MockPMUReader> pmu_ldst_valid_in_mock;
    NiceMock<MockPMUReader> pmu_ldst_atomic_in_mock;
    NiceMock<MockPMUReader> pmu_load_st_wait_in_mock;
    CreditPool feq_credits{"feq"};
    NiceMock<MockPutter<core::Uop>> uop_preview_rob;
    NiceMock<MockTaker<bool>> rob_empty_in_mock;
    NiceMock<MockPutter<uint>> frontend_stall;
    NiceMock<MockPMUReader> pmu_frontend_cache_miss;
    NiceMock<MockPMUReader> pmu_frontend_tlb_miss;
    NiceMock<MockPMUReader> pmu_frontend_l2_miss;
    NiceMock<MockPMUReader> pmu_frontend_feq_empty;
    NiceMock<MockPMUReader> pmu_frontend_mem_stall;
    NiceMock<MockPMUReader> pmu_btc_clear;
    NiceMock<MockPMUReader> pmu_btb_clear;
    Out<std::vector<bool>> pmu_sched_stall_xu_busy;
    Out<std::vector<bool>> pmu_sched_stall_ilock;
    NiceMock<MockPutter<core::Uop>> flush_req_to_gpc;
    NiceMock<MockTaker<bool>> lob_balance_in_mock;
};

TEST_F(IDRTest, testConstructor)
{
    Top top("top");
}

TEST_F(IDRTest, testRun)
{
    Top top("top");

    top.ig.set_max_instructions(40);
    tick(20);
    EXPECT_EQ(40, top.uc.uops.size());
}

TEST_F(IDRTest, testAddImmIsDecodedProperly)
{
    Top top("top");
    InstructionSimple::set_decoder_uarch(UopUarch::FURIOSA);

    top.ig.add_instruction(0x11002c22);
    tick(); // align
    tick(); // decode
    tick(); // rename
    tick(); // latency to UopCollector

    ASSERT_GE(top.uc.uops.size(), 1);
    EXPECT_EQ(top.uc.uops[0]->archx_uop->opcode, archx::Uopcode::ADD32);
}

TEST_F(IDRTest, testLdrbIsDecodedProperly)
{
    set("top.idr.lob.size", 1);

    Top top("top");

    top.ig.add_instruction(0x38400400, VirtAddr{0xdedefefe});
    tick();
    tick();
    tick();
    tick();

    ASSERT_GE(top.uc.uops.size(), 2);
    EXPECT_EQ(top.uc.uops[0]->archx_uop->opcode, archx::Uopcode::LDR_B_32);
    EXPECT_EQ(top.uc.uops[1]->archx_uop->opcode, archx::Uopcode::ADD64_IDXIMM);
    EXPECT_EQ(*top.uc.uops[0]->virt_addr, VirtAddr{0xdedefefe});
}

TEST_F(IDRTest, testLdpIsDecodedProperly)
{
    set("top.idr.lob.size", 1);

    Top top("top");

    // LDP 64 with Rt=X5, Rt2=X4
    InstructionSimple::set_decoder_uarch(UopUarch::FURIOSA);
    top.ig.add_instruction(0b10'101'0'010'1'0000000'00100'00000'00101, VirtAddr{0xdedefefe});
    tick(4);

    ASSERT_EQ(top.uc.uops.size(), 1);
    EXPECT_EQ(top.uc.uops[0]->archx_uop->opcode, archx::Uopcode::LDP_D_64);
    EXPECT_EQ(top.uc.uops[0]->reg_dst, archx::LReg::L5);
    EXPECT_EQ(top.uc.uops[0]->reg_dst2, archx::LReg::L4);
    EXPECT_EQ(top.uc.uops[0]->archx_uop->end_inst, 1);
}

TEST_F(IDRTest, WillAwaitCreditReturnToDispatchSecondStore)
{
    set("top.idr.sob.size", 4);

    Top top("top");
    InstructionSimple::set_decoder_uarch(UopUarch::FURIOSA);

    top.ig.set_max_instructions(8);

    const uint32_t opcode = 0b00'111'0'01'00'000000000000'00100'00101;

    for (size_t i = 0; i < 8; i++)
        top.ig.add_instruction(opcode, ProgramCounter{0x1040 + i * 4});

    tick(4);

    ASSERT_EQ(top.uc.uops.size(), 4);
    EXPECT_EQ(top.uc.uops[0]->instructions.front()->archx_instruction->program_counter(), ProgramCounter{0x1040});

    tick();

    EXPECT_EQ(top.uc.uops.size(), 4);

    tick(4);

    ASSERT_GE(top.uc.uops.size(), 8);
    EXPECT_EQ(top.uc.uops[4]->instructions.front()->archx_instruction->program_counter(), ProgramCounter{0x1050});
}

TEST_F(IDRTest, WillAwaitCreditReturnToDispatchSecondLoad)
{
    set("top.idr.lob.size", 1);

    Top top("top");
    InstructionSimple::set_decoder_uarch(UopUarch::FURIOSA);
    top.ig.set_max_instructions(8);

    // This pattern puts the two loads in different uop "quads" because quads are allocated in an all-or-nothing
    // manner. Thus if we put the two loads together in this test case with 1 LOB entry, they will never be
    // dispatched at all!
    //
    top.ig.add_instruction(0x38604800, ProgramCounter{0x1040});
    top.ig.add_instruction(archx::nop_opcode, ProgramCounter{0x1044});
    top.ig.add_instruction(archx::nop_opcode, ProgramCounter{0x1048});
    top.ig.add_instruction(archx::nop_opcode, ProgramCounter{0x104C});
    top.ig.add_instruction(0x38604800, ProgramCounter{0x1050});
    top.ig.add_instruction(archx::nop_opcode, ProgramCounter{0x1054});
    top.ig.add_instruction(archx::nop_opcode, ProgramCounter{0x1058});
    top.ig.add_instruction(archx::nop_opcode, ProgramCounter{0x105C});

    tick();
    tick();
    tick();
    tick();

    ASSERT_EQ(top.uc.uops.size(), 4);
    EXPECT_EQ(top.uc.uops[0]->instructions.front()->archx_instruction->program_counter(), ProgramCounter{0x1040});

    tick();

    EXPECT_EQ(top.uc.uops.size(), 4);

    tick();
    tick();

    ASSERT_GE(top.uc.uops.size(), 8);
    EXPECT_EQ(top.uc.uops[4]->instructions.front()->archx_instruction->program_counter(), ProgramCounter{0x1050});
}

TEST_F(IDRTest, UopDecodeHasDisassembly)
{
    get_environment()->event_stream.reset(new EventStreamDummy(event_announcer));

    Top top("top");
    top.ig.set_max_instructions(1);

    top.ig.add_instruction(0x11002c22);

    EXPECT_CALL(*event_announcer, post_event(_, _)).WillRepeatedly(Return());
    EXPECT_CALL(*event_announcer, add_uint_data(_, _, _, _)).WillRepeatedly(Return());
    EXPECT_CALL(*event_announcer, add_string_data(_, _, _, _)).WillRepeatedly(Return());
    EXPECT_CALL(*event_announcer, add_string_data("top.idr.uop_decode", _, "top.idr.uopcode_disasm", "add32 L2 <- L1"));

    tick(3);
}

TEST_F(IDRTest, FlushThrowsAwayPendingInstructions)
{
    Top top("top");
    top.ig.set_max_instructions(16);

    top.ig.add_instruction(0x11002c22, ProgramCounter{0x1040});
    top.ig.add_instruction(0x11002c22, ProgramCounter{0x1044});

    auto flush = sc::trans_make<Flush>(
        ProgramCounter{0x1000}, ProgramCounter{0xfffc}, nullptr, nullptr, 0, archx::FlushType::BR_MISPREDICT);

    EXPECT_CALL(top.flush, can_read()).WillOnce(Return(true)).WillRepeatedly(Return(false));
    EXPECT_CALL(top.flush, read()).WillOnce(Return(flush));

    tick();

    top.ig.add_instruction(0x11002c22, ProgramCounter{0x2040});

    tick(2);
    tick(2);

    ASSERT_GE(top.uc.uops.size(), 1);
    EXPECT_EQ(top.uc.uops[0]->instructions.front()->archx_instruction->program_counter(), ProgramCounter{0x2040});
}

TEST_F(IDRTest, FlushThrowsAwayPendingUops)
{
    set("top.idr.lob.size", 4);
    Top top("top");
    top.ig.set_max_instructions(16);

    top.uc.freeze_credit_return = true;
    EXPECT_CALL(top.flush, can_read()).WillRepeatedly(Return(false));

    // The second set of 4 loads will stall due to lack of LOB entries. We'll then issue a flush, so we should
    // never see those.
    for (size_t i = 0; i < 8; i++)
        top.ig.add_instruction(0x38604800, ProgramCounter{0x1040 + i * 4});

    tick(4);

    // Now in a state where the uop_row is filled with loads that can't go anywhere. The first four should have
    // been dispatched.
    //
    ASSERT_EQ(top.uc.uops.size(), 4);

    auto flush = sc::trans_make<Flush>(
        ProgramCounter{0x1000}, ProgramCounter{0xfffc}, nullptr, nullptr, 0, archx::FlushType::BR_MISPREDICT);

    EXPECT_CALL(top.flush, can_read()).WillOnce(Return(true)).RetiresOnSaturation();
    EXPECT_CALL(top.flush, read()).WillOnce(Return(flush));

    tick();

    top.ig.add_instruction(0x11002c22, ProgramCounter{0x2040});

    tick(2);
    tick(2);

    ASSERT_GE(top.uc.uops.size(), 5);
    EXPECT_EQ(top.uc.uops[4]->instructions.front()->archx_instruction->program_counter(), ProgramCounter{0x2040});

    top.uc.clear();
}

TEST_F(IDRTest, UopDecoderAlignment_5Singles)
{
    Top top("top");
    top.ig.set_max_instructions(5);

    top.ig.add_instruction(0xd503201f, ProgramCounter{0x1040}); // 1 uop
    top.ig.add_instruction(0xd503201f, ProgramCounter{0x1044}); // 1 uop
    top.ig.add_instruction(0xd503201f, ProgramCounter{0x1048}); // 1 uop
    top.ig.add_instruction(0xd503201f, ProgramCounter{0x104C}); // 1 uop
    top.ig.add_instruction(0xd503201f, ProgramCounter{0x1050}); // 1 uop

    tick(4); // align, decode, rename, latency to UopCollector
    ASSERT_EQ(top.uc.uops.size(), 4);

    tick(1);
    ASSERT_EQ(top.uc.uops.size(), 5);
}

TEST_F(IDRTest, UopDecoderAlignment_QuadSplit)
{
    // Multi-instruction uops must start on decoders 0 or 2.  Since the second
    // instruction is a quad-uop, only two uops can be formed in first grouping.
    // A second group of one instruction must occur
    // Instructions:
    //    Instr 0 (1 uop)
    //    Instr 1 (4 uop)
    // Expected uop group layout:
    //    0: Instr 0(1 uop), Instr1 (2 uops)
    //    1: Instr 1(2 uops)
    //
    Top top("top");
    top.ig.set_max_instructions(2);

    top.ig.add_instruction(0xd503201f, ProgramCounter{0x1040}); // 1 uop
    top.ig.add_instruction(0b0'1'0'01110'00'11000'11011'10'00001'00010,
                           ProgramCounter{0x1044}); // 4 uop (ADDV_asimdall_only)

    tick(4); // align, decode, rename, latency to UopCollector
    ASSERT_EQ(top.uc.uops.size(), 3);

    tick(1);
    ASSERT_EQ(top.uc.uops.size(), 5);
}

TEST_F(IDRTest, UopDecoderAlignment_SingleQuadSplitTripleSplit)
{
    // Instructions:
    //    Instr 0 (1 uop)
    //    Instr 1 (4 uop)
    //    Instr 2 (3 uop)
    // Expected uop group layout:
    //    0: Instr 0(1 uop), Instr 1 (2 uops)
    //    1: Instr 1(2 uops), Instr 2 (2 uops)
    //    2: Instr 2(1 uops)
    Top top("top");
    top.ig.set_max_instructions(3);

    top.ig.add_instruction(0xd503201f, ProgramCounter{0x1040});                                  // 1 uop
    top.ig.add_instruction(0b0'1'0'01110'00'11000'11011'10'00001'00010, ProgramCounter{0x1044}); // 4 uop
    top.ig.add_instruction(0b0'0'0'01110'00'11000'11011'10'00101'00110, ProgramCounter{0x1048}); // 3 uop

    tick(4); // align, decode, rename, latency to UopCollector
    ASSERT_EQ(top.uc.uops.size(), 3);

    tick(1);
    ASSERT_EQ(top.uc.uops.size(), 7);

    tick(1);
    ASSERT_EQ(top.uc.uops.size(), 8);
}

TEST_F(IDRTest, UopDecoderAlignment_2SinglesTriUops)
{
    // Instructions:
    //    Instr 0 (1 uop)
    //    Instr 1 (1 uop)
    //    Instr 2 (3 uop)
    // Expected uop group layout:
    //    0: Instr 0(1 uop), Instr 1 (1 uops), Instr 2 (2 uops)
    //    1: Instr 2(1 uop)
    Top top("top");
    top.ig.set_max_instructions(3);

    top.ig.add_instruction(0xd503201f, ProgramCounter{0x1040});                                  // 1 uop
    top.ig.add_instruction(0xd503201f, ProgramCounter{0x1044});                                  // 1 uop
    top.ig.add_instruction(0b0'0'0'01110'00'11000'11011'10'00101'00110, ProgramCounter{0x1048}); // 3 uop

    tick(4); // align, decode, rename, latency to UopCollector
    ASSERT_EQ(top.uc.uops.size(), 4);

    tick(1);
    ASSERT_EQ(top.uc.uops.size(), 5);
}

TEST_F(IDRTest, UopDecoderAlignment_SingleTriUops)
{
    // Instructions:
    //    Instr 0 (1 uop)
    //    Instr 1 (3 uop)
    // Expected uop group layout:
    //    0: Instr 0(1 uop), Instr 1 (3 uops)
    Top top("top");
    top.ig.set_max_instructions(2);

    top.ig.add_instruction(0xd503201f, ProgramCounter{0x1040});                                  // 1 uop
    top.ig.add_instruction(0b0'0'0'01110'00'11000'11011'10'00101'00110, ProgramCounter{0x1044}); // 3 uop

    tick(4); // align, decode, rename, latency to UopCollector
    ASSERT_EQ(top.uc.uops.size(), 4);
}

TEST_F(IDRTest, UopDecoderAlignment_SIRLimitationEachSeparateLines)
{
    set("top.idr.bob.size", 8);
    set("top.bob_alloc.bandwidth", 4);
    set("top.bob_credit_return.bandwidth", 4);

    Top top("top");
    top.ig.set_max_instructions(4);

    // 1 uop, jump a cacheline forward
    for (unsigned i = 0; i < 4; ++i)
        top.ig.add_instruction(0x14000010, ProgramCounter{0x1040 + i * ProgramCounter::LINE_SIZE});

    tick(4); // align, decode, rename, latency to UopCollector
    EXPECT_EQ(top.uc.uops.size(), 2);

    tick(1);
    EXPECT_EQ(top.uc.uops.size(), 4);
}

TEST_F(IDRTest, UopDecoderAlignment_SIRLimitationPreventsBranchFusion)
{
    set("top.idr.bob.size", 8);

    Top top("top");
    top.ig.set_max_instructions(4);

    top.ig.add_instruction(0x1400001f, ProgramCounter{0x1040}); // b #124, 1 uop
    top.ig.add_instruction(0x0b010000, ProgramCounter{0x10bc}); // add, 1 uop
    top.ig.add_instruction(0x14000010, ProgramCounter{0x10c0}); // b #64, 1 uop
    top.ig.add_instruction(0x0b010000, ProgramCounter{0x1100}); // add, 1 uop

    tick(4); // align, decode, rename, latency to UopCollector
    EXPECT_EQ(top.uc.uops.size(), 2);

    tick(1);
    EXPECT_EQ(top.uc.uops.size(), 4);
}

TEST_F(IDRTest, UopDecoderAlignment_SIRLimitationWithFusion)
{
    set("top.idr.bob.size", 8);

    Top top("top");
    top.ig.set_max_instructions(4);

    top.ig.add_instruction(0x1400001f, ProgramCounter{0x1040}); // b #124, 1 uop
    top.ig.add_instruction(0xf2800020, ProgramCounter{0x10bc}); // movk, 1 uop
    top.ig.add_instruction(0xf2a00020, ProgramCounter{0x10c0}); // movk
    top.ig.add_instruction(0x0b010000, ProgramCounter{0x10c4}); // add, 1 uop

    tick(4); // align, decode, rename, latency to UopCollector
    // SIR limit prevents fusion of the two movk's, so we get b and movk in the first cycle
    EXPECT_EQ(top.uc.uops.size(), 2);

    tick(1);
    // And then second movk and add in the next cycle
    EXPECT_EQ(top.uc.uops.size(), 4);
    EXPECT_FALSE(top.uc.uops[1]->is_fused());
}

TEST_F(IDRTest, UopDecoder_BankLoadBalancing)
{
    set("top.idr.sob.size", 8);
    Top top("top");
    top.ig.set_max_instructions(1);

    // Send UOP done, should be bank 0
    top.ig.add_instruction(0xd503201f, ProgramCounter{0x1040}); // 1 uop
    tick();

    // Send UOP done, should be bank 1
    top.ig.set_max_instructions(2);
    top.ig.add_instruction(0xd503201f, ProgramCounter{0x1044}); // 1 uop
    tick();

    // Send UOP done, should be bank 0
    top.ig.set_max_instructions(3);
    top.ig.add_instruction(0xd503201f, ProgramCounter{0x1048}); // 1 uop
    tick();

    // Send UOP done, should be bank 0
    top.ig.set_max_instructions(4);
    top.ig.add_instruction(0xd503201f, ProgramCounter{0x104C}); // 1 uop

    top.ig.set_max_instructions(5);
    top.ig.add_instruction(0xd503201f, ProgramCounter{0x1050}); // 1 uop
    tick();

    top.ig.set_max_instructions(6);
    // Send UOP done, bank 0 should have more uops
    top.ig.add_instruction(0b0'0'0'01110'00'11000'11011'10'00101'00110, ProgramCounter{0x1054}); // 3 uop
    tick();

    //// Send UOP done, bank 1 should have more uops
    top.ig.set_max_instructions(7);
    top.ig.add_instruction(0b0'0'0'01110'00'11000'11011'10'00101'00110, ProgramCounter{0x1058}); // 3 uop
    tick();

    //// Send UOP done, bank 0 should have more uops
    top.ig.set_max_instructions(8);
    top.ig.add_instruction(0b0'0'0'01110'00'11000'11011'10'00101'00110, ProgramCounter{0x105C}); // 3 uop
    tick();

    tick(3); // align, decode, rename, latency to UopCollector

    // Single Uop Ping Pong Partitions
    ASSERT_EQ(top.uc.uops[0]->decode.partition, 0);
    ASSERT_EQ(top.uc.uops[1]->decode.partition, 1);
    ASSERT_EQ(top.uc.uops[2]->decode.partition, 0);

    // Two Uop Split Paritions
    ASSERT_EQ(top.uc.uops[3]->decode.partition, 0);
    ASSERT_EQ(top.uc.uops[4]->decode.partition, 1);

    // Triple Uop Ping Pong
    ASSERT_EQ(top.uc.uops[5]->decode.partition, 0);
    ASSERT_EQ(top.uc.uops[6]->decode.partition, 0);
    ASSERT_EQ(top.uc.uops[7]->decode.partition, 1);

    ASSERT_EQ(top.uc.uops[8]->decode.partition, 0);
    ASSERT_EQ(top.uc.uops[9]->decode.partition, 1);
    ASSERT_EQ(top.uc.uops[10]->decode.partition, 1);

    ASSERT_EQ(top.uc.uops[11]->decode.partition, 0);
    ASSERT_EQ(top.uc.uops[12]->decode.partition, 0);
    ASSERT_EQ(top.uc.uops[13]->decode.partition, 1);
}

TEST_F(IDRTest, UopDecoder_PipeBalancingXY)
{
    Top top("top");
    // Force FSU uops to the same partition.  They should load balance between X and Y pipes
    top.ig.set_max_instructions(4);
    top.ig.add_instruction(0xd503201f, ProgramCounter{0x1040}); // 1 uop
    top.ig.add_instruction(0xd503201f, ProgramCounter{0x1044}); // 1 uop
    top.ig.add_instruction(0x1e200800, ProgramCounter{0x1048}); // 1 fsu uop
    top.ig.add_instruction(0x1e200800, ProgramCounter{0x104c}); // 1 fsu uop

    tick(4); // align, decode, rename, latency to UopCollector
    tick(4); // All Instructions

    // Correct Partitions
    ASSERT_EQ(top.uc.uops[0]->decode.partition, 0);
    ASSERT_EQ(top.uc.uops[1]->decode.partition, 0);
    ASSERT_EQ(top.uc.uops[2]->decode.partition, 1);
    ASSERT_EQ(top.uc.uops[3]->decode.partition, 1);

    ASSERT_EQ(top.uc.uops[0]->sch.pipe(), expand_pipe(PipeType::A, 0));
    ASSERT_EQ(top.uc.uops[1]->sch.pipe(), expand_pipe(PipeType::B, 0));
    ASSERT_EQ(top.uc.uops[2]->sch.pipe(), expand_pipe(PipeType::Y, 1));
    ASSERT_EQ(top.uc.uops[3]->sch.pipe(), expand_pipe(PipeType::X, 1));
}

TEST_F(IDRTest, UopDecoderBank_NoMemSwizzle)
{
    Top top("top");
    top.ig.set_max_instructions(6);

    // Send Memory UOP down bank 0
    top.ig.add_instruction(0xd503201f, ProgramCounter{0x1040}); // 1 uop
    top.ig.add_instruction(0xd503201f, ProgramCounter{0x1044}); // 1 uop
    top.ig.add_instruction(0xd503201f, ProgramCounter{0x1048}); // 1 uop
    top.ig.add_instruction(0xd503201f, ProgramCounter{0x104c}); // 1 uop

    top.ig.add_instruction(0xd503201f, ProgramCounter{0x1050}); // 1 uop
    top.ig.add_instruction(0xd503201f, ProgramCounter{0x1054}); // 1 uop

    tick(4); // align, decode, rename, latency to UopCollector
    tick(6); // All Instructions

    ASSERT_EQ(top.uc.uops[0]->decode.partition, 0);
    ASSERT_EQ(top.uc.uops[1]->decode.partition, 0);
    ASSERT_EQ(top.uc.uops[2]->decode.partition, 1);
    ASSERT_EQ(top.uc.uops[3]->decode.partition, 1);

    ASSERT_EQ(top.uc.uops[4]->decode.partition, 0);
    ASSERT_EQ(top.uc.uops[5]->decode.partition, 1);
}

TEST_F(IDRTest, UopDecoderBank_SwizzleEvenMemOpsSplitBanks)
{
    set("top.idr.sob.size", 32);
    Top top("top");
    top.ig.set_max_instructions(30);

    // Send Memory UOP down bank 0
    top.ig.add_instruction(0x39000085, ProgramCounter{0x1040}); // str
    top.ig.add_instruction(0x39000085, ProgramCounter{0x1044}); // str
    top.ig.add_instruction(0x39000085, ProgramCounter{0x1048}); // str
    top.ig.add_instruction(0x39000085, ProgramCounter{0x104c}); // str

    top.ig.add_instruction(0xd503201f, ProgramCounter{0x1050}); // 1 uop
    top.ig.add_instruction(0x39000085, ProgramCounter{0x1044}); // str
    top.ig.add_instruction(0x39000085, ProgramCounter{0x1048}); // str
    top.ig.add_instruction(0xd503201f, ProgramCounter{0x104c}); // 1 uop

    top.ig.add_instruction(0x39000085, ProgramCounter{0x1050}); // str
    top.ig.add_instruction(0xd503201f, ProgramCounter{0x1054}); // 1 uop
    top.ig.add_instruction(0x39000085, ProgramCounter{0x1058}); // str
    top.ig.add_instruction(0xd503201f, ProgramCounter{0x105c}); // 1 uop

    top.ig.add_instruction(0x39000085, ProgramCounter{0x1060}); // str
    top.ig.add_instruction(0xd503201f, ProgramCounter{0x1064}); // 1 uop
    top.ig.add_instruction(0xd503201f, ProgramCounter{0x1068}); // 1 uop
    top.ig.add_instruction(0x39000085, ProgramCounter{0x106c}); // str

    top.ig.add_instruction(0xd503201f, ProgramCounter{0x1070}); // 1 uop
    top.ig.add_instruction(0x39000085, ProgramCounter{0x1074}); // str
    top.ig.add_instruction(0xd503201f, ProgramCounter{0x1078}); // 1 uop
    top.ig.add_instruction(0x39000085, ProgramCounter{0x107c}); // str

    top.ig.add_instruction(0x39000085, ProgramCounter{0x1080}); // str
    top.ig.add_instruction(0x39000085, ProgramCounter{0x1084}); // str
    top.ig.add_instruction(0xd503201f, ProgramCounter{0x1088}); // 1 uop
    top.ig.add_instruction(0xd503201f, ProgramCounter{0x108c}); // 1 uop

    top.ig.add_instruction(0xd503201f, ProgramCounter{0x1090}); // 1 uop
    top.ig.add_instruction(0xd503201f, ProgramCounter{0x1094}); // 1 uop
    top.ig.add_instruction(0x39000085, ProgramCounter{0x1098}); // str
    top.ig.add_instruction(0x39000085, ProgramCounter{0x109c}); // str

    // 2 memory operations should always be in different banks
    top.ig.add_instruction(0x39000085, ProgramCounter{0x10a0}); // str
    top.ig.add_instruction(0x39000085, ProgramCounter{0x10a4}); // str

    tick(4);  // align, decode, rename, latency to UopCollector
    tick(30); // All Instructions

    ASSERT_EQ(top.uc.uops[0]->decode.partition, 0);
    ASSERT_EQ(top.uc.uops[1]->decode.partition, 0);
    ASSERT_EQ(top.uc.uops[2]->decode.partition, 1);
    ASSERT_EQ(top.uc.uops[3]->decode.partition, 1);

    ASSERT_EQ(top.uc.uops[4]->decode.partition, 0);
    ASSERT_EQ(top.uc.uops[5]->decode.partition, 0);
    ASSERT_EQ(top.uc.uops[6]->decode.partition, 1);
    ASSERT_EQ(top.uc.uops[7]->decode.partition, 1);

    ASSERT_EQ(top.uc.uops[8]->decode.partition, 0);
    ASSERT_EQ(top.uc.uops[9]->decode.partition, 0);
    ASSERT_EQ(top.uc.uops[10]->decode.partition, 1);
    ASSERT_EQ(top.uc.uops[11]->decode.partition, 1);

    ASSERT_EQ(top.uc.uops[12]->decode.partition, 0);
    ASSERT_EQ(top.uc.uops[13]->decode.partition, 0);
    ASSERT_EQ(top.uc.uops[14]->decode.partition, 1);
    ASSERT_EQ(top.uc.uops[15]->decode.partition, 1);

    ASSERT_EQ(top.uc.uops[16]->decode.partition, 0);
    ASSERT_EQ(top.uc.uops[17]->decode.partition, 0);
    ASSERT_EQ(top.uc.uops[18]->decode.partition, 1);
    ASSERT_EQ(top.uc.uops[19]->decode.partition, 1);

    // Should Middle swizzle
    ASSERT_EQ(top.uc.uops[20]->decode.partition, 0);
    ASSERT_EQ(top.uc.uops[21]->decode.partition, 1);
    ASSERT_EQ(top.uc.uops[22]->decode.partition, 0);
    ASSERT_EQ(top.uc.uops[23]->decode.partition, 1);

    // Should Middle swizzle
    ASSERT_EQ(top.uc.uops[24]->decode.partition, 0);
    ASSERT_EQ(top.uc.uops[25]->decode.partition, 1);
    ASSERT_EQ(top.uc.uops[26]->decode.partition, 0);
    ASSERT_EQ(top.uc.uops[27]->decode.partition, 1);

    ASSERT_EQ(top.uc.uops[28]->decode.partition, 0);
    ASSERT_EQ(top.uc.uops[29]->decode.partition, 1);
}

} // namespace idr
} // namespace ampere

/*
 * vim: sw=4:ts=4:et:si:cindent:cino=\:0,(0,g0,t0:
 */
