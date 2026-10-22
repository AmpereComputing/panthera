//
// Copyright (c) 2024 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//

#include "ampere/sys/sys.h"
#include "ampere/sc/testing/test_module.h"

using namespace ::testing;

class EmulationValidationIntegrationTest : public IntegrationTest
{
public:
    std::uint64_t divergences{0};
    void add_uint_data(const std::string& event_name,
                       int id,
                       const std::string& data_type_name,
                       std::uint64_t value) override
    {
        if (event_name == "sys.soc_0.core_0.gpc.emulation"
            && data_type_name == "sys.soc_0.core_0.gpc.emulation_divergences")
        {
            divergences = value;
        }
    }
};

TEST_F(EmulationValidationIntegrationTest, testEmulation)
{
    get_environment()->parameters.add_setting(
        "sys.soc_0.core_0.instruction_stream_name", TestModule::testfixture("ls_0.is.xz"), "test bench");
    get_environment()->parameters.add_setting("sys.soc_0.core_0.gpc.max_instructions", 30000, "test bench");
    get_environment()->parameters.add_setting("sys.soc_0.core_0.speculative_memory_image", "true", "test bench");
    get_environment()->parameters.add_setting("sys.soc_0.core_0.use_wrong_path_emulation", "true", "test bench");
    get_environment()->parameters.add_setting("sys.soc_0.core_0.validate_emulation", "1", "test bench");

    get_environment()->parameters.load(TestModule::testconfig("sys/domino.cfg"));

    Sys sys("sys", clock, get_environment());

    run_for(20000_ns);
    // We expect one divergence because of this tracer bug (SP wrong on EL0->EL1 transitions).
    EXPECT_EQ(divergences, 1);
}
