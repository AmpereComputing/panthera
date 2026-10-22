//
// Copyright (c) 2024 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//
#pragma once
#include <cstdint>

namespace ampere::archx {
class inum_t
{
public:
    inum_t(std::int64_t oid = 0) : oid(oid) {}
    bool wrong_path() const { return oid <= 0; }
    bool initial_bogus_inst() const { return oid == 0; }

    bool operator==(const inum_t& rhs) const { return oid == rhs.oid; }
    std::int64_t id() const { return oid; }

private:
    std::int64_t oid;
};

} // namespace ampere::archx
