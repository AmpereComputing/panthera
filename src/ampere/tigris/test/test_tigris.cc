//
// Copyright (c) 2023 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//

#include <gtest/gtest.h>
#include "ampere/tigris/tigris.h"
#include "ampere/sc/testing/test_module.h"

namespace bfs = boost::filesystem;

namespace ampere::tigris {

using namespace archx;
using namespace core;
using namespace sc;

class TigrisIntegration : public IntegrationTest
{
protected:
    TigrisIntegration() : IntegrationTest()
    {
        get_environment()->parameters.add_setting(
            "instruction_stream",
            (bfs::path(TEST_FIXTURE_DIRECTORY) / "../../core/test/sequential-stream_0.is.xz").string(),
            "test bench");
        get_environment()->parameters.load(
            (bfs::path(MODEL_CONFIG_TREE) / "core/prefetching/tigris/core.cfg").string());
    }

    void setup_and_run()
    {
        tigris = std::make_unique<InstructionStreamRunner<Tigris>>(*clock, *get_environment());
        tigris->setup();

        get_environment()->event_stream->enable();
        this->run_for(80000_ns);
    }

    std::unique_ptr<InstructionStreamRunner<Tigris>> tigris;
};

TEST_F(TigrisIntegration, NoPrefetchers)
{
    setup_and_run();

    EXPECT_GT(event_count("top.load_execute"), 1000);
    EXPECT_EQ(event_count("top.memory.l2.prefetcher.launch"), 0);
}

class TigrisPrefetcherIntegration : public TigrisIntegration, public ::testing::WithParamInterface<std::string>
{
    void SetUp() override
    {
        get_environment()->parameters.add_setting("top.memory.l2.prefetcher.type", GetParam(), "test bench");
    }
};

INSTANTIATE_TEST_SUITE_P(TigrisPrefetcherIntegration,
                         TigrisPrefetcherIntegration,
                         ::testing::Values("L2P_NextLine", "L2P_Spatial", "L2P_Oracle"));

TEST_P(TigrisPrefetcherIntegration, Launches)
{
    setup_and_run();

    EXPECT_GT(event_count("top.load_execute"), 1000);
    EXPECT_GT(event_count("top.memory.l2.prefetcher.launch"), 25);
}

} // namespace ampere::tigris

/*
 * vim: sw=4:ts=4:et:si:cindent:cino=\:0,(0,g0,t0:
 */
