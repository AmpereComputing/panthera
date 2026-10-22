//
// Copyright (c) 2019 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//
#pragma once

#include <sysc/communication/sc_interface.h>
#include <sysc/communication/sc_port.h>
#include <cstdint>
#include <functional>
#include <type_traits>

namespace ampere::sc {

template<typename T>
class Putter : public sc_core::sc_interface
{
public:
    virtual bool can_put() const = 0;
    virtual void put(T) = 0;

    virtual const T& get(uint64_t index = 0) const = 0;
    virtual uint64_t size() const = 0;

    const T* find_if(const std::function<bool(const T&)>& pred) const
    {
        for (uint64_t i = 0, l = size(); i < l; ++i) {
            const auto& e = get(i);
            if (pred(e))
                return &e;
        }

        return nullptr;
    }

protected:
    friend sc_core::sc_port_base; // the caller of register_port()

    virtual void register_putter_port(sc_core::sc_port_base& port [[maybe_unused]],
                                      const char* port_type [[maybe_unused]])
    {
    }

    // Proxy this call to another name so that we can distinguish between the Taker
    // and the Putter inside a Fifo.
    void register_port(sc_core::sc_port_base& port, const char* port_type) override
    {
        register_putter_port(port, port_type);
    }
};

template<typename T>
using Out = sc_core::sc_port<Putter<T>>;

template<typename T>
using OutOpt = sc_core::sc_port<Putter<T>, 1, sc_core::SC_ZERO_OR_MORE_BOUND>;

} // namespace ampere::sc

/*
 * vim: sw=4:ts=4:et:si:cindent:cino=\:0,(0,g0,t0:
 */
