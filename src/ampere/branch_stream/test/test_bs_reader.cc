//
// Copyright (c) 2021 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//
#include "ampere/branch_stream/branch_stream.h"
#include <boost/filesystem/operations.hpp>
#include <gtest/gtest.h>

namespace ampere::bs {

using namespace boost::filesystem;

class BranchStreamReaderTest : public ::testing::Test
{
protected:
    std::string testfixture(std::string filename) { return (path(TEST_FIXTURE_DIRECTORY) / filename).string(); }
};

TEST_F(BranchStreamReaderTest, testMissingFile)
{
    ASSERT_THROW(BranchStreamReader bs("doesnotexist.bs"), std::system_error);
}

TEST_F(BranchStreamReaderTest, testMissingXZFile)
{
    ASSERT_THROW(BranchStreamReader bs("doesnotexist.bs.xz"), std::system_error);
}

TEST_F(BranchStreamReaderTest, testRead)
{
    BranchStreamReader bs(testfixture("test.bs"));
    Branch branch;
    ASSERT_TRUE(bs.read(branch));
    ASSERT_TRUE(bs.read(branch));
    ASSERT_TRUE(bs.read(branch));
    ASSERT_FALSE(bs.read(branch));
    ASSERT_EQ(bs.version(), 1);
}

TEST_F(BranchStreamReaderTest, testReadXZBranchesOnly)
{
    BranchStreamReader bs(testfixture("test.bs.xz"));
    Branch branch;
    ASSERT_TRUE(bs.read(branch));
    ASSERT_TRUE(bs.read(branch));
    ASSERT_TRUE(bs.read(branch));
    ASSERT_FALSE(bs.read(branch));
    ASSERT_EQ(bs.version(), 1);
}

TEST_F(BranchStreamReaderTest, testReadXZAllEvents)
{
    BranchStreamReader bs(testfixture("test.bs.xz"));
    Event event;
    ASSERT_TRUE(bs.read(event));
    ASSERT_TRUE(bs.read(event));
    ASSERT_TRUE(bs.read(event));
    ASSERT_TRUE(bs.read(event));
    ASSERT_TRUE(bs.read(event));
    ASSERT_TRUE(bs.read(event));
    ASSERT_FALSE(bs.read(event));
    ASSERT_EQ(bs.version(), 1);
}

} // namespace ampere::bs

/*
 * vim: sw=4:ts=4:et:si:cindent:cino=\:0,(0,g0,t0:
 */
