//
// Copyright (c) 2020 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//
#include "ampere/memory_stream/memory_stream.h"
#include <boost/filesystem/operations.hpp>
#include <gtest/gtest.h>

namespace ampere {
namespace ms {

using namespace boost::filesystem;

class MemoryStreamReaderTest : public ::testing::Test
{
protected:
    std::string testfixture(std::string filename) { return (path(TEST_FIXTURE_DIRECTORY) / filename).string(); }
};

TEST_F(MemoryStreamReaderTest, testMissingFile)
{
    ASSERT_THROW(MemoryStreamReader ms("doesnotexist.ms"), std::system_error);
}

TEST_F(MemoryStreamReaderTest, testMissingXZFile)
{
    ASSERT_THROW(MemoryStreamReader ms("doesnotexist.ms.xz"), std::system_error);
}

TEST_F(MemoryStreamReaderTest, testRead)
{
    MemoryStreamReader ms(testfixture("test.ms"));
    Access access;
    ASSERT_TRUE(ms.read(access));
    ASSERT_TRUE(ms.read(access));
    ASSERT_FALSE(ms.read(access));
    ASSERT_EQ(ms.version(), 1);
}

TEST_F(MemoryStreamReaderTest, testReadXZ)
{
    MemoryStreamReader ms(testfixture("test.ms.xz"));
    Access access;
    ASSERT_TRUE(ms.read(access));
    ASSERT_TRUE(ms.read(access));
    ASSERT_FALSE(ms.read(access));
    ASSERT_EQ(ms.version(), 1);
}

TEST_F(MemoryStreamReaderTest, testReadv2)
{
    MemoryStreamReader ms(testfixture("pb-serialize-m1p2.2444_0.ms.xz"));
    Access access;
    Event event;
    ASSERT_TRUE(ms.read(event));
    ASSERT_TRUE(event.has_control());
    ASSERT_TRUE(Control_Type_START_MEMORY == event.control().type());
    ASSERT_TRUE(ms.read(event));
    ASSERT_TRUE(event.has_access());
    ASSERT_TRUE(ms.read(access));
    ASSERT_TRUE(ms.read(access));
    ASSERT_TRUE(ms.read(access));
    ASSERT_TRUE(ms.read(access));
    ASSERT_TRUE(ms.read(access));
    ASSERT_TRUE(ms.read(access));
    ASSERT_TRUE(ms.read(access));
    ASSERT_TRUE(ms.read(access));
    ASSERT_EQ(ms.version(), 2);
    ASSERT_TRUE(ms.filter_was_unified());
    ASSERT_EQ(ms.filter_dcache_size(), 2097152);
    ASSERT_EQ(ms.filter_icache_size(), 2097152);
}

TEST_F(MemoryStreamReaderTest, testReadv3)
{
    MemoryStreamReader ms(testfixture("nqueens-20.17_0.ms.xz"));
    Access access;
    Event event;
    ASSERT_TRUE(ms.read(event));
    ASSERT_TRUE(event.has_control());
    ASSERT_TRUE(Control_Type_START_MEMORY == event.control().type());
    ASSERT_TRUE(ms.read(event));
    ASSERT_TRUE(event.has_access());
    ASSERT_TRUE(ms.read(access));
    ASSERT_TRUE(ms.read(access));
    ASSERT_TRUE(ms.read(access));
    ASSERT_TRUE(ms.read(access));
    ASSERT_TRUE(ms.read(access));
    ASSERT_TRUE(ms.read(access));
    ASSERT_TRUE(ms.read(access));
    ASSERT_TRUE(ms.read(access));
    ASSERT_EQ(ms.version(), 3);
    ASSERT_FALSE(ms.filter_was_unified());
    ASSERT_EQ(ms.filter_dcache_size(), 65536);
    ASSERT_EQ(ms.filter_icache_size(), 32768);
}

} // namespace ms
} // namespace ampere

/*
 * vim: sw=4:ts=4:et:si:cindent:cino=\:0,(0,g0,t0:
 */
