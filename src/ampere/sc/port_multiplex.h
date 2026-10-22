//
// Copyright (c) 2019-2021 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//
#pragma once

#include <sysc/communication/sc_port.h>
#include <sysc/kernel/sc_object.h>
#include <algorithm>
#include <cstdint>
#include <functional>
#include <list>
#include <stdexcept>
#include <vector>

#include "ampere/sc/connection.h"
#include "ampere/sc/runnable.h"
#include "ampere/sc/taker.h"
#include "ampere/sc/putter.h"

namespace ampere::sc {

template<typename T, sc_core::sc_port_policy Policy = sc_core::SC_ONE_OR_MORE_BOUND>
class TakerMultiplexer : public Connection,
                         public Putter<T>,
                         public Taker<T>,
                         public Runnable,
                         public sc_core::sc_module
{
    using putter_port_type = sc_core::sc_port<Putter<T>, 1, Policy>;
    using taker_port_type = sc_core::sc_port<Taker<T>, 1, Policy>;
    using arbiter_func_type = std::function<bool(unsigned, unsigned)>;

    std::list<taker_port_type> source_ports;
    putter_port_type output_port{"output"};

    std::vector<unsigned> priorities;
    std::vector<Taker<T> *> takers;
    mutable std::vector<std::pair<Taker<T> *, unsigned>> arbitrated_takers;
    arbiter_func_type arbiter_func;

public:
    class Setup
    {
        friend TakerMultiplexer;
        using sources_type = std::vector<std::pair<taker_port_type *, std::optional<unsigned>>>;
        const char *name = nullptr;
        sources_type sources;
        putter_port_type *output = nullptr;
        arbiter_func_type arbiter_func;

    public:
        Setup() = default;
        Setup(const Setup &) = delete;
        Setup(Setup &&) = default;

        explicit Setup(const char *name, putter_port_type &output) : name(name), output(&output) {}
        explicit operator bool() const { return output; }

        ~Setup()
        {
            if (name && output && !sources.empty())
                new TakerMultiplexer(name, *output, sources, arbiter_func);
        }

        void arbiter(arbiter_func_type func) { arbiter_func = func; }

        auto &connect_in(taker_port_type &source, std::optional<unsigned> priority = std::nullopt)
        {
            sources.emplace_back(&source, priority);
            return *this;
        }
    };

    explicit TakerMultiplexer(const sc_core::sc_module_name &name,
                              putter_port_type &output,
                              typename Setup::sources_type sources,
                              arbiter_func_type arbiter = {})
        : Connection(*get_nearest_ancestor<Module>(&output)->get_clock()),
          sc_core::sc_module(name),
          arbiter_func(arbiter)
    {
        output.bind(*this);
        output_port.bind(output);
        unsigned explicit_priority = 0;
        for (auto src : sources) {
            if (src.second) {
                ++explicit_priority;
                add_with_priority(*src.first, *src.second);
            } else
                add(*src.first);
        }

        if (arbiter_func && explicit_priority < sources.size())
            throw std::logic_error{"All inputs should have an explicit priority when an arbiter is provided"};

        clock.add_runnable(this);
    }

    void add_target(Runnable *to, Runnable *from = nullptr) override
    {
        Connection::add_target(to, from);
        Connection::clock.add_zdelay_connection(from, to);
    }

    void register_taker_port(sc_core::sc_port_base &port, const char *port_name [[maybe_unused]]) override
    {
        this->add_target(get_nearest_ancestor<Runnable>(&port), this);
    }

    bool run() override
    {
        this->schedule_targets(0);
        return false;
    }

    bool can_put() const override { return true; }
    void put(T x [[maybe_unused]]) override {}

    const T &get(uint64_t index [[maybe_unused]] = 0) const override { throw std::runtime_error{"forwards only"}; }
    uint64_t size() const override { throw std::runtime_error{"forwards only"}; }

    bool can_take() const override
    {
        return std::any_of(arbitrated_takers.begin(), arbitrated_takers.end(), [](const auto &taker) {
            return taker.first->can_take();
        });
    }
    T take() override
    {
        arbitrate();
        for (const auto &taker : arbitrated_takers) {
            if (taker.first->can_take())
                return taker.first->take();
        }
        throw std::runtime_error{"nothing to take"};
    }
    void clear() override
    {
        for (auto *taker : takers)
            taker->clear();
    }
    void clear_if(const std::function<bool(const T &)> &pred) override
    {
        for (auto *taker : takers)
            taker->clear_if(pred);
    }

    const T &peek(uint64_t index = 0) const override
    {
        arbitrate();
        for (const auto &taker : arbitrated_takers) {
            auto ready = taker.first->num_ready();
            if (ready > index)
                return taker.first->peek(index);

            index -= ready;
        }
        throw std::runtime_error{"nothing to peek"};
    }
    uint64_t num_ready() const override
    {
        uint64_t total = 0;
        for (auto *taker : takers)
            total += taker->num_ready();

        return total;
    }

    const T &unsafe_peek(uint64_t index [[maybe_unused]] = 0) const override
    {
        throw std::runtime_error{"no unsafe peeking"};
    }
    uint64_t unsafe_size() const override { throw std::runtime_error{"no unsafe size"}; }

    void add(taker_port_type &port)
    {
        auto *taker = dynamic_cast<Taker<T> *>(port.get_interface());
        if (!taker)
            throw std::runtime_error{"not connected"};
        add_taker(*taker, &port);
    }

    void add_with_priority(taker_port_type &port, unsigned priority)
    {
        auto *taker = dynamic_cast<Taker<T> *>(port.get_interface());
        if (!taker)
            throw std::runtime_error{"not connected"};
        add_taker_with_priority(*taker, priority, &port);
    }

private:
    void add_taker(Taker<T> &taker, taker_port_type *target_port = nullptr)
    {
        auto priority = priorities.empty() ? 0 : priorities.back() + 1;
        priorities.emplace_back(priority);
        takers.emplace_back(&taker);
        arbitrated_takers.emplace_back(&taker, priority);
        add_taker_port(target_port);
    }

    void add_taker_with_priority(Taker<T> &taker, unsigned priority, taker_port_type *target_port = nullptr)
    {
        if (auto existing =
                std::find_if(priorities.begin(), priorities.end(), [=](auto other) { return priority < other; });
            existing != priorities.end())
        {
            auto index = std::distance(priorities.begin(), existing);
            priorities.insert(existing, priority);
            takers.insert(std::next(takers.begin(), index), &taker);
            arbitrated_takers.insert(std::next(arbitrated_takers.begin(), index), std::make_pair(&taker, priority));
        } else {
            priorities.emplace_back(priority);
            takers.emplace_back(&taker);
            arbitrated_takers.emplace_back(&taker, priority);
        }
        add_taker_port(target_port);
    }

    void arbitrate() const
    {
        if (!arbiter_func)
            return;

        std::sort(arbitrated_takers.begin(), arbitrated_takers.end(), [&](const auto &a, const auto &b) {
            return arbiter_func(a.second, b.second);
        });
    }

    void add_taker_port(taker_port_type *port)
    {
        if (port) {
            auto &inner_port = source_ports.emplace_back(port->basename());
            inner_port.bind(*port);
        } else {
            auto &inner_port = source_ports.emplace_back();
            inner_port.bind(*this);
        }
    }
};

} // namespace ampere::sc

/*
 * vim: sw=4:ts=4:et:si:cindent:cino=\:0,(0,g0,t0:
 */
