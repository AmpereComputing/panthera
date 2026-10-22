//
// Copyright (c) 2020 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//

#include "latency_test.h"

namespace ampere::soc {

using namespace ::testing;

TEST_F(LatencyTest, SLCHitLatency)
{
    get_environment()->parameters.load(TestModule::testconfig("soc/sn-quad2.cfg"), "soc");
    get_environment()->parameters.add_setting("soc.core_12.enable", true, "test bench");
    get_environment()->parameters.add_setting("soc.core_12.source.type", "UopSourceProg", "test bench");
    get_environment()->parameters.add_setting("soc.core_12.source.decoders", "[\"furiosa\"]"_json, "test bench");

    constexpr auto one_load = R"({{"op": "LDR_64_ldst_pos", "fields": {{"imm12": 0, "Rn": 1, "Rt": 1}}, "pa": {}}})";
    constexpr auto one_warm = R"({{"address": {}, "state": "SC"}})";

    std::string loads;
    std::string warms;

    uint64_t addr{0};

    // 64 loads should ensure that we visit all HNFs at least once
    for (auto count = 0; count < 64; ++count, addr += 64) {
        if (!loads.empty())
            loads += ",\n";
        if (!warms.empty())
            warms += ",\n";
        loads += fmt::format(one_load, addr);
        warms += fmt::format(one_warm, addr);
    }

    // final ADD instruction in the stream below makes sure test does not finish too quickly
    auto program = fmt::format(R"(
{{
    "code": [
        {},
        {{"op": "ADD_64_addsub_imm", "fields": {{"sh": 0, "imm12": 0, "Rn": 3, "Rd": 3}}}}
    ]
}}
)",
                               loads);

    auto warmstart = fmt::format("[{}]", warms);

    get_environment()->parameters.add_setting("soc.core_12.source.program", json::parse(program), "test bench");
    get_environment()->parameters.add_setting("soc.warmstart_table", json::parse(warmstart), "test bench");

    Soc soc{"soc", clock, get_environment()};

    run_for(2000_ns);

    EXPECT_EQ(req_to_req_count, 64);

    EXPECT_NEAR(req_to_dat_min(), 12.0, 1.0);
    EXPECT_NEAR(req_to_dat_max(), 26.0, 1.0);
    EXPECT_NEAR(req_to_dat_lat(), 19.0, 1.0);
}

TEST_F(LatencyTest, SLCMissLatency)
{
    get_environment()->parameters.load(TestModule::testconfig("soc/sn-quad2.cfg"), "soc");
    get_environment()->parameters.add_setting("soc.core_12.enable", true, "test bench");
    get_environment()->parameters.add_setting("soc.core_12.source.type", "UopSourceProg", "test bench");
    get_environment()->parameters.add_setting("soc.core_12.source.decoders", "[\"furiosa\"]"_json, "test bench");

    constexpr auto one_load = R"({{"op": "LDR_64_ldst_pos", "fields": {{"imm12": 0, "Rn": 1, "Rt": 1}}, "pa": {}}})";

    std::string loads;

    uint64_t addr{0};

    // 64 loads should ensure that we visit all HNFs at least once
    for (auto count = 0; count < 64; ++count, addr += 64) {
        if (!loads.empty())
            loads += ",\n";
        loads += fmt::format(one_load, addr);
    }

    // final ADD instruction in the stream below makes sure test does not finish too quickly
    auto program = fmt::format(R"(
{{
    "code": [
        {},
        {{"op": "ADD_64_addsub_imm", "fields": {{"sh": 0, "imm12": 0, "Rn": 3, "Rd": 3}}}}
    ]
}}
)",
                               loads);

    get_environment()->parameters.add_setting("soc.core_12.source.program", json::parse(program), "test bench");

    Soc soc{"soc", clock, get_environment()};

    run_for(10000_ns);

    EXPECT_EQ(req_to_req_count, 64);

    EXPECT_NEAR(req_to_dat_min(), 91.0, 2.0);
    EXPECT_NEAR(req_to_dat_max(), 112.0, 2.0);
    EXPECT_NEAR(req_to_dat_lat(), 102.0, 2.0);
}

// Specialty test class for checking that we're distributing properly to
// all HNFs and SLC sets. The check is a little bit roundabout since we
// don't directly post the set with the access event in the cache class.
// Instead, we look at the posted way and count only when we see a way
// of zero (which is the only way that should be used if we're distributing
// as expected). Then we can ensure that the total count is equal to the
// number of sets we configured.
class SLCSetDistributionTest : public LatencyTest
{
public:
    SLCSetDistributionTest() : LatencyTest{}, count(16, 0)
    {
        way_re = std::regex{"soc\\.homenode_([0-9]+)\\.slc\\.array\\.way"};
    }

    void add_uint_data(const std::string& event_name,
                       int id,
                       const std::string& data_type_name,
                       std::uint64_t value) override
    {
        LatencyTest::add_uint_data(event_name, id, data_type_name, value);
        std::smatch sm;

        if (std::regex_match(data_type_name, sm, way_re)) {
            if (value == 0) {
                int hnf = std::stoi(sm[1]);
                ++count.at(hnf);
            }
        }
    }

protected:
    std::vector<int> count;
    std::regex way_re;
};

// Make sure that all HNFs and SLC sets are being utilized. To do this we dial down the
// SLC set size to 8 sets and then issue 16*8 = 128 loads so that we reach each of the
// 8 sets in each of the 16 HNFs in a quad.
TEST_F(SLCSetDistributionTest, SLCSetDistribution)
{
    get_environment()->parameters.load(TestModule::testconfig("soc/sn-quad2.cfg"), "soc");
    get_environment()->parameters.add_setting("~soc\\.homenode_[0-9]+\\.slc\\.array\\.size", 8192, "test bench");
    get_environment()->parameters.add_setting("soc.core_12.enable", true, "test bench");
    get_environment()->parameters.add_setting("soc.core_12.source.type", "UopSourceProg", "test bench");
    get_environment()->parameters.add_setting("soc.core_12.source.decoders", "[\"furiosa\"]"_json, "test bench");

    constexpr auto one_load = R"({{"op": "LDR_64_ldst_pos", "fields": {{"imm12": 0, "Rn": 1, "Rt": 1}}, "pa": {}}})";
    constexpr auto one_warm = R"({{"address": {}, "state": "SC"}})";

    std::string loads;
    std::string warms;

    uint64_t addr{0};

    // 64 loads should ensure that we visit all HNFs at least once
    for (auto count = 0; count < 128; ++count, addr += 64) {
        if (!loads.empty())
            loads += ",\n";
        if (!warms.empty())
            warms += ",\n";
        loads += fmt::format(one_load, addr);
        warms += fmt::format(one_warm, addr);
    }

    // final ADD instruction in the stream below makes sure test does not finish too quickly
    auto program = fmt::format(R"(
{{
    "code": [
        {},
        {{"op": "ADD_64_addsub_imm", "fields": {{"sh": 0, "imm12": 0, "Rn": 3, "Rd": 3}}}}
    ]
}}
)",
                               loads);

    auto warmstart = fmt::format("[{}]", warms);

    get_environment()->parameters.add_setting("soc.core_12.source.program", json::parse(program), "test bench");
    get_environment()->parameters.add_setting("soc.warmstart_table", json::parse(warmstart), "test bench");

    Soc soc{"soc", clock, get_environment()};

    run_for(4000_ns);

    EXPECT_EQ(req_to_req_count, 128);

    EXPECT_NEAR(req_to_dat_min(), 12.0, 1.0);
    EXPECT_NEAR(req_to_dat_max(), 26.0, 1.0);
    EXPECT_NEAR(req_to_dat_lat(), 19.0, 1.0);

    for (auto i = 0; i < 16; ++i)
        EXPECT_EQ(count.at(i), 8);
}

} // namespace ampere::soc

/*
 * vim: sw=4:ts=4:et:si:cindent:cino=\:0,(0,g0,t0:
 */
