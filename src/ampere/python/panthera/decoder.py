#!/usr/bin/env python3
# Copyright (c) 2019-2025 Ampere Computing. All rights reserved.
# SPDX-License-Identifier: BSD-3-Clause

from __future__ import annotations

import collections
import datetime
import itertools
import os
import sys

from ampere.xlat.fields import Field
from ampere.xlat.signal import Signal
from ampere.xlat.translation import Fusion, translation_groups

from . import inst_info, uop_info

PROJECT_PATH = os.path.join(os.path.dirname(__file__), "..", "..", "..", "..")


def project_relative(path):
    return os.path.relpath(path, start=PROJECT_PATH)


def _dbg(s: str):
    # print(s,file=sys.stderr)
    pass


def bool_str(value: bool) -> str:
    return "true" if value else "false"


def set_if(uop_fields: dict, obj, field: str, default: bool = False):
    """Set uop_field if field equals eq_value."""
    value = getattr(obj, field, default)
    if value is True:
        uop_fields[field] = bool_str(True)


def set_if_present(uop_fields: dict, obj, field: str):
    """Set uop_field if field is present."""
    if hasattr(obj, field):
        value = getattr(obj, field)
        uop_fields[field] = bool_str(value) if isinstance(value, bool) else value


class CreateDecoderClass:
    HFILE_INTERNAL_TEMPLATE = """//
// Copyright (c) 2019-{year} Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//
#pragma once
#include <cstdint>
#include "ampere/archx/uop.h"
namespace ampere::archx::{ns} {{
{decode_func_decls}
}}  // namespace ampere::archx::{ns}
"""

    HFILE_TEMPLATE = """//
// Copyright (c) 2019-{year} Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//
#pragma once
#include <cstdint>
#include <memory>
#include <optional>
#include <vector>

#include "ampere/archx/instruction.h"
#include "ampere/archx/uop.h"

namespace ampere::archx::{ns} {{

UopSet decode(std::uint32_t insn_bytes, DecoderMode decoder_mode=DecoderModeDefault);
const char* decode_source(std::uint32_t insn_bytes, DecoderMode decoder_mode=DecoderModeDefault);
const InstInfo* decode_inst_info(std::uint32_t insn_bytes);

}}  // namespace ampere::archx::{ns}
"""

    HFILE_TEMPLATE_WITH_FUSION = """//
// Copyright (c) 2019-{year} Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//
#pragma once
#include "ampere/archx/decoder.h"
#include "ampere/archx/inst_info.h"


namespace ampere::archx::{ns} {{

UopSet decode(std::uint32_t insn_bytes, DecoderMode decoder_mode=DecoderModeDefault);
const char* decode_source(std::uint32_t insn_bytes, DecoderMode decoder_mode=DecoderModeDefault);
std::pair<FusionType, UopSet> decode_fusion(std::uint32_t* insn_bytes, bool skip_checks, DecoderMode decoder_mode=DecoderModeDefault);
const InstInfo* decode_inst_info(std::uint32_t insn_bytes);
const InstInfoFusion* decode_inst_info_fusion(std::uint32_t* insn_bytes);

}}  // namespace ampere::archx::{ns}
"""
    CC_DEC_FILE_TEMPLATE = """//
// Copyright (c) 2019-{year} Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//
namespace ampere::archx::{ns} {{
#include "{ns}.patterns.cc.inc"
#include "{ns}.inst_info.patterns.cc.inc"
}} // namespace
"""
    CCFILE_TEMPLATE_UOP_DEFN = """//
// Copyright (c) 2019-{year} Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//
#include <utility>
#include <vector>
#include "{ns}.h"
#include "{ns}-int.h"
#include "ampere/archx/register.h"
namespace ampere::archx::{ns} {{
extern UopInfo uop_info_tab[];
{decode_func_defns}
}}  // namespace ampere::archx::{ns}
"""

    CCFILE_TEMPLATE = """//
// Copyright (c) 2019-{year} Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//
#include <sstream>
#include <stdexcept>
#include <utility>
#include "{ns}.h"
#include "{ns}-int.h"
#include "ampere/archx/decoder_input.h"

namespace ampere::archx::{ns} {{

int inst_info_decode(const unsigned char* bytes);
int insn_decode(const unsigned char* bytes);

UopInfo uop_info_tab[] = {{
    {uop_info_entries}
}};

UopSet undefined_insn(std::uint32_t insn_bytes [[maybe_unused]])
{{
    return {{}};
}}

using decode_func_t = UopSet (*)(std::uint32_t);

static decode_func_t decoder_funcs[{insn_count}] = {{
    undefined_insn,
{decode_func_table}
}};

UopSet decode(std::uint32_t insn_bytes, DecoderMode decoder_mode)
{{
    DecoderInput di(insn_bytes, decoder_mode);
    return decoder_funcs[insn_decode(di.ptr())](insn_bytes);
}}

//
// Functions for reporting the source (file:line of the first uop) of an instruction decode.
// The structure is the same as for the normal decode functions.
//

static const char* undefined_source(std::uint32_t insn_bytes [[maybe_unused]])
{{
    return nullptr;
}}

{decode_source_func_defns}

using decode_source_func_t = const char* (*)(std::uint32_t);

static decode_source_func_t decoder_source_funcs[{insn_count}] = {{
    undefined_source,
{decode_source_func_table}
}};

const char* decode_source(std::uint32_t insn_bytes, DecoderMode decoder_mode)
{{
    DecoderInput di(insn_bytes, decoder_mode);
    return decoder_source_funcs[insn_decode(di.ptr())](insn_bytes);
}}

InstInfo inst_info_tab[] = {{
    {inst_info_entries}
}};

const InstInfo* decode_inst_info(std::uint32_t insn_bytes)
{{
    DecoderInput di(insn_bytes);
    auto idx = inst_info_decode(di.ptr_no_mode());
    return idx > 0 ? &inst_info_tab[idx] : nullptr;
}}


static bool registered_decoder = register_decoder("{ns}",
    {{decode, nullptr, decode_source, decode_inst_info, nullptr}});

}}  // namespace ampere::archx::{ns}
"""

    CC_DEC_FILE_TEMPLATE_WITH_FUSION = """//
// Copyright (c) 2019-{year} Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//
namespace ampere::archx::{ns} {{
#include "{ns}.patterns.cc.inc"
#include "{ns}.fusion.patterns.cc.inc"
#include "{ns}.inst_info.patterns.cc.inc"
#include "{ns}.inst_info_fusion.patterns.cc.inc"
}} // namespace
"""

    CCFILE_TEMPLATE_WITH_FUSION = """//
// Copyright (c) 2019-{year} Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//
#include "{ns}.h"
#include "{ns}-int.h"
#include <sstream>
#include <stdexcept>
#include <utility>
#include "ampere/archx/fusion_opnd_check.h"
#include "ampere/archx/decoder_input.h"

namespace ampere::archx::{ns} {{

int fusion_decode(const unsigned char* bytes);
int inst_info_fusion_decode(const unsigned char* bytes);
int inst_info_decode(const unsigned char* bytes);
int insn_decode(const unsigned char* bytes);

UopInfo uop_info_tab[] = {{
    {uop_info_entries}
}};

UopSet undefined_insn(std::uint32_t insn_bytes [[maybe_unused]])
{{
    return {{}};
}}

using decode_func_t = UopSet (*)(std::uint32_t);

static decode_func_t decoder_funcs[{insn_count}] = {{
    undefined_insn,
{decode_func_table}
}};

UopSet decode(std::uint32_t insn_bytes, DecoderMode decoder_mode)
{{
    DecoderInput di(insn_bytes, decoder_mode);
    return decoder_funcs[insn_decode(di.ptr())](insn_bytes);
}}

//
// Functions for reporting the source (file:line of the first uop) of an instruction decode.
// The structure is the same as for the normal decode functions.
//

static const char* undefined_source(std::uint32_t insn_bytes [[maybe_unused]])
{{
    return nullptr;
}}

{decode_source_func_defns}

using decode_source_func_t = const char* (*)(std::uint32_t);

static decode_source_func_t decoder_source_funcs[{insn_count}] = {{
    undefined_source,
{decode_source_func_table}
}};

const char* decode_source(std::uint32_t insn_bytes, DecoderMode decoder_mode)
{{
    DecoderInput di(insn_bytes, decoder_mode);
    return decoder_source_funcs[insn_decode(di.ptr())](insn_bytes);
}}

{decode_fusion_func_defns}

using decode_fusion_func_t = UopSet (*)(std::uint32_t*);

static decode_fusion_func_t decoder_fusion_funcs[{fusion_count}] = {{
    nullptr,
{decode_fusion_func_table}
}};

std::pair<FusionType, UopSet> decode_fusion(std::uint32_t* insn_bytes, bool skip_checks, DecoderMode decoder_mode)
{{
    DecoderInput di(insn_bytes[0], insn_bytes[1], decoder_mode);
    auto dec_id = fusion_decode(di.ptr());
    UopSet uops;
    UopSet empty;

    if (dec_id == 0)
        return std::make_pair(FusionType::INVALID, std::move(empty));

    // Fusion is only possible if both instructions decode to a single uop
    if (decode(insn_bytes[0], decoder_mode).size() != 1 ||
        decode(insn_bytes[1], decoder_mode).size() != 1)
        return std::make_pair(FusionType::INVALID, std::move(empty));

    if (dec_id >= 0 && dec_id < {fusion_count})
       uops = decoder_fusion_funcs[dec_id](insn_bytes);

    if (uops.size() == 1) {{
        return fusion_opnd_check(uops, insn_bytes, skip_checks);
    }}

    // Unknown fusion found
    return std::make_pair(FusionType::INVALID, std::move(uops));
}}

InstInfo inst_info_tab[] = {{
    {inst_info_entries}
}};

const InstInfo* decode_inst_info(std::uint32_t insn_bytes)
{{
    DecoderInput di(insn_bytes);
    auto idx = inst_info_decode(di.ptr_no_mode());
    return idx > 0 ? &inst_info_tab[idx] : nullptr;
}}

InstInfoFusion inst_info_fusion_tab[] = {{
    {inst_info_fusion_entries}
}};

const InstInfoFusion* decode_inst_info_fusion(std::uint32_t* insn_bytes)
{{
    DecoderInput di(insn_bytes[0], insn_bytes[1], DecoderModeDefault);
    auto idx = inst_info_fusion_decode(di.ptr_no_mode());
    return idx > 0 ? &inst_info_fusion_tab[idx] : nullptr;
}}


static bool registered_decoder = register_decoder("{ns}",
    {{decode, decode_fusion, decode_source, decode_inst_info, decode_inst_info_fusion}});

}}  // namespace ampere::archx::{ns}
"""

    def __init__(self, ns_name: str, fusion: bool = False, split_decoder: int = 1):
        super().__init__()
        self.ns_name = ns_name
        # number of files we should generate for the decoder
        self.split_decoder = split_decoder
        self.insn_index = 0
        self.fusion_index = 0
        self.pat_out = open(f"{self.ns_name}.patterns", "w")
        if fusion:
            self.fusion = True
            self.fusion_pat_out = open(f"{self.ns_name}.fusion.patterns", "w")
        else:
            self.fusion = False
        self.decode_func_table = []
        self.decode_func_decl = []  # headers
        self.decode_func_defns = []  # function bodies
        self.decode_source_func_table = []
        self.decode_source_func_defns = []
        self.decode_fusion_func_table = []
        self.decode_fusion_func_defns = []
        self.inst_info_entries = ""
        self.uop_info_entries = ""
        self.uopcodes = set()

    def generate_outputs(self):
        if self.fusion:
            hfile_template = CreateDecoderClass.HFILE_TEMPLATE_WITH_FUSION
            ccfile_template_main = CreateDecoderClass.CCFILE_TEMPLATE_WITH_FUSION
            cc_dec_file_template = CreateDecoderClass.CC_DEC_FILE_TEMPLATE_WITH_FUSION
        else:
            hfile_template = CreateDecoderClass.HFILE_TEMPLATE
            ccfile_template_main = CreateDecoderClass.CCFILE_TEMPLATE
            cc_dec_file_template = CreateDecoderClass.CC_DEC_FILE_TEMPLATE

        year = datetime.date.today().year
        with open(f"{self.ns_name}.h", "w") as hfile_out:
            hfile_out.write(hfile_template.format(ns=self.ns_name, year=year))

        # internal header for decoder functions
        with open(f"{self.ns_name}-int.h", "w") as hfile_out:
            hfile_out.write(
                CreateDecoderClass.HFILE_INTERNAL_TEMPLATE.format(
                    ns=self.ns_name, year=year, decode_func_decls="\n".join(self.decode_func_decl)
                )
            )

        # the main decoder file (without the uop fill-in-functions)
        with open(f"{self.ns_name}.cc", "w") as ccfile_out:
            ccfile_out.write(
                ccfile_template_main.format(
                    ns=self.ns_name,
                    year=year,
                    # decode_func_table refers to external functions from the numbered files we generate below
                    decode_func_table="\n".join(self.decode_func_table),
                    decode_source_func_defns="\n".join(self.decode_source_func_defns),  # large
                    decode_source_func_table="\n".join(self.decode_source_func_table),
                    decode_fusion_func_defns="\n".join(self.decode_fusion_func_defns),  # large
                    decode_fusion_func_table="\n".join(self.decode_fusion_func_table),
                    insn_count=self.insn_index + 1,
                    fusion_count=self.fusion_index + 1,
                    inst_info_entries=self.inst_info_entries,
                    inst_info_fusion_entries=self.inst_info_fusion_entries,
                    uop_info_entries=self.uop_info_entries,
                )
            )

        # the functions that fill in the details of the uops from the xlat & yaml
        n = len(self.decode_func_defns)
        chunk_size = max(n // self.split_decoder, 1)
        dec_lo = 0
        for file_num in range(self.split_decoder):
            if file_num == self.split_decoder - 1:
                dec_hi = n  # make sure to grab all the definitions in the last file
            else:
                dec_hi = dec_lo + chunk_size
            decode_func_defns_chunk = self.decode_func_defns[dec_lo:dec_hi]
            dec_lo = dec_hi  # for next iteration
            with open(f"{self.ns_name}-{file_num}.cc", "w") as ccfile_out:
                ccfile_out.write(
                    CreateDecoderClass.CCFILE_TEMPLATE_UOP_DEFN.format(
                        ns=self.ns_name, year=year, decode_func_defns="\n".join(decode_func_defns_chunk)
                    )
                )  # large

        # the dectab-generated byte-by-byte decoders
        with open(f"{self.ns_name}-dec.cc", "w") as cc_dec_file_out:
            cc_dec_file_out.write(cc_dec_file_template.format(ns=self.ns_name, year=year))

        self.pat_out.close()

        with open(f"{self.ns_name}.inst_info.patterns", "w") as inst_info_patterns:
            for patterns in self.inst_info_buckets.values():
                print("|".join(patterns), file=inst_info_patterns)
        with open(f"{self.ns_name}.inst_info_fusion.patterns", "w") as inst_info_fusion_patterns:
            for patterns in self.inst_info_fusion_buckets.values():
                print("|".join(patterns), file=inst_info_fusion_patterns)

        with open(f"{self.ns_name}.uopcodes", "w") as uopcodes:
            for uopcode in self.uopcodes:
                print(uopcode, file=uopcodes)

        if self.fusion:
            self.fusion_pat_out.close()

        with open(f"{self.ns_name}_decoder_mode.h", "w") as enum_out:
            print(self.mode_enum, file=enum_out)

    def _check_imm_sign_and_shift(self, uop):
        """Ensure that xlat imm_shift agrees with yaml offset_lsl and
        that xlat imm_signext agrees with yaml offset_ext"""
        if hasattr(uop, "imm_shift"):
            imm_shift: int = getattr(uop, "imm_shift")
            if hasattr(uop, "offset_lsl"):
                offset_lsl: int = getattr(uop, "offset_lsl")
                if imm_shift != offset_lsl:
                    sys.stderr.write(
                        f"imm_shift does not match offset_lsl for uop {uop.name}: {imm_shift} vs {offset_lsl}\n"
                    )
            else:
                sys.stderr.write(f"imm_shift exists but missing offset_lsl for uop {uop.name}\n")

        if hasattr(uop, "imm_signext"):
            imm_signext: bool = getattr(uop, "imm_signext")
            if hasattr(uop, "offset_ext"):
                imm_signext_str = "sign" if imm_signext else "zero"
                offset_ext: str = getattr(uop, "offset_ext")
                if imm_signext_str != offset_ext:
                    sys.stderr.write(
                        f"imm_signext does not match offset_ext for uop {uop.name}: {imm_signext_str} vs {offset_ext}\n"
                    )
            else:
                sys.stderr.write(f"imm_signext exists but missing offset_ext for uop {uop.name}\n")

    def handle_instruction(self, insn, patterns):
        field_extractions = "\n    ".join([self.extract_field(f) for f in insn.fields.__dict__.values()])
        build_uops = "\n    ".join([self.build_uop(insn, uop, i, len(insn.uops)) for i, uop in enumerate(insn.uops)])

        self.decode_func_table.append(f"        decode_{self.insn_index},")

        self.decode_func_decl.append(f"UopSet decode_{self.insn_index}(std::uint32_t insn_bytes);")

        self.decode_func_defns.append(
            """
// {source_file}:{line}
UopSet decode_{index}(std::uint32_t insn_bytes [[maybe_unused]])
{{
    UopSet uops{{}};
    {field_extractions}
    {build_uops}
    return uops;
}}
""".format(
                index=self.insn_index,
                field_extractions=field_extractions,
                build_uops=build_uops,
                num_uops=len(insn.uops),
                source_file=project_relative(insn.filename),
                line=insn.lineno,
            )
        )

        self.decode_source_func_table.append(f"        decode_source_{self.insn_index},")

        self.decode_source_func_defns.append(
            """
// {source_file}:{line}
static const char* decode_source_{index}(std::uint32_t insn_bytes [[maybe_unused]])
{{
    static const char* source = "{klass}:{source_file}:{uop_lines}";
    return source;
}}
""".format(
                index=self.insn_index,
                num_uops=len(insn.uops),
                klass=insn.__class__.__name__,
                source_file=project_relative(insn.uops[0].filename),
                line=insn.lineno,
                uop_lines=";".join([str(u.lineno) for u in insn.uops]),
            )
        )

        print(f"#line {project_relative(insn.filename)}:{insn.lineno}", file=self.pat_out)
        print("|".join([p.replace(" ", "") for p in patterns]), file=self.pat_out)

        self.insn_index += 1

    def extract_field(self, field):
        return f"std::uint32_t f_{field.name} = extract(insn_bytes, {field.hi}, {field.lo});"

    def handle_fusion(self, fusion, patterns):
        if not self.fusion:
            return

        field_extractions = ""

        for idx, insn in enumerate(fusion.translations):
            field_extractions += (
                "\n    ".join([self.extract_field_fusion(f, idx) for f in insn.fields.__dict__.values()]) + "\n"
            )

        build_uops = "\n    ".join(
            [self.build_uop(fusion, uop, i, len(fusion.uops)) for i, uop in enumerate(fusion.uops)]
        )

        self.decode_fusion_func_table.append(f"        decode_fusion_{self.fusion_index},")

        self.decode_fusion_func_defns.append(
            """
// {source_file}:{line}
static UopSet decode_fusion_{index}(std::uint32_t* insn_bytes)
{{
    UopSet uops{{}};
    {field_extractions}
    {build_uops}
    return uops;
}}
""".format(
                index=self.fusion_index,
                field_extractions=field_extractions,
                build_uops=build_uops,
                num_uops=len(fusion.uops),
                source_file=project_relative(fusion.filename),
                line=fusion.lineno,
            )
        )

        print(f"#line {fusion.filename}:{fusion.lineno}", file=self.fusion_pat_out)
        print("|".join([p.replace(" ", "") for p in patterns]), file=self.fusion_pat_out)

        self.fusion_index += 1

    def extract_field_fusion(self, field, idx):
        prefix = "f_" if idx == 0 else f"f{idx}_"
        return f"std::uint32_t {prefix}{field.name} = extract(insn_bytes[{idx}], {field.hi}, {field.lo});"

    def build_uop(self, insn, uop, index, num_uops):
        uop_fields = {}
        uop_fields["opcode"] = f"Uopcode::{uop.name.upper()}"
        _dbg(f"BUILDING {uop.name}")
        self.uopcodes.add(uop.name.upper())

        set_if(uop_fields, uop, "use_pc", default=False)
        set_if(uop_fields, uop, "set_imm_to_pc_next", default=False)

        if hasattr(uop, "tmpflag_dest"):
            uop_fields["produce_tmpflags"] = "true"

        # Furiosa/Banshee use xlat&yaml with flag_dest=1 to indicate
        # flags writers.
        #
        # Trinity/Connor have flag_dest=1 only in yaml but use
        # flagdest=whatever in xlat to specify which flag is written.
        #
        # So we check flagdest first to avoid double setting the
        # produce_flags & produce_tmpflags booleans upon seeing
        # flag_dest set in the yaml on the chips >= Trinity.
        flagdest = getattr(uop, "flagdest", None)
        if flagdest is not None:
            if int(flagdest) == 0:
                uop_fields["produce_flags"] = "true"
            elif int(flagdest) == 1:
                uop_fields["produce_tmpflags"] = "true"
            else:
                print(f"Unexpected flagdest value {flagdest}", file=sys.stderr)
        elif getattr(uop, "flag_dest", None) or getattr(uop, "NZCV_out", "N") == "Y":
            uop_fields["produce_flags"] = "true"

        self.set_reg(uop_fields, insn, uop, "dst", ["dst", "dest", "dest1"], is_stack_pointer="dest_is_stack_pointer")
        self.set_reg(uop_fields, insn, uop, "dst2", ["dest2", "dst2"])
        self.set_reg(uop_fields, insn, uop, "src1")
        self.set_reg(uop_fields, insn, uop, "src2")
        self.set_reg(uop_fields, insn, uop, "src3")
        self.set_reg(uop_fields, insn, uop, "pred")

        if index == 0:
            uop_fields["start_inst"] = "true"

        if index == num_uops - 1:
            if isinstance(insn, Fusion):
                uop_fields["end_inst"] = len(insn.translations)
            else:
                uop_fields["end_inst"] = 1

        set_if(uop_fields, uop, "ldp_2nd_uop", default=False)
        set_if(uop_fields, uop, "pre_drain", default=False)

        width, value = self.get_imm(uop)
        if value != 0:
            uop_fields["imm"] = value
        if width != 0:
            uop_fields["imm_width"] = width
        set_if(uop_fields, uop, "imm_signext", default=False)
        set_if_present(uop_fields, uop, "imm_shift")
        # Uncomment this to check yaml/xlat for inconsistencies
        # self._check_imm_sign_and_shift(uop)

        #
        # If the instruction needs to be isolated (decoded by itself) then
        # attach that property to its first and last uop.
        #
        if index == 0 or index == num_uops - 1:
            set_if(uop_fields, insn, "isolate", default=False)

        # If the instruction is forbidden or restricted from cleaving, attach that to
        # each of its uops (in practice these are all single-uop flows anyway).
        #
        set_if(uop_fields, insn, "no_cleave", default=False)
        set_if(uop_fields, insn, "cleavlet0_only", default=False)

        # Internal cleaving flags
        #
        set_if(uop_fields, uop, "cleavlet0", default=False)
        set_if(uop_fields, uop, "cleavlet1", default=False)

        # Finally, attach the static uop info to this uop
        uop_fields["info"] = f"&uop_info_tab[{uop_info.UopInfo(uop).index}]"

        return (
            "\n    auto uop{} = std::make_unique<Uop>();\n    ".format(index)
            + "\n    ".join(["uop{}->{} = {};".format(index, k, v) for k, v in uop_fields.items()])
            + "\n    uops.emplace_back(std::move(uop{}));".format(index)
        )

    def set_reg(self, uop_fields: dict, insn, uop, name: str, source_names: list[str] = None, is_stack_pointer=None):
        if source_names is None:
            source_names = [name]
        _dbg(f"\tname={name} source_names={source_names}")
        if is_stack_pointer is None:
            is_stack_pointer = f"{name}_is_stack_pointer"

        all_ones_is_sp = getattr(insn, is_stack_pointer, False)

        pred = False
        for source_name in source_names:
            # When present, r appears it can be be many different
            # things. It can be: (1) xlat.signal.Signal object which
            # is a bit string of 1s and 0s, (2) a xlat.fields.Field
            # object or (3) a tuple of Fields an Signals or (4) a
            # string "flag"

            r = getattr(uop, source_name, None)
            rtype = getattr(uop, f"{source_name}_type", None)
            if rtype == "pred":
                pred = True
            ftype = getattr(uop, f"{source_name}t", None)
            _dbg(f"\tsource_name={source_name}, type={type(r)} r={r} rtype={rtype} ftype={ftype}")

            if hasattr(uop, is_stack_pointer):
                all_ones_is_sp = getattr(uop, is_stack_pointer)
            if r is not None:
                _dbg("\t\tbreaking...")
                break

        # I think this is a bug in the fsu_uops.yaml, but sometimes I see "src3t: e" and "src3_type: none" when
        # it is supposed to be "src3_type: vec". So this is a workaround.
        #
        _dbg(f"\t1){name}: rtype = {rtype}, ftype={ftype}")

        if rtype is None and ftype in set("nvxle"):
            rtype = "vec"
        _dbg(f"\t2){name}: rtype = {rtype}")
        # Final Hail Mary.
        if rtype is None:
            if pred:
                rtype = "pred"
            else:
                ## need to rule out dst2 = s0, s1 for things like gmovpair_d_lv
                if uop.unit == "fsu" and name not in ["dst2"]:
                    rtype = "vec"
                else:
                    rtype = "gpr"
        _dbg(f"\t3){name}: rtype = {rtype}")

        def get_field_name(r):
            if isinstance(r, Field):
                prefix = "f1_" if r.source.endswith("1") else "f_"  # fusion - FIXME: generalize!
                return f"{prefix}{r.name}"
            if isinstance(r, Signal):
                return str(int(r))
            raise RuntimeError(f"Unhandled object type: {type(r)}.")

        def tuple_of_fields_or_signals(r):
            """Return true iff r is a tuple of Field or Signal objects"""
            if isinstance(r, tuple):
                return all([isinstance(x, Field) or isinstance(x, Signal) for x in r])
            return False

        def get_prefix_list(r):
            assert isinstance(r, tuple)
            names = [get_field_name(x) for x in r]
            widths = [x.width for x in r]
            # reverse prefix sum of widths, removing first value, adding zero at end
            shift_counts = list(reversed(list(itertools.accumulate(reversed(widths)))))[1:] + [0]
            return zip(names, shift_counts)

        def get_name(r):
            if isinstance(r, Field):
                return get_field_name(r)
            if isinstance(r, tuple):
                # generate a piece of C code that shifts & ORs the specified field names together
                return "|".join([f"({name}<<{suffix_width})" for name, suffix_width in get_prefix_list(r)])
            raise RuntimeError(f"Could not create a name for {r.name}")

        def cvt_name(nm: str) -> str:
            if nm == "dst":
                return "dest"
            return nm

        # the predicate field names should always use cvt_pred. The
        # above complexity does not comprehend that, so we fix it here.
        if isinstance(r, Field):
            # check to see if predicates use the rtype "pred" and the
            # SVE vectors use "vec" cannot check AdvSIMD vectors since
            # they use R[dnm] etc like the base GPR ISA. The above
            # classification algorithm gets predicates wrong. The SVE
            # vectors were ok.

            nm = get_name(r)
            if any([x in nm for x in ["Pd", "Pn", "Pg", "Pm", "Pt", "Pdm", "Pdn"]]):
                rtype = "pred"

            if any([x in nm for x in ["Zd", "Zn", "Zm", "Zt", "Zdn", "Za", "Zda"]]):
                if rtype != "vec":
                    print(
                        f"VEC OPND ERROR: uop={uop.name} name={name} source_names={source_names} rtype={rtype}",
                        file=sys.stderr,
                    )
                    rtype = "vec"

        if isinstance(r, Field) or tuple_of_fields_or_signals(r):
            if all_ones_is_sp and rtype == "gpr":
                uop_fields[name] = f"cvt_{rtype}({get_name(r)}, LReg::SP)"
            else:
                # Various instructions (especially load/store-multiple) operate on several sequentially numbered
                # logical registers. The src/dest fields in the XLAT use the same register, and there's a
                # dest_inc, src1_inc, etc. field to say how much to increment the logical register number by.
                #
                # The CASP instructions use src1_even_inc (same for dest, etc.) which just sets the LSB of the
                # register instead of adding one. Since software is required to only use an even-numbered register
                # for these operands, we just implement that as a +1.
                inc_str = f"{cvt_name(name)}_inc"
                inc_str2 = f"{cvt_name(name)}_even_inc"
                if name in ["dst", "src1", "src2", "src3"] and hasattr(uop, inc_str):
                    inc_val = getattr(uop, inc_str)
                    uop_fields[name] = f"cvt_{rtype}(({get_name(r)}) + {inc_val})"
                elif name in ["dst", "src1", "src2", "src3"] and hasattr(uop, inc_str2):
                    uop_fields[name] = f"cvt_{rtype}(({get_name(r)}) + 1)"
                else:  # common case
                    uop_fields[name] = f"cvt_{rtype}({get_name(r)})"

        elif isinstance(r, Signal):
            if all_ones_is_sp and rtype == "gpr":
                uop_fields[name] = f"cvt_{rtype}({int(r)}, LReg::SP)"
            else:
                uop_fields[name] = f"cvt_{rtype}({int(r)})"
        elif r == "flag":
            uop_fields[name] = "LReg::FLAGS"
        elif r in ["ffr", "FFR"]:  # a special predicate reg
            uop_fields[name] = "LReg::FFR"
        elif not r:
            # r can be None when there is no such operand present so
            # we have to let that case slide.
            pass
        else:
            # we shoulda done something with r above; throw exception
            case = "tuple" if isinstance(r, tuple) else "other"
            raise RuntimeError(f"UNHANDLED {case} {type(insn)} {type(uop)} name={name} r={r}")

    def get_imm(self, uop):
        #
        # We need the immediate when it's used as an address offset for instructions with multiple
        # loads and stores. There's also a special case for load/store-pair of 128-bit registers,
        # where the immediate field is already in use by the instruction, so the imm_incr
        # field says to add 16 to the address.
        #
        if getattr(uop, "imm_incr", False):
            return 0, 16

        if hasattr(uop, "imm"):
            return self.get_imm_helper(uop.imm)
        return 0, 0

    @staticmethod
    def get_imm_helper(imm):
        if isinstance(imm, Signal):
            return imm.width, int(imm)
        if isinstance(imm, Field):
            return imm.width, f"f_{imm.name}"  # FIXME: prefix problem for fusion?
        if isinstance(imm, tuple):
            if len(imm) == 1:
                return CreateDecoderClass.get_imm_helper(imm[0])
            w_first, v_first = CreateDecoderClass.get_imm_helper(imm[0])
            w_rest, v_rest = CreateDecoderClass.get_imm_helper(imm[1:])
            return w_first + w_rest, f"(({v_first} << {w_rest}) | ({v_rest}))"
        raise RuntimeError("cannot handle complex immediate")


def _create_mode_enum(ns_name: str, all_modes_widths: list, all_modes_values: dict) -> str:
    """Create C++ mode enum, store it in a string. Boolean (1-bit)
    modes get ON/OFF suffixes. For multibit modes, we just use the
    mode value for lack of a better solution."""
    offset = 0
    enum = [f"namespace ampere::archx::{ns_name} {{", "enum DecoderMode : std::uint8_t {", "DecoderModeDefault = 0,"]
    for mode_name, nbits in reversed(all_modes_widths):  # all the (mode names, bit widths) in order
        for value in all_modes_values[mode_name]:  # all the observed values for a particular mode
            if nbits == 1:
                onoff = "OFF" if value == 0 else "ON"
                enum_name = f"DecoderMode_{mode_name.title()}_{onoff}"
            else:
                enum_name = f"DecoderMode_{mode_name.title()}_{value}"  # multibit mode
            s = f"{enum_name} = 0b{value:0{nbits}b}<<{offset},"
            enum.append(s)
        offset += nbits
    enum.append("};")
    enum.append("}")  # namespace
    return "\n".join(enum)


# called by archx/build_decoder.py
def create_decoder(create_decoder_class):
    all_modes_values = collections.defaultdict(set)  # dict modename -> set(values)
    all_modes_widths = []  # list of (mode_name, nbits), lower bits higher bits
    MAX_MODE_BITS_SUPPORTED = 8
    assert MAX_MODE_BITS_SUPPORTED % 8 == 0

    def make_mode_str(trans) -> str:
        """Return a binary + dontcares string describing the required
        mode. If no mode is specified, return a mode of dont-care bits
        ('-'). The mode must be a multiple if 8 bits long, so we pad
        modes with dontcares in the upper bits."""

        mode: dict = getattr(trans, "mode_bits", None)
        if mode:
            all_bits = []
            for mode_name, nbits in all_modes_widths:
                if mode_name in mode:
                    value = mode[mode_name]
                    bits = f"{value:0{nbits}b}"
                else:
                    bits = "-" * nbits  # this mode not releevant for this pattern; dontcare
                all_bits.append(bits)
            bits_str = "".join(all_bits)
            nmode_bits = len(bits_str)
            assert nmode_bits <= MAX_MODE_BITS_SUPPORTED
            # fill in upper non-mode bits with dontcares
            mode_string = "-" * (MAX_MODE_BITS_SUPPORTED - nmode_bits) + bits_str
            # print(f'MODE STRING: {mode_string}', file=sys.stderr)
        else:
            mode_string = "-" * MAX_MODE_BITS_SUPPORTED  # default mode, max_mode_bits_supported bits of dontcare
        return mode_string

    def gather_mode_bits(trans):
        mode: dict = getattr(trans, "mode_bits", None)
        if mode:
            # get the definition of the mode. each mode has a defined number of bits
            mode_width_dict = trans.__class__.mode_bits
            for mode_name, nbits in mode_width_dict.items():
                if mode_name not in all_modes_values:
                    # this list becomes the order of the mode bits
                    all_modes_widths.append((mode_name, nbits))
                # record the specific values used by this translation
                value = mode[mode_name]
                all_modes_values[mode_name].add(value)

    # first gather up all the mode bits as they can differ for each translation
    for trans_group, _ in translation_groups:
        for trans in trans_group:
            if isinstance(trans, Fusion):
                gather_mode_bits(trans.translations[0])
            else:
                gather_mode_bits(trans)

    # now do the work of building the decoder patterns
    for trans_group, _ in translation_groups:  # trans_group is a set of equivlent Translations.
        patterns = []
        representative_trans = None
        trans_group_sorted = sorted(trans_group, key=lambda x: x.uops[0].filename + str(x.uops[0].lineno))
        for trans in trans_group_sorted:
            if representative_trans is None:
                representative_trans = trans
            if isinstance(trans, Fusion):
                # put one mode on the front of the fusion instruction pair
                pattern = make_mode_str(trans.translations[0]) + " ".join([t.pattern for t in trans.translations])
            else:
                pattern = make_mode_str(trans) + trans.pattern
            patterns.append(pattern)
        if representative_trans.uops[0].name != "not_implemented":
            if isinstance(representative_trans, Fusion):
                create_decoder_class.handle_fusion(representative_trans, patterns)
            else:
                create_decoder_class.handle_instruction(representative_trans, patterns)

    # dict where keys are payload data and values are instruction bit
    # patterns that map to that payload
    create_decoder_class.inst_info_buckets = inst_info.build_inst_info()
    create_decoder_class.inst_info_entries = inst_info.buckets_to_entries(
        list(create_decoder_class.inst_info_buckets.keys())
    )

    # dict where keys are payload data and values are instruction bit
    # patterns that map to that payload
    create_decoder_class.inst_info_fusion_buckets = inst_info.build_inst_info_fusion()
    create_decoder_class.inst_info_fusion_entries = inst_info.fusion_buckets_to_entries(
        list(create_decoder_class.inst_info_fusion_buckets.keys())
    )

    create_decoder_class.uop_info_entries = ",\n".join([format(ui) for ui in uop_info.uop_info_tab])
    create_decoder_class.mode_enum = _create_mode_enum(create_decoder_class.ns_name, all_modes_widths, all_modes_values)
