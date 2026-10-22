//
// Copyright(c) 2022 Ampere Computing. All Rights Reserved.
// SPDX-License-Identifier: BSD-3-Clause
//
#include <deque>
#include <regex>
#include <set>
#include <sstream>
#include <string>
#include <vector>

#include "ampere/evp/plugin.h"
#include "ampere/evp/utils.h"

namespace ampere::evp {

AMPERE_EVP_PLUGIN(Sum, "sum", "Sum metrics matching regex into new metric")
{
public:
    Sum(ProcessorIfc & proc_ifc, Args & args);

    static void help(int argc, const char** argv);

    void report(MetricTableTimeSeries & ts) override;

private:
    std::vector<std::pair<std::regex, std::string>> sums;

    struct SumValue
    {
        int64_t isum{0};
        uint64_t usum{0};
        double fsum{0.0};
        bool has_double{false};
        bool is_null{false};
    };

    void accumulate(std::vector<SumValue> & sum, MetricTableTimeSeries & ts, size_t col_idx);
};

Sum::Sum(ProcessorIfc& proc_ifc, Args& args) : Plugin{proc_ifc}
{
    while (args) {
        std::string arg;

        if (args.pop(arg)) {
            size_t pos = arg.find('=');

            if (pos == std::string::npos)
                throw std::runtime_error{"argument to sum must be <regex>=<new name>"};

            auto regex_str = glob_to_pattern(arg.substr(0, pos));
            auto new_name = arg.substr(pos + 1);

            sums.emplace_back(regex(regex_str), new_name);
        } else {
            break;
        }
    }

    args.done();
}

void Sum::help(int argc, const char** argv)
{
    print_help(argv[0], "sum", "<pattern>=<new name>", R"(Arguments:

    <pattern>=<new name>    Sum values matching regex pattern into new name
    --help, -h              This help message.
)");
}

void Sum::report(MetricTableTimeSeries& ts)
{
    std::unordered_map<std::string, std::set<size_t>> sums_by_name;

    size_t col_idx = 0;

    for (auto& name : ts.columns()) {
        for (auto& [re, replace] : sums) {
            if (std::regex_match(name, re)) {
                auto new_name = std::regex_replace(name, re, replace);
                sums_by_name[new_name].insert(col_idx);
            }
        }

        ++col_idx;
    }

    std::deque<MetricColumn> new_columns;

    for (auto& [name, col_set] : sums_by_name) {
        std::vector<SumValue> sum(ts.rows());

        for (auto col_idx : col_set)
            accumulate(sum, ts, col_idx);

        new_columns.emplace_back(name);
        auto& new_col = new_columns.back();

        for (auto& v : sum) {
            if (v.is_null)
                new_col.push_back(NA);
            else if (v.has_double)
                new_col.push_back(v.fsum);
            else if (v.isum < 0)
                new_col.push_back(v.isum);
            else
                new_col.push_back(v.usum);
        }
    }

    while (!new_columns.empty()) {
        ts.add_col(new_columns.front());
        new_columns.pop_front();
    }
}

void Sum::accumulate(std::vector<SumValue>& sum, MetricTableTimeSeries& ts, size_t col_idx)
{
    auto& col = ts.col(col_idx);

    for (size_t i = 0; i < sum.size(); ++i) {
        auto& the_sum = sum[i];
        auto& value = col[i];

        if (std::holds_alternative<int64_t>(value)) {
            auto ival = std::get<int64_t>(value);
            the_sum.isum += ival;
            the_sum.usum += ival;
            the_sum.fsum += ival;
        } else if (std::holds_alternative<uint64_t>(value)) {
            auto uval = std::get<uint64_t>(value);
            the_sum.isum += uval;
            the_sum.usum += uval;
            the_sum.fsum += uval;
        } else if (std::holds_alternative<double>(value)) {
            the_sum.has_double = true;
            the_sum.fsum += std::get<double>(value);
        } else if (std::holds_alternative<NAType>(value)) {
            the_sum.is_null = true;
        } else {
            throw std::runtime_error{
                fmt::format("tried to sum a metric ({}) of string type", std::get<std::string>(value))};
        }
    }
}

} // namespace ampere::evp
