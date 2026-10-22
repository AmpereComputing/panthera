//
// Copyright (c) 2019 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//

#ifndef AMPERE_BENCHMARK_VECTOR_TMPL_H
#define AMPERE_BENCHMARK_VECTOR_TMPL_H

#include <array>

const unsigned ALIGNMENT = 128;

template<typename T, size_t VL>
class Vector
{
public:
    explicit Vector(T value) { data.fill(value); }

    alignas(ALIGNMENT) std::array<T, VL> data;
};

template<typename T, size_t VL>
void vector_add(Vector<T, VL>& a, T t)
{
    for (size_t i = 0; i < a.data.size(); i++)
        a.data[i] += t;
}

template<typename T, size_t VL>
void vector_scale(Vector<T, VL>& a, T scale)
{
    for (size_t i = 0; i < a.data.size(); i++)
        a.data[i] *= scale;
}

template<typename T, size_t VL>
void vector_mac(Vector<T, VL>& a, Vector<T, VL>& b, T t)
{
    for (size_t i = 0; i < a.data.size(); i++)
        a.data[i] += t * b.data[i];
}

template<typename T, size_t VL>
T vector_sum(Vector<T, VL>& a)
{
    T sum = 0;
    for (size_t i = 0; i < a.data.size(); i++)
        sum += a.data[i];
    return sum;
}

template<typename T, size_t VL>
T vector_sum_parallel(Vector<T, VL>& a)
{
    std::array<T, 8> sum{0};
    for (size_t i = 0; i < a.data.size(); i += 8) {
        sum[0] = a.data[i] + 1;
        sum[1] = a.data[i + 1] + 2;
        sum[2] = a.data[i + 2] + 3;
        sum[3] = a.data[i + 3] + 4;
        sum[4] = a.data[i + 4] + 5;
        sum[5] = a.data[i + 5] + 6;
        sum[6] = a.data[i + 6] + 7;
        sum[7] = a.data[i + 7] + 8;
    }
    T result{0};
    for (size_t i = 0; i < 8; i++) {
        result += sum[i];
    }
    return result;
}

#endif // AMPERE_BENCHMARK_VECTOR_TMPL_H
