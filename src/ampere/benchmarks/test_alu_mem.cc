//
// Copyright (c) 2019 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//

#include <array>
#include <boost/preprocessor/repeat.hpp>
#include "ampere/benchmarks/benchmark.h"
#include "ampere/benchmarks/vector_tmpl.h"

extern "C" {
void independent_loads();
void independent_stores();
void independent_adds_loads();
void load_store();
void load_store_fsu();
void independent_fadds_stores();
}

namespace ampere::benchmarks {

using namespace ::testing;

class ALUMem : public Benchmark
{};

TEST_F(ALUMem, IndependentLoads)
{
    independent_loads();
}

TEST_F(ALUMem, IndependentStores)
{
    independent_stores();
}

TEST_F(ALUMem, IndependentAddsLoads)
{
    independent_adds_loads();
}

TEST_F(ALUMem, LoadStore)
{
    load_store();
}

TEST_F(ALUMem, LoadStoreFSU)
{
    load_store_fsu();
}

TEST_F(ALUMem, IndependentFaddsStores)
{
    independent_fadds_stores();
}

} // namespace ampere::benchmarks
