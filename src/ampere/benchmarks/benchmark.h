//
// Copyright (c) 2019 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//

#ifndef AMPERE_BENCHMARK_BENCHMARK_H
#define AMPERE_BENCHMARK_BENCHMARK_H

#include <chrono>
#include <deque>
#include <fstream>
#include <functional>
#include <map>

#include <gtest/gtest.h>

namespace ampere::benchmarks {

class DataCollector
{
public:
    void dump(std::string filename);
    void push(const std::map<std::string, std::string>& d) { data.push_back(d); }

private:
    std::deque<std::map<std::string, std::string>> data;
};

class Benchmark : public ::testing::Test
{
public:
    static DataCollector collector;

protected:
    void SetUp() override;
    void TearDown() override;

    void collect_timing();
    void start_timing() { start = std::chrono::system_clock::now(); }
    void stop_timing() { collect_timing(); }

    std::chrono::system_clock::time_point start;
    std::map<std::string, std::string> data;
};

class BenchmarkWithOfflineWarmup : public Benchmark
{
protected:
    virtual void OfflineWarmup();
    virtual void Cooldown();
    void SetUp() override;
    void TearDown() override;
};

class BenchmarkWithWarmup : public Benchmark
{
protected:
    virtual void OfflineWarmup();
    virtual void OnlineWarmup();
    virtual void Cooldown();
    void SetUp() override;
    void TearDown() override;
};

} // namespace ampere::benchmarks

#endif // AMPERE_BENCHMARK_BENCHMARK_H
