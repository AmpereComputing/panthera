//
// Copyright (c) 2024 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//

#pragma once

#include <string>

#include "ampere/sc/report.h"
#include "ampere/sc/transaction.h"

#define AMPERE_ASSERT_EVENT(expr, txn, ...)                           \
    do { /* NOLINT(cppcoreguidelines-avoid-do-while) */               \
        if (!(expr)) {                                                \
            auto message = AMPERE_FMT_STR(__VA_ARGS__);               \
            ::ampere::sc::post_assert_event(*txn, message);           \
            SC_REPORT_FATAL(AMPERE_SC_MESSAGE_TYPE, message.c_str()); \
        }                                                             \
    } while (0)

namespace ampere::sc {

void post_assert_event(Transaction& txn, const std::string& message);

}

/*
 * vim: sw=4:ts=4:et:si:cindent:cino=\:0,(0,g0,t0:
 */
