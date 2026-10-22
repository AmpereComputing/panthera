//
// Copyright (c) 2018-2019 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//

#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <string>
#include <utility>

#include "ampere/event_stream/event_stream.h"
#include "ampere/event_stream/event_stream_sink.h"
#include "ampere/event_stream/testing/event_stream_mock.h"
#include "ampere/sc/event.h"
#include "ampere/sc/module.h"
#include "ampere/util/enum.h"
#include "ampere/sc/testing/test.h"
#include "ampere/sc/transaction.h"
#include "ampere/sc/testing/clock_dummy.h"

make_enum(EnumForData,
    A,
    B,
    C
);

namespace ampere::sc {

using ::testing::_;
using ::testing::AtLeast;
using ::testing::DoDefault;
using ::testing::EndsWith;
using ::testing::InvokeWithoutArgs;
using ::testing::Return;

using namespace event_stream;

struct EventTestModule : Module
{
    explicit EventTestModule(sc_core::sc_module_name name) : Module(name) {}
    Event event{"event", "an event"};
};

template<typename T>
struct EventDataTestModule : EventTestModule
{
    template<typename... Args>
    explicit EventDataTestModule(sc_core::sc_module_name name, Args&&... args)
        : EventTestModule(name), data("data", "some data", std::forward<Args>(args)...)
    {
    }
    EventData<T> data;
};

struct EventDefinitionsTest : public Test
{
    EventDefinitionsTest()
    {
        event_stream = new MockEventStream(true);
        get_environment()->event_stream.reset(event_stream);
    }

    void TearDown() override { event_stream->do_catch_up(); }

    MockEventStream* event_stream{nullptr};
};

TEST_F(EventDefinitionsTest, Event)
{
    EXPECT_CALL(*event_stream, finalize_event_definition(_, "top.event", "an event"));
    EventTestModule top("top");
}

TEST_F(EventDefinitionsTest, EventData)
{
    EXPECT_CALL(*event_stream, finalize_event_definition(_, "top.event", "an event"));
    EXPECT_CALL(*event_stream, finalize_data_definition(_, "top.data", "some data"));
    EventDataTestModule<int> top("top");
}

TEST_F(EventDefinitionsTest, EventDataEnum)
{
    EXPECT_CALL(*event_stream, finalize_event_definition(_, "top.event", "an event"));
    EXPECT_CALL(*event_stream, finalize_data_definition(_, "top.data", "some data", 0));
    EXPECT_CALL(*event_stream,
                finalize_enum_definition(0,
                                         EnumMappingType({
                                             {0, "A"},
                                             {1, "B"},
                                             {2, "C"},
                                         })));
    EventDataTestModule<EnumForData> top("top");
}

TEST_F(EventDefinitionsTest, EventDataStaticEnum)
{
    EXPECT_CALL(*event_stream, finalize_event_definition(_, "top.event", "an event"));
    EXPECT_CALL(*event_stream, finalize_data_definition(_, "top.data", "some data", 0));
    EXPECT_CALL(*event_stream,
                finalize_enum_definition(0,
                                         EnumMappingType({
                                             {0, "X"},
                                             {1, "Y"},
                                             {2, "Z"},
                                         })));

    define_non_constant_enum(MyDynamicEnum);
    EventDataTestModule<MyDynamicEnum> top("top");

    auto builder = MyDynamicEnum::create();
    builder.add("X");
    builder.add("Y");
    builder.add("Z");

    top.data.finalize(std::move(builder));
}

TEST_F(EventDefinitionsTest, EventDataAnonyomousEnum)
{
    EXPECT_CALL(*event_stream, finalize_event_definition(_, "top.event", "an event"));
    EXPECT_CALL(*event_stream, finalize_data_definition(_, "top.data", "some data", 0));
    EXPECT_CALL(*event_stream,
                finalize_enum_definition(0,
                                         EnumMappingType({
                                             {0, "O"},
                                             {1, "M"},
                                             {2, "G"},
                                         })));

    util::DynamicEnumeration<> builder;
    builder.add("O");
    builder.add("M");
    builder.add("G");
    auto my_enum = builder.freeze();

    EventDataTestModule<decltype(my_enum)> top("top", my_enum);
}

class EventTest : public Test
{
protected:
    EventTest()
    {
        event_announcer = new MockEventAnnouncer{};
        event_stream = new EventStreamDummy{event_announcer};
        get_environment()->event_stream.reset(event_stream);
    }

    ~EventTest() override { delete event_announcer; }

    void SetUp() override
    {
        event_stream->reset();
        EXPECT_CALL(*event_announcer, post_event("start_transaction", _)).WillRepeatedly(Return());
        EXPECT_CALL(*event_announcer, post_event("end_transaction", _)).WillRepeatedly(Return());
        module = new EventTestModule("a");
    }

    void TearDown() override { delete module; }

    MockEventAnnouncer* event_announcer{nullptr};
    EventStreamDummy* event_stream{nullptr};
    EventTestModule* module{nullptr};
};

TEST_F(EventTest, RecordEvent)
{
    EXPECT_CALL(*event_announcer, post_event("a.event", 0));
    module->event();
}

TEST_F(EventTest, RecordEventAtTime)
{
    EXPECT_CALL(*event_announcer, post_event("a.event", 100));
    module->event.at(sc_core::sc_time{100, sc_core::SC_PS});
}

template<typename T>
class EventTestWithDataBase : public TestWithParam<T>
{
protected:
    using EventTestModule = EventDataTestModule<T>;

    EventTestWithDataBase()
    {
        event_announcer = new MockEventAnnouncer{};
        event_stream = new EventStreamDummy{event_announcer};
        this->get_environment()->event_stream.reset(event_stream);
    }

    ~EventTestWithDataBase() override { delete event_announcer; }

    void TearDown() override { delete module; }

    MockEventAnnouncer* event_announcer{nullptr};
    EventStreamDummy* event_stream{nullptr};
    EventTestModule* module{nullptr};
};

template<typename T>
class EventTestWithData : public EventTestWithDataBase<T>
{
protected:
    void SetUp() override
    {
        this->event_stream->reset();
        EXPECT_CALL(*this->event_announcer, post_event("start_transaction", _)).WillRepeatedly(Return());
        EXPECT_CALL(*this->event_announcer, post_event("end_transaction", _)).WillRepeatedly(Return());
        this->module = new typename EventTestWithDataBase<T>::EventTestModule("a");
    };

    void do_integer_test(const T& param, bool is_unsigned = false)
    {
        EXPECT_CALL(*this->event_announcer, post_event("a.event", _));

        if (is_unsigned)
            EXPECT_CALL(*this->event_announcer, add_uint_data("a.event", _, "a.data", param));
        else
            EXPECT_CALL(*this->event_announcer, add_int_data("a.event", _, "a.data", param, _));

        this->module->event(this->module->data(param));
    }

    void do_string_test(const T& param)
    {
        EXPECT_CALL(*this->event_announcer, post_event("a.event", _));
        EXPECT_CALL(*this->event_announcer, add_string_data("a.event", _, "a.data", param));

        this->module->event(this->module->data(param));
    }
};

using EventTestWithSignedChar = EventTestWithData<signed char>;
TEST_P(EventTestWithSignedChar, RecordEventWithSignedChar)
{
    do_integer_test(GetParam());
}
INSTANTIATE_TEST_SUITE_P(RecordSignedChar,
                         EventTestWithSignedChar,
                         ::testing::Values(std::numeric_limits<signed char>::min(),
                                           std::numeric_limits<signed char>::max()));

using EventTestWithUnsignedChar = EventTestWithData<unsigned char>;
TEST_P(EventTestWithUnsignedChar, RecordEventWithUnsignedChar)
{
    do_integer_test(GetParam(), true);
}
INSTANTIATE_TEST_SUITE_P(RecordUnsignedChar,
                         EventTestWithUnsignedChar,
                         ::testing::Values(std::numeric_limits<unsigned char>::min(),
                                           std::numeric_limits<unsigned char>::max()));

using EventTestWithShort = EventTestWithData<short>;
TEST_P(EventTestWithShort, RecordEventWithShort)
{
    do_integer_test(GetParam());
}
INSTANTIATE_TEST_SUITE_P(RecordShort,
                         EventTestWithShort,
                         ::testing::Values(std::numeric_limits<short>::min(), std::numeric_limits<short>::max()));

using EventTestWithUnsignedShort = EventTestWithData<unsigned char>;
TEST_P(EventTestWithUnsignedShort, RecordEventWithUnsignedShort)
{
    do_integer_test(GetParam(), true);
}
INSTANTIATE_TEST_SUITE_P(RecordUnsignedShort,
                         EventTestWithUnsignedShort,
                         ::testing::Values(std::numeric_limits<unsigned short>::min(),
                                           std::numeric_limits<unsigned short>::max()));

using EventTestWithInt = EventTestWithData<int>;
TEST_P(EventTestWithInt, RecordEventWithInt)
{
    do_integer_test(GetParam());
}
INSTANTIATE_TEST_SUITE_P(RecordInt,
                         EventTestWithInt,
                         ::testing::Values(std::numeric_limits<int>::min(), std::numeric_limits<int>::max()));

using EventTestWithUnsignedInt = EventTestWithData<unsigned int>;
TEST_P(EventTestWithUnsignedInt, RecordEventWithUnsignedInt)
{
    do_integer_test(GetParam(), true);
}
INSTANTIATE_TEST_SUITE_P(RecordUnsignedInt,
                         EventTestWithUnsignedInt,
                         ::testing::Values(std::numeric_limits<unsigned int>::min(),
                                           std::numeric_limits<unsigned int>::max()));

using EventTestWithLong = EventTestWithData<long>;
TEST_P(EventTestWithLong, RecordEventWithLong)
{
    do_integer_test(GetParam());
}
INSTANTIATE_TEST_SUITE_P(RecordLong,
                         EventTestWithLong,
                         ::testing::Values(std::numeric_limits<long>::min(), std::numeric_limits<long>::max()));

using EventTestWithUnsignedLong = EventTestWithData<unsigned long>;
TEST_P(EventTestWithUnsignedLong, RecordEventWithUnsignedLong)
{
    do_integer_test(GetParam(), true);
}
INSTANTIATE_TEST_SUITE_P(RecordUnsignedLong,
                         EventTestWithUnsignedLong,
                         ::testing::Values(std::numeric_limits<unsigned long>::min(),
                                           std::numeric_limits<unsigned long>::max()));

using EventTestWithLongLong = EventTestWithData<long long>;
TEST_P(EventTestWithLongLong, RecordEventWithLongLong)
{
    do_integer_test(GetParam());
}
INSTANTIATE_TEST_SUITE_P(RecordLongLong,
                         EventTestWithLongLong,
                         ::testing::Values(std::numeric_limits<long long>::min(),
                                           std::numeric_limits<long long>::max()));

using EventTestWithUnsignedLongLong = EventTestWithData<unsigned long long>;
TEST_P(EventTestWithUnsignedLongLong, RecordEventWithUnsignedLongLong)
{
    do_integer_test(GetParam(), true);
}
INSTANTIATE_TEST_SUITE_P(RecordUnsignedLongLong,
                         EventTestWithUnsignedLongLong,
                         ::testing::Values(std::numeric_limits<unsigned long long>::min(),
                                           std::numeric_limits<unsigned long long>::max()));

using EventTestWithCString = EventTestWithData<const char*>;
TEST_P(EventTestWithCString, RecordEventWithCString)
{
    do_string_test(GetParam());
}
INSTANTIATE_TEST_SUITE_P(RecordCString, EventTestWithCString, ::testing::Values("hello world"));

using EventTestWithString = EventTestWithData<std::string>;
TEST_P(EventTestWithString, RecordEventWithString)
{
    do_string_test(GetParam());
}
INSTANTIATE_TEST_SUITE_P(RecordString, EventTestWithString, ::testing::Values(std::string{"hello world"}));

using EventTestWithEnum = EventTestWithData<EnumForData>;
TEST_F(EventTestWithEnum, RecordEventWithEnum)
{
    EXPECT_CALL(*event_announcer, post_event("a.event", _));
    EXPECT_CALL(*event_announcer, add_int_data("a.event", _, "a.data", static_cast<int>(EnumForData::B), _));
    module->event(module->data(EnumForData::B));
}

define_non_constant_enum(StaticEnumForTest);
struct EventTestWithStaticEnum : EventTestWithData<StaticEnumForTest>
{
    void SetUp() override
    {
        EventTestWithData<StaticEnumForTest>::SetUp();

        auto builder = StaticEnumForTest::create();
        builder.add("A");
        builder.add("B");
        builder.add("C");
        module->data.finalize(std::move(builder));
    }
};
TEST_F(EventTestWithStaticEnum, RecordEventWithStaticEnumValue)
{
    EXPECT_CALL(*event_announcer, post_event("a.event", _));
    EXPECT_CALL(*event_announcer, add_int_data("a.event", _, "a.data", 1, _));
    module->event(module->data(StaticEnumForTest("B"sv)));
}

TEST_F(EventTestWithStaticEnum, RecordEventWithStaticEnumName)
{
    EXPECT_CALL(*event_announcer, post_event("a.event", _));
    EXPECT_CALL(*event_announcer, add_int_data("a.event", _, "a.data", 1, _));
    module->event(module->data("B"));
}

TEST_F(EventTestWithStaticEnum, RecordEventWithStaticEnumInteger)
{
    EXPECT_CALL(*event_announcer, post_event("a.event", _));
    EXPECT_CALL(*event_announcer, add_int_data("a.event", _, "a.data", 2, _));
    module->event(module->data(2));
}

struct EventTestWithFrozenEnum : EventTestWithDataBase<util::FrozenEnumeration<>>
{
    util::FrozenEnumeration<> frozen;

    void SetUp() override
    {
        event_stream->reset();
        EXPECT_CALL(*event_announcer, post_event("start_transaction", _)).WillRepeatedly(Return());
        EXPECT_CALL(*event_announcer, post_event("end_transaction", _)).WillRepeatedly(Return());

        auto builder = util::FrozenEnumeration<>::create();
        builder.add("X");
        builder.add("Y");
        builder.add("Z");
        frozen = builder.freeze();
        module = new EventTestModule("a", frozen);
    }
};
TEST_F(EventTestWithFrozenEnum, RecordEventWithFrozenEnumValue)
{
    EXPECT_CALL(*event_announcer, post_event("a.event", _));
    EXPECT_CALL(*event_announcer, add_int_data("a.event", _, "a.data", 1, _));
    module->event(module->data(frozen.at("Y")));
}

TEST_F(EventTestWithFrozenEnum, RecordEventWithFrozenEnumInteger)
{
    EXPECT_CALL(*event_announcer, post_event("a.event", _));
    EXPECT_CALL(*event_announcer, add_int_data("a.event", _, "a.data", 2, _));
    module->event(module->data(2));
}

class EventTestWithMultipleData : public Test
{
protected:
    class EventTestModule : public Module
    {
    public:
        EventTestModule(sc_core::sc_module_name name) : Module(name) {}

        Event event{"event", "an event"};
        EventData<int> idata{"idata", "some integer data"};
        EventData<std::string> sdata{"sdata", "some string data"};
    };

    EventTestWithMultipleData()
    {
        event_announcer = new MockEventAnnouncer{};
        event_stream = new EventStreamDummy{event_announcer};
        get_environment()->event_stream.reset(event_stream);
    }

    ~EventTestWithMultipleData() override { delete event_announcer; }

    void SetUp() override
    {
        event_stream->reset();
        EXPECT_CALL(*event_announcer, post_event("start_transaction", _)).WillRepeatedly(Return());
        EXPECT_CALL(*event_announcer, post_event("end_transaction", _)).WillRepeatedly(Return());
        module = new EventTestModule("a");
    }

    void TearDown() override { delete module; }

    MockEventAnnouncer* event_announcer{nullptr};
    EventStreamDummy* event_stream{nullptr};
    EventTestModule* module{nullptr};
};

template<typename T>
struct DataWrapper
{
    T value;
    DataWrapper(T value) : value(value) {}
};

using Boolean = DataWrapper<bool>;
AMPERE_EVENT_DATA_DEFN(boolean_data, "Boolean data");
AMPERE_EVENT_DATA_FUNC(Boolean x, boolean_data, x.value);

using Integer = DataWrapper<int>;
AMPERE_EVENT_DATA_DEFN(integer_data, "Integer data");
AMPERE_EVENT_DATA_FUNC(Integer x, integer_data, x.value);

TEST_F(EventTestWithMultipleData, RecordWithMultipleData)
{
    EXPECT_CALL(*event_announcer, post_event("a.event", _));
    EXPECT_CALL(*event_announcer, add_int_data("a.event", _, "a.idata", 1000001, _));
    EXPECT_CALL(*event_announcer, add_string_data("a.event", _, "a.sdata", "red roses"));

    module->event(module->idata(1000001), module->sdata("red roses"));
}

TEST_F(EventTestWithMultipleData, RecordWithOptionalData)
{
    EXPECT_CALL(*event_announcer, post_event("a.event", _));
    EXPECT_CALL(*event_announcer, add_string_data("a.event", _, "a.sdata", "red roses"));

    module->event(module->idata.if_set(0), module->sdata("red roses"));
}

TEST_F(EventTestWithMultipleData, RecordWithPredicatedOptionalData)
{
    EXPECT_CALL(*event_announcer, post_event("a.event", _));
    EXPECT_CALL(*event_announcer, add_int_data("a.event", _, "a.idata", 1000001, _));

    module->event(module->idata(1000001), module->sdata.if_set("red roses", false));
}

TEST_F(EventTestWithMultipleData, RecordWithMultipleOptionalData)
{
    EXPECT_CALL(*event_announcer, post_event("a.event", _));
    EXPECT_CALL(*event_announcer, add_int_data("a.event", _, "a.idata", 1000001, _));
    EXPECT_CALL(*event_announcer, add_string_data("a.event", _, "a.sdata", "red roses"));

    module->event(module->idata.if_set(1000001), module->sdata.if_set("red roses", true));
}

TEST_F(EventTestWithMultipleData, RecordWithToEventArg)
{
    EXPECT_CALL(*event_announcer, post_event("a.event", _));
    EXPECT_CALL(*event_announcer, add_uint_data("a.event", _, "boolean_data", 1U));

    module->event(Boolean(true));
}

TEST_F(EventTestWithMultipleData, RecordWithPairEventArg)
{
    EXPECT_CALL(*event_announcer, post_event("a.event", _));
    EXPECT_CALL(*event_announcer, add_uint_data("a.event", _, "boolean_data", 1U));
    EXPECT_CALL(*event_announcer, add_int_data("a.event", _, "integer_data", 5, _));

    module->event(std::make_pair(Boolean(true), Integer(5)));
}

TEST_F(EventTest, RecordEventWithTransaction)
{
    EXPECT_CALL(*event_announcer, post_event("start_transaction", _));
    EXPECT_CALL(*event_announcer, add_uint_data("start_transaction", _, _, _));
    EXPECT_CALL(*event_announcer, post_event("end_transaction", _));
    EXPECT_CALL(*event_announcer, add_uint_data("end_transaction", _, _, _));
    EXPECT_CALL(*event_announcer, post_event("a.event", _));
    EXPECT_CALL(*event_announcer, add_uint_data("a.event", _, "txid", _));

    Txn t = trans_make_es<Txn>(event_stream);
    module->event(t);
}

class EventSpeedTest : public ::testing::Test
{
protected:
    class EventTestModule : public Module
    {
    public:
        EventTestModule(sc_core::sc_module_name name) : Module(name) {}

        using Module::run;

        void run(int count)
        {
            auto int_data = idata(42);
            auto str_data = sdata("hello world");

            for (int i = 0; i < count; i++)
                event(int_data, str_data);
        }

        Event event{"event", "an event"};
        EventData<int> idata{"idata", "some integer data"};
        EventData<std::string> sdata{"sdata", "some string data"};
    };

    EventSpeedTest() { event_stream = new EventStreamSink{}; }

    ~EventSpeedTest() override { delete event_stream; }

    void SetUp() override { module = new EventTestModule("a"); }

    void TearDown() override { delete module; }

    ClockDummy clock;
    Environment env{"system"};
    EventStreamSink* event_stream{nullptr};
    EventTestModule* module{nullptr};
};

TEST_F(EventSpeedTest, testEventSpeed)
{
    module->run(10000000);
}

} // namespace ampere::sc

/*
 * vim: sw=4:ts=4:et:si:cindent:cino=\:0,(0,g0,t0:
 */
