//
// Copyright (c) 2018-2024 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//

#include "ampere/archx/driver.h"
#include "ampere/archx/driver_bs.h"
#include "ampere/archx/driver_is.h"

namespace ampere::archx {

std::unique_ptr<Driver> makeDriver(std::string filename,
                                   uint64_t skip_instructions,
                                   bool speculative_memory_image,
                                   bool use_wrong_path_emulation,
                                   std::uint64_t validate_emulation,
                                   std::string mem_image_path)
{
    return std::make_unique<DriverIS>(filename,
                                      skip_instructions,
                                      speculative_memory_image,
                                      use_wrong_path_emulation,
                                      validate_emulation,
                                      mem_image_path);
}

std::unique_ptr<Driver> makeDriver(std::string filename, uint64_t skip_instructions)
{
    return makeDriver(filename,
                      skip_instructions,
                      false /* speculative_memory_image */,
                      false /* use_wrong_path_emulation */,
                      0 /* validate_emulation */,
                      "" /* mem_image_path */);
}

std::unique_ptr<Driver> makeBSDriver(std::string filename)
{
    return std::make_unique<DriverBS>(filename);
}

} // namespace ampere::archx

/*
 * vim: sw=4:ts=4:et:si:cindent:cino=\:0,(0,g0,t0:
 */
