/*
 * Copyright (c) 2024 Ampere Computing. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "component.h"

#include "error.h"

namespace ampere::lynx {

Component::Component(Component&& other) : m_type{other.m_type}, m_loc{other.m_loc}, m_tag{std::move(other.m_tag)} {}

std::ostream& operator<<(std::ostream& os, const Component& comp)
{
    fmt::print(os, "{}: {} x{}y{}", comp.die_id(), comp.type(), comp.x(), comp.y());

    if (comp.port() != -1)
        fmt::print(os, "p{}", comp.port());
    if (comp.didx() != -1)
        fmt::print(os, "d{}", comp.didx());

    return os;
}

} // namespace ampere::lynx
