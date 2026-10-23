//
// Copyright (c) 2020 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#include <gtest/gtest.h>

#include "ampere/memory_stream/memory_stream.h"
#include "ampere/protobuf_utils/protobuf_utils.h"

#include <google/protobuf/io/zero_copy_stream_impl.h>

namespace ampere {

using namespace boost::filesystem;

class MemoryStreamWriterTest : public ::testing::Test
{
protected:
    path test_file_name(const char* extension)
    {
        const ::testing::TestInfo* const test_info = ::testing::UnitTest::GetInstance()->current_test_info();
        path p(test_info->name());
        p.replace_extension(extension);
        return p;
    }
    google::protobuf::io::FileOutputStream* output_stream;

    void TearDown() override
    {
        remove(test_file_name("ms"));
        remove(test_file_name("ms.xz"));
    }
};

TEST_F(MemoryStreamWriterTest, testWrite)
{
    ms::MemoryStreamWriter* writer = new ms::MemoryStreamWriter(this->test_file_name("ms").string().c_str(), 1024, 512);

    ms::Control control;
    control.set_type(ms::Control_Type_START_MEMORY);
    ASSERT_TRUE(writer->write(control));

    ms::Access access;
    access.set_type(ms::Access_Type_READ);
    access.set_source(ms::Access_Source_INSTRUCTION);
    access.set_pa(0xdeadbeef);
    access.set_size(8);
    access.set_offset(3);
    ASSERT_TRUE(writer->write(access));

    access.set_type(ms::Access_Type_WRITE);
    access.set_source(ms::Access_Source_DATA);
    access.set_pa(0xdeadc0de);
    access.set_size(4);
    access.set_offset(98);
    access.set_agent_id(3);
    access.set_pc(0x7fffff8304);
    ASSERT_TRUE(writer->write(access));

    delete writer;

    int fd = open(this->test_file_name("ms").string().c_str(), O_RDONLY);
    ASSERT_GT(fd, 2);

    google::protobuf::io::FileInputStream* input_stream;
    input_stream = new google::protobuf::io::FileInputStream(fd);

    uint32_t magic, ms_version;
    readHeaderFrom(input_stream, &magic, &ms_version);
    ASSERT_EQ(magic, 0x534D4250); // 0x50(P) 0x42(B) 0x4D(M) 0x53(S));
    ASSERT_EQ(ms_version, 4);
    ASSERT_TRUE(input_stream->Close());

    ms::MemoryStreamReader msreader(this->test_file_name("ms").string());
    ASSERT_EQ(msreader.filter_dcache_size(), 1024);
    ASSERT_EQ(msreader.filter_icache_size(), 512);
    ASSERT_FALSE(msreader.filter_was_unified());

    ms::Event event;
    ASSERT_TRUE(msreader.read(event));
    ASSERT_TRUE(event.has_control());
    ASSERT_TRUE(ms::Control_Type_START_MEMORY == event.control().type());
    event.Clear();

    ms::Access access1;
    ASSERT_TRUE(msreader.read(access1));
    ASSERT_EQ(ms::Access_Type_READ, access1.type());
    ASSERT_EQ(ms::Access_Source_INSTRUCTION, access1.source());
    ASSERT_EQ(0xdeadbeef, access1.pa());
    ASSERT_EQ(8, access1.size());
    ASSERT_EQ(3, access1.offset());
    ASSERT_EQ(0, access1.agent_id());
    ASSERT_EQ(0, access1.pc());

    ms::Access access2;
    ASSERT_TRUE(msreader.read(access2));
    ASSERT_EQ(ms::Access_Type_WRITE, access2.type());
    ASSERT_EQ(ms::Access_Source_DATA, access2.source());
    ASSERT_EQ(0xdeadc0de, access2.pa());
    ASSERT_EQ(4, access2.size());
    ASSERT_EQ(98, access2.offset());
    ASSERT_EQ(3, access2.agent_id());
    ASSERT_EQ(0x7fffff8304, access2.pc());
    ASSERT_FALSE(msreader.read(access2));

    delete input_stream;
}

TEST_F(MemoryStreamWriterTest, testWriteXz)
{
    ms::MemoryStreamWriter* writer =
        new ms::MemoryStreamWriter(this->test_file_name("ms.xz").string().c_str(), 1024, 1024, true);

    ms::Access access;
    access.set_type(ms::Access_Type_READ);
    access.set_pa(0xdeadbeef);
    access.set_size(8);
    access.set_offset(3);
    ASSERT_TRUE(writer->write(access));

    access.set_type(ms::Access_Type_WRITE);
    access.set_pa(0xdeadc0de);
    access.set_size(4);
    access.set_offset(98);
    access.set_agent_id(3);
    access.set_pc(0x7fffff8304);
    ASSERT_TRUE(writer->write(access));

    delete writer;

    ms::MemoryStreamReader* reader = new ms::MemoryStreamReader(test_file_name("ms.xz"));

    ASSERT_EQ(reader->version(), 4);
    ASSERT_EQ(reader->filter_dcache_size(), 1024);
    ASSERT_EQ(reader->filter_icache_size(), 1024);
    ASSERT_TRUE(reader->filter_was_unified());

    ms::Access access1;
    ASSERT_TRUE(reader->read(access1));
    ASSERT_EQ(ms::Access_Type_READ, access1.type());
    ASSERT_EQ(0xdeadbeef, access1.pa());
    ASSERT_EQ(8, access1.size());
    ASSERT_EQ(3, access1.offset());
    ASSERT_EQ(0, access1.agent_id());
    ASSERT_EQ(0, access1.pc());

    ms::Access access2;
    ASSERT_TRUE(reader->read(access2));
    ASSERT_EQ(ms::Access_Type_WRITE, access2.type());
    ASSERT_EQ(0xdeadc0de, access2.pa());
    ASSERT_EQ(4, access2.size());
    ASSERT_EQ(98, access2.offset());
    ASSERT_EQ(3, access2.agent_id());
    ASSERT_EQ(0x7fffff8304, access2.pc());

    delete reader;

    ASSERT_EQ(0, unlink(this->test_file_name("ms.xz").string().c_str()));
}

} // namespace ampere

/*
 * vim: sw=4:ts=4:et:si:cindent:cino=\:0,(0,g0,t0:
 */
