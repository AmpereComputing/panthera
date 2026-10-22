//
// Copyright (c) 2022 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//

#include <string>

#include <fmt/format.h>
#include <fmt/ostream.h>

namespace ampere::event_stream::testing {

std::string build_es(const std::string& input)
{
    std::string prefix;
    size_t dot = input.rfind('.');

    if (dot != std::string::npos)
        prefix = input.substr(0, dot);
    else
        prefix = input;

    std::string output = prefix + ".es";

    int r = system(fmt::format(GEN_ES_BIN " {} {}", input, output).c_str());
    if (r != 0)
        throw std::runtime_error(fmt::format("Error running gen_es_test. retval = {}", r));

    return output;
}

} // namespace ampere::event_stream::testing
