//
// Copyright (c) 2023-2025 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//

#pragma once

#include "ampere/sys/sys.h"

#include "ampere/sc/testing/test_module.h"

namespace ampere {

class SysIntegrationTest : public ampere::sc::IntegrationTest
{
protected:
    std::unique_ptr<Sys> sys;

    SysIntegrationTest() = default;

    explicit SysIntegrationTest(sc_core::sc_time clock_period) : IntegrationTest{clock_period, {}} {}

    SysIntegrationTest(std::initializer_list<std::pair<std::string, sc::json>> settings) : IntegrationTest{settings} {}

    SysIntegrationTest(sc_core::sc_time clock_period, std::initializer_list<std::pair<std::string, sc::json>> settings)
        : IntegrationTest{clock_period, settings}
    {
    }

    void run_for(const sc_core::sc_time& duration)
    {
        if (!sys)
            sys = std::make_unique<Sys>("sys", clock, get_environment());

        IntegrationTest::run_for(duration);
    }

    void run_instruction_stream(const std::string& stream_name, const sc_core::sc_time& duration)
    {
        get_environment()->parameters.add_setting(
            "sys.soc_0.core_0.instruction_stream_name", stream_name, "test bench");
        run_for(duration);
    }

    void run_instruction_stream(const std::string& stream_name, uint64_t instructions, const sc_core::sc_time& duration)
    {
        get_environment()->parameters.add_setting("sys.soc_0.core_0.gpc.max_instructions", instructions, "test bench");
        run_instruction_stream(stream_name, duration);
    }
};

} // namespace ampere

/*
 * vim: sw=4:ts=4:et:si:cindent:cino=\:0,(0,g0,t0:
 */
