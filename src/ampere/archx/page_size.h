//
// Copyright (c) 2019 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//
#pragma once

#include <cstdint>
#include <stdexcept>
#include <nlohmann/json.hpp>

#include "ampere/archx/register.h"
#include "ampere/util/enum.h"

make_ns_enum_with_type(ampere::archx::internal, PageSizeEnum, uint8_t,
    //4kb granule sizes
    PS_4KB = 0,
    PS_2MB = 1,
    PS_1GB = 2,
    //64kb granule sizes
    PS_64KB = 3,
    PS_512MB = 4,
    PS_4TB = 5,
    //Only used for uninitialized copies
    INVALID = 6
);

namespace ampere::archx {

using json = nlohmann::json;

class PageSize
{
public:
    using Size = internal::PageSizeEnum;
    inline static constexpr auto MAX = util::enum_size<internal::PageSizeEnum>() - 1;

    constexpr PageSize() = default;
    constexpr PageSize(Size ps) : size(ps) {}
    constexpr explicit PageSize(uint8_t page_bits)
    {
        switch (page_bits) {
        // 4kb granule
        case 12:
            size = Size::PS_4KB;
            return;
        case 21:
            size = Size::PS_2MB;
            return;
        case 30:
            size = Size::PS_1GB;
            return;
        // 64kb granule
        case 16:
            size = Size::PS_64KB;
            return;
        case 29:
            size = Size::PS_512MB;
            return;
        case 42:
            size = Size::PS_4TB;
            return;
        default:
            throw std::runtime_error("PageSize(uint8_t page_bits) passed invalid number of page bits}");
        }
    }

    explicit operator std::string() const
    {
        if (size == Size::INVALID)
            return "invalid";
        return std::string(util::enum_to_string(size).substr(3));
    }

    // The number of bits *unspecified* by the VA returned by the MMU for this
    // page size
    constexpr uint8_t page_bits() const
    {
        switch (size) {
        // 4kb granule sizes
        case Size::PS_4KB:
            return 12;
        case Size::PS_2MB:
            return 21;
        case Size::PS_1GB:
            return 30;
        // 64kb granule sizes
        case Size::PS_64KB:
            return 16;
        case Size::PS_512MB:
            return 29;
        case Size::PS_4TB:
            return 42;
        default:
            throw std::runtime_error("PageSize::page_bits() called on invalid size");
        }
    }

    constexpr uint64_t page_bytes() const { return 1ULL << page_bits(); }

    inline bool operator==(const PageSize& other) const { return size == other.size; }
    inline bool operator!=(const PageSize& other) const { return !this->operator==(other); }
    inline bool operator<(const PageSize& other) const { return page_bits() < other.page_bits(); }
    inline bool operator>(const PageSize& other) const { return other.operator<(*this); }
    inline bool operator<=(const PageSize& other) const { return !(this->operator>(other)); }
    inline bool operator>=(const PageSize& other) const { return !(this->operator<(other)); }

    Size value() const { return size; }
    uint8_t index() const { return uint8_t(size); }

private:
    Size size{Size::INVALID};
};

void from_json(const json& j, PageSize& ps);

} // namespace ampere::archx

/*
 * vim: sw=4:ts=4:et:si:cindent:cino=\:0,(0,g0,t0:
 */
