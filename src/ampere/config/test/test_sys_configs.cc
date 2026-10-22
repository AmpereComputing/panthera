//
// Copyright (c) 2023 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//

#include "ampere/config/test/config_test.h"
#include <gtest/gtest-param-test.h>
#include <gtest/gtest.h>

const auto soc_configs = testing::Values("sys/siryn.cfg",
                                         "sys/siryn-rate1.cfg",
                                         "sys/siryn-rate1-aggressive.cfg",
                                         "sys/siryn-rateN.cfg",
                                         // FIXME: soc/core-rtl.cfg broken
                                         //"sys/siryn-core-rtl.cfg",
                                         "sys/siryn-perfect-frontend.cfg",
                                         "sys/banshee.cfg",
                                         "sys/banshee-rate1.cfg",
                                         "sys/banshee-rate1-aggressive.cfg",
                                         "sys/banshee-rateN.cfg",
                                         // FIXME: soc/core-rtl.cfg broken
                                         //"sys/banshee-core-rtl.cfg",
                                         "sys/rogue.cfg",
                                         "sys/rogue-rate1.cfg",
                                         "sys/rogue-rate1-aggressive.cfg",
                                         "sys/rogue-rateN.cfg",
                                         "sys/rogue-core-rtl.cfg",
                                         "sys/rogue-perfect-frontend.cfg",
                                         "sys/domino.cfg",
                                         "sys/domino-rate1.cfg",
                                         "sys/domino-rate1-aggressive.cfg",
                                         "sys/domino-rateN.cfg",
                                         "sys/domino-core-rtl.cfg",
                                         "sys/domino-6wide.cfg",
                                         "sys/domino-perfect-frontend.cfg",
                                         "sys/nebula.cfg",
                                         "sys/nebula-rate1.cfg",
                                         "sys/nebula-rate1-aggressive.cfg",
                                         "sys/nebula-rateN.cfg",
                                         "sys/kestrel-user.cfg",
                                         "sys/kestrel-linux.cfg",
                                         "sys/kestrel-core-rtl-user.cfg",
                                         "sys/kestrel-core-rtl-linux.cfg");

struct SysConfigTest : ConfigTest
{};
INSTANTIATE_TEST_SUITE_P(SysConfigTest, SysConfigTest, soc_configs);

TEST_P(SysConfigTest, TestConfigIsValid) {}

/*
 * vim: sw=4:ts=4:et:si:cindent:cino=\:0,(0,g0,t0:
 */
