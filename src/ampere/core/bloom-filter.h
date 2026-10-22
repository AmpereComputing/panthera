/*
 * Copyright (c) 2023-2025 Ampere Computing. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#pragma once
#include <cstdint>
#include <vector>

using HashFunction = std::uint64_t (*)(std::uint64_t);

class BloomFilter
{
    size_t bits;
    std::vector<bool> array;
    std::vector<HashFunction> hash_functions;

public:
    BloomFilter(size_t bits) : bits(bits), array(bits, false) {}
    void add_hash_function(HashFunction hf) { hash_functions.push_back(hf); }
    void reset()
    {
        for (size_t i = 0; i < bits; i++) {
            array[i] = false;
        }
    }
    void insert(std::uint64_t x)
    {
        for (auto& hf : hash_functions) {
            auto spot = hf(x);
            array[spot] = true;
        }
    }
    bool query(std::uint64_t x) const
    {
        for (auto& hf : hash_functions) {
            auto spot = hf(x);
            if (array[spot] == 0) {
                return false;
            }
        }
        return true;
    }
};
