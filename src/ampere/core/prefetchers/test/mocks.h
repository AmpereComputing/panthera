//
// Copyright (c) 2023 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//

#pragma once

#include <gmock/gmock.h>
#include "ampere/archx/addr.h"
#include "ampere/core/flush.h"
#include "ampere/core/prefetcher.h"
#include "ampere/uarch_common/memop.h"

template<typename T, typename P>
class poly_mock : public P, ampere::sc::poly<T>
{
protected:
    static std::map<std::string, T*>& instances()
    {
        static std::map<std::string, T*> _instances;
        return _instances;
    }

    static void add_instance(const std::string& name, T* instance) { instances().emplace(name, instance); }
    static void remove_instance(T* instance)
    {
        for (auto it = instances().begin(); it != instances().end(); ++it) {
            if (it->second == instance) {
                instances().erase(it);
                return;
            }
        }
    }

public:
    template<typename U = T>
    inline static U* get_instance(const std::string& name)
    {
        if (auto i = instances().find(name); i != instances().end())
            return dynamic_cast<U*>(i->second);
        return nullptr;
    }

    explicit poly_mock(sc_core::sc_module_name name) : P(name)
    {
        add_instance(std::string(name), reinterpret_cast<T*>(this));
    }
    ~poly_mock() override { remove_instance(reinterpret_cast<T*>(this)); }
};

using ampere::archx::PhysAddr;
using ampere::core::Flush;
using ampere::uarch_common::MemOp::PrefetchType;

template<typename T>
class MockPrefetcher : public ampere::core::Prefetcher<T>
{
public:
    explicit MockPrefetcher(sc_core::sc_module_name name) : ampere::core::Prefetcher<T>(name) {}

    MOCK_METHOD(void, train_impl, (T req, bool hit, bool prefetch, bool demand), (override));
    MOCK_METHOD(void, drop_impl, (T req, core::PrefetchDropReason reason), (override));
    MOCK_METHOD(void, overflow_impl, (T req), (override));
    MOCK_METHOD(void, fill_impl, (T req, bool fb_pref, bool was_alloc_by_pf), (override));
    MOCK_METHOD(void, evict_impl, (PhysAddr pa, bool prefetch, bool demand), (override));
    MOCK_METHOD(void, launch_impl, (T req), (override));
    MOCK_METHOD(void, flush_impl, (Flush flush_info), (override));
    MOCK_METHOD(PrefetchType, prefetch_type, (), (const, override));
};

class L2P_Mock : public poly_mock<L2P_Mock, MockPrefetcher<ampere::core::MemReq>>
{
public:
    explicit L2P_Mock(sc_core::sc_module_name name) : poly_mock<L2P_Mock, MockPrefetcher<ampere::core::MemReq>>(name) {}
};

class L2P_Strict_Mock : public poly_mock<L2P_Strict_Mock, ::testing::StrictMock<MockPrefetcher<ampere::core::MemReq>>>
{
public:
    explicit L2P_Strict_Mock(sc_core::sc_module_name name)
        : poly_mock<L2P_Strict_Mock, ::testing::StrictMock<MockPrefetcher<ampere::core::MemReq>>>(name)
    {
    }
};

/*
 * vim: sw=4:ts=4:et:si:cindent:cino=\:0,(0,g0,t0:
 */

