//
// Copyright (c) 2021-2022 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//

#include "ampere/sys/sys.h"
#include "ampere/core/test/core_integration.h"

namespace bfs = boost::filesystem;

using namespace ::testing;

class GRETCHIntegrationTest : public CoreIntegrationTest
{
public:
    static const int DIV_LAT = 8;

    void post_event(const std::string& event_name, std::uint64_t time) override
    {
        IntegrationTest::post_event(event_name, time);

        if (event_name == "sys.soc_0.core_0.lsu.prefetcher.gretch.adt_epoch") {
            adt_epochs++;
            time_adt_done = time;
        }

        else if (event_name == "sys.soc_0.core_0.lsu.prefetcher.gretch.comp_queue")
        {
            epoch_has_computations = true;
        }

        else if (event_name == "sys.soc_0.core_0.lsu.prefetcher.gretch.comp_queue_handled")
        {
            if (epoch_has_computations) {
                // collect all the dds found in this epoch
                std::vector<std::tuple<uint64_t, uint64_t>> this_epoch_dds;
                for (int i = 0; i < base_addrs.size(); i++) {
                    this_epoch_dds.emplace_back(base_addrs[i], offsets[i]);
                }
                dds[adt_epochs] = this_epoch_dds;

                // collect computation nature and corresponding time diff
                time_diffs[adt_epochs] =
                    std::make_tuple(num_comp_this_epoch, num_comp_hl_this_epoch, time - time_adt_done);

                reset();
            }
        }
    }

    void add_uint_data(const std::string& event_name,
                       int id,
                       const std::string& data_type_name,
                       std::uint64_t value) override
    {
        IntegrationTest::add_uint_data(event_name, id, data_type_name, value);

        if (event_name == "sys.soc_0.core_0.lsu.prefetcher.gretch.rt_insert") {
            if (data_type_name == "sys.soc_0.core_0.lsu.prefetcher.gretch.rt_base_addr")
                base_addrs.push_back(value);

            else if (data_type_name == "sys.soc_0.core_0.lsu.prefetcher.gretch.rt_offset")
                offsets.push_back(value);
        }

        if (event_name == "sys.soc_0.core_0.lsu.prefetcher.gretch.comp_queue") {
            if (data_type_name == "sys.soc_0.core_0.lsu.prefetcher.gretch.num_comp")
                num_comp_this_epoch = value;

            else if (data_type_name == "sys.soc_0.core_0.lsu.prefetcher.gretch.num_comp_hl")
                num_comp_hl_this_epoch = value;
        }
    }

    // ground truth should be found in the first epoch
    bool ground_truth_found()
    {
        auto first_epoch = dds.find(1);
        if (first_epoch == dds.end())
            return false;

        auto dds_this_epoch = dds[1];
        for (auto x : dds_this_epoch) {
            if ((std::get<0>(x) == (0x3a8f3110_va).value()) && (std::get<1>(x) == 4))
                return true;
        }

        return false;
    }

    // prepare for next adt epoch
    void reset()
    {
        base_addrs.clear();
        offsets.clear();
        epoch_has_computations = false;
        num_comp_this_epoch = 0;
        num_comp_hl_this_epoch = 0;
        time_adt_done = 0;
    }

    /* Inputs:
     * num_comp: number of adt pair computations in this epoch
     * num_comp_hl: num of adt pair computations resulting in DIVs (DIV skips = num_comp - num_comp_hl)
     * Outputs: expected cycles between "adt population done" and "adt pair computations done"
     */
    uint64_t expected_cycle(uint64_t num_comp, uint64_t num_comp_hl)
    {
        return (num_comp_hl * DIV_LAT + (num_comp - num_comp_hl));
    }

    // global across-adt-epoch infos
    uint64_t adt_epochs{0};
    std::unordered_map<uint64_t, std::vector<std::tuple<uint64_t, uint64_t>>>
        dds; // key: epoch ID; value: dds found in this epoch ID
    std::unordered_map<uint64_t, std::tuple<uint64_t, uint64_t, uint64_t>>
        time_diffs; // key: epoch ID; value: tuple (num-comp, num-comp-hl, time-diff)

    // local adt-epoch infos
    std::vector<uint64_t> base_addrs;   // save base_addrs found in this epoch
    std::vector<uint64_t> offsets;      // save offsets found in this epoch
    bool epoch_has_computations{false}; // adt may be empty or single-entry, so need to check this
    uint64_t num_comp_this_epoch{0};    // number of adt-pair computations in this specific epoch
    uint64_t num_comp_hl_this_epoch{0}; // number of high-latency adt-pair computations in this specific epoch
    uint64_t time_adt_done{0};          // time at which adt population is done
};

/* Gretch integration test to verify two issues:
    * 1) Gretch can find ground truth for a GAP-PR-LJ trace in the first ADT epoch
    * 2) For every epoch, the time difference between "adt population done"
    *    and "handling computations" should match the expectation.
            * Expectation: see expected_cycle() in the class definition above.

 * gap-pr-livej.16972_0_chopped.is.xz characteristics:
    * 2K instructions and no warmup/cooldown.
    * Chopped off from V4 trace.
    * First ADT epoch provides 6 DDs -> only one is the ground truth
    * Second ADT epoch provides no DDs

 * .............Ground truth..............
 * Producer PC = 0x403468 (PC[10:2] = 11a)
 * Consumer PC = 0x403470 (PC[10:2] = 11c)
 * Base address = 0x3a8f3110
 * Offset = 4
*/
TEST_F(GRETCHIntegrationTest, testGAPPRLJ)
{
    get_environment()->parameters.load(TestModule::testconfig("sys/rogue.cfg"));

    // 2060ns is enough for one adt epoch and 6 DDs which are entered into RT.
    // To see two adt epochs, run ~3000ns
    run_instruction_stream(TestModule::testfixture("gap-pr-livej.16972_0_chopped.is.xz"), 1000, 3000_ns);

    // check that ground truth is found
    EXPECT_EQ(ground_truth_found(), true);

    auto core = dynamic_cast<sc::Module*>(sc_core::sc_find_object("sys.soc_0.core_0"));
    double period = core->get_clock()->get_period().value() / 1000.0; // core's clock period in NS

    // times between "adt population done" and "adt pair computations done" should match expectation
    for (auto& time_diff : time_diffs) {
        auto [computations, high_latency_computations, time_difference] = time_diff.second;
        double expected_time_difference = expected_cycle(computations, high_latency_computations) * period; // in NS
        EXPECT_NEAR((double)time_difference / 1000.0, expected_time_difference, 1.0);
    }
}

/*
 * vim: sw=4:ts=4:et:si:cindent:cino=\:0,(0,g0,t0:
 */
