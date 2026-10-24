//
// Copyright (c) 2021-2022 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//

#include <gtest/gtest.h>
#include "ampere/archx/instruction_simple.h"
#include "ampere/core/prefetchers/l1p_gretch.h"
#include "ampere/sc/module.h"
#include "ampere/core/testing/uops.h"
#include "ampere/sc/testing/test_module.h"

/* Tests to check functionality/correctness of Gretch's ADT */

namespace ampere::gretch {

using namespace core;
using namespace ::testing;

struct FakePCP : sc::Module
{
    sc_core::sc_port<PCPTraining> training;
    explicit FakePCP(const sc_core::sc_module_name& name) : Module{name} {}
};

struct GRETCHTest : TestModule
{
    FakePCP pcp{"pcp"};
    GRETCH gretch{"gretch"};

    ::testing::StrictMock<sc::MockPutter<core::BackendMemReq>> prefetch_request;

    GRETCHTest() : TestModule{{"gretch.adt_size", 2}, {"gretch.commit_threshold", 6}, {"gretch.use_drt", false}}
    {
        bind_ports();
    }

    void bind_ports() { gretch.prefetch_request.bind(prefetch_request); }

    void SetUp() override
    {
        archx::Instruction::set_decoders({"furiosa"});
        pcp.training.bind(gretch.pcp_training);
    }

    static auto create_uop(ProgramCounter pc, VirtAddr virt_addr) { return create_load_uop(virt_addr, 0x0_pa, pc); }
};

TEST_F(GRETCHTest, Init)
{
    EXPECT_EQ(gretch.get_state(), GretchState::INIT);
}

TEST_F(GRETCHTest, AnyPartitionTakenIfSinglePCPTrain)
{
    auto uop = create_uop(0x0_pc, 0x0_va);
    uop->sch.partition = 1;
    pcp.training->train(uop, StrideTrainingMethod::STABLE_HI);
    gretch.update_pcp_state_change_pc();
    EXPECT_FALSE(gretch.update_pth(0x0_pc));
    EXPECT_TRUE(pcp.training->empty());
}

TEST_F(GRETCHTest, LowestPartitionTakenIfMultiplePCPTrain)
{
    for (unsigned i = 0; i < 2; ++i) {
        auto uop = create_uop(0x0_pc + i * 8, 0x0_va);
        uop->sch.partition = 1 - i;
        pcp.training->train(uop, StrideTrainingMethod::STABLE_HI);
    }
    gretch.update_pcp_state_change_pc();
    EXPECT_FALSE(gretch.update_pth(0x8_pc));
    EXPECT_TRUE(gretch.update_pth(0x0_pc));
    EXPECT_TRUE(pcp.training->empty());
}

TEST_F(GRETCHTest, ADTTriggerUnsuccessful)
{
    // 0xff40_pc is not a PCP-feedback-PC, so ADT epoch should not be triggered
    gretch.handle_load_committed_from_lsu(create_uop(0xff40_pc, 0xadcd_va), true);

    EXPECT_EQ(gretch.get_state(), GretchState::INIT);
    EXPECT_EQ(gretch.get_num_addr(), 0);
    EXPECT_EQ(gretch.get_adt_table().empty(), true);
}

TEST_F(GRETCHTest, ADTTriggerSuccessful)
{
    // 0xff40_pc is a PCP-feedback-PC, so ADT epoch should be triggered
    gretch.update_pth(0xff40_pc);
    gretch.handle_load_committed_from_lsu(create_uop(0xff40_pc, 0xadcd_va), true);

    EXPECT_EQ(gretch.get_state(), GretchState::POPULATE_ADT);
    EXPECT_EQ(gretch.get_num_addr(), 1);

    const auto& adt_table = gretch.get_adt_table();
    EXPECT_EQ(adt_table.size(), 1);

    auto adt_entry = adt_table[0];
    EXPECT_EQ(adt_entry.pc, 0x1d0_pc); // Gretch can access PC[10:2] only
    EXPECT_EQ(adt_entry.addr[0], 0xadcd_va);
    EXPECT_EQ(adt_entry.num_addresses, 1);
    EXPECT_EQ(adt_entry.num_data, 1);
}

TEST_F(GRETCHTest, ADT2AddrPerPC)
{
    gretch.update_pth(0xff40_pc);
    // PCP-feedback PC, should activate new round of ADT population
    gretch.handle_load_committed_from_lsu(create_uop(0xff40_pc, 0xadcd_va), true);
    // same pc, different addr (ADT should accept it)
    gretch.handle_load_committed_from_lsu(create_uop(0xff40_pc, 0xaddd_va), true);

    EXPECT_EQ(gretch.get_state(), GretchState::POPULATE_ADT);
    EXPECT_EQ(gretch.get_num_addr(), 2);

    const auto& adt_table = gretch.get_adt_table();
    EXPECT_EQ(adt_table.size(), 1);

    auto adt_entry = adt_table[0];
    EXPECT_EQ(adt_entry.pc, 0x1d0_pc); // Gretch can access PC[10:2] only
    EXPECT_EQ(adt_entry.addr[0], 0xadcd_va);
    EXPECT_EQ(adt_entry.addr[1], 0xaddd_va);
    EXPECT_EQ(adt_entry.num_addresses, 2);
    EXPECT_EQ(adt_entry.num_data, 2);
}

TEST_F(GRETCHTest, ADT3AddrPerPC)
{
    gretch.update_pth(0xff40_pc);
    // PCP-feedback PC, should activate new round of ADT population
    gretch.handle_load_committed_from_lsu(create_uop(0xff40_pc, 0xadcd_va), true);
    // same pc, diff addr (ADT should accept this)
    gretch.handle_load_committed_from_lsu(create_uop(0xff40_pc, 0xaddd_va), true);
    // same pc, diff addr (ADT should not accept this because only 2 unique addrs per PC)
    gretch.handle_load_committed_from_lsu(create_uop(0xff40_pc, 0xa2dd_va), true);

    EXPECT_EQ(gretch.get_state(), GretchState::POPULATE_ADT);
    EXPECT_EQ(gretch.get_num_addr(), 3);

    const auto& adt_table = gretch.get_adt_table();
    EXPECT_EQ(adt_table.size(), 1);

    auto adt_entry = adt_table[0];
    EXPECT_EQ(adt_entry.pc, 0x1d0_pc); // Gretch can access PC[10:2] only
    EXPECT_EQ(adt_entry.addr[0], 0xadcd_va);
    EXPECT_EQ(adt_entry.addr[1], 0xaddd_va);
    EXPECT_EQ(adt_entry.num_addresses, 2);
    EXPECT_EQ(adt_entry.num_data, 2);
}

TEST_F(GRETCHTest, ADTNewEntry)
{
    gretch.update_pth(0xff40_pc);
    // PCP-feedback PC, should activate new round of ADT population
    gretch.handle_load_committed_from_lsu(create_uop(0xff40_pc, 0xadcd_va), true);
    // diff pc (a new empty entry in ADT should be started)
    gretch.handle_load_committed_from_lsu(create_uop(0xff48_pc, 0xaefd_va), true);

    EXPECT_EQ(gretch.get_state(), GretchState::POPULATE_ADT);
    EXPECT_EQ(gretch.get_num_addr(), 2);

    const auto& adt_table = gretch.get_adt_table();
    EXPECT_EQ(adt_table.size(), 2);

    auto adt_entry = adt_table[0];
    EXPECT_EQ(adt_entry.pc, 0x1d0_pc); // Gretch can access PC[10:2] only
    EXPECT_EQ(adt_entry.addr[0], 0xadcd_va);
    EXPECT_EQ(adt_entry.num_addresses, 1);
    EXPECT_EQ(adt_entry.num_data, 1);

    adt_entry = adt_table[1];
    EXPECT_EQ(adt_entry.pc, 0x1d2_pc); // Gretch can access PC[10:2] only
    EXPECT_EQ(adt_entry.addr[0], 0xaefd_va);
    EXPECT_EQ(adt_entry.num_addresses, 1);
    EXPECT_EQ(adt_entry.num_data, 0); // data is not entered into non-first ADT entry
}

TEST_F(GRETCHTest, ADTFull)
{
    gretch.update_pth(0xff40_pc);
    // PCP-feedback PC, should activate new round of ADT population
    gretch.handle_load_committed_from_lsu(create_uop(0xff40_pc, 0xadcd_va), true);
    // diff pc (a new empty entry in ADT should be started)
    gretch.handle_load_committed_from_lsu(create_uop(0xff48_pc, 0xaefd_va), true);
    gretch.handle_load_committed_from_lsu(create_uop(0xff40_pc, 0xabcd_va), true);
    gretch.handle_load_committed_from_lsu(create_uop(0xff48_pc, 0xaecd_va), true);

    const auto& adt_table = gretch.get_adt_table();
    EXPECT_EQ(adt_table.size(), 2);

    auto adt_entry = adt_table[0];
    EXPECT_EQ(adt_entry.pc, 0x1d0_pc);
    EXPECT_EQ(adt_entry.addr[0], 0xadcd_va);
    EXPECT_EQ(adt_entry.addr[1], 0xabcd_va);
    EXPECT_EQ(adt_entry.num_addresses, 2);
    EXPECT_EQ(adt_entry.num_data, 2);

    adt_entry = adt_table[1];
    EXPECT_EQ(adt_entry.pc, 0x1d2_pc);
    EXPECT_EQ(adt_entry.addr[0], 0xaefd_va);
    EXPECT_EQ(adt_entry.addr[1], 0xaecd_va);
    EXPECT_EQ(adt_entry.num_addresses, 2);
    EXPECT_EQ(adt_entry.num_data, 0);                  // data is not entered into non-first ADT entry
    EXPECT_EQ(gretch.get_state(), GretchState::TRAIN); // ADT is full, switch to TRAIN state
    EXPECT_EQ(gretch.get_num_addr(), 0);               // reset for next epoch
}

TEST_F(GRETCHTest, ADTThreshold)
{
    gretch.update_pth(0xff40_pc);
    // PCP-feedback PC, should activate new round of ADT population
    gretch.handle_load_committed_from_lsu(create_uop(0xff40_pc, 0xadcd_va), true);
    // diff pc (a new empty entry in ADT should be started)
    gretch.handle_load_committed_from_lsu(create_uop(0xff48_pc, 0xaefd_va), true);
    gretch.handle_load_committed_from_lsu(create_uop(0xff48_pc, 0xaecd_va), true);
    // following uops should do nothing to ADT table. ADT only has 2 entries (i.e., 2 unique PCs).
    gretch.handle_load_committed_from_lsu(create_uop(0xff78_pc, 0xdf12_va), true);
    gretch.handle_load_committed_from_lsu(create_uop(0xff78_pc, 0xabcd_va), true);
    EXPECT_EQ(gretch.get_num_addr(), 5); // ADT has seen 5 addresses
    // this should get ADT into threshold condition
    gretch.handle_load_committed_from_lsu(create_uop(0xff98_pc, 0xdfc2_va), true);

    const auto& adt_table = gretch.get_adt_table();
    EXPECT_EQ(adt_table.size(), 2);

    auto adt_entry = adt_table[0];
    EXPECT_EQ(adt_entry.pc, 0x1d0_pc);
    EXPECT_EQ(adt_entry.addr[0], 0xadcd_va);
    EXPECT_EQ(adt_entry.num_addresses, 1);
    EXPECT_EQ(adt_entry.num_data, 1);

    adt_entry = adt_table[1];
    EXPECT_EQ(adt_entry.pc, 0x1d2_pc);
    EXPECT_EQ(adt_entry.addr[0], 0xaefd_va);
    EXPECT_EQ(adt_entry.addr[1], 0xaecd_va);
    EXPECT_EQ(adt_entry.num_addresses, 2);
    EXPECT_EQ(adt_entry.num_data, 0); // data is not entered into non-first ADT entry

    EXPECT_EQ(gretch.get_state(), GretchState::TRAIN); // threshold is reached, switch to TRAIN state
    EXPECT_EQ(gretch.get_num_addr(), 0);               // reset for next epoch
}

TEST_F(GRETCHTest, DDRelationship)
{
    // example from gap-pr-lj
    GRETCH::ADTEntry a;
    a.pc = ProgramCounter(0x403468_pc);
    a.addr[0] = 0xfffbc0bb18e8_va;
    a.addr[1] = 0xfffbc0bb18ec_va;
    a.data[0] = 2206824;
    a.data[1] = 2206844;
    a.num_addresses = 2;
    a.num_data = 2;

    GRETCH::ADTEntry b;
    b.pc = ProgramCounter(0x403470_pc);
    b.addr[0] = 0x3b15e2b0_va;
    b.addr[1] = 0x3b15e300_va;
    b.num_addresses = 2;
    b.num_data = 0;

    uint64_t base_addr = 0;
    uint64_t offset = 0;
    bool early_exit = false;
    bool dd_found = false;

    dd_found = gretch.dd_exists(a, b, base_addr, offset, early_exit);
    EXPECT_EQ(dd_found, true);
    EXPECT_EQ(base_addr, 0x3a8f3110);
    EXPECT_EQ(offset, 4);
    EXPECT_EQ(early_exit, false);

    // make one entry not-full -> division should be skipped
    base_addr = 0;
    offset = 0;
    early_exit = false;
    b.num_addresses = 1;
    dd_found = gretch.dd_exists(a, b, base_addr, offset, early_exit);
    EXPECT_EQ(dd_found, false);
    EXPECT_EQ(base_addr, 0);
    EXPECT_EQ(offset, 0);
    EXPECT_EQ(early_exit, true);

    // make data_diff in entry a equal 0 -> division should be skipped
    base_addr = 0;
    offset = 0;
    early_exit = false;
    a.data[1] = 2206824;
    dd_found = gretch.dd_exists(a, b, base_addr, offset, early_exit);
    EXPECT_EQ(dd_found, false);
    EXPECT_EQ(base_addr, 0);
    EXPECT_EQ(offset, 0);
    EXPECT_EQ(early_exit, true);

    // make data_diff > addr_diff -> division should be skipped
    base_addr = 0;
    offset = 0;
    early_exit = false;
    a.data[1] = 2206944;
    dd_found = gretch.dd_exists(a, b, base_addr, offset, early_exit);
    EXPECT_EQ(dd_found, false);
    EXPECT_EQ(base_addr, 0);
    EXPECT_EQ(offset, 0);
    EXPECT_EQ(early_exit, true);
}

} // namespace ampere::gretch

/*
 * vim: sw=4:ts=4:et:si:cindent:cino=\:0,(0,g0,t0:
 */
