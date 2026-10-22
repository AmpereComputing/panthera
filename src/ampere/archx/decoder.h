//
// Copyright (c) 2021 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//

#pragma once

#include <map>
#include "ampere/archx/inst_info.h"
#include "ampere/archx/uop.h"
#include "ampere/archx/decoder_mode.h"
#include "ampere/util/enum.h"

make_ns_enum(ampere::archx, FusionType,
    INVALID = 0,
    ADR = 1,
    AES = 2,
    BC = 3,
    MOV_MOV = 4,
    MOV_MOV_TRINITY = 5,
    CBZ = 6,
    ADDP1 = 7,
    ADDSUB_IMM24 = 8,
    ADDSSUBS_CSET = 9,

    // The following two entries DO NOT EXIST in xlat. They need to float at
    // the bottom so that fusion cases which get added to xlat later can get
    // assigned appropriate enum values.
    NOP = 101,
    B_UNCOND = 102,
    MOVPRFX_PREDICATED = 103,
    MOVPRFX_UNPREDICATED = 104,
    DIV_MSUB = 105
);

namespace ampere::archx {

using DecodeFunction = UopSet (*)(std::uint32_t insn_bytes, DecoderMode decoder_mode);
using DecodeFusionFunction = std::pair<FusionType, UopSet> (*)(std::uint32_t* insn_bytes,
                                                               bool skip_checks,
                                                               DecoderMode decoder_mode);
using DecodeSourceFunction = const char* (*)(std::uint32_t insn_bytes, DecoderMode decoder_mode);
using DecodeInstInfoFunction = const InstInfo* (*)(std::uint32_t insn_bytes);
using DecodeInstInfoFusionFunction = const InstInfoFusion* (*)(std::uint32_t* insn_bytes);

struct DecoderFunctions
{
    DecodeFunction decode;
    DecodeFusionFunction decode_fusion;
    DecodeSourceFunction decode_source;
    DecodeInstInfoFunction decode_inst_info;
    DecodeInstInfoFusionFunction decode_inst_info_fusion;
};

bool register_decoder(const char* name, DecoderFunctions functions);
std::map<std::string, DecoderFunctions>& get_decoders();

} // namespace ampere::archx
