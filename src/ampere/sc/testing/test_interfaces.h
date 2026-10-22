//
// Copyright (c) 2019 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//

#pragma once

#include <gmock/gmock.h>
#include <deque>
#include <vector>

#include "ampere/sc/putter.h"
#include "ampere/sc/reader.h"
#include "ampere/sc/taker.h"
#include "ampere/sc/credit.h"

namespace ampere::sc {

template<typename T>
class MockPutter : public Putter<T>
{
public:
    MOCK_METHOD(bool, can_put, (), (const, override));
    MOCK_METHOD(void, put, (T), (override));
    MOCK_METHOD(const T&, get, (uint64_t), (const, override));
    MOCK_METHOD(uint64_t, size, (), (const, override));
};

template<typename T>
class MockReader : public Reader<T>
{
public:
    MOCK_METHOD(bool, can_read, (), (const, override));
    MOCK_METHOD(T, read, (), (override));
};

template<typename T>
class MockTaker : public Taker<T>
{
public:
    MOCK_METHOD(bool, can_take, (), (const, override));
    MOCK_METHOD(T, take, (), (override));
    MOCK_METHOD(void, clear, (), (override));
    MOCK_METHOD(void, clear_if, (const std::function<bool(const T&)>&), (override));
    MOCK_METHOD(const T&, peek, (uint64_t), (const, override));
    MOCK_METHOD(uint64_t, num_ready, (), (const, override));
    MOCK_METHOD(const T&, unsafe_peek, (uint64_t), (const, override));
    MOCK_METHOD(uint64_t, unsafe_size, (), (const, override));
};

template<typename T>
class MockTakerWithCredit : public TakerWithCredit<T>
{
public:
    MOCK_METHOD(bool, can_take, (), (const, override));
    MOCK_METHOD(bool, can_take_with_credit, (), (const, override));
    MOCK_METHOD(T, take, (), (override));
    MOCK_METHOD((std::pair<T, Credit>), take_with_credit, (), (override));
    MOCK_METHOD(void, clear, (), (override));
    MOCK_METHOD(void, clear_if, (const std::function<bool(const T&)>&), (override));
    MOCK_METHOD(bool, can_put_credit, (), (const, override));
    // MOCK_METHOD(void, put_credit, (Credit), (override));
    MOCK_METHOD(const T&, peek, (uint64_t), (const, override));
    MOCK_METHOD(uint64_t, num_ready, (), (const, override));
    MOCK_METHOD(const T&, unsafe_peek, (uint64_t), (const, override));
    MOCK_METHOD(uint64_t, unsafe_size, (), (const, override));

    void put_credit(Credit c) override
    {
        if (c)
            c->release();
    }
};

template<typename T>
class NeverPut : public Putter<T>
{
public:
    bool can_put() const override { return false; }
    void put(T) override {}
    const T& get(uint64_t index = 0) const override { throw std::runtime_error{"cannot get"}; }
    uint64_t size() const override { throw std::runtime_error{"cannot call size"}; }
};

template<typename T>
class NeverRead : public Reader<T>
{
public:
    bool can_read() const override { return false; }
    T read() override { return T{}; }
};

template<typename T>
class NeverTake : public Taker<T>
{
public:
    bool can_take() const override { return false; }
    T take() override { return T{}; }
    const T& peek(uint64_t index = 0) const override { throw std::runtime_error{"cannot peek"}; }
    uint64_t num_ready() const override { throw std::runtime_error{"cannot call num_ready"}; }
    const T& unsafe_peek(uint64_t index = 0) const override { throw std::runtime_error{"cannot unsafe_peek"}; }
    uint64_t unsafe_size() const override { throw std::runtime_error{"cannot call unsafe_size"}; }
    void clear() override {};
    void clear_if(const std::function<bool(const T&)>& pred) override {};
};

template<typename T>
class Consumer : public Putter<T>
{
public:
    bool can_put() const override { return true; }
    void put(T thing) override { items.push_back(std::move(thing)); }
    const T& get(uint64_t index = 0) const override { return items[index]; }
    uint64_t size() const override { return items.size(); }

    std::vector<T> items;
};

template<typename T>
class Producer : public Taker<T>
{
public:
    Producer() = default;
    Producer(const Producer&) = default;
    Producer(std::initializer_list<T> stuff) : items{stuff} {}

    bool can_take() const override { return !items.empty(); }
    uint64_t num_ready() const override { return items.size(); }
    const T& unsafe_peek(uint64_t index = 0) const override { return items[index]; }
    uint64_t unsafe_size() const override { return items.size(); }

    T take() override
    {
        auto t = std::move(items.front());
        items.pop_front();
        return t;
    }

    const T& peek(uint64_t index = 0) const override
    {
        if (!can_take())
            throw std::runtime_error{"nothing to peek at in Producer"};
        AMPERE_ASSERT(index < num_ready(), "Peek index greater than queue size");
        return items[index];
    }

    void clear() override { items.clear(); }
    void clear_if(const std::function<bool(const T&)>& pred) override
    {
        items.erase(std::remove_if(items.begin(), items.end(), pred), items.end());
    }

    void add(const T& thing) { items.push_back(thing); }
    void add(T&& thing) { items.push_back(std::move(thing)); }
    void add(std::initializer_list<T> stuff);

    std::deque<T> items;
};

template<typename T>
void Producer<T>::add(std::initializer_list<T> stuff)
{
    for (auto& x : stuff)
        items.push_back(x);
}

struct CreditReleaser : public Putter<sc::Credit>
{
    bool can_put() const override { return true; }
    void put(sc::Credit ch) override
    {
        if (ch)
            ch->release();
    }
    const sc::Credit& get(uint64_t index = 0) const override { throw std::runtime_error{"cannot get"}; }
    uint64_t size() const override { throw std::runtime_error{"cannot call size"}; }
};

} // namespace ampere::sc

