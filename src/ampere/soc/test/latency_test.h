//
// Copyright (c) 2020 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//

#include "ampere/soc/soc.h"
#include "ampere/sc/testing/test_module.h"

namespace ampere::soc {

using namespace ::testing;

class LatencyTest : public IntegrationTest
{
public:
    LatencyTest() : IntegrationTest{sc_core::sc_time{500, sc_core::SC_PS}} {}

    void post_event(const std::string& event_name, std::uint64_t time) override
    {
        IntegrationTest::post_event(event_name, time);

        if (event_name == "soc.core_12.send_req") {
            sum_req_to_req_time += time - last_req_time;
            req_to_req_count += 1;
            last_req_time = time;
        } else if (event_name == "soc.core_12.recv_dat") {
            if (chunk == 0) {
                ++chunk;
            } else {
                auto latency = time - last_req_time;
                min_latency = std::min(min_latency, latency);
                max_latency = std::max(max_latency, latency);
                sum_req_to_dat_time += latency;
                req_to_dat_count += 1;
                chunk = 0;
            }
        }
    }

    std::uint64_t last_req_time{0};
    std::uint64_t sum_req_to_req_time{0};
    std::uint64_t sum_req_to_dat_time{0};
    std::uint64_t req_to_req_count{0};
    std::uint64_t req_to_dat_count{0};
    std::uint64_t min_latency{std::numeric_limits<std::uint64_t>::max()};
    std::uint64_t max_latency{0};
    int chunk{0};

    double req_to_req_lat() { return ((double)sum_req_to_req_time) / req_to_req_count / 1000.0; }
    double req_to_dat_lat() { return ((double)sum_req_to_dat_time) / req_to_dat_count / 1000.0; }
    double req_to_dat_min() { return (double)min_latency / 1000.0; }
    double req_to_dat_max() { return (double)max_latency / 1000.0; }
};

class LatencyTestDomino : public IntegrationTest
{
public:
    LatencyTestDomino() : IntegrationTest{sc_core::sc_time{500, sc_core::SC_PS}} {}

    void post_event(const std::string& event_name, std::uint64_t time) override
    {
        IntegrationTest::post_event(event_name, time);

        if (event_name == "soc.core_0.send_req") {
            sum_req_to_req_time += time - last_req_time;
            req_to_req_count += 1;
            last_req_time = time;
        } else if (event_name == "soc.core_0.recv_dat") {
            last_time = time;
            ++num_dat;
        }
    }

    std::uint64_t last_req_time{0};
    std::uint64_t sum_req_to_req_time{0};
    std::uint64_t req_to_req_count{0};
    std::uint64_t num_dat{0};
    std::uint64_t last_time{0};

    double req_to_req_lat() { return ((double)sum_req_to_req_time) / req_to_req_count / 1000.0; }
    double bw() { return ((double)(num_dat * 64)) / (last_time / 1000.0); }
};

class StressTestDomino : public IntegrationTest
{
public:
    StressTestDomino() : IntegrationTest{sc_core::sc_time{500, sc_core::SC_PS}} {}

    void initialize(const std::string& prog)
    {
        auto& params = get_environment()->parameters;

        params.load(TestModule::testconfig("soc/dn-quad1.cfg"), "soc");
        params.load(TestModule::testconfig(fmt::format("inj/{}.cfg", prog)), "~soc\\.core_[0-9]+\\.source");
        params.add_setting("~soc\\.core_[0-9]+\\.enable", true, "test bench");
        params.add_setting("~soc\\.core_[0-9]+\\.cache\\.size", 256, "test bench");
        params.add_setting("~soc\\.core_[0-9]+\\.cache\\.ways", 2, "test bench");
        params.add_setting("~soc\\.core_[0-9]+\\.l3c\\.size", 128, "test bench");
        params.add_setting("~soc\\.core_[0-9]+\\.l3c\\.ways", 2, "test bench");
        params.add_setting("~soc\\.p[xm]ha_[0-9]+\\.snoop_filter\\.sets", 2, "test bench");
        params.add_setting("~soc\\.p[xm]ha_[0-9]+\\.snoop_filter\\.ways", 2, "test bench");
        params.add_setting("~soc\\.p[xm]ha_[0-9]+\\.snoop_filter\\.bank_bits", 0, "test bench");

        for (int i = 0; i < 128; i++) {
            params.add_setting(fmt::format("soc.core_{}.data_offset", i), 0x1000001000 * i, "test bench");
        }
    }
};

} // namespace ampere::soc

/*
 * vim: sw=4:ts=4:et:si:cindent:cino=\:0,(0,g0,t0:
 */
