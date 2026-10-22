//
// Copyright (c) 2018 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//

#ifndef AMPERE_CORE_HASH_H
#define AMPERE_CORE_HASH_H

#include <cstdint>
#include <vector>

namespace ampere {
namespace core {

class RauHash
{
public:
    RauHash(unsigned input_order, std::uint64_t polynomial) : output_order(poly_order(polynomial))
    {
        auto max_order = input_order > 64 ? 64 : input_order;

        for (unsigned i = 0; i < max_order; i++) {
            std::uint64_t remainder = 1 << i;
            auto rorder = poly_order(remainder);

            while (rorder >= output_order) {
                remainder = remainder ^ (polynomial << (rorder - output_order));
                rorder = poly_order(remainder);
            }
            remainder_table.push_back(remainder);
        }
    }

    unsigned operator()(std::uint64_t val) const
    {
        unsigned hash = 0;

        for (auto rem = remainder_table.begin(); val && rem != remainder_table.end(); ++rem) {
            hash ^= (val & 0x1) * (*rem);
            val >>= 1;
        }

        return hash;
    }

    const unsigned get_order() const { return output_order; }

private:
    const unsigned output_order;

    std::vector<std::uint64_t> remainder_table;

    static inline unsigned poly_order(std::uint64_t bits)
    {
        auto order = 0;

        // Ignore constant term (LSB)
        bits >>= 1;

        while (bits) {
            order++;
            bits >>= 1;
        }

        return order;
    }
};

} // namespace core
} // namespace ampere

#endif
