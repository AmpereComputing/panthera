//
// Copyright (c) 2022 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//
#include "ampere/archx/page_size.h"

#include <fmt/format.h>
#include <stdexcept>
#include "ampere/util/enum.h"

namespace ampere::archx {

namespace {

PageSize string_to_page_size(std::string page_size)
{
    std::transform(
        page_size.begin(), page_size.end(), page_size.begin(), [](unsigned char c) { return std::toupper(c); });

    for (auto ps : util::each_enum<PageSize::Size>()) {
        if (util::enum_to_string(ps).substr(3) == page_size)
            return PageSize(ps);
    }
    throw std::runtime_error{fmt::format("unrecognized page size \"{}\"", page_size)};
}

} // namespace

void from_json(const json& j, PageSize& ps)
{
    if (j.is_string())
        ps = string_to_page_size(j.get<std::string>());
    else
        throw std::runtime_error{"bad page size format"};
}

} // namespace ampere::archx
