//
// Copyright (c) 2018-2019 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//

#pragma once

#include <fmt/core.h>
#include <fmt/ostream.h>
#include <cstdint>
#include <ostream>

namespace ampere::archx {

class Bitfield
{
public:
    Bitfield() = default;
    explicit Bitfield(std::uint64_t value) : contents(value) {}

    std::uint64_t value() const { return contents; }
    explicit operator std::uint64_t() const { return contents; }

    Bitfield& operator=(uint64_t rhs)
    {
        contents = rhs;
        return *this;
    }

    Bitfield& operator&=(std::uint64_t rhs)
    {
        this->contents &= rhs;
        return *this;
    }

    Bitfield& operator|=(const Bitfield& rhs)
    {
        this->contents |= rhs.value();
        return *this;
    }

    Bitfield& operator|=(std::uint64_t rhs)
    {
        this->contents |= rhs;
        return *this;
    }

    friend Bitfield operator&(Bitfield lhs, const Bitfield& rhs) { return Bitfield{lhs.value() & rhs.value()}; }

    friend Bitfield operator&(Bitfield lhs, std::uint64_t rhs) { return Bitfield{lhs.value() & rhs}; }

    friend Bitfield operator|(Bitfield lhs, const Bitfield& rhs) { return Bitfield{lhs.value() | rhs.value()}; }

    friend Bitfield operator|(Bitfield lhs, std::uint64_t rhs) { return Bitfield{lhs.value() | rhs}; }

    friend std::ostream& operator<<(std::ostream& os, const Bitfield& addr)
    {
        auto flags = os.setf(std::ios::hex, std::ios::basefield);
        os << addr.value();
        os.setf(flags);
        return os;
    }

    friend bool operator==(const Bitfield& lhs, const Bitfield& rhs) { return lhs.value() == rhs.value(); }
    friend bool operator!=(const Bitfield& lhs, const Bitfield& rhs) { return !(lhs == rhs); }

    std::uint64_t extract_range(unsigned hi, unsigned lo) const
    {
        if (hi >= 64) {
            std::string error("Upper bound on range is greater than 64");
            throw std::runtime_error(error);
        }
        if (lo > hi) {
            std::string error("Upper bound is less than lower bound");
            throw std::runtime_error(error);
        }

        auto num_bits = hi - lo + 1;
        std::uint64_t mask = 0;
        if (num_bits == 64)
            mask = UINT64_MAX;
        else
            mask = (1ul << num_bits) - 1;
        return (contents >> lo) & mask;
    }

    // extract range from lo to most signficant (hi)
    std::uint64_t extract_upper_range(unsigned lo) const { return extract_range(63, lo); }

    // extract range from 0 to hi
    std::uint64_t extract_lower_range(unsigned hi) const { return extract_range(hi, 0); }

    std::uint64_t extract_bits(std::initializer_list<unsigned> bits) const
    {
        uint64_t result = 0;
        if (bits.size() > 64) {
            std::string error("Attempted to extract more than 64 bits");
            throw std::runtime_error(error);
        }
        for (auto bit : bits) {
            if (bit > 63) {
                std::string error("Attempted to extract a bit higher than bit 63");
                throw std::runtime_error(error);
            }
            result = (result << 1) | extract_range(bit, bit);
        }
        return result;
    }

    std::uint64_t extract_bit(unsigned bit) const { return (contents >> bit) & 1ULL; }
    std::uint64_t operator[](unsigned bit) const { return extract_bit(bit); }

    std::uint64_t extract_from_mask(uint64_t mask) const
    {
        uint64_t result = 0;
        uint64_t one_hot = 1;
        uint64_t hash_one_hot = 1;

        while (one_hot) {
            if (one_hot & mask) {
                if (one_hot & contents)
                    result |= hash_one_hot;
                hash_one_hot <<= 1;
            }
            one_hot <<= 1;
        }
        return result;
    }

#ifdef USE_BOOST_SERIALIZATION
    template<typename Archive>
    void serialize(Archive& ar, const unsigned int version [[maybe_unused]])
    {
        ar & contents;
    }
#endif

protected:
    std::uint64_t contents{0};
};

} // namespace ampere::archx

template<>
struct fmt::formatter<ampere::archx::Bitfield> : fmt::ostream_formatter
{};

/*
 * vim: sw=4:ts=4:et:si:cindent:cino=\:0,(0,g0,t0:
 */
