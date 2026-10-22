//
// Copyright (c) 2019 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//
#include "ampere/instruction_stream/instruction_stream.h"
#include <boost/filesystem/operations.hpp>
#include <gtest/gtest.h>

namespace ampere {
namespace is {

using namespace boost::filesystem;

class InstructionStreamReaderTest : public ::testing::Test
{
protected:
    std::string testfixture(std::string filename) { return (path(TEST_FIXTURE_DIRECTORY) / filename).string(); }
};

TEST_F(InstructionStreamReaderTest, testMissingFile)
{
    ASSERT_THROW(InstructionStreamReader is("doesnotexist.is"), std::system_error);
}

TEST_F(InstructionStreamReaderTest, testMissingXZFile)
{
    ASSERT_THROW(InstructionStreamReader is("doesnotexist.is.xz"), std::system_error);
}

TEST_F(InstructionStreamReaderTest, testRead)
{
    InstructionStreamReader is(testfixture("test.is"));
    Instruction instruction;
    ASSERT_TRUE(is.read(instruction));
    ASSERT_TRUE(is.read(instruction));
    ASSERT_FALSE(is.read(instruction));
    ASSERT_EQ(is.version(), 1);

    // are the feature bits all false, since we are reading version 1?
    ASSERT_FALSE(is.features().divide_sqrt_registers());
    ASSERT_FALSE(is.features().cache_maintenance_registers());
    ASSERT_FALSE(is.features().memory_translation());
    ASSERT_FALSE(is.features().memory_values());
}

TEST_F(InstructionStreamReaderTest, testReadXZ)
{
    InstructionStreamReader is(testfixture("test.is.xz"));
    Instruction instruction;
    ASSERT_TRUE(is.read(instruction));
    ASSERT_TRUE(is.read(instruction));
    ASSERT_FALSE(is.read(instruction));
    ASSERT_EQ(is.version(), 1);

    // are the feature bits all false, since we are reading version 1?
    ASSERT_FALSE(is.features().divide_sqrt_registers());
    ASSERT_FALSE(is.features().cache_maintenance_registers());
    ASSERT_FALSE(is.features().memory_translation());
    ASSERT_FALSE(is.features().memory_values());
}

} // namespace is
} // namespace ampere

/*
 * vim: sw=4:ts=4:et:si:cindent:cino=\:0,(0,g0,t0:
 */
