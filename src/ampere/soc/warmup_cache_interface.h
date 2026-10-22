/*
 * Copyright (c) 2025 Ampere Computing. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#pragma once

#include <optional>

#include "ampere/core/l2/l2c_bank.h"
#include "ampere/core/l2/l2c_line.h"
#include "ampere/mesh/injector/mesh_injector.h"

namespace ampere::soc {

struct CacheInterface
{
    CacheInterface() = default;
    CacheInterface(const CacheInterface&) = default;
    CacheInterface(CacheInterface&&) = default;
    virtual ~CacheInterface() = default;
    CacheInterface& operator=(const CacheInterface&) = default;
    CacheInterface& operator=(CacheInterface&&) = default;

    using State = mesh::MeshInjector::CacheEntry::State;

    class CacheValue
    {
    public:
        CacheValue(sc::PhysAddr addr, State state) : addr{addr}, state{state} {}

        bool is_unique() const { return state == State::UC || state == State::UD; }
        bool is_dirty() const { return state == State::UD || state == State::SD; }

        sc::PhysAddr addr;
        State state;
    };

    virtual std::optional<CacheValue> lookup(sc::Txn, sc::PhysAddr) = 0;
    virtual void invalidate(sc::Txn, sc::PhysAddr) = 0;
    virtual std::optional<CacheValue> refill(sc::Txn, sc::PhysAddr, State) = 0;
    virtual bool would_fill_without_victim(sc::PhysAddr) const = 0;
};

template<typename T>
class MeshInjectorCacheInterface : public CacheInterface
{
public:
    using CacheType = T;

    explicit MeshInjectorCacheInterface(CacheType* underlying_cache) : underlying_cache{underlying_cache} {}

    std::optional<CacheValue> lookup(sc::Txn txn, sc::PhysAddr addr) override
    {
        auto result = underlying_cache->lookup(txn, addr);

        if (result)
            return CacheValue{result->addr, result->state};
        else
            return std::nullopt;
    }

    void invalidate(sc::Txn txn, sc::PhysAddr addr) override { underlying_cache->invalidate(txn, addr); }

    std::optional<CacheValue> refill(sc::Txn txn, sc::PhysAddr addr, State state) override
    {
        auto result = underlying_cache->refill(txn, addr, mesh::MeshInjector::CacheEntry{addr, state});

        if (result)
            return CacheValue{result->addr, result->state};
        else
            return std::nullopt;
    }

    bool would_fill_without_victim(sc::PhysAddr addr) const override
    {
        return underlying_cache->would_fill_without_victim(addr);
    }

private:
    CacheType* underlying_cache;
};

class CoreL2Interface : public CacheInterface
{
public:
    CoreL2Interface(core::l2::BankSet* underlying_cache, core::l2::CacheType cache_type)
        : underlying_cache{underlying_cache}, cache_type{cache_type}
    {
    }

    std::optional<CacheValue> lookup(sc::Txn txn, sc::PhysAddr addr) override
    {
        auto bank = underlying_cache->select(addr);
        auto hit = (*underlying_cache)[bank].lookup(addr, true);

        if (hit)
            return CacheValue{hit->addr, line_state_to_state(hit->state)};
        else
            return std::nullopt;
    }

    void invalidate(sc::Txn txn, sc::PhysAddr addr) override
    {
        auto bank = underlying_cache->select(addr);
        auto hit = (*underlying_cache)[bank].lookup(addr, false);
        if (hit)
            hit->state = core::l2::LineState::I;
    }

    std::optional<CacheValue> refill(sc::Txn txn, sc::PhysAddr addr, State state) override
    {
        auto bank = underlying_cache->select(addr);
        auto line = core::l2::Line{addr, state_to_line_state(state), cache_type};
        auto victim = (*underlying_cache)[bank].fill_or_select_victim(line).first;

        if (victim) {
            (*underlying_cache)[bank].replace(line, victim->way);
            return CacheValue{victim->addr, line_state_to_state(victim->state)};
        } else {
            return std::nullopt;
        }
    }

    bool would_fill_without_victim(sc::PhysAddr addr) const override
    {
        auto bank = underlying_cache->select(addr);
        return const_cast<core::l2::Bank&>((*underlying_cache)[bank]).would_fill_without_victim(addr, cache_type);
    }

private:
    core::l2::BankSet* underlying_cache;
    core::l2::CacheType cache_type;

    static State line_state_to_state(core::l2::LineState line_state)
    {
        switch (line_state) {
        case core::l2::LineState::I:
            return State::I;
        case core::l2::LineState::SC:
            return State::SC;
        case core::l2::LineState::UC:
            return State::UC;
        case core::l2::LineState::SD:
            return State::SD;
        case core::l2::LineState::UD:
            return State::UD;
        }
        return State::I;
    }

    static core::l2::LineState state_to_line_state(State state)
    {
        switch (state) {
        case State::I:
            return core::l2::LineState::I;
        case State::SC:
            return core::l2::LineState::SC;
        case State::UC:
            return core::l2::LineState::UC;
        case State::SD:
            return core::l2::LineState::SD;
        case State::UD:
            return core::l2::LineState::UD;
        }
        return core::l2::LineState::I;
    }
};

} // namespace ampere::soc
