// Copyright (c) 2022 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//
#pragma once

#include <string>

namespace ampere::event_stream {

class EventBase
{
public:
    explicit EventBase(const char* name, const std::string& description) : name(name), description(description) {}
    virtual void enable() {}
    virtual void disable() {}

    const char* name;
    std::string description;
};
} // namespace ampere::event_stream

/*
 * vim: sw=4:ts=4:et:si:cindent:cino=\:0,(0,g0,t0:
 */
