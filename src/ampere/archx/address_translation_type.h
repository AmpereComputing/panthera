//
// Copyright (c) 2020 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//

#ifndef AMPERE_ARCHX_ADDRESS_TRANSLATION_TYPE_H
#define AMPERE_ARCHX_ADDRESS_TRANSLATION_TYPE_H

namespace ampere::archx {

enum class AddressTranslationType {
    NONE,
    S12E0R,
    S12E0W,
    S12E1R,
    S12E1W,
    S1E0R,
    S1E0W,
    S1E1R,
    S1E1RP,
    S1E1W,
    S1E1WP,
    S1E2R,
    S1E2W,
    S1E3R,
    S1E3W,
    UNSPECIFIED
};

} // namespace ampere::archx

#endif // AMPERE_ARCHX_ADDRESS_TRANSLATION_TYPE_H
