//
// Copyright (c) 2022 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//
#pragma once
#include <cstdint>
#include "ampere/archx/decoder_mode.h"

namespace ampere::archx {

class DecoderInput
{
    unsigned char buffer[2 * sizeof(std::uint32_t) + sizeof(DecoderMode)];

public:
    DecoderInput(std::uint32_t opcode, DecoderMode decoder_mode = DecoderModeDefault)
    {
        buffer[0] = decoder_mode;
        buffer[1] = (opcode >> 24) & 0xFF; // high byte to low byte
        buffer[2] = (opcode >> 16) & 0xFF;
        buffer[3] = (opcode >> 8) & 0xFF;
        buffer[4] = (opcode >> 0) & 0xFF;
        buffer[5] = 0;
        buffer[6] = 0;
        buffer[7] = 0;
        buffer[8] = 0;
    }
    DecoderInput(std::uint32_t opcode1, std::uint32_t opcode2, DecoderMode decoder_mode)
    {
        unsigned pos = 0;
        buffer[pos++] = decoder_mode;
        buffer[pos++] = (opcode1 >> 24) & 0xFF; // high byte to low byte
        buffer[pos++] = (opcode1 >> 16) & 0xFF;
        buffer[pos++] = (opcode1 >> 8) & 0xFF;
        buffer[pos++] = (opcode1 >> 0) & 0xFF;

        buffer[pos++] = (opcode2 >> 24) & 0xFF; // high byte to low byte
        buffer[pos++] = (opcode2 >> 16) & 0xFF;
        buffer[pos++] = (opcode2 >> 8) & 0xFF;
        buffer[pos++] = (opcode2 >> 0) & 0xFF;
    }
    unsigned char* ptr() { return buffer; }
    unsigned char* ptr_no_mode() { return buffer + 1; }
};

} // namespace ampere::archx
