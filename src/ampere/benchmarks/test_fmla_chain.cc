//
// Copyright (c) 2023 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//

#include "ampere/benchmarks/micro.h"

extern "C" {
int fmla_chain_par_2(int iterations);
int fmla_chain_par_4(int iterations);
int fmla_chain_par_6(int iterations);
int fmla_chain_par_8(int iterations);
int fmla_chain(int iterations);
}

namespace ampere::benchmarks {

using namespace ::testing;

INSTANTIATE_TEST_SUITE_P(FMLABandwidth,
                         Microbenchmark,
                         ::testing::Values(std::function<int(int)>(fmla_chain_par_2),
                                           std::function<int(int)>(fmla_chain_par_4),
                                           std::function<int(int)>(fmla_chain_par_6),
                                           std::function<int(int)>(fmla_chain_par_8),
                                           std::function<int(int)>(fmla_chain)));

} // namespace ampere::benchmarks
