//
// Copyright (c) 2020 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//
#include <gtest/gtest.h>
#include "ampere/sc/testing/test_module.h"
#include "ampere/sc/credit.h"

using namespace ampere::sc;

struct CreditPoolIntegrationTest : ampere::sc::IntegrationTest
{};

TEST_F(CreditPoolIntegrationTest, CanNotShrinkAfterElaboration)
{
    Environment::get_root()->parameters.add_setting("pool1.size", 2, "unit test");
    CreditPool pool{"pool1"};
    run_for(sc_core::sc_time{1.0, sc_core::SC_NS});
    ASSERT_ANY_THROW(pool.shrink(1));
}

/*
 * vim: sw=4:ts=4:et:si:cindent:cino=\:0,(0,g0,t0:
 */

