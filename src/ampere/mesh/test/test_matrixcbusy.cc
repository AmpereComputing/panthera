//
// Copyright (c) 2025 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//
#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "ampere/sc/environment.h"
#include "ampere/mesh/matrixcbusy.h"

using namespace ampere::mesh;

TEST(MatrixCBusyTest, TestOccupancyBusyness)
{
    auto& params = ampere::sc::Environment::get_root()->parameters;

    // Basic completer params
    params.add_setting("cbusy.enable", true, "test bench");
    params.add_setting("cbusy.throttle_start", 1, "test bench");
    params.add_setting("cbusy.highest_cbusy", 6, "test bench");
    params.add_setting("cbusy.window", 64, "test bench");
    params.add_setting("cbusy.phase_cycles", 16, "test bench");
    params.add_setting("cbusy.completer_lut", "[0, 0.4, 0.9, 1.0, 1.1, 1.2, 2.0, 100]"_json, "test bench");
    params.add_setting("cbusy.threshold", 32, "test bench");

    // Occupancy Busyness
    params.add_setting("cbusy.occupancy_busyness", "[1, 3, 5, 7]"_json, "test bench");
    params.add_setting("cbusy.occupancy_cbusy_value", "[0, 3, 4, 5, 6]"_json, "test bench");

    MatrixCBusy cbusy{"cbusy"};

    EXPECT_EQ(cbusy.update_cbusy(0), chi::CBusyState::None);
    EXPECT_EQ(cbusy.update_cbusy(1), chi::CBusyState::High);
    EXPECT_EQ(cbusy.update_cbusy(2), chi::CBusyState::High);
    EXPECT_EQ(cbusy.update_cbusy(3), chi::CBusyState::Four);
    EXPECT_EQ(cbusy.update_cbusy(4), chi::CBusyState::Four);
    EXPECT_EQ(cbusy.update_cbusy(5), chi::CBusyState::Five);
    EXPECT_EQ(cbusy.update_cbusy(6), chi::CBusyState::Five);
    EXPECT_EQ(cbusy.update_cbusy(7), chi::CBusyState::Six);
    EXPECT_EQ(cbusy.update_cbusy(8), chi::CBusyState::Six);
}
