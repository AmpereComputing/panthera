//
// Copyright (c) 2019 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//

#include <fmt/format.h>
#include "ampere/benchmarks/benchmark.h"
#include "ampere/tracing_tools/tracing_hints.h"

namespace ampere::benchmarks {

DataCollector Benchmark::collector;

void DataCollector::dump(std::string filename)
{
    std::set<std::string> columns;
    std::ofstream output(filename);

    for (auto& element : data) {
        for (auto& it : element) {
            columns.insert(it.first);
        }
    }

    bool first{true};
    for (const auto& column : columns) {
        if (first)
            first = false;
        else
            output << ",";
        output << column;
    }
    output << std::endl;

    for (auto& element : data) {
        bool first{true};
        for (const auto& column : columns) {
            if (first)
                first = false;
            else
                output << ",";
            if (element.contains(column))
                output << element.at(column);
        }
        output << std::endl;
    }
}

void Benchmark::SetUp()
{
    start = std::chrono::system_clock::now();
    hint_start_tracing();
}

void Benchmark::TearDown()
{
    hint_stop_tracing();
    collect_timing();
}

void Benchmark::collect_timing()
{
    auto end = std::chrono::system_clock::now();
    const auto* test_info = ::testing::UnitTest::GetInstance()->current_test_info();

    std::chrono::duration<double> duration = end - start;

    data["TestCaseName"] = fmt::format("\"{}\"", test_info->test_case_name());
    data["TestName"] = fmt::format("\"{}\"", test_info->name());

    if (test_info->type_param())
        data["Param"] = fmt::format("\"{}\"", test_info->type_param());
    else if (test_info->value_param())
        data["Param"] = fmt::format("\"{}\"", test_info->value_param());

    data["Duration"] = std::to_string(duration.count());
    collector.push(data);
}

void BenchmarkWithOfflineWarmup::SetUp()
{
    start_timing();
    hint_start_tracing();
    this->OfflineWarmup();
    hint_start_online_warmup();
    hint_start_measuring();
}

void BenchmarkWithOfflineWarmup::TearDown()
{
    hint_stop_tracing();
    this->Cooldown();
    collect_timing();
}

void BenchmarkWithOfflineWarmup::OfflineWarmup() {}

void BenchmarkWithOfflineWarmup::Cooldown() {}

void BenchmarkWithWarmup::SetUp()
{
    start = std::chrono::system_clock::now();
    hint_start_tracing();
    this->OfflineWarmup();
    hint_start_online_warmup();
    this->OnlineWarmup();
    hint_start_measuring();
}

void BenchmarkWithWarmup::TearDown()
{
    hint_stop_tracing();
    this->Cooldown();
    collect_timing();
}

void BenchmarkWithWarmup::OfflineWarmup() {}

void BenchmarkWithWarmup::OnlineWarmup() {}

void BenchmarkWithWarmup::Cooldown() {}

} // namespace ampere::benchmarks

