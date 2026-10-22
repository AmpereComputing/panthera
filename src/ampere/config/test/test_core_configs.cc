//
// Copyright (c) 2023 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//

#include "ampere/config/test/config_test.h"
#include "ampere/sc/param_registry.h"
#include <array>
#include <regex>
#include <string>
#include <utility>
#include <gtest/gtest-param-test.h>
#include <gtest/gtest.h>

const auto core_configs = testing::Values("core/furiosa/furiosa.cfg",
                                          "core/furiosa/furiosa-perfect-frontend.cfg",
                                          "core/ripley/ripley.cfg",
                                          "core/ripley/ripley-perfect-frontend.cfg",
                                          "core/trinity/trinity.cfg",
                                          "core/trinity/trinity-perfect-frontend.cfg",
                                          "core/connor/connor.cfg",
                                          "core/connor/connor-perfect-frontend.cfg",
                                          "core/connor-ai/connor-ai-user.cfg",
                                          "core/connor-ai/connor-ai-linux.cfg",
                                          "core/alita/alita.cfg",
                                          "core/alita/alita-perfect-frontend.cfg",
                                          "core/alita/pathfinding/alita.cfg",
                                          "core/alita/pathfinding/alita-perfect-frontend.cfg");

using config_matcher = std::pair<std::regex, std::string>;

// Configs at the core-level
const std::array core_feature_configs = {
    config_matcher(R"(furiosa|ripley)", "core/lsu_prefetchers/disable_pcp.cfg"),
    // Only match 'connor/' because gretch is already disabled on connor-ai
    config_matcher(R"(trinity|connor\/|alita)", "core/lsu_prefetchers/disable_pcp_gretch.cfg"),
    config_matcher(R"(trinity|connor\/|alita)", "core/lsu_prefetchers/disable_gretch.cfg"),
    config_matcher(R"(.*)", "core/pv/disable_bop.cfg"),
    // Only match 'connor/' because gretch is already disabled on connor-ai
    config_matcher(R"(connor\/)", "core/pv/all_prefetchers_disabled.cfg"),
    config_matcher(R"(connor\/)", "core/pv/all_prefetchers_enabled.cfg"),
    config_matcher(R"(connor)", "core/pv/disable_sf_aux_pred.cfg"),
};

// LSU prefetcher configs which override the lsu.prefetcher
const std::array lsu_prefetcher_configs = {
    // FIXME: broken on alita
    config_matcher(R"r(^((?!alita).)*$)r", "core/lsu_prefetchers/none.cfg"),
    config_matcher(R"r(^((?!alita).)*$)r", "core/lsu_prefetchers/oracle.cfg"),
};

// L2C prefetcher configs which override the l2c.prefetcher
const std::array l2c_prefetcher_configs = {
    config_matcher(R"(.*)", "core/l2c_prefetchers/none.cfg"),
    config_matcher(R"(.*)", "core/l2c_prefetchers/oracle.cfg"),
};

struct CoreConfigTest : ConfigTest
{
    void SetUp() override
    {
        get_environment()->parameters.add_setting(
            "sys.soc_0.core_0.instruction_stream_name", TestModule::testfixture("dhrystone.is.xz"), "test bench");
        get_environment()->parameters.load(TestModule::testconfig("sys/core-test-soc.cfg"), "sys.soc_0");
        get_environment()->parameters.load(TestModule::testconfig(GetParam()), "sys.soc_0.core_0");
    }
};
INSTANTIATE_TEST_SUITE_P(CoreConfigTest, CoreConfigTest, core_configs);

TEST_P(CoreConfigTest, TestConfigIsValid) {}

struct CoreConfigFeatureTest : ConfigsTest<std::tuple<std::string, config_matcher>>
{
    std::string feature_path;
    bool skipped{false};
    void SetUp() override
    {
        auto [re, name] = std::get<1>(GetParam());
        feature_path = name;
        skipped = !std::regex_search(std::get<0>(GetParam()), re);
        if (skipped) {
            GTEST_SKIP();
        } else {
            ConfigsTest<std::tuple<std::string, config_matcher>>::SetUp();
            get_environment()->parameters.load(TestModule::testconfig("sys/core-test-soc.cfg"), "sys.soc_0");
            get_environment()->parameters.load(TestModule::testconfig(std::get<0>(GetParam())), "sys.soc_0.core_0");
        }
    }
};
struct CoreConfigTestWithCoreLevelFeatures : CoreConfigFeatureTest
{
    void SetUp() override
    {
        CoreConfigFeatureTest::SetUp();
        if (!skipped) {
            get_environment()->parameters.load(TestModule::testconfig(feature_path),
                                               "sys.soc_0.core_0",
                                               ampere::sc::ParameterRegistry::ConfigScopeAction::APPEND);
        }
    }
};
INSTANTIATE_TEST_SUITE_P(CoreConfigTestWithCoreLevelFeatures,
                         CoreConfigTestWithCoreLevelFeatures,
                         ::testing::Combine(core_configs, ::testing::ValuesIn(core_feature_configs)));

TEST_P(CoreConfigTestWithCoreLevelFeatures, TestConfigIsValid) {}

struct CoreConfigTestWithLSUPrefetchers : CoreConfigFeatureTest
{
    void SetUp() override
    {
        CoreConfigFeatureTest::SetUp();
        if (!skipped) {
            get_environment()->parameters.add_setting(
                "sys.soc_0.core_0.lsu.prefetcher:override-cfg", TestModule::testconfig(feature_path), "test");
        }
    }
};
INSTANTIATE_TEST_SUITE_P(CoreConfigTestWithLSUPrefetchers,
                         CoreConfigTestWithLSUPrefetchers,
                         ::testing::Combine(core_configs, ::testing::ValuesIn(lsu_prefetcher_configs)));

TEST_P(CoreConfigTestWithLSUPrefetchers, TestConfigIsValid) {}

struct CoreConfigTestWithL2CPrefetchers : CoreConfigFeatureTest
{
    void SetUp() override
    {
        CoreConfigFeatureTest::SetUp();
        if (!skipped) {
            get_environment()->parameters.add_setting(
                "sys.soc_0.core_0.l2c.prefetcher:override-cfg", TestModule::testconfig(feature_path), "test");
        }
    }
};
INSTANTIATE_TEST_SUITE_P(CoreConfigTestWithL2CPrefetchers,
                         CoreConfigTestWithL2CPrefetchers,
                         ::testing::Combine(core_configs, ::testing::ValuesIn(l2c_prefetcher_configs)));

TEST_P(CoreConfigTestWithL2CPrefetchers, TestConfigIsValid) {}

/*
 * vim: sw=4:ts=4:et:si:cindent:cino=\:0,(0,g0,t0:
 */
