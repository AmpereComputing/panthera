//
// Copyright (c) 2019 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//
#include "ampere/benchmarks/benchmark.h"

int main(int argc, char **argv)
{
    testing::InitGoogleTest(&argc, argv);
    auto status = RUN_ALL_TESTS();
    ampere::benchmarks::Benchmark::collector.dump("benchmark.csv");
    return status;
}
