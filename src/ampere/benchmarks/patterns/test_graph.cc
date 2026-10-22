//
// Copyright (c) 2024 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//
#include <gtest/gtest.h>
#include <cstdint>
#include <cstring>
#include <deque>
#include <type_traits>
#include <fmt/core.h>

#include "ampere/benchmarks/common/constants.h"
#include "ampere/benchmarks/micro.h"

namespace ampere::benchmarks {

template<size_t MAX_RADIX, bool INDEXED>
struct Node
{
    static constexpr bool indexed = INDEXED;
    static constexpr size_t radix = MAX_RADIX;
    using node_t = std::conditional_t<INDEXED, size_t, Node *>;
    size_t id = 0;
    size_t size = 0;
    std::array<node_t, MAX_RADIX> neighbors;
    void add(node_t neighbor) { neighbors[size++] = neighbor; }
};

template<typename T>
struct GraphBenchmark : public BenchmarkWithSetup, public ::testing::WithParamInterface<std::tuple<size_t, size_t>>
{
    size_t size;
    size_t radix;

    T *nodes;
    size_t *seen;
    GraphBenchmark()
    {
        std::tie(size, radix) = GetParam();
        nodes = new T[size];
        seen = new size_t[size];
    }
    ~GraphBenchmark() override { delete[] nodes; }
    void SetUp() override
    {
        reset();
        for (size_t i = 0, j = 0; i < size; j += i) {
            auto &node = nodes[i];
            node.id = i;

            size_t neighbors = j % radix;
            for (size_t k = 0; k < neighbors; ++k) {
                if (size_t m = (j % size); m != (i + 1)) {
                    if constexpr (T::indexed) {
                        node.add(m);
                    } else {
                        node.add(&nodes[m]);
                    }
                }
            }

            ++i;
            if (i < size) {
                if constexpr (T::indexed) {
                    node.add(i);
                } else {
                    node.add(&nodes[i]);
                }
            }
        }
    }
    void TearDown() override {}

    inline std::pair<size_t, T *> get_neighbor(T *node, size_t index) const
    {
        size_t neighbor_id;
        T *neighbor;
        if constexpr (T::indexed) {
            neighbor_id = node->neighbors[index];
            neighbor = &nodes[neighbor_id];
        } else {
            neighbor = node->neighbors[index];
            neighbor_id = neighbor->id;
        }
        return {neighbor_id, neighbor};
    }

    size_t depth_first_search_recursive(T *node)
    {
        size_t sum = 0;
        for (size_t i = 0; i < node->size; ++i) {
            auto [neighbor_id, neighbor] = get_neighbor(node, i);
            if (seen[neighbor_id] > 0)
                continue;

            seen[neighbor_id] = 1;
            sum += depth_first_search_recursive(neighbor);
        }

        return 1;
    }

    size_t depth_first_search_iterative(T *node)
    {
        size_t sum = 0;
        std::deque<std::pair<T *, size_t>> stack{std::make_pair(node, 0)};

        while (!stack.empty()) {
            auto &[n, i] = stack.back();
            if (i < n->size) {
                auto [neighbor_id, neighbor] = get_neighbor(n, i);
                if (seen[neighbor_id] == 0) {
                    seen[neighbor_id] = 1;
                    stack.emplace_back(neighbor, 0);
                }
                ++i;
            } else {
                stack.pop_back();
                ++sum;
            }
        }

        return sum;
    }

    size_t depth_first_search(T *node)
    {
        if (size > BYTES_64K)
            return depth_first_search_iterative(node);
        return depth_first_search_recursive(node);
    }

    std::pair<bool, size_t> breadth_first_search_step(T *node, size_t offset = 0, size_t level = 1)
    {
        bool remaining = false;
        size_t sum = 0;
        size_t place = level * size + node->id;
        for (size_t i = 0; i < node->size; ++i) {
            auto [neighbor_id, neighbor] = get_neighbor(node, i);
            if (offset > 0 && seen[neighbor_id] == place) {
                auto [r, s] = breadth_first_search_step(neighbor, offset - 1, level + 1);
                remaining |= r;
                sum += s;
            } else if (!seen[neighbor_id]) {
                remaining = true;
                seen[neighbor_id] = place;
            }
        }
        return {remaining, sum + offset};
    }

    size_t breadth_first_search_recursive(T *node)
    {
        size_t sum = 0;
        bool remaining = true;
        seen[node->id] = 1;
        for (size_t depth = 0; remaining; ++depth) {
            auto [r, s] = breadth_first_search_step(node, depth);
            remaining = r;
            sum += s;
        }
        return sum;
    }

    size_t breadth_first_search_iterative(T *node)
    {
        size_t sum = 0;
        std::deque<T *> frontier{node};
        for (size_t depth = 1; frontier.empty(); ++depth) {
            std::deque<T *> next_frontier;
            for (auto *n : frontier) {
                for (size_t i = 0; i < n->size; ++i) {
                    auto [neighbor_id, neighbor] = get_neighbor(n, i);
                    if (!seen[neighbor_id]) {
                        seen[neighbor_id] = depth;
                        next_frontier.emplace_back(neighbor);
                    } else {
                        sum += depth;
                    }
                }
            }
            frontier = std::move(next_frontier);
        }

        return sum;
    }

    size_t breadth_first_search(T *node)
    {
        if (size > 32)
            return breadth_first_search_iterative(node);
        return breadth_first_search_recursive(node);
    }

    void reset()
    {
        for (size_t i = 0; i < size; ++i)
            seen[i] = 0;
    }
};

using IndexedGraphBenchmark = GraphBenchmark<Node<32, true>>;

TEST_P(IndexedGraphBenchmark, DepthFirstSearch)
{
    volatile size_t x = 0;
    run_benchmark([this, &x](size_t iter) {
        for (size_t i = 0; i < iter; ++i) {
            x += depth_first_search(&nodes[i % size]);
            reset();
        }
    });
}

TEST_P(IndexedGraphBenchmark, BreadthFirstSearch)
{
    volatile size_t x = 0;
    run_benchmark([this, &x](size_t iter) {
        for (size_t i = 0; i < iter; ++i) {
            x += breadth_first_search(&nodes[i % size]);
            reset();
        }
    });
}

template<typename T>
std::string param_names(const testing::TestParamInfo<typename T::ParamType> &info)
{
    return fmt::format("nodes{}Xradix{}", std::get<0>(info.param), std::get<1>(info.param));
}

INSTANTIATE_TEST_SUITE_P(IndexedGraphBenchmark,
                         IndexedGraphBenchmark,
                         ::testing::Combine(::testing::Values(8, 32, 64, 128, 256, 1024, BYTES_64K, BYTES_1M, BYTES_4M),
                                            ::testing::Values(2, 4, 8, 16, 32)),
                         param_names<IndexedGraphBenchmark>);

using PointerGraphBenchmark = GraphBenchmark<Node<32, false>>;

TEST_P(PointerGraphBenchmark, DepthFirstSearch)
{
    volatile size_t x = 0;
    run_benchmark([this, &x](size_t iter) {
        for (size_t i = 0; i < iter; ++i) {
            x += depth_first_search(&nodes[i % size]);
            reset();
        }
    });
}

TEST_P(PointerGraphBenchmark, BreadthFirstSearch)
{
    volatile size_t x = 0;
    run_benchmark([this, &x](size_t iter) {
        for (size_t i = 0; i < iter; ++i) {
            x += breadth_first_search(&nodes[i % size]);
            reset();
        }
    });
}

INSTANTIATE_TEST_SUITE_P(PointerGraphBenchmark,
                         PointerGraphBenchmark,
                         ::testing::Combine(::testing::Values(8, 32, 64, 128, 256, 1024, BYTES_64K, BYTES_1M, BYTES_4M),
                                            ::testing::Values(2, 4, 8, 16, 32)),
                         param_names<PointerGraphBenchmark>);

GTEST_ALLOW_UNINSTANTIATED_PARAMETERIZED_TEST(Microbenchmark);

} // namespace ampere::benchmarks
