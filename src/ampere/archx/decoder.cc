//
// Copyright (c) 2021 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//

#include "ampere/archx/decoder.h"
#include <fmt/format.h>

namespace ampere::archx {

std::map<std::string, DecoderFunctions>& get_decoders()
{
    static std::map<std::string, DecoderFunctions>* decoder_map_ptr{nullptr};

    if (!decoder_map_ptr) {
        decoder_map_ptr = new std::map<std::string, DecoderFunctions>;
    }

    return *decoder_map_ptr;
}

bool register_decoder(const char* name, DecoderFunctions functions)
{
    bool inserted;
    std::tie(std::ignore, inserted) = get_decoders().insert({name, functions});

    if (!inserted)
        throw std::runtime_error{fmt::format("duplicate decoder {}", name)};

    return true;
}

} // namespace ampere::archx
