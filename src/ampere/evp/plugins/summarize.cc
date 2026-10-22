//
// Copyright(c) 2022 Ampere Computing LLC
// SPDX-License-Identifier: BSD-3-Clause
//
#include <filesystem>
#include <fmt/ostream.h>
#include <fstream>
#include <iostream>
#include <set>
#include <string>
#include <vector>

#include "ampere/util/compressed_fstream.h"

#include "ampere/evp/plugin.h"

namespace ampere::evp {

namespace fs = std::filesystem;

AMPERE_EVP_PLUGIN(Summarize, "summarize", "Output metric summary or timeseries as CSV")
{
public:
    Summarize(ProcessorIfc & proc_ifc, Args & args);
    static void help(int argc, const char** argv);
    void report(MetricTableTimeSeries & ts) override;

private:
    std::string ts_fname;
    std::string summary_fname;

    using MetricNames = std::set<std::string, NatComp>;

    void check_output_directory(const std::string& fname) const;

    void print_ts(std::ostream & out, const MetricTableTimeSeries& ts) const;
    void print_summary(std::ostream & out, const MetricTableTimeSeries& ts) const;
    void print_human_summary(std::ostream & out, const MetricTableTimeSeries& ts) const;
    void print_header(std::ostream & out, const MetricTableTimeSeries& ts) const;
    void print_row(std::ostream & out, const MetricTableTimeSeries& ts, size_t row, uint64_t start_time) const;
    void print_value(std::ostream & out, const MetricValue& value) const;

    uint64_t get_start_time(const MetricTableTimeSeries& ts, size_t row) const;
};

// Forwards
MetricTableTimeSeries compute_deltas(const MetricTableTimeSeries& ts_in);
MetricValue subtract(const MetricValue& a, const MetricValue& b);

Summarize::Summarize(ProcessorIfc& proc_ifc, Args& args) : Plugin{proc_ifc}
{
    while (args) {
        if (args.pop("--timeseries", ts_fname))
            check_output_directory(ts_fname);
        else if (args.pop("--summary", summary_fname))
            check_output_directory(summary_fname);
        else
            break;
    }

    args.done();
}

void Summarize::help(int argc, const char** argv)
{
    print_help(argv[0], "summarize", "[--timeseries <name>] [--summary <name>]", R"(Arguments:

    --summary <name>       Summary metrics CSV
    --timeseries <name>    Timeseries metrics CSV (per interval)
    --help, -h             This help message.
)");
}

void Summarize::check_output_directory(const std::string& fname) const
{
    auto dir = fs::path(fname).remove_filename();

    if (dir.empty())
        dir = fs::path(".");

    if (!fs::exists(dir))
        fs::create_directories(dir);
}

std::ostream& operator<<(std::ostream& out, const MetricValue& value)
{
    if (std::holds_alternative<int64_t>(value))
        out << std::get<int64_t>(value);
    else if (std::holds_alternative<uint64_t>(value))
        out << std::get<uint64_t>(value);
    else if (std::holds_alternative<double_t>(value))
        out << std::get<double_t>(value);
    else if (std::holds_alternative<std::string>(value))
        out << std::get<std::string>(value);
    else if (std::holds_alternative<NAType>(value))
        out << "NA";

    return out;
}

/** Generate up to two kinds of reports: a timeseries CSV, and a summary CSV.
 *
 *  The timeseries has a metric header and a row for each time point. The summary
 *  has the final metric values (last row of the time series), and produces a CSV
 *  with a metric header and single row of data.
 */
void Summarize::report(MetricTableTimeSeries& ts)
{
    if (ts.rows() == 0)
        return;

    if (!ts_fname.empty()) {
        auto out = util::open_compressed_ostream(ts_fname.c_str());
        if (!out)
            throw PluginError{fmt::format("could not create timeseries file \"{}\"\n", summary_fname)};
        print_ts(*out, ts);
    }

    if (summary_fname.empty()) {
        print_human_summary(std::cout, ts);
    } else {
        auto out = util::open_compressed_ostream(summary_fname.c_str());
        if (!out)
            throw PluginError{fmt::format("could not create summary file \"{}\"\n", summary_fname)};
        print_summary(*out, ts);
    }
}

void Summarize::print_ts(std::ostream& out, const MetricTableTimeSeries& ts) const
{
    auto delta_ts = compute_deltas(ts);

    print_header(out, delta_ts);

    for (size_t row = 0; row < ts.rows(); ++row)
        print_row(out, delta_ts, row, get_start_time(delta_ts, row));
}

void Summarize::print_summary(std::ostream& out, const MetricTableTimeSeries& ts) const
{
    print_header(out, ts);
    print_row(out, ts, ts.rows() - 1, get_first_event_time());
}

void Summarize::print_header(std::ostream& out, const MetricTableTimeSeries& ts) const
{
    out << "start_time,stop_time";

    for (auto& name : ts.columns())
        out << ',' << name;

    out << std::endl;
}

void Summarize::print_row(std::ostream& out, const MetricTableTimeSeries& ts, size_t row, uint64_t start_time) const
{
    uint64_t stop_time = ts.times[row];

    out << start_time << ',' << stop_time;

    for (auto& value : ts.row(row))
        out << ',' << value;

    out << std::endl;
}

void Summarize::print_human_summary(std::ostream& out, const MetricTableTimeSeries& ts) const
{
    size_t longest_name{0};

    for (auto& name : ts.columns())
        longest_name = std::max(longest_name, name.length());

    auto r = ts.rows() - 1;

    size_t c = 0;

    for (auto& name : ts.columns()) {
        fmt::print(out, "{:<{}} {}\n", name, longest_name, ts.at(r, c));
        ++c;
    }
}

uint64_t Summarize::get_start_time(const MetricTableTimeSeries& ts, size_t row) const
{
    return row == 0 ? get_first_event_time() : ts.times[row - 1] + 1;
}

MetricTableTimeSeries compute_deltas(const MetricTableTimeSeries& ts_in)
{
    MetricTableTimeSeries ts_out;

    if (ts_in.rows() == 1) {
        ts_out = ts_in;
    } else {
        ts_out.times = ts_in.times;

        for (auto& [col_name, col] : ts_in) {
            MetricColumn new_col{col_name, col};

            for (size_t r = new_col.size() - 1; r > 0; --r) {
                auto& v2 = new_col[r];
                auto& v1 = new_col[r - 1];

                new_col[r] = subtract(v2, v1);
            }

            ts_out.add_col(new_col);
        }
    }

    return ts_out;
}

MetricValue subtract(const MetricValue& a, const MetricValue& b)
{
    if (std::holds_alternative<int64_t>(a)) {
        if (std::holds_alternative<int64_t>(b)) {
            return std::get<int64_t>(a) - std::get<int64_t>(b);
        } else if (std::holds_alternative<uint64_t>(b)) {
            return std::get<int64_t>(a) - std::get<uint64_t>(b);
        } else if (std::holds_alternative<double>(b)) {
            return std::get<int64_t>(a) - std::get<double>(b);
        }
    } else if (std::holds_alternative<uint64_t>(a)) {
        if (std::holds_alternative<int64_t>(b)) {
            return std::get<uint64_t>(a) - std::get<int64_t>(b);
        } else if (std::holds_alternative<uint64_t>(b)) {
            return std::get<uint64_t>(a) - std::get<uint64_t>(b);
        } else if (std::holds_alternative<double>(b)) {
            return std::get<uint64_t>(a) - std::get<double>(b);
        }
    } else if (std::holds_alternative<double>(a)) {
        if (std::holds_alternative<int64_t>(b)) {
            return std::get<double>(a) - std::get<int64_t>(b);
        } else if (std::holds_alternative<uint64_t>(b)) {
            return std::get<double>(a) - std::get<uint64_t>(b);
        } else if (std::holds_alternative<double>(b)) {
            return std::get<double>(a) - std::get<double>(b);
        }
    }

    return a;
}

} // namespace ampere::evp
