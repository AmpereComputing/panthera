//
// Copyright (c) 2025 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//

#include <map>
#include <vector>
#include "aice_ramp.h"

namespace ampere::kestrel::aice {

static std::vector<int> make_ramp(int ii)
{
    const int len = ii + 15;
    std::vector<int> ramp(len, ii);   // fill vector with ii
    for (int jj = 0; jj < ii; ++jj) { // fix the ends to be ramp-like
        ramp[jj] = jj + 1;
        ramp[len - (jj + 1)] = jj + 1;
    }
    return ramp;
}
static std::map<int, std::vector<int>> make_ramp_vectors()
{
    std::map<int, std::vector<int>> ramp_vectors;
    for (int ii = 1; ii <= 16; ++ii) {
        ramp_vectors[ii] = make_ramp(ii);
    }
    return ramp_vectors;
}

std::vector<int> mar_ramp(unsigned mm, unsigned nn)
{
    static const auto ramp_vectors = make_ramp_vectors();
    const int ns = (nn + 3) / 4;
    const auto& vec = ramp_vectors.at(ns);
    const int total_len = mm + vec.size();
    std::vector<int> slist(total_len, 0); // fill with 0

    int pos = 0;
    for (const auto rval : vec) {
        slist[pos++] = rval;
    }
    pos = 0;
    for (const auto rval : vec) {
        slist[mm + pos] -= rval;
        ++pos;
    }
    std::vector<int> oramp(total_len);
    pos = 0;
    int tot = 0;
    for (const auto sval : slist) {
        tot += sval;
        oramp[pos++] = tot;
    }
    oramp.pop_back(); // remove trailing zero
    return oramp;
}

} // namespace ampere::kestrel::aice
