//
// Copyright (c) 2018-2019 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//

#include <gtest/gtest.h>
#include <fcntl.h>
#include <boost/filesystem/operations.hpp>
#include "ampere/event_stream/event_stream_reader.h"
#include "ampere/event_stream/testing/build_event_stream.h"

using boost::filesystem::path;

namespace ampere::event_stream {

class EventStreamReaderTest : public ::testing::Test
{
protected:
    std::string testfixture(std::string filename) { return (path(TEST_FIXTURE_DIRECTORY) / filename).string(); }

    std::string build_es(const std::string& name) { return ampere::event_stream::testing::build_es(testfixture(name)); }
};

TEST_F(EventStreamReaderTest, TestMissingFile)
{
    ASSERT_ANY_THROW(EventStreamReader reader("missing.es"));
}

TEST_F(EventStreamReaderTest, TestFileDescriptorConstructor)
{
    auto filename = build_es("four_events_with_data.in");
    int fd = open(filename.c_str(), O_RDONLY);
    ASSERT_GT(fd, 2);
    EventStreamReader reader(fd);
    reader.include("foo");

    for (auto i = 0; i < 2; i++)
        ASSERT_TRUE(reader.read_event());

    ASSERT_FALSE(reader.read_event());
}

TEST_F(EventStreamReaderTest, TestReadFourEventsWithData)
{
    auto filename = build_es("four_events_with_data.in");
    EventStreamReader reader(filename);

    auto record = reader.read();
    ASSERT_TRUE(record->has_definition());

    ASSERT_TRUE(reader.read()->has_definition());
    ASSERT_TRUE(reader.read()->has_definition());
    ASSERT_TRUE(reader.read()->has_definition());
    ASSERT_TRUE(reader.read()->has_control());

    ASSERT_TRUE(reader.read(*record));
    ASSERT_TRUE(record->has_event());
    auto event1 = record->event();

    ASSERT_EQ("foo", reader.name(event1));
    ASSERT_EQ(2, event1.values_size());
    ASSERT_EQ("bar", reader.name(event1.values(0)));
    ASSERT_EQ(1234, event1.values(0).int_value());
    ASSERT_EQ("baz", reader.name(event1.values(1)));
    ASSERT_EQ(5678, event1.values(1).int_value());

    ASSERT_TRUE(reader.read()->has_event());
    ASSERT_TRUE(reader.read()->has_event());
    ASSERT_TRUE(reader.read()->has_event());
    ASSERT_FALSE(reader.read()->has_event());
    ASSERT_FALSE(reader.read()->has_definition());
}

TEST_F(EventStreamReaderTest, TestReadTillEnd)
{
    EventStreamReader reader(testfixture("four_events_with_data.es"));
    unsigned records = 0;
    while (reader.read()) {
        ++records;
    }
    ASSERT_EQ(records, 9U);
}

} // namespace ampere::event_stream

