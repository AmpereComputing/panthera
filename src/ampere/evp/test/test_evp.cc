//
// Copyright (c) 2022 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//
#include <cstdint>
#include <fstream>
#include <optional>
#include <sstream>
#include <cstdio>

#include <fmt/format.h>
#include <fmt/ostream.h>
#include <gmock/gmock.h>

#include "ampere/evp/bounds.h"
#include "ampere/event_stream/testing/build_event_stream.h"

#define EVP_PROGRAM BINDIR "/../evp"

#define EXPECT_PLUGIN_ERROR(output, plugin, message) \
    EXPECT_EQ(output, "Error: issue constructing plugin \"" plugin "\"\n  " message)

class EVPTest : public ::testing::Test
{
protected:
    std::string run(const std::string& cmd, int expected_status = 0)
    {
        auto [output, status] = run_with_status(cmd);
        EXPECT_EQ(status, expected_status);
        return output;
    }

    std::string run_expecting_error(const std::string& cmd) { return run(cmd, 256); }

    std::pair<std::string, int> run_with_status(const std::string& cmd)
    {
        std::string full_cmd = fmt::format("{} {} 2>&1", EVP_PROGRAM, cmd);
        FILE* cmd_out = popen(full_cmd.c_str(), "r");
        std::stringstream output;
        char buf[512];
        for (;;) {
            size_t count = fread(buf, 1, sizeof(buf), cmd_out);

            if (count > 0)
                output.write(buf, count);

            if (count < sizeof(buf))
                break;
        }
        EXPECT_EQ(ferror(cmd_out), 0);
        EXPECT_NE(feof(cmd_out), 0);
        int status = pclose(cmd_out);
        return {output.str(), status};
    }

    std::string fixture(const std::string& name) { return fmt::format("{}/{}", TEST_FIXTURE_DIRECTORY, name); }

    std::string test_file(const std::string& name, const std::string& prefix = "") const
    {
        const auto* test_info = testing::UnitTest::GetInstance()->current_test_info();
        return fmt::format("{}{}_{}_{}", prefix, test_info->test_case_name(), test_info->name(), name);
    }

    std::string read_and_remove_file(const std::string& filename)
    {
        std::ifstream inf{filename};
        std::stringstream ss;
        ss << inf.rdbuf();

        std::remove(filename.c_str());
        return ss.str();
    }

    std::string build_es(const std::string& name) { return ampere::event_stream::testing::build_es(fixture(name)); }
};

TEST_F(EVPTest, CanCountAllEvents)
{
    auto output = run(fmt::format("--es {} +count -a +summarize", build_es("basic.in")));
    EXPECT_EQ(output, "one 2\ntwo 2\n");
}

TEST_F(EVPTest, CanCountOneEvent)
{
    auto output = run(fmt::format("--es {} +count -e two +summarize", build_es("basic.in")));
    EXPECT_EQ(output, "two 2\n");
}

TEST_F(EVPTest, CanCountPatterns)
{
    auto patterns_es = build_es("patterns.in");

    auto output = run(fmt::format("--es {} +count -e 'a_0.*.foo' +summarize", patterns_es));
    auto expected = R"(a_0.b_0.foo 1
a_0.b_1.foo 1
a_0.b_2.foo 1
a_0.b_3.foo 1
)";
    EXPECT_EQ(output, expected);

    output = run(fmt::format("--es {} +count -e 'a_1.*.foo' +summarize", patterns_es));
    EXPECT_EQ(output, "a_1.b_0.foo 1\na_1.b_1.foo 2\n");

    output = run(fmt::format("--es {} +count -e '*.b_1.foo' +summarize", patterns_es));
    EXPECT_EQ(output, "a_0.b_1.foo 1\na_1.b_1.foo 2\n");

    output = run(fmt::format("--es {} +count -e '*.b_3.foo' +summarize", patterns_es));
    EXPECT_EQ(output, "a_0.b_3.foo 1\n");

    output = run(fmt::format("--es {} +count -e 'a_#.b_1.*' +summarize", patterns_es));
    EXPECT_EQ(output, "a_0.b_1.foo 1\na_1.b_1.foo 2\n");

    output = run(fmt::format("--es {} +count -e '*b_0.f*' +summarize", patterns_es));
    EXPECT_EQ(output, "a_0.b_0.foo 1\na_1.b_0.foo 1\n");

    output = run(fmt::format("--es {} +count -e 'a_0.b_?.f*' +summarize", patterns_es));
    EXPECT_EQ(output, "a_0.b_0.foo 1\na_0.b_1.foo 1\na_0.b_2.foo 1\na_0.b_3.foo 1\n");

    output = run(fmt::format("--es {} +count -e 'a_0.b_[02].f*' +summarize", patterns_es));
    EXPECT_EQ(output, "a_0.b_0.foo 1\na_0.b_2.foo 1\n");

    output = run(fmt::format("--es {} +count -e 'a_0.b_[!02].f*' +summarize", patterns_es));
    EXPECT_EQ(output, "a_0.b_1.foo 1\na_0.b_3.foo 1\n");
}

TEST_F(EVPTest, CanExcludeCountingEvents)
{
    auto output = run(fmt::format("--es {} +count -e one -e two -x one +summarize", build_es("basic.in")));
    EXPECT_EQ(output, "two 2\n");

    output = run(fmt::format("--es {} +count -e one -e two -x one -e one -x two +summarize", build_es("basic.in")));
    EXPECT_EQ(output, "one 2\n");
}

TEST_F(EVPTest, CanExcludeCountingPatterns)
{
    auto patterns_es = build_es("patterns.in");

    auto output = run(fmt::format("--es {} +count -e 'a_0.*.foo' -x 'a_0.b_[13].foo' +summarize", patterns_es));
    auto expected = R"(a_0.b_0.foo 1
a_0.b_2.foo 1
)";
    EXPECT_EQ(output, expected);

    output =
        run(fmt::format("--es {} +count -e 'a_0.*.foo' -x 'a_0.b_*.foo' -e a_0.b_[02].foo +summarize", patterns_es));
    EXPECT_EQ(output, expected);
}

TEST_F(EVPTest, CountOfNonexistantEventIsAnError)
{
    auto output = run_expecting_error(fmt::format("--es {} +count -e blah +summarize", build_es("basic.in")));
    EXPECT_PLUGIN_ERROR(output, "count", "no definition for event or data item \"blah\"\n");
}

TEST_F(EVPTest, MultipleCount)
{
    auto output =
        run(fmt::format("--es {} +count -e 'a_0.b_1.foo' +summarize +count -e 'a_1.b_0.foo'", build_es("patterns.in")));
    EXPECT_EQ(output, "a_0.b_1.foo 1\na_1.b_0.foo 1\n");
}

TEST_F(EVPTest, BoundsParseFromSpec)
{
    auto f = ampere::evp::FactorBounds::from_spec("[0:40:23]");
    EXPECT_EQ(f.minimum, 0);
    EXPECT_EQ(f.maximum, 40);
    EXPECT_EQ(f.granularity, 23);
    EXPECT_EQ(f.sequence, ampere::evp::FactorBounds::LINEAR);

    f = ampere::evp::FactorBounds::from_spec("[-13:923]");
    EXPECT_EQ(f.minimum, -13);
    EXPECT_EQ(f.maximum, 923);
    EXPECT_EQ(f.granularity, 1);
    EXPECT_EQ(f.sequence, ampere::evp::FactorBounds::LINEAR);

    f = ampere::evp::FactorBounds::from_spec("[1:]");
    EXPECT_EQ(f.minimum, 1);
    EXPECT_EQ(f.maximum, std::nullopt);
    EXPECT_EQ(f.granularity, 1);
    EXPECT_EQ(f.sequence, ampere::evp::FactorBounds::LINEAR);

    f = ampere::evp::FactorBounds::from_spec("[10::]");
    EXPECT_EQ(f.minimum, 10);
    EXPECT_EQ(f.maximum, std::nullopt);
    EXPECT_EQ(f.granularity, 1);
    EXPECT_EQ(f.sequence, ampere::evp::FactorBounds::LINEAR);

    f = ampere::evp::FactorBounds::from_spec("[:10:]");
    EXPECT_EQ(f.minimum, std::nullopt);
    EXPECT_EQ(f.maximum, 10);
    EXPECT_EQ(f.granularity, 1);
    EXPECT_EQ(f.sequence, ampere::evp::FactorBounds::LINEAR);

    f = ampere::evp::FactorBounds::from_spec("[::10]");
    EXPECT_EQ(f.minimum, std::nullopt);
    EXPECT_EQ(f.maximum, std::nullopt);
    EXPECT_EQ(f.granularity, 10);
    EXPECT_EQ(f.sequence, ampere::evp::FactorBounds::LINEAR);

    f = ampere::evp::FactorBounds::from_spec("[::2:exp]");
    EXPECT_EQ(f.minimum, std::nullopt);
    EXPECT_EQ(f.maximum, std::nullopt);
    EXPECT_EQ(f.granularity, 2);
    EXPECT_EQ(f.sequence, ampere::evp::FactorBounds::EXPONENTIAL);

    f = ampere::evp::FactorBounds::from_spec("[2:16:2:exp]");
    EXPECT_EQ(f.minimum, 2);
    EXPECT_EQ(f.maximum, 16);
    EXPECT_EQ(f.granularity, 2);
    EXPECT_EQ(f.sequence, ampere::evp::FactorBounds::EXPONENTIAL);

    f = ampere::evp::FactorBounds::from_spec("[:10::linear]");
    EXPECT_EQ(f.minimum, std::nullopt);
    EXPECT_EQ(f.maximum, 10);
    EXPECT_EQ(f.granularity, 1);
    EXPECT_EQ(f.sequence, ampere::evp::FactorBounds::LINEAR);

    f = ampere::evp::FactorBounds::from_spec("[1:10::linear]");
    EXPECT_EQ(f.minimum, 1);
    EXPECT_EQ(f.maximum, 10);
    EXPECT_EQ(f.granularity, 1);
    EXPECT_EQ(f.sequence, ampere::evp::FactorBounds::LINEAR);
}

TEST_F(EVPTest, CountByFactors)
{
    auto output = run(fmt::format("--es {} +count -e ev/a +summarize", build_es("factors.in")));
    EXPECT_EQ(output, R"(ev     10
ev/a:1 1
ev/a:2 2
ev/a:3 3
ev/a:4 4
)");
}

TEST_F(EVPTest, CountByFactorsWithMalformedBucketsRaiseError)
{
    EXPECT_NE(run_with_status(fmt::format("--es {} +count -e ev/a[] +summarize", build_es("factors.in"))).second, 0);
    EXPECT_NE(run_with_status(fmt::format("--es {} +count -e ev/a[: +summarize", build_es("factors.in"))).second, 0);
    EXPECT_NE(run_with_status(fmt::format("--es {} +count -e ev/a[:] +summarize", build_es("factors.in"))).second, 0);
}

TEST_F(EVPTest, CountByFactorsWithBucketMiniumnMaximum)
{
    auto output = run(fmt::format("--es {} +count -e ev/a[2:3] +summarize", build_es("factors.in")));
    EXPECT_EQ(output, R"(ev     10
ev/a:2 3
ev/a:3 7
)");
}

TEST_F(EVPTest, CountByFactorsWithBucketMinimumGranularity)
{
    auto output = run(fmt::format("--es {} +count -e ev/a[1::2] +summarize", build_es("factors.in")));
    EXPECT_EQ(output, R"(ev     10
ev/a:1 1
ev/a:2 5
ev/a:4 4
)");
}

TEST_F(EVPTest, CountByFactorsWithBucketMaximumGranularity)
{
    auto output = run(fmt::format("--es {} +count -e ev/a[:3:2] +summarize", build_es("factors.in")));
    EXPECT_EQ(output, R"(ev     10
ev/a:0 1
ev/a:2 5
ev/a:3 4
)");
}

TEST_F(EVPTest, CountByFactorsWithGranularityOnly)
{
    auto output = run(fmt::format("--es {} +count -e ev/a[::2] +summarize", build_es("factors.in")));
    EXPECT_EQ(output, R"(ev     10
ev/a:0 1
ev/a:2 5
ev/a:4 4
)");
}

TEST_F(EVPTest, CountByFactorsWithBucketAll)
{
    auto output = run(fmt::format("--es {} +count -e ev/a[0:4:2] +summarize", build_es("factors.in")));
    EXPECT_EQ(output, R"(ev     10
ev/a:0 1
ev/a:2 5
ev/a:4 4
)");
}

TEST_F(EVPTest, CountByFactorsWithBucketExponentialBase2)
{
    auto output = run(fmt::format("--es {} +count -e ev/a[::2:exp] +summarize", build_es("larger_factors.in")));
    EXPECT_EQ(output, R"(ev      13
ev/a:1  1
ev/a:2  4
ev/a:4  5
ev/a:8  2
ev/a:16 1
)");
}

TEST_F(EVPTest, CountByFactorsWithBucketExponentialBase10)
{
    auto output = run(fmt::format("--es {} +count -e ev/a[::10:exp] +summarize", build_es("larger_factors.in")));
    EXPECT_EQ(output, R"(ev      13
ev/a:1  10
ev/a:10 3
)");
}

TEST_F(EVPTest, CountByFactorsWithBucketExponential)
{
    auto output = run(fmt::format("--es {} +count -e ev/a[::3:exp] +summarize", build_es("larger_factors.in")));
    EXPECT_EQ(output, R"(ev     13
ev/a:1 3
ev/a:3 7
ev/a:9 3
)");
}

TEST_F(EVPTest, CountByTwoFactors)
{
    auto output = run(fmt::format("--es {} +count -e ev/a/b +summarize", build_es("factors.in")));
    EXPECT_EQ(output, R"(ev         10
ev/a:1     1
ev/a:2/b:1 2
ev/a:3     2
ev/a:3/b:2 1
ev/a:4     3
ev/a:4/b:2 1
)");
}

TEST_F(EVPTest, ScopedFactorComesBeforeGlobalFactor)
{
    auto output = run(fmt::format("--es {} +count -e mod.ev/a +summarize", build_es("factors_scoped.in")));
    EXPECT_EQ(output, R"(mod.ev         2
mod.ev/mod.a:0 1
mod.ev/mod.a:1 1
)");
}

TEST_F(EVPTest, ForceGlobalFactor)
{
    auto output = run(fmt::format("--es {} +count -e mod.ev2/.a +summarize", build_es("factors_scoped.in")));
    EXPECT_EQ(output, R"(mod.ev2     1
mod.ev2/a:2 1
)");
}

TEST_F(EVPTest, CanAccumulateData)
{
    auto output =
        run(fmt::format("--es {} +count --accumulate ev/a --accumulate ev/b +summarize", build_es("factors.in")));
    EXPECT_EQ(output, R"(ev/a 30
ev/b 6
)");
}

TEST_F(EVPTest, CantAccumulateWithMoreThanOneDataItem)
{
    auto output =
        run_expecting_error(fmt::format("--es {} +count --accumulate ev/a/b +summarize", build_es("factors.in")));
    EXPECT_PLUGIN_ERROR(output, "count", "malformed data counter; should have exactly one data item: <event>/<data>\n");
}

TEST_F(EVPTest, CantAccumulateDataItemWithValue)
{
    auto output =
        run_expecting_error(fmt::format("--es {} +count --accumulate ev/a:2 +summarize", build_es("factors.in")));
    EXPECT_PLUGIN_ERROR(output, "count", "factor \"a\" is not allowed a value in \"a/ev\"\n");
}

TEST_F(EVPTest, CanRename)
{
    auto output = run(fmt::format("--es {} +count -e one +rename -r one=One +summarize", build_es("basic.in")));
    EXPECT_EQ(output, "One 2\n");
}

TEST_F(EVPTest, CanCopy)
{
    auto output = run(fmt::format("--es {} +count -e one +rename -c one=One +summarize", build_es("basic.in")));
    EXPECT_EQ(output, "One 2\none 2\n");
}

TEST_F(EVPTest, CanRenameToLowercase)
{
    auto output = run(fmt::format("--es {} +count -e ev/myenum +rename -rl '(*)/myenum:(*)=$1.$2' +summarize",
                                  build_es("uppercase_enums.in")));
    auto expected = R"(ev           2
ev.blue_fish 1
ev.red_fish  1
)";
    EXPECT_EQ(output, expected);
}

TEST_F(EVPTest, CanCopyToLowercase)
{
    auto output = run(fmt::format("--es {} +count -e ev/myenum +rename -cl '(*)/myenum:(*)=$1.$2' +summarize",
                                  build_es("uppercase_enums.in")));
    auto expected = R"(ev                  2
ev.blue_fish        1
ev.red_fish         1
ev/myenum:BLUE_FISH 1
ev/myenum:RED_FISH  1
)";
    EXPECT_EQ(output, expected);
}

TEST_F(EVPTest, CanCopyRenamed)
{
    auto output =
        run(fmt::format("--es {} +count -e one +rename -r one=One -c One=Two +summarize", build_es("basic.in")));
    EXPECT_EQ(output, "One 2\nTwo 2\n");
}

TEST_F(EVPTest, RenameCopyMaintainOrdering)
{
    auto output =
        run(fmt::format("--es {} +count -e one +rename -r one=One -c One=Two -r Two=two -c two=Three +summarize",
                        build_es("basic.in")));
    EXPECT_EQ(output, "One   2\nThree 2\ntwo   2\n");
}

TEST_F(EVPTest, CanRenamePattern)
{
    auto output =
        run(fmt::format("--es {} +count -e a_0.* +rename -r 'a_0.b_(#).(*)=$2$1' +summarize", build_es("patterns.in")));
    EXPECT_EQ(output, "foo0 1\nfoo1 1\nfoo2 1\nfoo3 1\n");
}

TEST_F(EVPTest, CanRenamePatternWithBackReference)
{
    auto output = run(
        fmt::format("--es {} +count -a +rename -r 'a_(#).b_(\\1).foo=match_$1' +summarize", build_es("patterns.in")));
    auto expected = R"(a_0.b_1.foo 1
a_0.b_2.foo 1
a_0.b_3.foo 1
a_1.b_0.foo 1
match_0     1
match_1     2
)";
    EXPECT_EQ(output, expected);
}

TEST_F(EVPTest, CanSum)
{
    auto output =
        run(fmt::format("--es {} +count -a +sum 'a_0.*=sum_0' 'a_1.*=sum_1' +summarize", build_es("patterns.in")));
    auto expected = R"(a_0.b_0.foo 1
a_0.b_1.foo 1
a_0.b_2.foo 1
a_0.b_3.foo 1
a_1.b_0.foo 1
a_1.b_1.foo 2
sum_0       4
sum_1       3
)";
    EXPECT_EQ(output, expected);
}

TEST_F(EVPTest, CanSumWithBackReference)
{
    auto output = run(fmt::format("--es {} +count -a +sum 'a_(#).*=sum_$1' +summarize", build_es("patterns.in")));
    auto expected = R"(a_0.b_0.foo 1
a_0.b_1.foo 1
a_0.b_2.foo 1
a_0.b_3.foo 1
a_1.b_0.foo 1
a_1.b_1.foo 2
sum_0       4
sum_1       3
)";
    EXPECT_EQ(output, expected);
}

TEST_F(EVPTest, MultipleSums)
{
    auto output =
        run(fmt::format("--es {} +count -a +sum '*.foo=sum_foo' '*.bar=sum_bar' +summarize", build_es("multi_sum.in")));
    auto expected = R"(sum_bar 3
sum_foo 4
x.bar   1
x.foo   3
y.bar   2
y.foo   1
)";
    EXPECT_EQ(output, expected);
}

TEST_F(EVPTest, CanGenerateTimeSeries)
{
    auto ts_file = test_file("ts.csv");
    auto output =
        run(fmt::format("--es {} -i 30 +count -a +summarize --timeseries {}", build_es("series.in"), ts_file));

    auto expected = R"(start_time,stop_time,a
10,29,3
30,59,5
60,89,3
90,90,1
)";

    auto actual = read_and_remove_file(ts_file);
    EXPECT_EQ(actual, expected);
}

TEST_F(EVPTest, CanGenerateCompressedTimeSeries)
{
    auto ts_file = test_file("ts.csv.xz");
    auto output =
        run(fmt::format("--es {} -i 30 +count -a +summarize --timeseries {}", build_es("series.in"), ts_file));

    EXPECT_EQ(0, std::remove(ts_file.c_str()));
}

TEST_F(EVPTest, TimeSeriesIgnoresBeforeFirst)
{
    auto ts_file = test_file("ts.csv");
    auto output =
        run(fmt::format("--es {} -i 30 +count -a +summarize --timeseries {}", build_es("delayed_series.in"), ts_file));

    auto expected = R"(start_time,stop_time,a
40,59,3
60,89,5
90,119,3
120,120,1
)";

    auto actual = read_and_remove_file(ts_file);
    EXPECT_EQ(actual, expected);
}

TEST_F(EVPTest, SummaryStillWorksWithTimeSeries)
{
    auto summary_file = test_file("summary.csv");
    auto output =
        run(fmt::format("--es {} -i 30 +count -a +summarize --summary {}", build_es("series.in"), summary_file));

    auto expected = "start_time,stop_time,a\n10,90,12\n";
    auto actual = read_and_remove_file(summary_file);
    EXPECT_EQ(actual, expected);
}

TEST_F(EVPTest, CanMeasureLatency)
{
    auto output = run(fmt::format("--es {} +latency a b +summarize", build_es("latency.in")));
    auto expected = R"(a_b.count           2
a_b.max_avg_latency 175
a_b.max_latency     250
a_b.min_latency     100
a_b.stdev           428.661
a_b.sum_latency     350
)";
    EXPECT_EQ(output, expected);
}

TEST_F(EVPTest, CanMeasureLatencyAndRenameOutput)
{
    auto output = run(fmt::format("--es {} +latency a b --name renamed +summarize", build_es("latency.in")));
    auto expected = R"(renamed.count           2
renamed.max_avg_latency 175
renamed.max_latency     250
renamed.min_latency     100
renamed.stdev           428.661
renamed.sum_latency     350
)";
    EXPECT_EQ(output, expected);
}

TEST_F(EVPTest, CanMeasureLatencyWithPrefix)
{
    auto output = run(fmt::format("--es {} +latency a b -p foo +summarize", build_es("latency.in")));
    auto expected = R"(a_b.count           1
a_b.max_avg_latency 280
a_b.max_latency     280
a_b.min_latency     280
a_b.stdev           0
a_b.sum_latency     280
)";
    EXPECT_EQ(output, expected);
}

TEST_F(EVPTest, CanMeasureLatencyWithPattern)
{
    auto output = run(fmt::format("--es {} +latency \\*a \\*b --name renamed +summarize", build_es("latency.in")));
    auto expected = R"(renamed.count           3
renamed.max_avg_latency 210
renamed.max_latency     280
renamed.min_latency     100
renamed.stdev           514.393
renamed.sum_latency     630
)";
    EXPECT_EQ(output, expected);
}

TEST_F(EVPTest, TwoLatenciesInOneTransaction)
{
    auto output = run(fmt::format("--es {} +latency multi_x multi_y +summarize", build_es("latency.in")));
    auto expected = R"(multi_x_multi_y.count           2
multi_x_multi_y.max_avg_latency 170
multi_x_multi_y.max_latency     170
multi_x_multi_y.min_latency     170
multi_x_multi_y.stdev           208.207
multi_x_multi_y.sum_latency     170
)";
    EXPECT_EQ(output, expected);
}

TEST_F(EVPTest, CanMeasureZeroLatency)
{
    auto output =
        run(fmt::format("--es {} +latency zero_lat_start zero_lat_end -n z +summarize", build_es("latency.in")));
    auto expected = R"(z.count       1
z.stdev       0
z.sum_latency 0
)";
    EXPECT_EQ(output, expected);
}

TEST_F(EVPTest, LatencyChain)
{
    auto output = run(fmt::format("--es {} +latency -n chain m n o p +summarize", build_es("latency.in")));
    auto expected = R"(chain.count             2
chain.m.count           2
chain.m.max_avg_latency 85
chain.m.max_latency     150
chain.m.min_latency     20
chain.m.stdev           208.207
chain.m.sum_latency     170
chain.max_avg_latency   85
chain.max_latency       150
chain.min_latency       20
chain.n.count           2
chain.n.max_avg_latency 335
chain.n.max_latency     350
chain.n.min_latency     320
chain.n.stdev           820.579
chain.n.sum_latency     670
chain.o.count           1
chain.o.max_avg_latency 510
chain.o.max_latency     510
chain.o.min_latency     510
chain.o.stdev           0
chain.o.sum_latency     510
chain.stdev             208.207
chain.sum_latency       170
)";
    EXPECT_EQ(output, expected);
}

TEST_F(EVPTest, LatencyWithAliasesChain)
{
    auto output = run(fmt::format("--es {} +latency -n chain m=M n=N o p=P +summarize", build_es("latency.in")));
    auto expected = R"(chain.M.count           2
chain.M.max_avg_latency 85
chain.M.max_latency     150
chain.M.min_latency     20
chain.M.stdev           208.207
chain.M.sum_latency     170
chain.N.count           2
chain.N.max_avg_latency 335
chain.N.max_latency     350
chain.N.min_latency     320
chain.N.stdev           820.579
chain.N.sum_latency     670
chain.count             2
chain.max_avg_latency   85
chain.max_latency       150
chain.min_latency       20
chain.o.count           1
chain.o.max_avg_latency 510
chain.o.max_latency     510
chain.o.min_latency     510
chain.o.stdev           0
chain.o.sum_latency     510
chain.stdev             208.207
chain.sum_latency       170
)";
    EXPECT_EQ(output, expected);
}

TEST_F(EVPTest, LatencyWithKeys)
{
    auto output = run(fmt::format("--es {} +latency -k x.b -k y.b x.a y.c +summarize", build_es("latency_keyed.in")));
    auto expected = R"(x.a_y_c.count           3
x.a_y_c.max_avg_latency 130
x.a_y_c.max_latency     130
x.a_y_c.min_latency     130
x.a_y_c.stdev           106.141
x.a_y_c.sum_latency     130
)";
    EXPECT_EQ(output, expected);
}

TEST_F(EVPTest, LatencyWithHistogram)
{
    auto output = run(fmt::format("--es {} +latency a b --histogram latency +summarize", build_es("latency.in")));
    auto expected = R"(a_b.count           2
a_b.latency.100     1
a_b.latency.250     1
a_b.max_avg_latency 175
a_b.max_latency     250
a_b.min_latency     100
a_b.stdev           428.661
a_b.sum_latency     350
)";
    EXPECT_EQ(output, expected);
}

TEST_F(EVPTest, LatencyWithFactoredHistogram)
{
    auto output =
        run(fmt::format("--es {} +latency a b --factored --histogram latency +summarize", build_es("latency.in")));
    auto expected = R"(a_b.count           2
a_b.max_avg_latency 175
a_b.max_latency     250
a_b.min_latency     100
a_b.stdev           428.661
a_b.sum_latency     350
a_b/latency:100     1
a_b/latency:250     1
)";
    EXPECT_EQ(output, expected);
}

TEST_F(EVPTest, LatencyWithFactoredBoundedHistogram)
{
    auto output = run(
        fmt::format("--es {} +latency a b --factored --histogram latency[::100] +summarize", build_es("latency.in")));
    auto expected = R"(a_b.count           2
a_b.max_avg_latency 175
a_b.max_latency     250
a_b.min_latency     100
a_b.stdev           428.661
a_b.sum_latency     350
a_b/latency:100     1
a_b/latency:200     1
)";
    EXPECT_EQ(output, expected);
}

TEST_F(EVPTest, PythonTest)
{
    auto output = run(fmt::format("--es {} +python {}", build_es("basic.in"), fixture("pytest.py")));
    auto expected = R"(saw one at 100
event one at 100 with data1=0 and data2=0
event two at 100 with data1=0 and data2=0
saw one at 200
event one at 200 with data1=42 and data2=903
event two at 300 with data1=21 and data2=760
)";
    EXPECT_EQ(output, expected);
}

TEST_F(EVPTest, MissingPythonFile)
{
    auto output = run_expecting_error(fmt::format("--es {} +python missing.py", build_es("basic.in")));
    EXPECT_PLUGIN_ERROR(output, "python", "could not open file missing.py\n");
}

TEST_F(EVPTest, PythonFilesWontShare)
{
    auto output = run_expecting_error(
        fmt::format("--es {} +python {} +python {}", build_es("basic.in"), fixture("one.py"), fixture("two.py")));
    EXPECT_TRUE(output.find("name 'x' is not defined") != std::string::npos);
}

TEST_F(EVPTest, PythonArgumentPassing)
{
    auto output = run(fmt::format("--es {} +python {} Raleigh --time 6pm +python {} --hello 'Portland' --time 3pm",
                                  build_es("basic.in"),
                                  fixture("arguments.py"),
                                  fixture("arguments.py")));
    EXPECT_TRUE(output.find("Goodbye, Raleigh! It is 6pm") != std::string::npos);
    EXPECT_TRUE(output.find("Hello, Portland! It is 3pm") != std::string::npos);
}

TEST_F(EVPTest, CanCollectFromPython)
{
    auto ts_file = test_file("ts.csv");
    auto output = run(fmt::format("--es {} -i 30 +count -a +summarize --timeseries {} +python {} +python {}",
                                  build_es("series.in"),
                                  ts_file,
                                  fixture("collect.py"),
                                  fixture("one.py")));

    auto expected = R"(start_time,stop_time,a,time
10,29,3,29
30,59,5,30
60,89,3,30
90,90,1,1
)";

    auto actual = read_and_remove_file(ts_file);
    EXPECT_EQ(actual, expected);
}

TEST_F(EVPTest, GetParamFromPython)
{
    auto output = run(fmt::format("--es {} -i 30 +count -a +python {}", build_es("factors.in"), fixture("param.py")));

    auto expected = R"(int 4
int 192
list [7, 4]
NoneType None
)";

    EXPECT_EQ(output, expected);
}

TEST_F(EVPTest, TestVariables)
{
    auto output =
        run(fmt::format("--es {} -s hello_to=world -P python {}", build_es("basic.in"), fixture("say_hello.py")));
    EXPECT_EQ(output, "hello world\n");
}

TEST_F(EVPTest, TestHelp)
{
    auto output_h = run_expecting_error("-h");
    auto output_help = run_expecting_error("--help");

    EXPECT_EQ(output_h, output_help);
    EXPECT_NE(output_help.find("--es FILENAME.es"), std::string::npos);
}

TEST_F(EVPTest, Dump)
{
    auto output = run_expecting_error(
        "--dump --start 0 +count -e xxx +summarize --summary one.csv +summarize --summary two.csv +count -e yyy");
    const auto* expected_output = R"(global options:
  --start 0

count[0] plugin options:
  -e xxx
  -e yyy

summarize[0] plugin options:
  --summary one.csv

summarize[1] plugin options:
  --summary two.csv


)";

    EXPECT_EQ(output, expected_output);
}

TEST_F(EVPTest, MissingPlugin)
{
    auto output = run_expecting_error(fmt::format("--es {} -P noplugin", build_es("basic.in")));
    EXPECT_NE(output.find("could not find plugin \"noplugin\""), std::string::npos);
}

TEST_F(EVPTest, MissingPluginArg)
{
    auto output = run_expecting_error(fmt::format("--es {} -P", build_es("basic.in")));
    EXPECT_NE(output.find("missing plugin name after \"-P\""), std::string::npos);
}

TEST_F(EVPTest, CanReadCompressedFile)
{
    auto output = run(fmt::format("--es {} +count -a +summarize", fixture("compressed.es.xz")));
    EXPECT_EQ(output, "one 2\ntwo 2\n");
}

TEST_F(EVPTest, CanCapture)
{
    auto captured = test_file("captured.es");
    auto output = run(fmt::format("--es {} +count -a +capture {}", build_es("factors.in"), captured));
    EXPECT_EQ(
        output,
        "Captured 1 event definitions, 2 value definitions, 0 enum definitions, 3 parameter values, and 10 events.\n");
    std::remove(captured.c_str());
}

TEST_F(EVPTest, CanCaptureOverwrite)
{
    auto captured = test_file("captured.es");
    auto output = run(fmt::format("--es {} +count -a +capture {}", build_es("factors.in"), captured));
    EXPECT_EQ(
        output,
        "Captured 1 event definitions, 2 value definitions, 0 enum definitions, 3 parameter values, and 10 events.\n");

    output = run(fmt::format("--es {} +count -a +capture {} --force", build_es("factors.in"), captured));
    std::remove(captured.c_str());
}

TEST_F(EVPTest, CanCaptureCompressed)
{
    auto captured = test_file("captured.es.xz");
    auto output = run(fmt::format("--es {} +count -a +capture {}", build_es("factors.in"), captured));
    EXPECT_EQ(
        output,
        "Captured 1 event definitions, 2 value definitions, 0 enum definitions, 3 parameter values, and 10 events.\n");
    std::remove(captured.c_str());
}

TEST_F(EVPTest, CanCaptureNumericalPrefix)
{
    auto captured = test_file("captured.es", "403");
    auto output = run(fmt::format("--es {} +count -a +capture {}", build_es("factors.in"), captured));
    EXPECT_EQ(
        output,
        "Captured 1 event definitions, 2 value definitions, 0 enum definitions, 3 parameter values, and 10 events.\n");
    std::remove(captured.c_str());
}

TEST_F(EVPTest, CanCaptureWithFilter)
{
    auto captured = test_file("captured.es");
    auto output = run(fmt::format("--es {} +count -a +capture {} --filter b=1", build_es("factors.in"), captured));
    EXPECT_EQ(
        output,
        "Captured 1 event definitions, 2 value definitions, 0 enum definitions, 3 parameter values, and 2 events.\n");
    std::remove(captured.c_str());
}

TEST_F(EVPTest, CanCaptureEnums)
{
    auto captured = test_file("captured.es");
    auto output = run(fmt::format("--es {} +count -a +capture {}", build_es("enums.in"), captured));
    EXPECT_EQ(
        output,
        "Captured 1 event definitions, 1 value definitions, 1 enum definitions, 0 parameter values, and 3 events.\n");
    std::remove(captured.c_str());
}

TEST_F(EVPTest, CanCaptureToStdout)
{
    auto output = run(fmt::format("--es {} +count -a +capture -", build_es("factors.in")));
    EXPECT_GT(output.size(), 16);
    EXPECT_THAT(output, testing::Not(testing::HasSubstr("Captured 1 event definitions")));
}

TEST_F(EVPTest, TimeSkip)
{
    auto captured = test_file("captured.es");
    auto output =
        run(fmt::format("--es {} +count -a +time_skip {} --event clock_skip", build_es("skipping.in"), captured));
    auto expected =
        R"(Captured 1 event definitions, 0 value definitions, 0 enum definitions, 0 parameter values, and 3 events.
Skipped a total of 30ps via clock_skip
)";
    EXPECT_EQ(output, expected);
    std::remove(captured.c_str());
}

TEST_F(EVPTest, CountImmediatelyAtTimeZero)
{
    auto summary_file = test_file("summary.csv");
    auto output =
        run(fmt::format("--es {} --start 0 +count -a +summarize --summary {}", build_es("basic.in"), summary_file));

    auto expected = R"(start_time,stop_time,one,two
100,300,2,2
)";

    auto actual = read_and_remove_file(summary_file);
    EXPECT_EQ(actual, expected);
}

TEST_F(EVPTest, CountAtStart)
{
    auto summary_file = test_file("summary.csv");
    auto output =
        run(fmt::format("--es {} --start 200 +count -a +summarize --summary {}", build_es("basic.in"), summary_file));

    auto expected = R"(start_time,stop_time,one,two
200,300,1,1
)";

    auto actual = read_and_remove_file(summary_file);
    EXPECT_EQ(actual, expected);
}

TEST_F(EVPTest, CountAfterStart)
{
    auto summary_file = test_file("summary.csv");
    auto output =
        run(fmt::format("--es {} --start 63 +count -a +summarize --summary {}", build_es("series.in"), summary_file));

    auto expected = R"(start_time,stop_time,a
70,90,3
)";

    auto actual = read_and_remove_file(summary_file);
    EXPECT_EQ(actual, expected);
}

TEST_F(EVPTest, CountBeforeStop)
{
    auto summary_file = test_file("summary.csv");
    auto output =
        run(fmt::format("--es {} --stop 43 +count -a +summarize --summary {}", build_es("series.in"), summary_file));

    auto expected = R"(start_time,stop_time,a
10,44,6
)";

    auto actual = read_and_remove_file(summary_file);
    EXPECT_EQ(actual, expected);
}

TEST_F(EVPTest, CountBetweenStartStop)
{
    auto summary_file = test_file("summary.csv");
    auto output = run(fmt::format(
        "--es {} --start 27 --stop 75 +count -a +summarize --summary {}", build_es("series.in"), summary_file));

    auto expected = R"(start_time,stop_time,a
30,79,7
)";

    auto actual = read_and_remove_file(summary_file);
    EXPECT_EQ(actual, expected);
}

TEST_F(EVPTest, CountImmediatelyAtNoEvents)
{
    auto summary_file = test_file("summary.csv");
    auto output = run(fmt::format(
        "--es {} --start a=0 --stop a=10 +count -a +summarize --summary {}", build_es("series.in"), summary_file));

    auto expected = R"(start_time,stop_time,a
10,69,10
)";
    auto actual = read_and_remove_file(summary_file);
    EXPECT_EQ(actual, expected);
}

TEST_F(EVPTest, CountBetweenStartStopEvents)
{
    auto summary_file = test_file("summary.csv");
    auto output = run(fmt::format(
        "--es {} --start a=3 --stop a=10 +count -a +summarize --summary {}", build_es("series.in"), summary_file));

    auto expected = R"(start_time,stop_time,a
30,69,7
)";
    auto actual = read_and_remove_file(summary_file);
    EXPECT_EQ(actual, expected);
}

TEST_F(EVPTest, ExitAfterStop)
{
    auto summary_file = test_file("summary.csv");
    auto output = run(
        fmt::format("--es {} --stop 43 --exit +count -a +summarize --summary {}", build_es("series.in"), summary_file));

    auto expected = R"(start_time,stop_time,a
10,44,6
)";

    auto actual = read_and_remove_file(summary_file);
    EXPECT_EQ(actual, expected);
    EXPECT_EQ(output, "Stopping: exiting on stop\n");
}

TEST_F(EVPTest, ReportParams)
{
    auto output = run(fmt::format("--es {} +param -a +summarize", build_es("basic.in")));
    auto expected = R"(machine_width                            4
rob.size                                 192
sys.soc_0.core_0.scheduler_configuration [7,4]
three                                    "four"
)";
    EXPECT_EQ(output, expected);
}

TEST_F(EVPTest, ReportParamsFiltered)
{
    auto output = run(fmt::format("--es {} +param -p three +summarize", build_es("basic.in")));
    EXPECT_EQ(output, "three \"four\"\n");
}

TEST_F(EVPTest, ReportParamsFilteredGlob)
{
    auto output = run(fmt::format("--es {} +param -p *.core_0.* +summarize", build_es("basic.in")));
    EXPECT_EQ(output, "sys.soc_0.core_0.scheduler_configuration [7,4]\n");
}

TEST_F(EVPTest, ReportParamsWithEventCounts)
{
    auto output = run(fmt::format("--es {} +count -a +param -p three +summarize", build_es("basic.in")));
    EXPECT_EQ(output, "one   2\nthree \"four\"\ntwo   2\n");
}

TEST_F(EVPTest, CanReadFromConfigFile)
{
    auto output = run(fmt::format("--es {} @{} +summarize", build_es("basic.in"), fixture("basic.cfg")));
    EXPECT_EQ(output, "two 2\n");
}

TEST_F(EVPTest, CanReadFromConfigFileWithComments)
{
    auto output = run(fmt::format("--es {} @{} +summarize", build_es("basic.in"), fixture("comment.cfg")));
    EXPECT_EQ(output, "two 2\n");
}

TEST_F(EVPTest, MissingConfigFile)
{
    run_expecting_error(fmt::format("--es {} @missing.cfg +summarize", build_es("basic.in")));
}

TEST_F(EVPTest, CanReadFromInstalledConfigFile)
{
    auto output = run(fmt::format("--es {} @all.cfg +summarize", build_es("basic.in")));
    EXPECT_EQ(output, "one 2\ntwo 2\n");
}

TEST_F(EVPTest, ExplicitRelativeDoesNotSearchConfigPath)
{
    auto output = run_expecting_error(fmt::format("--es {} @./all.cfg +summarize", build_es("basic.in")));
    EXPECT_EQ(output, "Error: could not find config file \"./all.cfg\"\n");
}

TEST_F(EVPTest, ExplicitParentRelativeDoesNotSearchConfigPath)
{
    auto output =
        run_expecting_error(fmt::format("--es {} @../events.processor/all.cfg +summarize", build_es("basic.in")));
    EXPECT_EQ(output, "Error: could not find config file \"../events.processor/all.cfg\"\n");
}

TEST_F(EVPTest, CanReadFromChainedConfigFile)
{
    auto output = run(fmt::format("--es {} @{} +summarize", build_es("basic.in"), fixture("chained.cfg")));
    EXPECT_EQ(output, "one 2\ntwo 2\n");
}

TEST_F(EVPTest, CanMeasureRateWithinTime)
{
    auto output = run(fmt::format("--es {} +rate 30 -e a +summarize", build_es("series.in")));
    EXPECT_EQ(output, "a.rate.1 1\na.rate.3 2\na.rate.5 1\n");
}

TEST_F(EVPTest, CanMeasureRateWithinTimeFactored)
{
    auto output = run(fmt::format("--es {} +rate 30 --factored -e a +summarize", build_es("series.in")));
    EXPECT_EQ(output, "a/rate:1 1\na/rate:3 2\na/rate:5 1\n");
}

TEST_F(EVPTest, CanMeasureRateWithinTimeFactoredBounded)
{
    auto output = run(fmt::format("--es {} +rate 30 --factored -e a[::2] +summarize", build_es("series.in")));
    EXPECT_EQ(output, "a/rate:0 1\na/rate:2 2\na/rate:4 1\n");
}

TEST_F(EVPTest, CanMeasureRateWithinEvent)
{
    auto output = run(fmt::format("--es {} +rate b -e a +summarize", build_es("multiple_series.in")));
    EXPECT_EQ(output, "a.rate.0 1\na.rate.1 3\na.rate.2 1\na.rate.3 1\na.rate.4 1\n");
}

TEST_F(EVPTest, CanMeasureRateWithinEventBounded)
{
    auto output = run(fmt::format("--es {} +rate b -e a[1:4:2] +summarize", build_es("multiple_series.in")));
    EXPECT_EQ(output, "a.rate.1 4\na.rate.2 2\na.rate.4 1\n");
}

TEST_F(EVPTest, CanConvertFactoredEnums)
{
    auto output = run(fmt::format("--es {} +count -e ev/myenum +summarize", build_es("enums.in")));
    EXPECT_EQ(output, R"(ev          3
ev/myenum:a 1
ev/myenum:b 1
ev/myenum:c 1
)");
}

TEST_F(EVPTest, CanNotConvertFactoredEnums)
{
    auto output = run(fmt::format("--es {} +count --no-enum -e ev/myenum +summarize", build_es("enums.in")));
    EXPECT_EQ(output, R"(ev          3
ev/myenum:1 1
ev/myenum:2 1
ev/myenum:3 1
)");
}

TEST_F(EVPTest, CanConvertEnumsAndPrint)
{
    auto output = run(fmt::format("--es {} +count -a +print", build_es("enums.in")));
    EXPECT_EQ(output, R"(100 ev myenum=a
200 ev myenum=b
300 ev myenum=c
)");
}

TEST_F(EVPTest, CanNotConvertEnumsAndPrint)
{
    auto output = run(fmt::format("--es {} +count -a +print --no-enum", build_es("enums.in")));
    EXPECT_EQ(output, R"(100 ev myenum=1
200 ev myenum=2
300 ev myenum=3
)");
}

TEST_F(EVPTest, CanLsEvents)
{
    auto output = run(fmt::format("--es {} +ls", build_es("basic.in")));
    EXPECT_EQ(output, R"(event one event
event two event
)");
}

TEST_F(EVPTest, CanLsValues)
{
    auto output = run(fmt::format("--es {} +ls --values", build_es("basic.in")));
    EXPECT_EQ(output, R"(value data1 data
value data2 data
)");
}

TEST_F(EVPTest, CanLsEnums)
{
    auto output = run(fmt::format("--es {} +ls --enum myenum", build_es("enums.in")));
    EXPECT_EQ(output, R"(value myenum
enum  value[1] a
enum  value[2] b
enum  value[3] c
)");
}

TEST_F(EVPTest, PythonDoesNotCreateEnumTypesForNonEnums)
{
    auto output = run(fmt::format("--es {} +python {} one", build_es("basic.in"), fixture("enums.py")));
    EXPECT_EQ(output, R"(100 one
200 one data1(i)=42, data2(i)=903
)");
}

TEST_F(EVPTest, PythonDoesNotCreateEnumTypesForNonEnumsAndStrWorksAsExpected)
{
    auto output = run(fmt::format("--es {} +python {} one --expand-enums", build_es("basic.in"), fixture("enums.py")));
    EXPECT_EQ(output, R"(100 one
200 one data1(i)=42, data2(i)=903
)");
}

TEST_F(EVPTest, PythonDoesNotConvertEnumsByDefault)
{
    auto output = run(fmt::format("--es {} +python {} ev", build_es("enums.in"), fixture("enums.py")));
    EXPECT_EQ(output, R"(100 ev myenum(E)=1
200 ev myenum(E)=2
300 ev myenum(E)=3
)");
}

TEST_F(EVPTest, PythonConvertEnumsIfConfigured)
{
    auto output = run(fmt::format("--es {} +python {} ev --expand-enums", build_es("enums.in"), fixture("enums.py")));
    EXPECT_EQ(output, R"(100 ev myenum(E)=a
200 ev myenum(E)=b
300 ev myenum(E)=c
)");
}

TEST_F(EVPTest, PythonEnumIntEquality)
{
    auto output = run(fmt::format("--es {} +python {} ev --eq 2", build_es("enums.in"), fixture("enums.py")));
    EXPECT_EQ(output, R"(100 ev myenum(E)=1
200 ev myenum(E)=2
equal: myenum=b == 2
300 ev myenum(E)=3
)");
}

TEST_F(EVPTest, PythonEnumStrEquality)
{
    auto output =
        run(fmt::format("--es {} +python {} ev --expand-enums --eq b", build_es("enums.in"), fixture("enums.py")));
    EXPECT_EQ(output, R"(100 ev myenum(E)=a
200 ev myenum(E)=b
equal: myenum=b == b
300 ev myenum(E)=c
)");
}

TEST_F(EVPTest, PythonEnumsDict)
{
    auto output =
        run(fmt::format("--es {} +python {} ev --expect-enum myenum", build_es("enums.in"), fixture("enums.py")));
    EXPECT_EQ(output, R"(100 ev myenum(E)=1
200 ev myenum(E)=2
300 ev myenum(E)=3
)");
}
