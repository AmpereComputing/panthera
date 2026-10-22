//
// Copyright (c) 2023 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//

#include "ampere/sc/testing/test_module.h"
#include "ampere/sys/sys.h"
#include <gtest/gtest-param-test.h>
#include <gtest/gtest.h>

using namespace ::testing;
using namespace ampere::sc;
using ampere::Sys;

template<typename T>
struct ConfigsTest : TestModule, WithParamInterface<T>
{
    void SetUp() override
    {
        get_environment()->parameters.add_setting(
            "sys.soc_0.core_0.instruction_stream_name", TestModule::testfixture("dhrystone.is.xz"), "test bench");
    }

    void TearDown() override
    {
        Sys sys("sys", clock, get_environment());
        bool all_matched = get_environment()->parameters.check_all_matched();
        ASSERT_TRUE(all_matched);
    }
};

struct ConfigTest : ConfigsTest<std::string>
{
    void SetUp() override
    {
        ConfigsTest<std::string>::SetUp();
        get_environment()->parameters.load(TestModule::testconfig(GetParam()));
    }
};

/*
 * vim: sw=4:ts=4:et:si:cindent:cino=\:0,(0,g0,t0:
 */
