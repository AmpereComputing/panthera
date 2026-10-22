//
// Copyright (c) 2018-2020 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//
#include <boost/algorithm/string/predicate.hpp>

#include "ampere/core/test/core_integration.h"

namespace bfs = boost::filesystem;

using namespace ::testing;

class ConnorIntegrationTest : public CoreIntegrationTest
{
public:
    void add_uint_data(const std::string& event_name,
                       int id,
                       const std::string& data_type_name,
                       std::uint64_t value) override
    {
        CoreIntegrationTest::add_uint_data(event_name, id, data_type_name, value);

        if (event_name == "sys.soc_0.core_0.l2c.eviction_data") {
            if (data_type_name == "sys.soc_0.core_0.l2c.l2") {
                if (value)
                    l2_data_evicts++;
                else
                    l3_data_evicts++;
            }
        }

        if (event_name == "sys.soc_0.core_0.l2c.refill") {
            if (data_type_name == "sys.soc_0.core_0.l2c.l2") {
                if (!value)
                    l3_refills++;
            }
        }
    }

    uint64_t l2_data_evicts{0};
    uint64_t l3_data_evicts{0};
    uint64_t l3_refills{0};

    uint64_t count_l2_data_evicts() { return l2_data_evicts; }
    uint64_t count_l3_data_evicts() { return l3_data_evicts; }
    uint64_t count_l3_refills() { return l3_refills; }
};

TEST_F(ConnorIntegrationTest, testConnorConfig)
{
    get_environment()->parameters.load(TestModule::testconfig("sys/domino_wip.cfg"));
    get_environment()->parameters.add_setting("~sys.soc_0.core_0.l2c.banks.bank_[0-9]+.size", "327680", "test bench");
    get_environment()->parameters.add_setting("~sys.soc_0.core_0.l2c.banks.bank_[0-9]+.ways", "10", "test bench");
    get_environment()->parameters.add_setting(
        "~sys.soc_0.core_0.l2c.banks.bank_[0-9]+.ways_mask", "0xff", "test bench");
    get_environment()->parameters.add_setting("sys.soc_0.core_1.cache.size", "2097152", "test bench");
    get_environment()->parameters.add_setting("sys.soc_0.core_1.cache.ways", "8", "test bench");
    get_environment()->parameters.add_setting("sys.soc_0.core_1.l3c.size", "524288", "test bench");
    get_environment()->parameters.add_setting("sys.soc_0.core_1.l3c.ways", "2", "test bench");
    get_environment()->parameters.add_setting("sys.soc_0.core_1.random_dummy_evict_delay", "false", "test bench");

    run_instruction_stream(TestModule::testfixture("458-sjeng.80244_49_20211029_0b8401.is.xz"), 200000, 75000_ns);

    uint64_t l2_data_evictions = count_l2_data_evicts();
    uint64_t stashes = event_count("sys.soc_0.core_0.l2c.recv_snp");
    uint64_t dummy_read = event_count("sys.soc_0.core_1.read_dummy");
    uint64_t dummy_mem_req = event_count("sys.soc_0.memory_1.recv_req");
    uint64_t dummy_l2_evicts = event_count("sys.soc_0.core_1.evict_dummy");
    uint64_t stashes_rejected = event_count("sys.soc_0.core_0.l2c.stash_reject");

    std::cout << "Core L2 data evicts = " << l2_data_evictions << std::endl;
    std::cout << "MI L3 refills = " << event_count("sys.soc_0.core_1.l3c.refill") << std::endl;
    std::cout << "Dummy MI reads = stash snoops accepted by MI = " << dummy_read << std::endl;
    std::cout << "Dummy MI mem req = " << dummy_mem_req << std::endl;
    std::cout << "Dummy MI L2 refills = " << event_count("sys.soc_0.core_1.cache.refill") << std::endl;
    std::cout << "Dummy MI evicts = " << dummy_l2_evicts << std::endl;
    std::cout << "Core receives stash snoops = " << stashes << std::endl;
    std::cout << "Core rejects stash snoops = " << stashes_rejected << std::endl;
    std::cout << "Core L3 refills = " << count_l3_refills() << std::endl;

    EXPECT_NEAR(l2_data_evictions, stashes, 0.05 * l2_data_evictions);
    EXPECT_NEAR(dummy_read, dummy_mem_req, 0.05 * dummy_read);
    EXPECT_NEAR(dummy_l2_evicts, event_count("sys.soc_0.core_1.cache.refill"), 0);
    EXPECT_EQ(dummy_mem_req <= dummy_read, true);
    EXPECT_NEAR(dummy_mem_req, dummy_l2_evicts, 0.05 * dummy_mem_req);
    EXPECT_NEAR(count_l3_refills(), stashes - stashes_rejected, 0.05 * (stashes - stashes_rejected));
}

/*
 * vim: sw=4:ts=4:et:si:cindent:cino=\:0,(0,g0,t0:
 */
