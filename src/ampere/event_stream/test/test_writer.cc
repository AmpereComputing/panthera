//
// Copyright (c) 2019 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#include <sstream>
#include <utility>
#include <gtest/gtest.h>

#include <google/protobuf/io/zero_copy_stream_impl.h>

#include "ampere/event_stream/event_stream.h"
#include "ampere/event_stream/event_stream.pb.h"
#include "ampere/event_stream/event_stream_proto.h"
#include "ampere/protobuf_utils/protobuf_utils.h"

using namespace std;
using namespace ampere;
using namespace ampere::event_stream;

class EventStreamTest : public ::testing::Test
{
protected:
    void SetUp() override
    {
        fd = open(test_file_name(), O_CREAT | O_RDWR | O_TRUNC, S_IRUSR | S_IWUSR);
        ASSERT_GT(fd, 2);
    }

    void TearDown() override { ASSERT_EQ(0, unlink(this->test_file_name())); }

    std::unique_ptr<EventStream> make_es()
    {
        EventStream::reset_global_definitions();
        EventStream::reset_global_enumerations();
        return std::make_unique<EventStreamProto>(fd);
    }

    static void verify_file_version(google::protobuf::io::ZeroCopyInputStream *stream)
    {
        std::uint32_t magic;
        std::uint32_t es_version;

        ampere::readHeaderFrom(stream, &magic, &es_version);
        ASSERT_EQ(magic, ampere::event_stream::protobuf_magic);
        ASSERT_EQ(es_version, ampere::event_stream::protobuf_es_version);
    }

    static std::unique_ptr<google::protobuf::io::FileInputStream> reopen_test_file()
    {
        int fd = open(test_file_name(), O_RDONLY);
        auto input_stream = std::make_unique<google::protobuf::io::FileInputStream>(fd);
        verify_file_version(input_stream.get());
        return input_stream;
    }

    bool read()
    {
        if (!input_stream)
            input_stream = reopen_test_file();
        return clearAndReadDelimitedFrom(input_stream.get(), &read_record, nullptr);
    }

    int fd{0};
    event_stream_proto::Record read_record;
    std::unique_ptr<google::protobuf::io::FileInputStream> input_stream;

public:
    static const char *test_file_name()
    {
        const ::testing::TestInfo *const test_info = ::testing::UnitTest::GetInstance()->current_test_info();
        return test_info->name();
    }
};

TEST(EventStreamBasic, OpenBadFileNameFails)
{
    ASSERT_THROW(std::make_unique<EventStreamProto>(""), runtime_error);
}

TEST_F(EventStreamTest, OpenByFileName)
{
    ASSERT_NO_THROW(std::make_unique<EventStreamProto>(test_file_name()));
}

TEST_F(EventStreamTest, PostEvent)
{
    {
        auto es = make_es();
        auto event_def = es->define_event("foo", "a foo");
        es->post_event(event_def, 200);
    }

    ASSERT_TRUE(read());
    ASSERT_TRUE(read_record.has_definition());
    EXPECT_EQ("foo", read_record.definition().name());

    ASSERT_TRUE(read());
    ASSERT_TRUE(read_record.has_event());
    EXPECT_EQ(200, read_record.event().time());
}

TEST_F(EventStreamTest, DisabledPostEvent)
{
    {
        auto es = make_es();
        es->disable();
        auto event_def = es->define_event("foo", "a foo");
        es->post_event(event_def, 200);
    }

    ASSERT_TRUE(read());
    ASSERT_TRUE(read_record.has_definition());
    EXPECT_EQ("foo", read_record.definition().name());

    ASSERT_FALSE(read());
}

TEST_F(EventStreamTest, PostEventWithOneValue)
{
    {
        auto es = make_es();
        auto event_def = es->define_event("foo", "a foo");
        auto val_def = es->define_data("bar", "a bar");
        auto event = es->open_event(event_def, 200);
        es->add_int_data(event, val_def, 1234);
        es->close_event(event);
    }

    ASSERT_TRUE(read());
    ASSERT_TRUE(read_record.has_definition());
    EXPECT_EQ("foo", read_record.definition().name());

    ASSERT_TRUE(read());
    ASSERT_TRUE(read_record.has_definition());
    EXPECT_EQ("bar", read_record.definition().name());

    ASSERT_TRUE(read());
    ASSERT_TRUE(read_record.has_event());
    EXPECT_EQ(200, read_record.event().time());
    EXPECT_EQ(1, read_record.event().values_size());
    EXPECT_EQ(1234, read_record.event().values(0).int_value());
}

TEST_F(EventStreamTest, PostEventWithTwoValues)
{
    {
        auto es = make_es();
        auto event_def = es->define_event("foo", "a foo");
        auto bar_def = es->define_data("bar", "a bar");
        auto baz_def = es->define_data("baz", "a foo");
        auto event = es->open_event(event_def, 1950);
        es->add_int_data(event, bar_def, 1234);
        es->add_int_data(event, baz_def, 5678);
        es->close_event(event);
    }

    do
        ASSERT_TRUE(read());
    while (read_record.has_definition());

    ASSERT_TRUE(read_record.has_event());
    EXPECT_EQ(1950, read_record.event().time());
    EXPECT_EQ(2, read_record.event().values_size());
    EXPECT_EQ(1234, read_record.event().values(0).int_value());
    EXPECT_EQ(5678, read_record.event().values(1).int_value());
}

TEST_F(EventStreamTest, PostStringValue)
{
    {
        auto es = make_es();
        auto event_def = es->define_event("foo", "a foo");
        auto val_def = es->define_data("bar", "a bar");
        auto event = es->open_event(event_def, 200);
        es->add_string_data(event, val_def, "hello world");
        es->close_event(event);
    }

    do
        ASSERT_TRUE(read());
    while (read_record.has_definition());

    ASSERT_TRUE(read_record.has_event());
    ASSERT_EQ(1, read_record.event().values_size());
    EXPECT_EQ(read_record.event().values(0).values_case(), event_stream_proto::Value::kStringValue);
    EXPECT_EQ(read_record.event().values(0).string_value(), "hello world");
}

TEST_F(EventStreamTest, PostAllIntegralTypes)
{
    signed char sch = std::numeric_limits<signed char>::max();
    unsigned char uch = std::numeric_limits<unsigned char>::max();
    signed short ssh = std::numeric_limits<signed short>::max();
    unsigned short ush = std::numeric_limits<unsigned short>::max();
    signed int si = std::numeric_limits<signed int>::max();
    unsigned int ui = std::numeric_limits<unsigned int>::max();
    signed long sl = std::numeric_limits<signed long>::max();
    unsigned long ul = std::numeric_limits<unsigned long>::max();
    signed long long sll = std::numeric_limits<signed long long>::max();
    unsigned long long ull = std::numeric_limits<unsigned long long>::max();

    {
        auto es = make_es();
        auto event_def = es->define_event("foo", "a foo");
        auto val_def = es->define_data("bar", "a bar");
        auto event = es->open_event(event_def, 1950);

        es->add_int_data(event, val_def, sch);
        es->add_uint_data(event, val_def, uch);
        es->add_int_data(event, val_def, ssh);
        es->add_uint_data(event, val_def, ush);
        es->add_int_data(event, val_def, si);
        es->add_uint_data(event, val_def, ui);
        es->add_int_data(event, val_def, sl);
        es->add_uint_data(event, val_def, ul);
        es->add_int_data(event, val_def, sll);
        es->add_uint_data(event, val_def, ull);

        es->close_event(event);
    }

    do
        ASSERT_TRUE(read());
    while (read_record.has_definition());

    ASSERT_TRUE(read_record.has_event());
    ASSERT_EQ(10, read_record.event().values_size());
    EXPECT_EQ(read_record.event().values(0).int_value(), sch);
    EXPECT_EQ(read_record.event().values(1).uint_value(), uch);
    EXPECT_EQ(read_record.event().values(2).int_value(), ssh);
    EXPECT_EQ(read_record.event().values(3).uint_value(), ush);
    EXPECT_EQ(read_record.event().values(4).int_value(), si);
    EXPECT_EQ(read_record.event().values(5).uint_value(), ui);
    EXPECT_EQ(read_record.event().values(6).int_value(), sl);
    EXPECT_EQ(read_record.event().values(7).uint_value(), ul);
    EXPECT_EQ(read_record.event().values(8).int_value(), sll);
    EXPECT_EQ(read_record.event().values(9).uint_value(), ull);
}

TEST_F(EventStreamTest, PostParameters)
{
    {
        auto es = make_es();
        es->set_bool_parameter("bool_param", "something", true);
        es->set_int_parameter("int_param", "something", -4);
        es->set_uint_parameter("uint_param", "something", 9);
        es->set_string_parameter("string_param", "something", "hello");
    }

    ASSERT_TRUE(read());
    EXPECT_EQ(read_record.parameter().name(), "bool_param");
    EXPECT_EQ(read_record.parameter().bool_value(), true);

    ASSERT_TRUE(read());
    EXPECT_EQ(read_record.parameter().name(), "int_param");
    EXPECT_EQ(read_record.parameter().int_value(), -4);

    ASSERT_TRUE(read());
    EXPECT_EQ(read_record.parameter().name(), "uint_param");
    EXPECT_EQ(read_record.parameter().uint_value(), 9);

    ASSERT_TRUE(read());
    EXPECT_EQ(read_record.parameter().name(), "string_param");
    EXPECT_EQ(read_record.parameter().string_value(), "hello");
}

TEST_F(EventStreamTest, PostEnumeration)
{
    EnumMappingType values = {{1, "one"}, {2, "two"}, {3, "three"}};
    enum class SomeEnum {
        one = 1,
        two,
        three
    };
    EnumMappingType other_values = {{1, "one"}, {2, "two"}, {3, "three"}, {4, "four"}};
    enum class OtherEnum {
        one = 1,
        two,
        three,
        four
    };

    {
        auto es = make_es();
        es->define_data_with_enum<SomeEnum>("some_data", "something", values);
        es->define_data("another_data", "something");
        es->define_data_with_enum<OtherEnum>("other_data", "something", other_values);
        es->define_data_with_enum<SomeEnum>("some_other_data", "something", values);

        es->start_simulation();
    }

    ASSERT_TRUE(read());
    EXPECT_EQ(read_record.definition().kind(), event_stream_proto::Kind::VALUE);
    EXPECT_EQ(read_record.definition().name(), "some_data");
    EXPECT_EQ(read_record.definition().enumeration_id(), 0);

    ASSERT_TRUE(read());
    EXPECT_EQ(read_record.definition().kind(), event_stream_proto::Kind::VALUE);
    EXPECT_EQ(read_record.definition().name(), "another_data");
    EXPECT_FALSE(read_record.definition().has_enumeration_id());

    ASSERT_TRUE(read());
    EXPECT_EQ(read_record.definition().kind(), event_stream_proto::Kind::VALUE);
    EXPECT_EQ(read_record.definition().name(), "other_data");
    EXPECT_EQ(read_record.definition().enumeration_id(), 1);

    ASSERT_TRUE(read());
    EXPECT_EQ(read_record.definition().kind(), event_stream_proto::Kind::VALUE);
    EXPECT_EQ(read_record.definition().name(), "some_other_data");
    EXPECT_EQ(read_record.definition().enumeration_id(), 0);

    ASSERT_TRUE(read());
    EXPECT_EQ(read_record.enumeration().id(), 0);
    EXPECT_EQ(read_record.enumeration().values().size(), values.size());
    for (unsigned i = 1; i <= values.size(); ++i)
        EXPECT_EQ(read_record.enumeration().values().at(i), values[i]);

    ASSERT_TRUE(read());
    EXPECT_EQ(read_record.enumeration().id(), 1);
    EXPECT_EQ(read_record.enumeration().values().size(), other_values.size());
    for (unsigned i = 1; i <= other_values.size(); ++i)
        EXPECT_EQ(read_record.enumeration().values().at(i), other_values[i]);
}

/*
 * vim: sw=4:ts=4:et:si:cindent:cino=\:0,(0,g0,t0:
 */
