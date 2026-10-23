//
// Copyright (c) 2018-2019 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//

#include <systemc.h>
#include <gtest/gtest.h>

#include "ampere/sc/report.h"

extern "C" int sc_main(int argc, char** argv)
{
    testing::InitGoogleTest(&argc, argv);
    sc_report_handler::set_actions(SC_ID_INSTANCE_EXISTS_, SC_DO_NOTHING);

    // Convert calls to AMPERE_FATAL from SC_ABORT to SC_THROW to allow
    // catching them as exceptions in tests (via EXPECT_AMPERE_FATAL()), and
    // also avoids printing their output to stdout.
    sc_report_handler::set_actions(AMPERE_SC_MESSAGE_TYPE, SC_FATAL, SC_LOG | SC_CACHE_REPORT | SC_THROW);

    // Uncomment the following line if you need to debug an error in the integration
    // tests which is throwing an exception and exiting too early (such as a brain
    // death). Unfortunately this can't be set all the time or certain "death tests"
    // won't detect exceptions.
    // sc_core::sc_report_handler::set_actions(sc_core::SC_ERROR, sc_core::SC_DISPLAY | sc_core::SC_STOP);
    return RUN_ALL_TESTS();
}

/*
 * vim: sw=4:ts=4:et:si:cindent:cino=\:0,(0,g0,t0:
 */
