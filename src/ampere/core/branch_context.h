//
// Copyright (c) 2022 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//

// Implement branch context (ideally would comprehend at least EL, ASID, VMID,
// NS, translation regime, SCXTNUM_ELx) along with pseudo-random tokens to use
// to XOR against tags for security protection. The TokenGenerator below is
// intended to be deterministic, but NOT to match RTL's LFSR implementation.

#pragma once

#include <random>
#include <vector>
#include <sstream>
#include <stdexcept>

#include <boost/multiprecision/cpp_int.hpp>
#include <nlohmann/json.hpp>

#include "ampere/archx/el.h"
#include "ampere/util/bits.h"

namespace ampere::core {

using namespace boost::multiprecision;
using json = nlohmann::json;

class Token
{
    /*
     * Token holds a bitfield primarily for use to protect branch prediction
     * structures against security vulnerabilities by XORing into their tags.
     */
public:
    Token() = default;
    Token(uint128_t value, unsigned bit_width = 128) : value{value}, bit_width{bit_width}
    {
        if (bit_width < 1 || bit_width > 128)
            throw std::runtime_error("Token created with invalid width");
    }

    unsigned width() { return bit_width; }
    uint64_t bits(unsigned width, unsigned start = 0)
    {
        if (start + width > bit_width)
            throw std::runtime_error("Token.bits() caller asked for higher bits than Token contains");
        if (width > 64)
            throw std::runtime_error("Token.bits() caller asked for more than 64 bits");

        return (uint64_t)((value >> start) & util::mask(width));
    }

    void to_json(json& j) const
    {
        std::ostringstream os;
        os << "0x" << std::hex << value;
        j = json{
            {"bit_width", bit_width},
            {"value", os.str()},
        };
    }

    void from_json(const json& j)
    {
        std::string value_str;
        j.at("bit_width").get_to(bit_width);
        j.at("value").get_to(value_str);
        value = uint128_t(value_str);
    }

private:
    uint128_t value{0};
    unsigned bit_width{128};
};

class TokenGenerator
{
    /*
     * TokenGenerator generates a pseudo-random (yet deterministic) token,
     * primarily for use to protect branch prediction structures against
     * security vulnerabilities by XORing into their tags.
     *
     * Perhaps ideally this would be an LFSR to match RTL, but this seems close
     * enough for performance modeling purposes (as long as we can restore the
     * most recent token on RTL for offline BPU warmup).
     */
public:
    TokenGenerator(unsigned bit_width = 64, unsigned seed = 1) : width{bit_width}
    {
        rand_src.seed(seed);
        unsigned remaining_width = bit_width;
        while (remaining_width > 0) {
            unsigned this_width = std::min(64U, remaining_width);
            distributions.emplace_back(0, util::mask(this_width));
            remaining_width -= this_width;
        }
    }
    Token next(bool always_zero = false)
    {
        uint128_t value = 0;
        size_t shift = 0;
        for (auto& distribution : distributions) {
            uint128_t rand_value = distribution(rand_src);
            value |= (rand_value << shift);
            shift += 64;
        }
        if (always_zero)
            return Token(0, width);
        else
            return Token(value, width);
    }

private:
    unsigned width;
    std::mt19937 rand_src;
    std::vector<std::uniform_int_distribution<uint64_t>> distributions;
};

class BranchContext
{
public:
    archx::EL el;
    uint16_t asid;
    Token token;
};

inline void to_json(json& j, const BranchContext& context)
{
    auto token = json{};
    context.token.to_json(token);

    j = json{
        {"el", context.el},
        {"asid", context.asid},
        {"token", token},
    };
}

inline void from_json(const json& j, BranchContext& context)
{
    j.at("el").get_to(context.el);
    j.at("asid").get_to(context.asid);
    context.token.from_json(j.at("token"));
}

} // namespace ampere::core
