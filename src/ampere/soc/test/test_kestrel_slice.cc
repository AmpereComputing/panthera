//
// Copyright (c) 2025 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//
#include "ampere/soc/soc.h"
#include "ampere/sc/testing/test_module.h"

namespace ampere::soc {

class KestrelIntegration : public IntegrationTest
{
public:
    KestrelIntegration() : IntegrationTest{sc_core::sc_time{500, sc_core::SC_PS}} {}

    void post_event(const std::string& event_name, std::uint64_t time) override
    {
        IntegrationTest::post_event(event_name, time);

        if (event_name == "soc.tdma_0.recv_dat") {
            if (num_dat == 0)
                first_time = time;
            ++num_dat;
            last_time = time;
        }
        if (event_name == "soc.tdma_0.send_dat") {
            if (num_dat == 0)
                first_time = time;
            ++num_dat;
            last_time = time;
        }
    }

    std::uint64_t num_dat{0};
    std::uint64_t first_time{0};
    std::uint64_t last_time{0};

    double bw() { return ((double)(num_dat * 128)) / ((last_time - first_time) / 1000.0); }
};

TEST_F(KestrelIntegration, SliceReadBandwidth)
{
    auto& params = get_environment()->parameters;

    params.load(TestModule::testconfig("soc/ks-aicl-one-slice.cfg"), "soc");
    params.add_setting("~soc\\.tdma_[0-9]+\\.enable", false, "test bench");
    params.add_setting("soc.tdma_0.enable", true, "test bench");
    params.add_setting("soc.tdma_0.max_transactions", 4096, "test bench");

    Soc soc{"soc", clock, get_environment()};

    run_for(1000_ns);

    auto result_bw = bw();

    EXPECT_GE(result_bw, 250.0);
}

TEST_F(KestrelIntegration, SliceWriteBandwidth)
{
    auto& params = get_environment()->parameters;

    params.load(TestModule::testconfig("soc/ks-aicl-one-slice.cfg"), "soc");
    params.add_setting("~soc\\.tdma_[0-9]+\\.enable", false, "test bench");
    params.add_setting("soc.tdma_0.enable", true, "test bench");
    params.add_setting("soc.tdma_0.max_transactions", 4096, "test bench");
    params.add_setting("soc.tdma_0.write_mode", true, "test bench");

    Soc soc{"soc", clock, get_environment()};

    run_for(1000_ns);

    auto result_bw = bw();

    EXPECT_GE(result_bw, 250.0);
}

TEST_F(KestrelIntegration, TileToTileReadBandwidth)
{
    // get_environment()->event_stream = std::make_unique<event_stream::EventStreamProto>("debug.es");
    auto& params = get_environment()->parameters;

    params.load(TestModule::testconfig("soc/ks-aicl-one-slice.cfg"), "soc");
    params.add_setting("~soc\\.tdma_[0-9]+\\.enable", false, "test bench");
    params.add_setting("soc.tdma_0.enable", true, "test bench");
    params.add_setting("soc.tdma_0.max_transactions", 4096, "test bench");
    params.add_setting("~soc\\.tdma_[0-9]+\\.start_addr", 1717986918400, "test bench"); // 0x190_0000_0000
    params.add_setting("~soc\\.tdma_[0-9]+\\.stop_addr", 1718121136127, "test bench");  // 0x190_07ff_ffff

    Soc soc{"soc", clock, get_environment()};

    run_for(1000_ns);

    auto result_bw = bw();

    EXPECT_GE(result_bw, 250.0);
}

} // namespace ampere::soc
