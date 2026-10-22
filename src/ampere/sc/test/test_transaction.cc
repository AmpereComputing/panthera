//
// Copyright (c) 2018-2019 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//

#include <type_traits>
#include "ampere/event_stream/testing/event_stream_mock.h"
#include "ampere/sc/event.h"
#include "ampere/sc/module.h"
#include "ampere/sc/testing/test.h"
#include "ampere/sc/transaction.h"

using namespace ampere::sc;
using namespace ampere::event_stream;

using ::testing::_;

class TransactionTest : public Test
{
protected:
    TransactionTest()
    {
        event_announcer = new MockEventAnnouncer{};
        event_stream = new EventStreamDummy{event_announcer};
        get_environment()->event_stream.reset(event_stream);
    }

    ~TransactionTest() override { delete event_announcer; }

    void SetUp() override { event_stream->reset(); }

    EventStreamDummy* event_stream{nullptr};
    MockEventAnnouncer* event_announcer{nullptr};
};

class TestTransModule : public Module
{
public:
    TestTransModule(sc_core::sc_module_name name) : Module(name) {}

    void make_parent_and_child()
    {
        par = make<Txn>();
        child = make_child<Txn>(par);
    }

    void make_parent_and_clone()
    {
        par = make<Txn>();
        clone = make_clone<Txn>(par);
    }

    Txn par;
    Txn child;
    Txn clone;
};

TEST_F(TransactionTest, MakeTransaction)
{
    EXPECT_CALL(*event_announcer, post_event("start_transaction", 0));
    EXPECT_CALL(*event_announcer, add_uint_data("start_transaction", _, "txid", _));
    EXPECT_CALL(*event_announcer, post_event("end_transaction", 0));
    EXPECT_CALL(*event_announcer, add_uint_data("end_transaction", _, "txid", _));

    {
        trans_make_es<Txn>(event_stream);
    }
}

TEST_F(TransactionTest, MakeChildTransaction)
{
    std::uint64_t tid;

    EXPECT_CALL(*event_announcer, post_event("start_transaction", 0)).Times(2);
    EXPECT_CALL(*event_announcer, add_uint_data("start_transaction", _, "txid", _)).Times(2);
    EXPECT_CALL(*event_announcer, add_uint_data("start_transaction", _, "parent", _))
        .WillOnce(::testing::SaveArg<3>(&tid));
    EXPECT_CALL(*event_announcer, post_event("end_transaction", 0)).Times(2);
    EXPECT_CALL(*event_announcer, add_uint_data("end_transaction", _, "txid", _)).Times(2);

    {
        auto p = trans_make_es<Txn>(event_stream);
        trans_make_child<Txn>(event_stream, p);

        EXPECT_EQ(p->txid().value, tid);
    }
}

TEST_F(TransactionTest, MakeTransactionsFromModule)
{
    std::uint64_t tid;

    EXPECT_CALL(*event_announcer, post_event("start_transaction", 0)).Times(2);
    EXPECT_CALL(*event_announcer, add_uint_data("start_transaction", _, "txid", _)).Times(2);
    EXPECT_CALL(*event_announcer, add_uint_data("start_transaction", _, "parent", _))
        .WillOnce(::testing::SaveArg<3>(&tid));
    EXPECT_CALL(*event_announcer, post_event("end_transaction", 0)).Times(2);
    EXPECT_CALL(*event_announcer, add_uint_data("end_transaction", _, "txid", _)).Times(2);

    {
        TestTransModule mod{"mod"};
        mod.make_parent_and_child();
        EXPECT_EQ(mod.par->txid().value, tid);
    }
}

TEST_F(TransactionTest, MakeCloneTransaction)
{
    EXPECT_CALL(*event_announcer, post_event("start_transaction", 0)).Times(2);
    EXPECT_CALL(*event_announcer, add_uint_data("start_transaction", _, "txid", _)).Times(2);
    EXPECT_CALL(*event_announcer, post_event("end_transaction", 0)).Times(2);
    EXPECT_CALL(*event_announcer, add_uint_data("end_transaction", _, "txid", _)).Times(2);

    auto p = trans_make_es<Txn>(event_stream);
    auto c = trans_make_clone<Txn>(event_stream, p);
    auto q = trans_make_es<Txn>(event_stream);

    EXPECT_EQ(p->txid().value, c->txid().value);
    EXPECT_EQ(q->txid().value, p->txid().value + 1);
}

TEST_F(TransactionTest, MakeCloneTransactionFromModule)
{
    EXPECT_CALL(*event_announcer, post_event("start_transaction", 0));
    EXPECT_CALL(*event_announcer, add_uint_data("start_transaction", _, "txid", _));
    EXPECT_CALL(*event_announcer, post_event("end_transaction", 0));
    EXPECT_CALL(*event_announcer, add_uint_data("end_transaction", _, "txid", _));

    TestTransModule mod{"mod"};
    mod.make_parent_and_clone();
    EXPECT_EQ(mod.par->txid().value, mod.clone->txid().value);
}

struct SomethingObj
{
    int x{0};
};
using Something = transaction_ptr<SomethingObj>;

static_assert(std::is_same_v<const_ptr<Something>, transaction_ptr<const SomethingObj>>);

TEST(TransactionPtrTest, AsConst)
{
    auto something = trans_make<Something>();
    something->x = 1;

    auto const_something = as_const_ptr(something);
    static_assert(std::is_same_v<decltype(const_something), transaction_ptr<const SomethingObj>>);

    something = nullptr;
    EXPECT_EQ(const_something->x, 1);
}

/*
 * vim: sw=4:ts=4:et:si:cindent:cino=\:0,(0,g0,t0:
 */
