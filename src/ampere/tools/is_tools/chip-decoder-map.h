//
// Copyright (c) 2022 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//

#pragma once
#include <map>
#include <string>
#include <vector>
#include <stdexcept>
#include <fmt/format.h>

namespace ampere {
namespace is_tools {

inline static std::vector<std::string> get_decoders(std::string chip)
{
    static const std::map<std::string, std::vector<std::string>> chips = {
        {"furiosa", {"furiosa"}},
        {"ripley", {"ripley"}},
        {"trinity", {"trinity"}},
        {"connor", {"connor"}},
        {"connor_ai", {"connor_ai"}},
        {"max", {"connor_ai"}} // always the highest ISA
    };

    auto idecoders = chips.find(chip);
    if (idecoders == chips.end()) {
        throw std::runtime_error(fmt::format("Chip not found: {}", chip));
    }
    return idecoders->second;
}

} // namespace is_tools
} // namespace ampere
