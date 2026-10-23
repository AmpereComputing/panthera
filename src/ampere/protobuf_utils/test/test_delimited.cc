//
// Copyright (c) 2019 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sstream>
#include <gtest/gtest.h>

#include <google/protobuf/io/zero_copy_stream_impl.h>

#include "ampere/protobuf_utils/example.pb.h"
#include "ampere/protobuf_utils/protobuf_utils.h"

using namespace std;
using namespace ampere;

class DelimitedTest : public ::testing::Test
{
protected:
    void SetUp() override
    {
        int fd = open("example.es", O_CREAT | O_WRONLY | O_TRUNC, S_IRUSR | S_IWUSR);
        ASSERT_GT(fd, 2);
        output_stream = new google::protobuf::io::FileOutputStream(fd);
    }
    void TearDown() override
    {
        ASSERT_EQ(0, unlink("example.es"));
        delete output_stream;
    }
    google::protobuf::io::FileOutputStream* output_stream;
};

TEST_F(DelimitedTest, testWriteDelimited)
{
    example::Hello hello;
    hello.set_id(1);
    hello.set_message("world");
    ASSERT_TRUE(writeDelimitedTo(hello, output_stream));
    ASSERT_TRUE(output_stream->Close());
}

TEST_F(DelimitedTest, testReadDelimited)
{
    example::Hello hello;
    hello.set_id(1);
    hello.set_message("hello");
    ASSERT_TRUE(writeDelimitedTo(hello, output_stream));
    hello.set_id(2);
    hello.set_message("world");
    ASSERT_TRUE(writeDelimitedTo(hello, output_stream));
    ASSERT_TRUE(output_stream->Close());

    int fd = open("example.es", O_RDONLY);
    google::protobuf::io::FileInputStream* input_stream;
    input_stream = new google::protobuf::io::FileInputStream(fd);

    example::Hello read_hello;
    ASSERT_TRUE(readDelimitedFrom(input_stream, &read_hello, nullptr));
    ASSERT_EQ("hello", read_hello.message());

    ASSERT_TRUE(readDelimitedFrom(input_stream, &read_hello, nullptr));
    ASSERT_EQ("world", read_hello.message());
    delete input_stream;
}

/*
 * vim: sw=4:ts=4:et:si:cindent:cino=\:0,(0,g0,t0:
 */
