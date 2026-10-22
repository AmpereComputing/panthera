//
// Copyright (c) 2022 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//

#include "ampere/archx/register.h"

#include <sstream>
#include <fmt/format.h>

namespace ampere::archx {

std::string regstr(LReg r)
{
    std::ostringstream o;
    o << r;
    return o.str();
}

std::string nzcv_str(std::uint32_t nzcv)
{
    const std::uint32_t nibble = nzcv >> 28;
    char n = (nibble & 8) ? 'n' : '-';
    char z = (nibble & 4) ? 'z' : '-';
    char c = (nibble & 2) ? 'c' : '-';
    char v = (nibble & 1) ? 'v' : '-';
    return fmt::format("{}{}{}{}", n, z, c, v);
}

} // namespace ampere::archx
