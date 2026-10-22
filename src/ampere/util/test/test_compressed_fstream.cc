//
// Copyright (c) 2022 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//

#include <ostream>
#include <filesystem>
#include <sstream>

#include "ampere/util/compressed_fstream.h"

#include "ampere/util/testing/capture.h"
#include <gtest/gtest.h>

namespace ampere::util {

using namespace std::filesystem;

class CompressedFStreamTest : public ::testing::Test
{
protected:
    std::filesystem::path test_file_name(const std::string &extension)
    {
        const ::testing::TestInfo *const test_info = ::testing::UnitTest::GetInstance()->current_test_info();
        path p(test_info->name());
        p.replace_extension(extension);
        return p;
    }
};

TEST_F(CompressedFStreamTest, CompressionFromFilename)
{
    ASSERT_EQ(CompressionType::NONE, compression_from_filename("traces.is"));
    ASSERT_EQ(CompressionType::NONE, compression_from_filename("really_long.log"));
    ASSERT_EQ(CompressionType::ZLIB, compression_from_filename("http.logs.zlib"));
    ASSERT_EQ(CompressionType::ZLIB, compression_from_filename("http.logs.zl"));
    ASSERT_EQ(CompressionType::BZ2, compression_from_filename("package.tar.bz2"));
    ASSERT_EQ(CompressionType::GZIP, compression_from_filename("boring.log.gz"));
    ASSERT_EQ(CompressionType::XZ, compression_from_filename("boring.log.xz"));
}

class CompressedFStreamIOTest : public CompressedFStreamTest, public ::testing::WithParamInterface<std::string>
{
public:
    std::string test_string;
    std::string suffix;
    std::filesystem::path test_path;

    CompressedFStreamIOTest()
        : test_string((GetParam().empty() ? "un" : GetParam() + "_") + "compressed_testing_123_testing"),
          suffix(GetParam().empty() ? "log" : "log." + GetParam()),
          test_path((GetParam().empty() ? "un" : GetParam()) + "compressed." + suffix)
    {
    }

    void TearDown() override { std::filesystem::remove(test_path); }
};

TEST(StreamIOTest, Stdout)
{
    auto capture = CaptureStdout();
    auto out = open_compressed_ostream("-");
    *out << "test" << std::endl;
    out.reset();
    EXPECT_EQ(*capture, "test\n");
}

TEST_P(CompressedFStreamIOTest, ReadWrite)
{
    auto out = open_compressed_ostream(test_path.string().c_str());
    *out << test_string << std::endl;
    out.reset();
    ASSERT_TRUE(std::filesystem::exists(test_path));

    auto in = open_compressed_istream(test_path.string().c_str());
    std::string line;
    *in >> line;
    EXPECT_EQ(line, test_string);
}

INSTANTIATE_TEST_SUITE_P(CompressedFStreamIOTest,
                         CompressedFStreamIOTest,
                         ::testing::Values("", "zlib", "gz", "bz2", "xz"));

} // namespace ampere::util

