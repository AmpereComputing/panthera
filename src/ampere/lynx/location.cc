/*
 * Copyright (c) 2024 Ampere Computing. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "location.h"

#include <fmt/ostream.h>

namespace ampere::lynx {

std::ostream& operator<<(std::ostream& os, const Location& loc)
{
    fmt::print(os, "{}: x{}y{}", loc.die_id, loc.x, loc.y);

    if (loc.port != -1)
        fmt::print(os, "p{}", loc.port);
    if (loc.didx != -1)
        fmt::print(os, "d{}", loc.didx);

    return os;
}

} // namespace ampere::lynx
