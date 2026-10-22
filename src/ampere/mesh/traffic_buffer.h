//
// Copyright (c) 2024 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//

#pragma once

#include <deque>

#include "ampere/mesh/mesh_module.h"

namespace ampere::mesh {

template<class T, class Allocator = std::allocator<T>>
class TrafficBuffer : public MeshModule, std::deque<T, Allocator>
{
public:
    explicit TrafficBuffer(const sc_core::sc_module_name& name) : MeshModule{name, nullptr} {}

    void push_back(const T& x)
    {
        ev_insert(x);
        std::deque<T, Allocator>::push_back(x);
    }
    void push_back(T&& x)
    {
        ev_insert(x);
        std::deque<T, Allocator>::push_back(x);
    }
    void pop_front()
    {
        ev_remove((const T&)front());
        std::deque<T, Allocator>::pop_front();
    }

    using std::deque<T, Allocator>::front;
    using std::deque<T, Allocator>::size;
    using std::deque<T, Allocator>::empty;
    using typename std::deque<T, Allocator>::iterator;
    using typename std::deque<T, Allocator>::const_iterator;
    using std::deque<T, Allocator>::begin;
    using std::deque<T, Allocator>::end;

private:
    Event ev_insert{"insert", "Inserted an element into the unbounded traffic buffer"};
    Event ev_remove{"remove", "Removed an element from the unbounded traffic buffer"};
};

} // namespace ampere::mesh
