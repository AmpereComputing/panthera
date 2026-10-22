//
// Copyright (c) 2025 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//

#pragma once

#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include "ampere/sc/report.h"

#define EXPECT_AMPERE_SC_MESSAGE(stmt, severity, matcher)                                     \
    do {                                                                                      \
        try {                                                                                 \
            stmt;                                                                             \
            FAIL() << "Expected AMPERE_FATAL exception, but statement executed successfully"; \
        } catch (sc_core::sc_report const& report) {                                          \
            EXPECT_STREQ(report.get_msg_type(), AMPERE_SC_MESSAGE_TYPE)                       \
                << "Expected AMPERE_SC_MESSAGE_TYPE, found " << report.get_msg_type();        \
            EXPECT_EQ(report.get_severity(), severity);                                       \
            EXPECT_THAT(report.get_msg(), matcher);                                           \
            SUCCEED();                                                                        \
        } catch (...) {                                                                       \
            FAIL() << "Expected AMPERE_FATAL exception, caught exception of different type";  \
        }                                                                                     \
    } while (0)

// Test that AMPERE_FATAL or AMPERE_FATAL is called when expected for the
// supplied statement, `stmt` (for example, via a failing AMPERE_ASSERT).
// `matcher` is a googletest matcher (i.e.
// https://google.github.io/googletest/reference/matchers.html) which will be
// checked against the AMPERE_FATAL/AMPERE_ERROR/AMPERE_ASSERT's message
// string.
#define EXPECT_AMPERE_FATAL(stmt, matcher) EXPECT_AMPERE_SC_MESSAGE(stmt, sc_core::SC_FATAL, matcher)
#define EXPECT_AMPERE_ERROR(stmt, matcher) EXPECT_AMPERE_SC_MESSAGE(stmt, sc_core::SC_ERROR, matcher)
