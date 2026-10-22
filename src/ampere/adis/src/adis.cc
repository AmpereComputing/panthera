//
// Copyright (c) 2022 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//

#include <cstdint>
#include <cassert>

#include "dectab.h"
#include "adis.h"
namespace adis {
encoding_enum_t decode(std::uint32_t itext) {
    std::uint16_t node = 0;
    int i = 0;

    for( ; i<4;i++) {
        std::uint8_t byte = itext >> ((3-i)*8);
        std::int16_t next_state = states[node][byte];
        if (next_state <= 0) {
            int enc_num = -next_state;
            encoding_enum_t e = static_cast<encoding_enum_t>(enc_num);
            return e;
        }
        node = (std::uint16_t)next_state;
    }
    return ENC_INVALID;
}

}
