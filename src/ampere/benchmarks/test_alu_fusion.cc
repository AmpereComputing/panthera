//
// Copyright (c) 2019 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//

#include <array>
#include <boost/preprocessor/repeat.hpp>
#include "ampere/benchmarks/benchmark.h"
#include "ampere/benchmarks/vector_tmpl.h"

extern "C" {
void mov_movk_fusion();
void mov_movk_movk_fusion();
}

namespace ampere::benchmarks {

using namespace ::testing;

class ALUFusion : public Benchmark
{};

TEST_F(ALUFusion, MovMovkFusion)
{
    mov_movk_fusion();
}

TEST_F(ALUFusion, MovMovkMovkFusion)
{
    mov_movk_movk_fusion();
}

} // namespace ampere::benchmarks
