//
// Copyright (c) 2018-2020 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//

#include "ampere/uarch_common/cache.h"

namespace ampere::uarch_common {

CacheConfiguration::CacheConfiguration(const sc_core::sc_module_name& name) : Module(name)
{
    // Set the member variables if the corresponding parameter was set
    size = size_param.value_or(1024);
    num_sets = sets_param.value_or(1);
    num_ways = *ways_param;
    line_size = *line_size_param;

    if (size_param && sets_param)
        AMPERE_MODULE_THROW("can't configure both the size and sets for the cache");
    else if (!size_param && sets_param)
        size = num_sets * num_ways * line_size;
    else if (!sets_param)
        num_sets = size / (line_size * num_ways);

    if (size < line_size * num_ways)
        AMPERE_MODULE_THROW("size of cache is smaller than the line_size * num_ways");

    line_bits = compute_num_address_bits(line_size);
    set_bits = compute_num_address_bits(num_sets);
}

unsigned compute_num_address_bits(size_t value)
{
    if (value == 0)
        throw std::runtime_error("Can't take log2 of 0");
    if (!util::is_power_of_two(value))
        throw std::runtime_error("Value is not a power of 2");
    return util::log2(value);
}

} // namespace ampere::uarch_common

