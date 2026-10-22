//
// Copyright (c) 2024-2025 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//
#ifndef BENCHMARK_CONSTANTS_H_
#define BENCHMARK_CONSTANTS_H_

#define CACHELINE_SIZE 64
#define INSTRUCTION_SIZE 4

#define BYTES_1K 1024
#define BYTES_4K (4 * BYTES_1K)
#define BYTES_16K (16 * BYTES_1K)
#define BYTES_64K (64 * BYTES_1K)

#define BYTES_1M (BYTES_1K * BYTES_1K)
#define BYTES_2M (2 * BYTES_1M)
#define BYTES_4M (4 * BYTES_1M)
#define BYTES_8M (8 * BYTES_1M)
#define BYTES_16M (16 * BYTES_1M)

#endif
