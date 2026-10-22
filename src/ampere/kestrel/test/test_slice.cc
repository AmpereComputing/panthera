//
// Copyright (c) 2024 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//
#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "ampere/kestrel/mesh/aicl.h"
#include "ampere/sc/cycle.h"
#include "ampere/sc/testing/test_module.h"

using namespace ampere::sc;

class SliceTest : public IntegrationTest
{
public:
    void post_event(const std::string& event_name, std::uint64_t time) override
    {
        IntegrationTest::post_event(event_name, time);

        if (event_name == "soc.cluster_0.slice_0.tile_0.core_0.send_req")
            ++num_reqs;
        else if (event_name == "soc.cluster_0.slice_0.tile_0.core_0.recv_rsp")
            ++num_rsps;
        else if (event_name == "soc.cluster_0.slice_0.tile_0.core_0.recv_dat")
            ++num_dats;
    }

protected:
    int num_reqs{0};
    int num_rsps{0};
    int num_dats{0};
};

TEST_F(SliceTest, RunOneLoadOnOneSlice)
{
    auto& params = get_environment()->parameters;

    params.add_setting("soc.system_address_map.ddr_snr_mode", 8, "test bench");
    params.add_setting("soc.num_clusters", 1, "test bench");
    params.add_setting("soc.cluster_0.num_slices", 1, "test bench");
    params.add_setting("soc.cluster_0.slice_0.slice_id", 0, "test bench");
    params.load(TestModule::testconfig("mesh/ks-slice.cfg"), "soc.cluster_0.slice_0");

    // Test set up: start with all cores and AICE disabled
    params.add_setting(R"(~soc\.cluster_0\.slice_0\.tile_[0-9]+\.core_[0-9]+\.enable)", false, "test bench");
    params.add_setting(R"(~soc\.cluster_0\.slice_0\.tile_[0-9]+\.aice_[0-9]+\.enable)", false, "test bench");

    // Now enable the one core that we want to perform the load
    params.add_setting("soc.cluster_0.slice_0.tile_0.core_0.enable", true, "test bench");
    params.add_setting("soc.cluster_0.slice_0.tile_0.core_0.source.type", "UopSourceProg", "test bench");
    params.add_setting("soc.cluster_0.slice_0.tile_0.core_0.source.decoders", R"(["connor"])"_json, "test bench");
    params.add_setting("soc.cluster_0.slice_0.tile_0.core_0.source.program",
                       R"({
"code": [
    {"op": "LDR_64_ldst_pos", "fields": {"imm12": 0, "Rn": 1, "Rt": 2}, "pa": 8192}
]}
)"_json,
                       "test bench");

    ampere::kestrel::AICL soc{"soc"};

    run_for(200_ns);

    EXPECT_EQ(num_reqs, 1);
    EXPECT_EQ(num_rsps, 1);
    EXPECT_EQ(num_dats, 1);
}
