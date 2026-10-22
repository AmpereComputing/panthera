//
// Copyright(c) 2024 Ampere Computing. All Rights Reserved.
// SPDX-License-Identifier: BSD-3-Clause
//

#pragma once

#include <exception>
#include <iostream>

namespace ampere::util {

inline void print_exception(std::ostream& os, const std::exception& e, unsigned level = 0)
{
    os << std::string(level * 2U, ' ');
    if (!level)
        os << "Error: ";

    os << e.what() << '\n';
    try {
        std::rethrow_if_nested(e);
    } catch (const std::exception& nested) {
        print_exception(os, nested, level + 1);
    } catch (...) {
    }
}

} // namespace ampere::util

/*
 * vim: sw=4:ts=4:et:si:cindent:cino=\:0,(0,g0,t0:
 */
