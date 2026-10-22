//
// Copyright (c) 2019 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//
#pragma once

#include <boost/smart_ptr/local_shared_ptr.hpp>
#include <boost/smart_ptr/make_local_shared.hpp>
#include <cstdint>
#include <memory>
#include <type_traits>
#include <string>

#include "ampere/sc/event.h"

namespace ampere::sc {

struct TransactionId
{
    std::uint64_t value;
};

AMPERE_EVENT_DATA_FUNC(TransactionId t, txid, t.value);

template<typename T>
using transaction_ptr = boost::local_shared_ptr<T>;

template<typename T>
struct ptr_transformer
{
    using type = std::remove_const_t<T>;
    using const_type = const std::remove_const_t<T>;
};

template<typename T>
struct ptr_transformer<transaction_ptr<T>>
{
    using element_type = T;
    using type = transaction_ptr<std::remove_const_t<T>>;
    using const_type = transaction_ptr<const std::remove_const_t<T>>;
};

template<typename T>
struct ptr_transformer<std::shared_ptr<T>>
{
    using element_type = T;
    using type = std::shared_ptr<std::remove_const_t<T>>;
    using const_type = std::shared_ptr<const std::remove_const_t<T>>;
};

template<typename T>
using const_ptr = typename ptr_transformer<T>::const_type;

template<typename T>
auto as_const_ptr(const T& ptr) -> typename ptr_transformer<T>::const_type
{
    return ptr;
}

class Transaction;

using Txn = transaction_ptr<Transaction>;

void post_start_transaction(event_stream::EventStream*, Transaction&);
void post_start_transaction_with_parent(event_stream::EventStream*, Transaction&, Transaction&);

class Transaction
{
public:
    Transaction();
    virtual ~Transaction();

    TransactionId txid() const { return _txid; }

protected:
    TransactionId _txid;
    Txn extended_from;
    static std::uint64_t current_id;

private:
    event_stream::EventStream* event_stream_{nullptr};

    friend void post_start_transaction(event_stream::EventStream*, Transaction&);
    friend void post_start_transaction_with_parent(event_stream::EventStream*, Transaction&, Transaction&);
    friend std::uint64_t set_current_id(std::uint64_t new_current_id);
    friend void set_clone_transaction(Transaction& t, Txn extended_from);
    friend void post_assert_event(Transaction&, const std::string&);
};

inline void set_clone_transaction(Transaction& t, Txn extended_from)
{
    t.extended_from = extended_from->extended_from ? extended_from->extended_from : extended_from;
}

inline std::uint64_t set_current_id(std::uint64_t new_current_id)
{
    auto ret = Transaction::current_id;
    Transaction::current_id = new_current_id;
    return ret;
}

template<typename T, typename... Args>
T trans_make(Args&&... args)
{
    return boost::make_local_shared<typename T::element_type>(std::forward<Args>(args)...);
}

template<typename T, typename... Args>
T trans_make_es(event_stream::EventStream* stream, Args&&... args)
{
    auto t = boost::make_local_shared<typename T::element_type>(std::forward<Args>(args)...);
    post_start_transaction(stream, *t);
    return t;
}

template<typename T, typename P, typename... Args>
T trans_make_child(event_stream::EventStream* stream, const transaction_ptr<P>& p, Args&&... args)
{
    auto t = boost::make_local_shared<typename T::element_type>(std::forward<Args>(args)...);
    post_start_transaction_with_parent(stream, *t, *p);
    return t;
}

template<typename T, typename P, typename... Args>
T trans_make_clone(event_stream::EventStream* stream [[maybe_unused]], const transaction_ptr<P>& p, Args&&... args)
{
    auto save_current_id = set_current_id(p->txid().value - 1);
    auto t = boost::make_local_shared<typename T::element_type>(std::forward<Args>(args)...);
    set_clone_transaction(*t, p);
    set_current_id(save_current_id);
    return t;
}

#ifdef AMPERE_TRACK_TXN_USE_COUNT
template<typename T>
inline ampere::sc::EventArg<boost::local_shared_ptr<T>> to_event_arg(const transaction_ptr<T>& t)
{
    static event_stream::DataType _defn =
        event_stream::EventStream::define_data("txn_use_count", "Transaction reference count");
    return EventArg<boost::local_shared_ptr<T>>{_defn, t};
}
#else
template<typename T>
inline ampere::sc::EventArg<std::uint64_t> to_event_arg(const transaction_ptr<T>& t)
{
    return to_event_arg(t ? t->txid() : TransactionId{0});
}
#endif // AMPERE_TRACK_TXN_USE_COUNT

} // namespace ampere::sc

/*
 * vim: sw=4:ts=4:et:si:cindent:cino=\:0,(0,g0,t0:
 */
