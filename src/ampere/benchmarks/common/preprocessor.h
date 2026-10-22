//
// Copyright (c) 2024-2025 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//
#ifndef BENCHMARK_PREPROCESSOR_H_
#define BENCHMARK_PREPROCESSOR_H_

#define TOKENIZE(first, second) first##second
#define LABELIZE(first, second) TOKENIZE(first, second)

#endif
