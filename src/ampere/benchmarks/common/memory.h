//
// Copyright (c) 2024-2025 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//
#ifndef BENCHMARK_MEMORY_H_
#define BENCHMARK_MEMORY_H_

#include <cstddef>
#include <cstdlib>
#include "ampere/benchmarks/common/constants.h"

namespace ampere::benchmarks {

template<typename T>
inline void populate_physical_pages(T *data, size_t size, size_t page_size = BYTES_4K)
{
    // Commit physical backing pages
    // (for avoiding exceptions during measurement on kernel paging policies)
    size_t elements_per_page = page_size / sizeof(T);
    for (size_t i = 0; i < size; i += elements_per_page)
        data[i] = 0;
}

void thrash_cache(size_t size)
{
    volatile char *c = new char[size];
    for (size_t i = 0; i < size; ++i)
        c[i] = i;
    delete[] c;
}

} // namespace ampere::benchmarks

#endif
