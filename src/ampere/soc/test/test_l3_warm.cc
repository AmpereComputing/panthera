//
// Copyright (c) 2022 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//
#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "ampere/mesh/injector/mesh_injector.h"
#include "ampere/sc/testing/test_module.h"
#include "ampere/soc/soc.h"

namespace ampere::soc {

using namespace ampere::mesh;
using namespace ampere::archx;

struct WarmupTest : public IntegrationTest
{};

TEST_F(WarmupTest, CacheAndSFWarmupUC)
{
    auto& params = get_environment()->parameters;

    params.load(TestModule::testconfig("soc/domino-pha.cfg"), "soc");
    params.load(TestModule::testconfig("inj/prog_chase.cfg"), "soc.core_0.source");

    // shrink the caches to just 4 lines each
    params.add_setting("~soc\\.core_[012]\\.cache.sets", 4, "test bench");
    params.add_setting("~soc\\.core_[012]\\.cache.ways", 1, "test bench");
    params.add_setting("~soc\\.core_[012]\\.l3c.sets", 4, "test bench");
    params.add_setting("~soc\\.core_[012]\\.l3c.ways", 1, "test bench");

    // warm up with 8 lines, which means the oldest should end up in the L3 of
    // injectors 1 and 2, the youngest in the L2 of injector 0
    params.add_setting("soc.core_0.source.enable_warmup", true, "test bench");
    params.add_setting("soc.core_0.source.warmup_start_addr", 1024, "test bench");
    params.add_setting("soc.core_0.source.warmup_stop_addr", 1536, "test bench");
    params.add_setting("soc.core_0.source.enable_events_on_start", true, "test bench");

    Soc soc{"soc", clock, get_environment()};

    run_for(0_ns); // just need to complete elaboration for warmup to occur

    auto inj0 = dynamic_cast<MeshInjector*>(sc_core::sc_find_object("soc.core_0"));
    auto inj1 = dynamic_cast<MeshInjector*>(sc_core::sc_find_object("soc.core_1"));
    auto inj2 = dynamic_cast<MeshInjector*>(sc_core::sc_find_object("soc.core_2"));
    auto sf = dynamic_cast<SnoopFilter*>(sc_core::sc_find_object("soc.pxha_0.snoop_filter"));

    std::ostringstream oss;

    inj0->dump_l2(oss);
    inj0->dump_l3(oss);
    inj1->dump_l2(oss);
    inj1->dump_l3(oss);
    inj2->dump_l2(oss);
    inj2->dump_l3(oss);
    sf->dump(oss);

    std::string expected = R"(soc.core_0 L2:
set 0: addr:500 state:UC
set 1: addr:540 state:UC
set 2: addr:580 state:UC
set 3: addr:5c0 state:UC
soc.core_0 L3:
soc.core_1 L2:
soc.core_1 L3:
set 0: addr:400 state:UC
set 1: addr:480 state:UC
soc.core_2 L2:
soc.core_2 L3:
set 0: addr:440 state:UC
set 1: addr:4c0 state:UC
soc.pxha_0.snoop_filter:

 Set | Way | L |       Addr         | State |   PST   | CVs
   0 |  0  |   | 0x0000000000000400 |  UC   |  0.0.4  |
   0 |  1  |   | 0x0000000000000500 |  UC   |  0.0.0  |
   1 |  0  |   | 0x0000000000000440 |  UC   |  0.0.5  |
   1 |  1  |   | 0x0000000000000540 |  UC   |  0.0.0  |
   2 |  0  |   | 0x0000000000000480 |  UC   |  0.0.4  |
   2 |  1  |   | 0x0000000000000580 |  UC   |  0.0.0  |
   3 |  0  |   | 0x00000000000004c0 |  UC   |  0.0.5  |
   3 |  1  |   | 0x00000000000005c0 |  UC   |  0.0.0  |
soc.pxha_0.snoop_filter: snoop filter occupancy: 8
)";

    EXPECT_EQ(oss.str(), expected);
}

TEST_F(WarmupTest, CacheAndSFWarmupUD)
{
    auto& params = get_environment()->parameters;

    params.load(TestModule::testconfig("soc/domino-pha.cfg"), "soc");
    params.load(TestModule::testconfig("inj/prog_write.cfg"), "soc.core_0.source");

    // shrink the caches to just 4 lines each
    params.add_setting("~soc\\.core_[012]\\.cache.sets", 4, "test bench");
    params.add_setting("~soc\\.core_[012]\\.cache.ways", 1, "test bench");
    params.add_setting("~soc\\.core_[012]\\.l3c.sets", 4, "test bench");
    params.add_setting("~soc\\.core_[012]\\.l3c.ways", 1, "test bench");

    // warm up with 8 lines, which means the oldest should end up in the L3 of
    // injectors 0 and 1, the youngest in the L2 of injector 0
    params.add_setting("soc.core_0.source.enable_warmup", true, "test bench");
    params.add_setting("soc.core_0.source.start_addr", 1024, "test bench");
    params.add_setting("soc.core_0.source.stop_addr", 1536, "test bench");
    params.add_setting("soc.core_0.source.enable_events_on_start", true, "test bench");

    Soc soc{"soc", clock, get_environment()};

    run_for(0_ns); // just need to complete elaboration for warmup to occur

    auto inj0 = dynamic_cast<MeshInjector*>(sc_core::sc_find_object("soc.core_0"));
    auto inj1 = dynamic_cast<MeshInjector*>(sc_core::sc_find_object("soc.core_1"));
    auto inj2 = dynamic_cast<MeshInjector*>(sc_core::sc_find_object("soc.core_2"));

    // For the L3, stashes alternate between inj1 and inj2
    EXPECT_EQ(inj1->peek_state_l3(1024_pa), MeshInjector::CacheEntry::State::UD);
    EXPECT_EQ(inj2->peek_state_l3(1088_pa), MeshInjector::CacheEntry::State::UD);
    EXPECT_EQ(inj1->peek_state_l3(1152_pa), MeshInjector::CacheEntry::State::UD);
    EXPECT_EQ(inj2->peek_state_l3(1216_pa), MeshInjector::CacheEntry::State::UD);

    EXPECT_EQ(inj0->peek_state_l2(1280_pa), MeshInjector::CacheEntry::State::UD);
    EXPECT_EQ(inj0->peek_state_l2(1344_pa), MeshInjector::CacheEntry::State::UD);
    EXPECT_EQ(inj0->peek_state_l2(1408_pa), MeshInjector::CacheEntry::State::UD);
    EXPECT_EQ(inj0->peek_state_l2(1472_pa), MeshInjector::CacheEntry::State::UD);
}

TEST_F(WarmupTest, CacheAndSFWarmupMixed)
{
    auto& params = get_environment()->parameters;

    params.load(TestModule::testconfig("soc/domino-pha.cfg"), "soc");
    params.load(TestModule::testconfig("inj/prog_2r1w.cfg"), "soc.core_0.source");

    // shrink the caches to just 4 lines each
    params.add_setting("~soc\\.core_[012]\\.cache.sets", 4, "test bench");
    params.add_setting("~soc\\.core_[012]\\.cache.ways", 1, "test bench");
    params.add_setting("~soc\\.core_[012]\\.l3c.sets", 4, "test bench");
    params.add_setting("~soc\\.core_[012]\\.l3c.ways", 1, "test bench");

    // warm up with 8 lines, which means the oldest should end up in the L3 of
    // injectors 0 and 1, the youngest in the L2 of injector 0
    params.add_setting("soc.core_0.source.enable_warmup", true, "test bench");
    params.add_setting("soc.core_0.source.warmup_start_addr", 1024, "test bench");
    params.add_setting("soc.core_0.source.warmup_stop_addr", 1536, "test bench");
    params.add_setting("soc.core_0.source.enable_events_on_start", true, "test bench");

    Soc soc{"soc", clock, get_environment()};

    run_for(0_ns); // just need to complete elaboration for warmup to occur

    auto inj0 = dynamic_cast<MeshInjector*>(sc_core::sc_find_object("soc.core_0"));
    auto inj1 = dynamic_cast<MeshInjector*>(sc_core::sc_find_object("soc.core_1"));
    auto inj2 = dynamic_cast<MeshInjector*>(sc_core::sc_find_object("soc.core_2"));

    // For the L3, stashes alternate between inj1 and inj2
    EXPECT_EQ(inj1->peek_state_l3(1024_pa), MeshInjector::CacheEntry::State::UD);
    EXPECT_EQ(inj2->peek_state_l3(1088_pa), MeshInjector::CacheEntry::State::UC);
    EXPECT_EQ(inj1->peek_state_l3(1152_pa), MeshInjector::CacheEntry::State::UD);
    EXPECT_EQ(inj2->peek_state_l3(1216_pa), MeshInjector::CacheEntry::State::UC);

    EXPECT_EQ(inj0->peek_state_l2(1280_pa), MeshInjector::CacheEntry::State::UD);
    EXPECT_EQ(inj0->peek_state_l2(1344_pa), MeshInjector::CacheEntry::State::UC);
    EXPECT_EQ(inj0->peek_state_l2(1408_pa), MeshInjector::CacheEntry::State::UD);
    EXPECT_EQ(inj0->peek_state_l2(1472_pa), MeshInjector::CacheEntry::State::UC);
}

TEST_F(WarmupTest, CacheAndSFWarmupSC)
{
    auto& params = get_environment()->parameters;

    params.load(TestModule::testconfig("soc/domino-pha.cfg"), "soc");
    params.load(TestModule::testconfig("inj/prog_read.cfg"), "soc.core_0.source");
    params.load(TestModule::testconfig("inj/prog_read.cfg"), "soc.core_1.source");

    // shrink the caches to just 4 lines each
    params.add_setting("~soc\\.core_[012]\\.cache.sets", 4, "test bench");
    params.add_setting("~soc\\.core_[012]\\.cache.ways", 1, "test bench");
    params.add_setting("~soc\\.core_[012]\\.l3c.sets", 4, "test bench");
    params.add_setting("~soc\\.core_[012]\\.l3c.ways", 1, "test bench");

    // warm up with 8 lines but both injectors are trying to read the same memory
    params.add_setting("soc.core_0.source.enable_warmup", true, "test bench");
    params.add_setting("soc.core_0.source.start_addr", 0x400, "test bench");
    params.add_setting("soc.core_0.source.stop_addr", 0x600, "test bench");
    params.add_setting("soc.core_0.source.enable_events_on_start", true, "test bench");
    params.add_setting("soc.core_1.source.enable_warmup", true, "test bench");
    params.add_setting("soc.core_1.source.start_addr", 0x400, "test bench");
    params.add_setting("soc.core_1.source.stop_addr", 0x600, "test bench");
    params.add_setting("soc.core_1.source.enable_events_on_start", true, "test bench");
    params.add_setting("soc.core_1.enable", true, "test bench");
    params.add_setting("soc.core_1.stash_only", false, "test bench");
    params.add_setting("soc.core_2.enable", false, "test bench");

    Soc soc{"soc", clock, get_environment()};

    run_for(0_ns); // just need to complete elaboration for warmup to occur

    auto inj0 = dynamic_cast<MeshInjector*>(sc_core::sc_find_object("soc.core_0"));
    auto inj1 = dynamic_cast<MeshInjector*>(sc_core::sc_find_object("soc.core_1"));
    auto sf = dynamic_cast<SnoopFilter*>(sc_core::sc_find_object("soc.pxha_0.snoop_filter"));

    std::ostringstream oss;

    inj0->dump_l2(oss);
    inj0->dump_l3(oss);
    inj1->dump_l2(oss);
    inj1->dump_l3(oss);
    sf->dump(oss);

    std::string expected = R"(soc.core_0 L2:
set 0: addr:500 state:SC
set 1: addr:540 state:SC
set 2: addr:580 state:SC
set 3: addr:5c0 state:SC
soc.core_0 L3:
set 0: addr:400 state:SC
set 1: addr:440 state:SC
set 2: addr:480 state:SC
set 3: addr:4c0 state:SC
soc.core_1 L2:
set 0: addr:500 state:SC
set 1: addr:540 state:SC
set 2: addr:580 state:SC
set 3: addr:5c0 state:SC
soc.core_1 L3:
soc.pxha_0.snoop_filter:

 Set | Way | L |       Addr         | State |   PST   | CVs
   0 |  0  |   | 0x0000000000000400 |  SC   |  0.0.0  |
   0 |  1  |   | 0x0000000000000500 |  SC   |  0.0.0  | 0.0.4
   1 |  0  |   | 0x0000000000000440 |  SC   |  0.0.0  |
   1 |  1  |   | 0x0000000000000540 |  SC   |  0.0.0  | 0.0.4
   2 |  0  |   | 0x0000000000000480 |  SC   |  0.0.0  |
   2 |  1  |   | 0x0000000000000580 |  SC   |  0.0.0  | 0.0.4
   3 |  0  |   | 0x00000000000004c0 |  SC   |  0.0.0  |
   3 |  1  |   | 0x00000000000005c0 |  SC   |  0.0.0  | 0.0.4
soc.pxha_0.snoop_filter: snoop filter occupancy: 8
)";

    EXPECT_EQ(oss.str(), expected);
}

TEST_F(WarmupTest, RunWithWarmCachesHasNoMemoryAccesses)
{
    auto& params = get_environment()->parameters;

    params.load(TestModule::testconfig("soc/domino-pha.cfg"), "soc");
    params.load(TestModule::testconfig("inj/prog_read.cfg"), "soc.core_0.source");

    // shrink the caches to just 4 lines each
    params.add_setting("~soc\\.core_[012]\\.cache.sets", 4, "test bench");
    params.add_setting("~soc\\.core_[012]\\.cache.ways", 1, "test bench");
    params.add_setting("~soc\\.core_[012]\\.l3c.sets", 4, "test bench");
    params.add_setting("~soc\\.core_[012]\\.l3c.ways", 2, "test bench");

    // the memory range is only 8 lines, so it should fit entirely in the L2 and
    // L3 of the mesh injectors. After reading memory, we should not have generated
    // any requests out to the MCUs
    params.add_setting("soc.core_0.source.enable_warmup", true, "test bench");
    params.add_setting("soc.core_0.source.start_addr", 1024, "test bench");
    params.add_setting("soc.core_0.source.stop_addr", 1536, "test bench");
    params.add_setting("soc.core_0.source.enable_events_on_start", true, "test bench");
    params.add_setting("soc.core_2.enable", false, "test bench"); // Force stashing into inj #1

    Soc soc{"soc", clock, get_environment()};

    run_for(1000_ns);

    EXPECT_GE(event_count("soc.core_0.send_req"), 20);
    EXPECT_EQ(event_count("soc.memory_0.recv_req"), 0);
}

} // namespace ampere::soc
