#!/usr/bin/env python3
#
# Copyright (c) 2021-2024 Ampere Computing. All rights reserved.
# SPDX-License-Identifier: BSD-3-Clause
#
"""Generate an ADIS decoder (DFA), disassembler,
 and property support functions.  The decoder is built using the
 Brzozowski derivative of regular expressions.
"""

from __future__ import annotations

import argparse
import collections
import copy
import itertools
import re
import sys
import time
import typing
from pathlib import Path

import aarchmrs
import brz_dfa
import encoding
import exec_stats
import field
import instr
import util

msr_to_sysreg_func = { 'MRRS_RS_systemmovepr' : 'MRRS',
                       'MRS_RS_systemmove'    : 'MRS',
                       'MSR_SR_systemmove'    : 'MSR_reg',
                       'MSR_imm'              : 'MSR_imm',
                       'MSRR_SR_systemmovepr' : 'MSRR' }


def pragma_once(fout):
    print("#pragma once",file=fout)
def copyright(fout, script:bool = False):
    if script:
        cmt = '#'
    else:
        cmt = '//'
    print(f"{cmt} Copyright (c) 2021-2024 Ampere Computing. All rights reserved.", file=fout)

def namespace(f):
    print("namespace adis {",file=f)
def namespace_end(f):
    print("}",file=f)


gstats = exec_stats.execution_stats_t()

def find_bits_for_fields(de: instr.decorated_encoding_t,fields:set[str]):
    enc: encoding.encoding_t = de.get_enc()
    out = []
    for fld_name in fields:

        if '<' in fld_name:
            raw_fld_name = re.sub(r'<.*','',fld_name)
        else:
            raw_fld_name = fld_name

        fld: field.field_t = enc.get_field(raw_fld_name)
        if fld:
            (hi,lo) = fld.get_bits_for_subfield(fld_name)
            out.append(f'{raw_fld_name}:{hi}:{lo}')
        else:
            out.append(f'{fld_name}')
    return out

def _collect_operand_strings(des: list[instr.decorated_encoding_t]) -> set[str]:
    strings_set = set()
    for de in des:
        omit_curly_brackets = False
        for i,ao in enumerate(de.get_operands()):
            omit_curly_brackets |= ao.omit_subsequent_curly_brackets
            if ao.is_punctuation():
                if i > 0:
                    txt = ao.get_text(omit_curly_brackets)
                    if txt:
                        strings_set.add(txt)
    #print(f"string set {strings_set}",file=sys.stderr)
    return strings_set

def _emit_asm_pattern(offset: int, pattern_id: int, tokens: list[str], cf) -> int:
    """tokens is a list of strtab enum values. cf is the output stream"""
    # ADIS_STRTAB_INVALID -> variable, otherwise punctuation...
    # ADIS_STRTAB_LAST -> end of sequence
    """typedef struct { adis_strtab_enum_t enum; } adis_asm_pattern_t;
    """
    for i,enum_value in enumerate(tokens):
        if i == 0:
            print(f"{{ {enum_value} }}, // {offset} pattern_id={pattern_id} ntokens={len(tokens)}", file=cf)
        else:
            print(f"{{ {enum_value} }}, // {offset}", file=cf)
        offset += 1

    print(f'{{ADIS_STRTAB_LAST}}, // {offset} SENTINEL', file=cf) # sentinel for the group
    offset += 1
    return offset

def _emit_operand_schema(des: list[instr.decorated_encoding_t], string_to_strtab_map: dict[str,str] ):
    """write adis-asm-pattern.cc and adis-asm-pattern.h with the sequence of tokens for the disassembler.
    ADIS_STRTAB_INVALID is used as a marker to use adis_vdat_* field-based variables."""
    def secondary_vdat_qualifier(op):
        """These are things that don't increment the vdata element;
        they instead supply a number to refine the prior
        element. Admittedly fragile code."""
        if (re.search(r'<[dnmt]>',op.get_text()) or
            op.get_text() in ['<dn>','<n|SP>']):
            return True
        return False

    cfile = 'adis-asm-pattern.cc'
    hfile = 'adis-asm-pattern.h'
    offset = 0
    offset_dict = {}
    pattern_dict = {}
    pattern_max = 0
    with open(cfile,'w') as cf:
        copyright(cf)
        print(f'#include "{hfile}"', file=cf)
        namespace(cf)
        print("extern const adis_asm_pattern_t adis_asm_patterns[] = {", file=cf)

        for de in des:
            tokens = []
            omit_curly_brackets = False
            for i,ao in enumerate(de.get_operands()):
                #print(f'AOA: {de.get_mnemonic()}  i={i} punct={ao.is_punctuation()} text={ao.get_text()} curomit={ao.omit_subsequent_curly_brackets} omit_curly_brackets={omit_curly_brackets}', file=sys.stderr)
                omit_curly_brackets |= ao.omit_subsequent_curly_brackets
                if i > 0:
                    if ao.is_punctuation():
                        txt = ao.get_text(omit_curly_brackets)
                        if txt:
                            ao.string_enum = string_to_strtab_map[txt]
                            tokens.append(f'ADIS_STRTAB_{ao.string_enum}')
                    else:
                        if not secondary_vdat_qualifier(ao):
                            tokens.append('ADIS_STRTAB_INVALID') # means we have a adis_vdat_ field-based variable.

            key = " ".join(tokens)
            if key in pattern_dict: # we saw this pattern before...no codegen action required
                de.operand_pattern_id = pattern_dict[key]
                de.operand_pattern_offset = offset_dict[key]
            else: # new pattern, do code gen
                pattern_dict[key] = pattern_max
                de.operand_pattern_id = pattern_max
                de.operand_pattern_offset = offset
                offset_dict[key] = offset
                offset = _emit_asm_pattern(offset, pattern_max, tokens, cf)
                pattern_max += 1

        print("};", file=cf)
        namespace_end(cf)


def _build_set(r: list[instr.decorated_encoding_t]) -> set:
    """Construct a set with bit patterns representing the whole
    ISA. There are a couple of special cases that need additional
    handling as their decoding is not expressible as one pattern."""


    def _replace_at(slist:list, rlist:list, spos:int) -> list:
        """Replaced bits in slist with letters from rlist starting at
        spos. Bits in slist are numbered starting at 31 on the
        left. This will silently fail if there are not-equals
        constraints to the left of the insertion point so this is not
        general purpose."""
        nlist = copy.copy(slist)
        rlist.reverse()
        for i,c in enumerate(rlist):
            p = 31-(spos+i)
            assert slist[p] == '-'
            nlist[p]=c
        return nlist


    def _matches_instruction(record: instr.decorated_encoding_t, itext: int) -> bool:
        enc = record.get_enc()
        if (itext & enc.required_mask_num()) != enc.required_bits_num():
            return False
        if enc.requires_forbid_test() and (itext & enc.forbid_mask_num()) == enc.forbid_bits_num():
            return False
        return True

    def _splatter_hint(inum: int, de: instr.decorated_encoding_t, r: set, f: typing.TextIO,
                       all_records: list[instr.decorated_encoding_t]):
        """Emit only HINT encodings not claimed by a concrete instruction."""
        s = f'{de.get_rexp()}@{inum}'
        for n in range(128):
            b = f'{n:07b}'
            slist = _replace_at(list(s), list(b), 5)
            snew = "".join(slist)
            itext = int(snew[:32], 2)
            if any(other is not de and _matches_instruction(other, itext) for other in all_records):
                continue
            print(f'{i:4} {snew:42} {de.asm_string()}', file=f)
            r.add(snew)

    def _splatter_msr_imm(inum:int, de:instr.decorated_encoding_t, r:set, f: typing.TextIO):
        s = f'{de.get_rexp()}@{inum}'
        for n in range(64):
            b = f'{n:06b}'
            op1 = b[:3]   # dest 18:16
            op2 = b[3:]   # dest 7:5
            if op1 == '000' and op2 in ['000','001','010']:
                continue # skip CFINV, XAFLAG, AXFLAG
            slist = _replace_at(list(s), list(op1), 16)
            slist = _replace_at(slist,   list(op2), 5)
            snew = "".join(slist)
            print(f'{i:4} {snew:42} {de.asm_string()}', file=f)
            r.add(snew)

    with open('dec-patterns.txt', 'w') as f:
        retval = set([])
        for i,de in enumerate(r):
            # HINT and MSR_imm require decoder specialization to reserve
            # patterns claimed by more-specific instructions.
            inum = i + 1 # we reserve 0 as "undefined"
            de.set_inum(inum)
            if de.get_mnemonic()  == 'HINT':
                _splatter_hint(inum, de, retval, f, r)
            elif de.attr['inst_id']  == 'MSR_imm':
                _splatter_msr_imm(inum, de, retval, f)
            else:
                patterns = getattr(de, 'json_patterns', (de.get_rexp(),))
                for pattern in patterns:
                    s = f'{pattern}@{inum}'
                    print(f'{i:4} {s:42} {de.asm_string()}', file=f)
                    retval.add(s)
    return retval

def _emit_per_encoding_field_extractors(r: list[instr.decorated_encoding_t]):
    """Emits one switch() per encoding-named function with an calls to the
    appropriate field extractors for that encoding."""
    field_names = set()
    with open('adis-encoding-fields.h', 'w') as f:
        copyright(f)
        pragma_once(f)
        print("#include <cstdint>", file=f)
        print("#include <limits>", file=f)
        print("#include <stdexcept>", file=f)
        print('#include "fmt/core.h"', file=f)
        print('#include "adis-encoding-enum.h"', file=f)
        print('#include "adis-field-extract.h"', file=f)
        namespace(f)

        # gather the field names
        for de in r:
            for fld in de.enc.fields:
                # the r"field\d+" are internal made-up names. nothing references them.
                if not fld.name.startswith('field'):
                    field_names.add(fld.semi_unique_name())

        # emit an enum
        enum_name='fields_t'
        print(f"enum class {enum_name} {{", file=f)
        for fld_name in sorted(field_names):
            print(f"{fld_name},", file=f)
        print("}; // enum end", file=f)

        # emit prototype for the generic extractor
        print(f"std::uint32_t fldfrom(encoding_enum_t enc, std::uint32_t itxt, {enum_name} f);", file=f)

        # emit one function with a switch() per encoding class
        for de in r:
            enc_name = de.get_encoding_name()
            print(f"[[maybe_unused]] static std::uint32_t fldfrom_{enc_name}([[maybe_unused]] std::uint32_t itxt, [[maybe_unused]] {enum_name} f) {{", file=f)
            print("switch(f) {", file=f)
            for fld in de.enc.fields:
                # the r"field\d+" are internal made-up names. nothing references them.
                if not fld.name.startswith('field'):
                    #print(f"EFIELD {fld.name} {fld.ternbits=} {fld.variable()=}")
                    if fld.constant():
                        retval = fld.function_name_or_constant()
                    else:
                        retval = f'{fld.function_name()}(itxt)'
                    print(f"\tcase {enum_name}::{fld.semi_unique_name()}: return {retval};", file=f)
            print(f'\tdefault: throw std::runtime_error(std::string(fmt::format("fldfrom_{enc_name} bad field: {{}}", static_cast<int>(f))));', file=f)
            #print(f'\tdefault: throw std::runtime_error("adis::fldfrm_{enc_name} invalid field requested");', file=f)

            print("}\n}", file=f) # switch and function
        namespace_end(f)

    # emit a generic fldfrom function which takes a variable encoding an itext and the field
    with open('adis-encoding-fields.cc', 'w') as f:
        copyright(f)
        print("#include <cstdint>", file=f)
        print("#include <limits>", file=f)

        print('#include "adis-encoding-fields.h"', file=f)
        namespace(f)

        print(f"std::uint32_t fldfrom(encoding_enum_t enc, std::uint32_t itxt, {enum_name} fld) {{", file=f)
        print("switch(enc) {", file=f)
        for de in r:
            enc_name = de.get_encoding_name()
            print(f'case ENC_{enc_name}: return fldfrom_{enc_name}(itxt, fld);', file=f)
        print("default: return std::numeric_limits<std::uint32_t>::max();", file=f)
        print("}", file=f)  # switch
        print("}", file=f)  # function
        namespace_end(f)

def _emit_field_extractors(r: list[instr.decorated_encoding_t]):
    """This is not used internally by ADIS. It is primarily for users to extract
    fields as needed."""
    # unique-ify and count functions
    s = collections.Counter()

    for de in r:
        for fld in de.enc.fields:
            # the r"field\d+" are internal made-up names. nothing references them.
            if not fld.name.startswith('field'):
                if not fld.constant():
                    s[f'{fld.emit_extract_function()}'] += 1

    # sort functions by frequency
    #x = sorted([(k,v) for k,v in s.items()], key=lambda z: -z[1])
    x = sorted(s.items(), key=lambda z: -z[1])

    with open('adis-field-extract.h', 'w') as f:
        copyright(f)
        pragma_once(f)
        namespace(f)
        for k,v  in x:
            print(f'/*frequency={v}*/ {k}', file=f)
        namespace_end(f)

def _straight_concat(de : instr.decorated_encoding_t, ei : list[str], debug: bool = False) -> str:
    """compute a boolean concatenation of the arguments (in ei) to pass to the
    semantic disassembly function."""

    if debug:
        print(f"EI LIST: {ei}", file=sys.stderr)
    # find the fields so we know the sizes of everything
    flds = []
    for estr in ei:
        # Look for qualified field names, like "imm5<4>".
        # Handle bit ranges when they appear.
        m = util.search_onebit_specifier(estr)
        if m:
            efld = m.group('field')
        else:
            efld = estr

        for fld in de.enc.fields:
            # imm5<4> (in estr)  starts with "imm5" (in fld)
            if efld == fld.get_name():
                # put fully qualified bit field in the 3rd element
                # of the tuple for later use in extracting the
                # bit(s).
                if m:
                    flds.append((fld, 1, estr))
                else:
                    flds.append((fld, fld.len(), estr))
                break

    if len(ei) != len(flds):
        x = ", ".join([str(q) for q in de.enc.fields])
        nms = ", ".join([fld.get_name() for fld,_,_ in flds])
        print(f"MISMATCH: {len(ei)} != {len(flds)},  ei: {ei} vs flds: {nms} --- {x}")

    assert len(ei) == len(flds)
    nbits = sum([length for _, length, _ in flds])
    s = []
    pos = nbits
    for fld, length, eifld in flds:
        if debug:
            print(f'{fld=} {length=} {eifld=}', file=sys.stderr)
        pos = pos - length
        if eifld: # like imm5<4>
            fname = util.extract_bits(eifld)
        else:
            fname = fld.flat_name()
        # shift the relevant bits to the right position
        if pos:
            s.append(f'({fname}<<{pos})')
        else:
            s.append(f'{fname}')

    return "|".join(s)

def _finalize_args(s: str) -> str:
    """Convert to comma separated list with vdis the first element"""
    return ",".join(['vdis', s])

def _iconcat(de: instr.decorated_encoding_t, ei:list[str], debug: bool = False) -> str:
    return _finalize_args(_straight_concat(de, ei, debug))


def _concat(lst: list[tuple[int,str]]) -> str:
    """Create an C expression to concatenate bits from strings
    representing a number of bits. Each field is (#bits, string) in a
    list. Most significant bits are first."""
    input_bit_widths = [x[0] for x in lst]
    field_expressions = [x[1] for x in lst]
    # reverse the bit widths, do prefix sum, reverse that, remove first element and tack on a zero.
    # rightmost chunk shifts by 0.
    shift_vals = list(reversed(list(itertools.accumulate(reversed(input_bit_widths)))))[1:] + [0]
    mask_vals = [ ((1<<x)-1) for x in input_bit_widths]
    chunks  = [f'((({x})&{m})<<{y})' for x,y,m in zip(field_expressions, shift_vals, mask_vals)]
    s = "|".join(chunks)
    return f'({s})'


def _use_decode_bit_masks_algorithm(de: instr.decorated_encoding_t, ei:list[str], logical: bool, size: int) -> (str,str):
    # pass std::optional to the consumer function. They can deal with any potential errors
    logical_true_false = 'true' if logical else 'false'
    if size == 64: # and for logicals: 'N' in ei:
        args = f'dec_imm13_64((N<<12)|(imms<<6)|immr, {logical_true_false})'
        tfunc = 'imm_optional_64'
    else:
        args = f'dec_imm13_32((imms<<6)|immr, {logical_true_false})'
        tfunc = 'imm_optional_32'
    return tfunc, _finalize_args(args)


def _use_simm9(de: instr.decorated_encoding_t, ei:list[str]) -> (str,str):
    tfunc = 'simm'
    if len(ei) == 1:
        args = f'sign_extend9({ei[0]})'
    else:
        args = 'sign_extend9((imm9h<<3)|imm9l)'
    return tfunc, _finalize_args(args)

def _use_simm14(de: instr.decorated_encoding_t, ei:list[str]) -> (str,str):
    tfunc = 'simm'
    assert len(ei) == 1
    args = f'sign_extend14({ei[0]})'
    return tfunc, _finalize_args(args)

def _use_simm19(de: instr.decorated_encoding_t, ei:list[str]) -> (str,str):
    tfunc = 'simm'
    assert len(ei) == 1
    args = f'sign_extend19({ei[0]})'
    return tfunc, _finalize_args(args)

def _use_simm21(de: instr.decorated_encoding_t, ei:list[str]) -> (str,str):
    tfunc = 'simm'
    if len(ei) == 1:
        args = f'sign_extend21({ei[0]})'
    else:
        args = 'sign_extend21((immhi<<2)|immlo)'
    return tfunc, _finalize_args(args)

def _use_simm26(de: instr.decorated_encoding_t, ei:list[str]) -> (str,str):
    tfunc = 'simm'
    assert len(ei) == 1
    args = f'sign_extend26({ei[0]})<<2'
    return tfunc, _finalize_args(args)

def _use_simm10_scaled(de: instr.decorated_encoding_t, ei:list[str]) -> str:
    args = 'sign_extend10((S<<9)|imm9)*8'
    return  _finalize_args(args)

def _sve_shift_alg(de: instr.decorated_encoding_t) -> bool: # ASL sensitive
    pcd = de.get_pseudocode()
    tgt = 'constant integer shift = (2 * esize) - UInt(tsize:imm3);'
    tgt2 = 'constant integer rot = (2 * esize) - UInt(tsize:imm3);'
    if tgt in pcd or tgt2 in pcd:
        return True
    return False

def _sve_shift2_alg(de: instr.decorated_encoding_t) -> bool: # ASL sensitive
    pcd = de.get_pseudocode()
    tgt = 'constant integer shift = UInt(tsize:imm3) - esize;'
    if tgt in pcd:
        return True
    return False


def _emit_decoding_actions(r: list[instr.decorated_encoding_t]):
    """This is to emit a semantic (typed) decoding of the
    fields. Another function will emit a disassembly based on this
    semantic decoding. """
    for de in r:
        print(f"Processing: {de.get_encoding_name()}")
        blist = []
        field_names = []
        blist.append('\t[[maybe_unused]] const std::uint32_t itext = adis->itext;')

        # part 1: extract the fields into variables
        for fld in de.enc.fields:
            skip = False
            if fld.get_name().startswith('field'):
                skip = True

            if not skip:
                fname = fld.flat_name()
                if fld.constant():
                    s = f"const std::uint32_t {fname} = {fld.constant_value()};"
                else:
                    rhs = fld.emit_extract('itext')
                    if fname == 'Rv' and de.get_encoding_name() in ['psel_p_ppi_']:
                        # PSEL W12...W15 is a special case field decode
                        rhs  = f'(3<<2)|{rhs}'
                    s = f"const std::uint32_t {fname} = {rhs};"
                if '[' in fname:
                    s = '// unused ' + s
                else:
                    field_names.append(fname)
                blist.append('\t'+s)



        # part 2: call functions to store the raw fields in
        # semantically meaningful containers in (adis_vdata_t*)
        for ao in de.get_operands():
            if ao.get_type() == 'register-prefix':
                tfunc = ao.get_text()
                args = 'vdis'
                u = f'\tadis_vdat_{tfunc}({args});'
                blist.append(u)
                continue
            link = ao.get_link()
            if link:
                explain = de.get_explanation(link)
                tfunc = ao.get_legal_function_root()
                ei:list = explain.get_encoded_in()  # becomes fn args
                assert isinstance(ei,list)
                if not ei:
                    util.msge(f"UNEXPECTED TOKEN WITH NO ENCODING: {de.get_encoding_name()} - {ao}")
                    assert 0

                variable_args = [util.extract_bits(x) for x in ei]
                args = [ "vdis" ]
                if variable_args and variable_args[0] != '':
                    args.extend(variable_args)
                args = ",".join(args)

                tbl = explain.get_table()
                if tbl:
                    tfunc2 = tbl.get_unique_name()

                    variable_args = [util.extract_bits(x) for x in tbl.get_inputs()]
                    args = [ "vdis" ]
                    if variable_args and variable_args[0] != '':
                        args.extend(variable_args)
                    args = ",".join(args)

                    u = f'\tadis_vdat_{tfunc2}({args});  // operand: {tfunc}'
                else: # FIXME: extract this clause to a function
                    if tfunc == 'shift' and ei == ['immh', 'immb']:
                        # Scalar shifts encode this operand as immh:immb.
                        # FIXME: SLI uses this formula inappropriately. Should be: ((immh<<3)|immb) - 64;
                        args = _finalize_args('128 - ((immh<<3)|immb)')
                    elif tfunc == 'index':
                        if ei == ['imm5']:
                            # AdvSIMD element indices encode their element
                            # size in the least significant set bit of imm5.
                            tfunc = 'index1'
                        else:
                            args = _iconcat(de,ei)
                    elif tfunc == 'R' and ei == ['imm5']:
                        # AdvSIMD scalar-register forms select W or X from
                        # the element-size encoding in imm5.
                        tfunc = 'R_from_imm5'
                    elif tfunc == 'const': # applies to 39 SVE instr
                        if ei == ['tszh', 'tszl','imm3']:
                            if _sve_shift_alg(de):
                                args = _finalize_args('shift_opnd(tszh, tszl, imm3)')
                            elif _sve_shift2_alg(de):
                                args = _finalize_args('shift2_opnd(tszh, tszl, imm3)')
                            else:
                                args = _iconcat(de,ei)
                    elif tfunc == 'imm': # applies to 34+18 SVE instr + more for SVE2 FP8
                        if ei in (['i3h', 'i3l'], ['i2h', 'i2l'], ['i4h', 'i4l']):
                            args = _iconcat(de,ei)
                        elif ei == ['a','b','c','d','e','f','g','h']: # 5 neon instr (FMOV,MOVI)
                            args = _iconcat(de,ei)
                        elif de.get_encoding_name().endswith("_log_imm") or de.get_uses_decode_bit_masks_algorithm_logical():
                            # The non-logical versions (like BFM, SBFM, UBFM) have immr and imms as separate operands
                            size = 64 if "_64_" in de.get_encoding_name() else 32
                            tfunc, args = _use_decode_bit_masks_algorithm(de,ei, True, size)
                        elif ei == ['imm9h', 'imm9l']: # signed 9b imm
                            tfunc, args = _use_simm9(de,ei)
                        elif ei == ['imm8h', 'imm8l']: # u8 EXT(sve)
                            args = _iconcat(de,ei)
                        elif ei == ['b5', 'b40']:
                            args = _iconcat(de,ei)
                        elif ei == ['CRm', 'op2']: # hint
                            args = _iconcat(de, ei)
                        elif ei == ['imm2', 'tsz']: # DUP(indexed,sve)
                            args = _iconcat(de,ei)
                        elif ei == ['i1', 'tszh','tszl']: # PSEL(sve)
                            args = _iconcat(de,ei)
                        elif ei == ['i1', 'tsz']:  # DUPQ (SVE2p1)
                            args = _iconcat(de,ei)
                        elif de.get_encoding_name() == 'MSR_SI_pstate' and ei == ['CRm', 'op1', 'op2']:
                            # The immediate operand is CRm.  The other two
                            # fields select the PSTATE field and are consumed
                            # by the preceding operand.
                            args = _finalize_args('CRm')
                    elif tfunc == 'Pd1' and ei == ['Pd']:  # SVE2p1 WHILE*
                        tfunc, args = 'Pd', _finalize_args('2*Pd')
                    elif tfunc == 'Pd2' and ei == ['Pd']:   # SVE2p1 WHILE*
                        tfunc, args = 'Pd', _finalize_args('2*Pd+1')
                    elif tfunc == 'Vm': # applies to 15 neon instr
                        if ei == ['M','Rm']:
                            args = _iconcat(de,ei)
                    elif tfunc in ['imm8']: # applies to 11 neon instr
                        if ei == ['a','b','c','d','e','f','g','h']:
                            args = _iconcat(de,ei)
                    elif tfunc == 'label':
                        if ei == ['immhi', 'immlo']:
                            tfunc, args = _use_simm21(de,ei)
                        if ei == ['imm9']:
                            tfunc, args = _use_simm9(de,ei)
                        if ei == ['imm14']:
                            tfunc, args = _use_simm14(de,ei)
                        if ei == ['imm19']:
                            tfunc, args = _use_simm19(de,ei)
                        if ei == ['imm21']:
                            tfunc, args = _use_simm21(de,ei)
                        if ei == ['imm26']:
                            tfunc, args = _use_simm26(de,ei)
                    elif tfunc == 'simm':
                        if ei == ['S', 'imm9']:
                            args = _use_simm10_scaled(de,ei)
                        elif ei == ['imm9']:
                            _,args = _use_simm9(de,ei)
                    elif tfunc == 'pimm':
                        if ei == ['imm12']:
                            scale = explain.get_pimm_scale()
                            args = _iconcat(de,ei) + f'*{scale}'
                    elif tfunc == 'systemreg':
                        tfunc = msr_to_sysreg_func[de.get_encoding_name()]
                        if ei == ['o0','op1','CRn','CRm','op2']:
                            #args = _iconcat(de,ei)
                            args = _finalize_args('sysreg_t(2|o0, op1, CRn, CRm, op2).as_int()')

                    if tfunc in ['d','n', 'dn']:
                        #print(f'dnvdat: {ao} {ao.operand_type:12} {ao.hover}')
                        if ao.operand_type == 'vfp':
                            tfunc = f'{tfunc}_vfp'

                    # Maintain ADIS's established predicate-register spelling.
                    if tfunc == 'PNn' and de.get_encoding_name() == 'cntp_r_pn_':
                        tfunc = 'Pn'
                    if tfunc == 'imm6' and de.get_encoding_name() == 'RPRFM_R_ldst_regoff':
                        args = _finalize_args(_concat([(1,'(option>>2)&1'), (1,'(option&1)'), (1,'S'), (3,'Rt&0x7')] )) # OMG...
                    if ao.signed_imm():
                        if tfunc == 'simm':
                            pass
                        elif tfunc in ['uimm', 'imm', 'imm1', 'imm2']:
                            # imm1 and imm2 are for the SVE INDEX instr
                            #print("CHANGING IMM to SIMM", file=sys.stderr)
                            tfunc = 'simm'
                        else:
                            print(f"WARNING SIMM not imm: {tfunc}", file=sys.stderr)
                    if tfunc in ['simm', 'label']:
                        if de.is_pcrel_plus():
                            tfunc += "_pcplus"
                        elif de.is_pcrel_minus():
                            tfunc += "_pcminus"
                        if de.get_encoding_name()  in ['ADRP_only_pcreladdr']:
                            tfunc += '_pcpage'
                    if tfunc in ['Zdn1', 'Zdn2', 'Zdn4'] and ei == ['Zdn']:
                        nbits = 0
                        for fld in de.enc.fields:
                            if fld.name == 'Zdn':
                                nbits = fld.len()
                                break
                        assert(nbits != 0)
                        scale = 2 if nbits == 4 else 4
                        offset = 0 if tfunc == 'Zdn1' else (scale-1)
                        args = _finalize_args(f'Zdn*{scale}+{offset}')

                    u = f'\tadis_vdat_{tfunc}({args});'
                    if tfunc == 'opt2':
                        u = f'// {u}' # comment it out
                blist.append(u)

        # part3: emit (void) "usages" to avoid compiler unused-variable warnings
        blist.append('\t' + "".join([f'(void){fldname};' for fldname in field_names]))
        de.deca = blist

    hfn = 'adis-decodeable-items.h'
    with open(hfn,'w') as h:
        copyright(h)
        pragma_once(h)
        print('#include "adis.h"',file=h)
        namespace(h)
        print("void adis_dec_detail(adis_data_t* adis, adis_vdata_t* vdis);",file=h)
        namespace_end(h)

    with open('adis-decodeable-items.cc', 'w') as f:
        copyright(f)
        print('#include <cassert>',file=f)
        print(f'#include "{hfn}"',file=f)
        print('#include "adis-opnd-helper.h"',file=f)
        namespace(f)
        for de in r:
            de.deca_fn_name = f"adis_deca_{de.get_encoding_name()}"
            de.deca_fn_header = f'static void {de.deca_fn_name}(adis_data_t* adis, [[maybe_unused]] adis_vdata_t* vdis)'
            de.deca_fn_body  =  [ f'{de.deca_fn_header}{{  // inum={de.get_inum()}' ]
            de.deca_fn_body.extend(de.deca)
            de.deca_fn_body.append('}')

            # function name, function, function header
            proto = de.ainst.get_prototype()
            print(f"// {proto}", file=f)
            for line in de.deca_fn_body:
                print(line, file=f)

        # could just pull the last one...
        max_inum = 1 + max([x.get_inum() for x in r])
        print("typedef void (*adis_dec_detail_fn_t)(adis_data_t* adis, adis_vdata_t* vdis);",file=f)
        print(f"static const adis_dec_detail_fn_t adis_dec_detail_tbl[{max_inum}] = {{ 0,/*invalid*/",file=f)
        for de in r:
            print(f'{de.deca_fn_name},',file=f)
        print("};",file=f)
        print(f"""void adis_dec_detail(adis_data_t* adis, adis_vdata_t* vdis) {{
    assert(adis->inum > 0 && adis->inum < {max_inum});
    (*adis_dec_detail_tbl[adis->inum])(adis,vdis);
}}""",file=f)
        namespace_end(f)


def _emit_disas_field(r: list[instr.decorated_encoding_t]):
    """Emit a simple raw field disassembly in C++. Lacks all semantic
    interpretation of the fields. This is a temporary thing while the
    actual disassembler is in progress"""
    for de in r:
        blist = []
        field_names = []
        first = True

        blist.append(f'\tos << "{de.get_mnemonic()} " << std::hex;')
        blist.append('\t[[maybe_unused]] const std::uint32_t itext = adis->itext;')
        for fld in de.enc.fields:
            skip = False
            if fld.get_name().startswith('field'):
                skip = True

            if not skip:
                fname = fld.flat_name()
                if fld.constant():
                    s = f"const std::uint32_t {fname} = {fld.constant_value()};"
                else:
                    s = f"const std::uint32_t {fname} = {fld.emit_extract('itext')};"
                if '[' in fname:
                    s = '// unused ' + s
                    skip = True
                else:
                    field_names.append(fname)
                blist.append('\t'+s)

            if not skip:
                if first:
                    first = False
                else:
                    blist.append('\tos << ", ";')

                if fld.len() <= 3:
                    hex_prefix=''
                else:
                    hex_prefix='0x'

                blist.append(f'\tos << "{fname}={hex_prefix}" << {fname};')

        blist.append('\tos << std::dec;')
        # (void) usages for avoiding compiler warnings
        s = ['\t']
        for fldname in field_names:
            s.append(f'(void){fldname}; ')
        blist.append("".join(s))
        de.decf = blist


    hfn = 'adis-field-disas.h'
    with open(hfn,'w') as h:
        copyright(h)
        pragma_once(h)
        print("#include <iostream>",file=h)
        print("#include <sstream>",file=h)
        print('#include "adis.h"',file=h)
        namespace(h)
        print("void adis_field_disas(adis_data_t const* d, std::ostream& o);",file=h)
        namespace_end(h)

    with open('adis-field-disas.cc', 'w') as f:
        copyright(f)
        print("#include <iomanip>",file=f)
        print("#include <cassert>",file=f)
        print(f'#include "{hfn}"',file=f)
        namespace(f)
        for de in r:
            de.decf_fn_name = f"adis_decf_{de.get_encoding_name()}"
            de.decf_fn_header = f'[[maybe_unused]] static void {de.decf_fn_name}(adis_data_t const* adis, std::ostream& os)'
            de.decf_fn_body  =  [ f'{de.decf_fn_header}{{  // inum={de.get_inum()}' ]
            de.decf_fn_body.extend(de.decf)
            de.decf_fn_body.append('}')

            # function name, function, function header
            proto = de.ainst.get_prototype()
            print(f"// {proto}", file=f)
            for line in de.decf_fn_body:
                print(line, file=f)

        # could just pull the last one...
        max_inum = 1 + max([x.get_inum() for x in r])
        print('typedef void (*field_dis_fn_t)(adis_data_t const* adis, std::ostream& os);',file=f)
        print(f"static const field_dis_fn_t adis_field_dis_fn[{max_inum}] = {{",file=f)
        print('0, //inum=0',file=f)
        for de in r:
            print(f'{de.decf_fn_name}, // inum={de.get_inum()}',file=f)
        print("};",file=f)

        print(f"""void adis_field_disas(adis_data_t const* d, std::ostream& o) {{
           assert(d->inum > 0 && d->inum < {max_inum});
           (*adis_field_dis_fn[d->inum])(d,o);
        }}""",file=f)
        namespace_end(f)

def _emit_table_strings(dtables: dict[str,list[instr.table_t]],
                        strings_set: set[str] ) -> dict[str,str]:
    """Gather the right hand sides of all the 'generic string'
    tables. The strings_set is a set of "extra" strings we use for
    punctuation in disassembly.  Return a dict of table
    right-hand-sides to enum values for the table strings. Emit an
    enum and string conversion table.    """

    def _convert_to_identifier(s:str) -> str:
        x = s
        x = re.sub(r'[ ]','space',x)
        x = re.sub(r'[.]','dot',x)
        x = re.sub(r'[,]','comma',x)
        x = re.sub(r'[_]','underscore',x)
        x = re.sub(r'[(]','leftparen',x)
        x = re.sub(r'[)]','rightparen',x)
        #x = re.sub(r'[:<]','_',x)
        x = re.sub(r'[:]','colon',x)
        x = re.sub(r'[<]','lessthan',x)
        x = re.sub(r'[|]','bar',x)
        x = re.sub(r'[>]','',x)
        x = re.sub(r'[-]','dash',x)
        x = re.sub(r'[#]','n',x)
        x = re.sub(r'[\[]','leftbracket',x)
        x = re.sub(r'[\]]','rightbracket',x)
        x = re.sub(r'/','slash',x)
        x = re.sub(r'!','bang',x)
        x = re.sub(r'[{]','leftbrace',x)
        x = re.sub(r'[}]','rightbrace',x)
        return x

    # also needed:
    # '[',']', for memory base regs
    # '{','}' for ld/str with multiple regs
    # '!' pre-index base-address updates
    # '/', before guard predicates
    # '.' before vector element specifiers

    table_strings = copy.deepcopy(strings_set) # start with the punctuation strings
    # adis_vdat_table_cleanup() classifies these architecture register
    # designators after a table lookup.  Keep the corresponding enum values
    # available even when a particular supported-entry selection does not
    # happen to produce every one through an operand table.
    table_strings.update({'B', 'H', 'S', 'D', 'Q', 'Z', 'W', 'X'})
    for tlist in dtables.values():
        for tbl in tlist:
            if tbl.get_table_type() in ['generic string', 'single letter']:
                for rhs in tbl.row_dict.values():
                    table_strings.add(rhs)

    table_strings  = sorted(table_strings) # make list
    #print(f'{table_strings=}',file=sys.stderr)

    table_rhs_to_enum = {}
    identifiers = set()
    for strng in table_strings:
        identifier =_convert_to_identifier(strng)
        table_rhs_to_enum[strng] = identifier
        print(f"{identifier} from {strng}", end='')
        if identifier in identifiers:
            print("Duplicate", end='')
        print('')
        identifiers.add(identifier)
    table_rhs_to_enum['(omitted)'] = 'null'
    print(f'{len(identifiers)=}  vs {len(table_strings)=}')
    # check for duplicates due to sloppy conversion
    assert len(identifiers) == len(table_strings)

    header_filename = 'adis-strtab-enum.h'
    with open('adis-strtab-enum.cc', 'w') as fc, open(header_filename,'w') as fh:
        copyright(fc)
        copyright(fh)
        pragma_once(fh)
        print('#include <cassert>',file=fh)
        print(f'#include "{header_filename}"',file=fc)
        namespace(fh)
        namespace(fc)
        print("typedef enum {",file=fh)
        print("static char const* adis_strtab_text[] = {",file=fc)
        print('  ADIS_STRTAB_INVALID,',file=fh)
        print('  "INVALID",',file=fc)
        for rhs,enum in table_rhs_to_enum.items():
            if enum == 'null':
                rhs = ''
            print(f'  ADIS_STRTAB_{enum}, // {rhs}',file=fh)
            print(f'  "{rhs}",',file=fc)
        print('  ADIS_STRTAB_LAST,',file=fh)
        print('  "LAST",',file=fc)
        print("} adis_strtab_enum_t;",file=fh)
        print("};",file=fc)
        proto = "const char* adis_strtab_enum2str(adis_strtab_enum_t x)"
        print(f'{proto};',file=fh)
        print(proto,file=fc, end='')
        print("""{
    assert(x < ADIS_STRTAB_LAST);
    return adis_strtab_text[x];
}""",file=fc)
        namespace_end(fh)
        namespace_end(fc)


    return table_rhs_to_enum


def _emit_table_functions(r: list[instr.decorated_encoding_t],
                          dtables: dict[str,list[instr.table_t]],
                          strings_set: set[str]) -> dict[str,str]:
    """the strings_set is a set of "extra" punctuation strings for
       disassembly. The dtables are the look-up tables we've
       collected.    Return the dict mapping strings to their enum name."""
    table_rhs_to_enum : dict[str,str] = _emit_table_strings(dtables, strings_set)
    emitted = set([])
    with open('adis-tables.cc', 'w') as f, open('adis-tables.h', 'w') as fh:
        copyright(f)
        copyright(fh)
        pragma_once(fh)
        print('#include "adis.h"',file=fh)
        print('#include <tuple>',file=f)
        print('#include <map>',file=f)
        print('#include "adis-tables.h"',file=f)
        namespace(fh)
        namespace(f)
        for de in r:
            for e in de.get_explanations().values():
                t = e.get_table()
                if t:
                    if t.get_unique_name() not in emitted:
                        proto,function_body = t.emit_function(table_rhs_to_enum)
                        print(function_body,file=f)
                        print(proto,file=fh)
                        emitted.add(t.get_unique_name())
        namespace_end(fh)
        namespace_end(f)
    return table_rhs_to_enum

def _uniqueify_tables(r: list[instr.decorated_encoding_t]) -> dict[str,list[instr.table_t]]:
    """Tables often have the same names but translate to different
    actions. This function studies the table inputs and outputs, and
    then finds the unique set of variations for each table referenced
    by the argument instruction list. The tables are given a
    unique_name corresponding to their variation.    """

    d = collections.defaultdict(list) # key -> [table]
    use_count = collections.Counter() #  count sharing
    for de in r:
        for _,e in de.get_explanations().items():
            t = e.get_table()
            if t:
                key = t.make_unique_key()
                found = False
                if key in d:
                    for v in d[key]:
                        if v == t: # custom equality operator for class table_t
                            found=True
                            t.set_unique_name(v.unique_name)
                            v.extend_idstr(t.idstr)
                            break
                if not found:
                    t.set_unique_name( f'{key}_variation{len(d[key])}' )
                    d[ key ].append(t)
                use_count[ key ] += 1

    # This next part is not strictly necessary; just a simplification step.
    # remove _varation0 from things with only one variation.
    for de in r:
        for _,e in de.get_explanations().items():
            t = e.get_table()
            if t:
                key = t.make_unique_key()
                if len(d[key]) == 1:
                    t.set_unique_name( key ) # remove "_varation0"

    # debug printing...
    for k,v in d.items():
        print(f"TABLE {k}   uses: {use_count[k]}   variations with this name: {len(v)}")
        for i,tbl in enumerate(v):
            print(f'{i}:', tbl)
        print()
    return d

def p2c_bool(x:bool) -> str:
    if x:
        return 'true'
    return 'false'

def _emit_encoding_enum(recs : list[instr.decorated_encoding_t]):
    header_fn = 'adis-encoding-enum.h'
    with open(header_fn, 'w') as f:
        copyright(f)
        pragma_once(f)
        print("#include <string>", file=f)
        namespace(f)
        print('typedef enum {', file=f)
        print('ENC_INVALID,',file=f)
        for i,rec in enumerate(recs):
            print(f'ENC_{rec.get_encoding_name()},',file=f)
        print('ENC_LAST',file=f)
        print('} encoding_enum_t;', file=f)
        print('encoding_enum_t str2encoding_enum(const char* p);', file=f)
        print('// use adis::encoding_name(encoding_enum_t) to convert to a string', file=f)
        namespace_end(f)

def _emit_sys_alias_qualifiers(recs: list[instr.decorated_encoding_t]):
    qualifiers = set()
    for rec in recs:
        qual = rec.get_sys_alias_qualifier()
        if qual != 'INVALID':
            qualifiers.add(qual)
    h_fn = 'adis-sys-alias-qualifiers.h'
    with open(h_fn, 'w') as fout:
        copyright(fout)
        pragma_once(fout)
        namespace(fout)
        print('enum class sys_alias_t {', file=fout)
        print('   INVALID,', file=fout)
        for qualifier in sorted(qualifiers):
            print(f'   {qualifier},',file=fout)
        print('   LAST', file=fout)
        print('};', file=fout)
        namespace_end(fout)

def _emit_encoding_enum_converter(recs: list[instr.decorated_encoding_t]):
    c_fn = 'adis-encoding-enum.cc'
    with open(c_fn, 'w') as f:
        copyright(f)
        print('#include <string>', file=f)
        print('#include "adis-encoding-enum.h"', file=f)
        namespace(f)

        # print an array of strings
        print("static const std::string enc_names[] = {", file=f)
        print('  "INVALID",', file=f)
        for rec in recs:
            ename = rec.get_encoding_name()
            print(f'  "{ename}",', file=f)
        print("};", file=f)

        # print a function that compares a given input to the above array
        print('''encoding_enum_t str2encoding_enum(const char* p) {
    const std::string s(p);
    for(encoding_enum_t indx = ENC_INVALID;  indx < ENC_LAST ; indx = static_cast<encoding_enum_t>(indx + 1) ) {
        if (s == enc_names[indx])
           return indx;
    }
    return ENC_INVALID;
}''', file=f)

        # disabled this. use adis::encoding_name() instead
        if 0:
            print('''
static const std::string unknown{"UKNOWN"};
const std::string& encoding_enum2str(encoding_enum_t r) {
    if (r < ENC_LAST) {
        return enc_names[r];
    }
    return unknown;
}''', file=f)

        namespace_end(f)


def _emit_static_properties(recs : list[instr.decorated_encoding_t]):
    methods = [('bool','pre_index', 'false' ),
               ('bool','post_index', 'false'),
               ('bool','predicated', 'false'),
               ('bool','takes_movprfx_unpred', 'false'),
               ('bool','takes_movprfx_pred', 'false'),
               ('bool','dit', 'false'),
               ('std::uint8_t ', 'vector_srcs', '0'),
               ('const char*', 'arm_id', '"UNDEFINED"'),
               ('const char*', 'mnemonic', '"UNDEFINED"'),
               ('const char*', 'encoding_name', '"UNDEFINED"'),
               ('const char*', 'asm_prototype', '"UNDEFINED"'),
               ('const char*', 'isa_feature', '"INVALID"'),
               ('const char*', 'iclass', '"INVALID"'),
               ('const char*', 'category', '"INVALID"'),
               ('std::uint32_t', 'asm_pattern_offset', '0'),  # FIXME: this should not be exposed externally
               ('bool', 'advsimd', 'false'),
               ('bool', 'advsimd_simd', 'false'),
               ('bool', 'sve_family', 'false'),
               ('bool', 'sve', 'false'),
               ('bool', 'sve2', 'false'),
               ('bool', 'sve_simd', 'false'),
               ('bool', 'sme', 'false'),
               ('bool', 'memop', 'false'),
               ('bool', 'load', 'false'),
               ('bool', 'load_ff', 'false'),
               ('bool', 'load_nf', 'false'),
               ('bool', 'store', 'false'),
               ('bool', 'gather', 'false'),
               ('bool', 'scatter', 'false'),
               ('bool', 'prefetch', 'false'),
               ('bool', 'branch', 'false'),
               ('bool', 'branch_cond', 'false'),
               ('bool', 'branch_indirect', 'false'),
               ('bool', 'branch_call', 'false'),
               ('bool', 'branch_return', 'false'),
               ('bool', 'exception', 'false'),
               ('bool', 'address_translation', 'false'),
               ('bool', 'eret_any', 'false'), # any of the eret variations (adis-properties.h has convenience functions)
               ('bool', 'gather_prefetch', 'false'),
               ('bool', 'atomic', 'false'),
               ('std::uint8_t', 'nregs', '0'),
               ('bool', 'sparse_unscaled', 'false'),
               ('bool', 'sparse_dsz_index', 'false'),
               ('bool', 'sparse_packed', 'false'),
               ('bool', 'sve_agen_reg_reg', 'false'),
               ('bool', 'sve_agen_reg_imm', 'false'),
               ('std::uint8_t', 'memop_esize', '0'),
               ('std::uint8_t', 'memop_msize', '0'),
               ('bool', 'divide', 'false'),
               ('bool', 'fp_divide', 'false'),
               ('bool', 'int_divide', 'false'),
               ('bool', 'divide_reversed', 'false'),
               ('bool', 'sqrt', 'false'),
               ('bool', 'rsqrt', 'false'),
               ('bool', 'ic', 'false'),
               ('bool', 'dc', 'false'),
               ('bool', 'tlbi', 'false'),
               ('bool', 'may_read_flags', 'false'),
               ('bool', 'may_write_flags', 'false'),

               ('sys_alias_t', 'sysql', 'sys_alias_t::INVALID'),
               #('agen_enum_t', 'agen_enum', 'agen_enum_t::INVALID'),
               ]

    header_fn = 'adis-static-iinfo.h'

    with open(header_fn, 'w') as f:
        copyright(f)
        pragma_once(f)
        print('#include <cstdint>', file=f)
        print('#include "adis-encoding-enum.h"', file=f)
        print('#include "adis-sys-alias-qualifiers.h"', file=f)
        namespace(f)
        for return_type, field_nm, _ in methods:
            print(f'{return_type} {field_nm}(encoding_enum_t r);', file=f)
        namespace_end(f)

    with open('adis-static-iinfo.cc', 'w') as f:
        copyright(f)
        print('#include <cassert>', file=f)
        print('#include "adis-const.h"', file=f)
        print(f'#include "{header_fn}"', file=f)
        namespace(f)
        print('struct adis_static_iinfo_t {',file=f)
        for return_type, field_nm, _ in methods:
            print(f'  {return_type} {field_nm};', file=f)
        print('};',file=f)
        print('static const struct adis_static_iinfo_t adis_static_iinfo[ADIS_MAX_INST] = {',file=f)

        s = ",".join([default_value for _, _, default_value in methods])
        print(f'/* 0 */ {{ {s}  }}, ', file=f)

        for i,rec in enumerate(recs):
            print(f'/* {i+1} */ {{'
                  f'{p2c_bool(rec.pre_index())},'
                  f'{p2c_bool(rec.post_index())},'
                  f'{p2c_bool(rec.predicated())},'
                  f'{p2c_bool(rec.takes_movprfx())},'
                  f'{p2c_bool(rec.takes_movprfx_pred())},'
                  f'{p2c_bool(rec.dit())},'
                  f'{rec.get_vector_srcs()},'
                  f'"{rec.get_instr_id()}",'
                  f'"{rec.get_mnemonic()}",'
                  f'"{rec.get_encoding_name()}",'
                  f'"{rec.asm_string()}",'
                  f'"{rec.get_feature()}",'
                  f'"{rec.instruction_class()}",'
                  f'"{rec.get_category()}",'
                  f'{rec.operand_pattern_offset},'
                  f'{p2c_bool(rec.is_advsimd())},'
                  f'{p2c_bool(rec.is_advsimd_simd())},'
                  f'{p2c_bool(rec.is_sve_family())},'
                  f'{p2c_bool(rec.is_sve())},'
                  f'{p2c_bool(rec.is_sve2())},'
                  f'{p2c_bool(rec.is_sve_simd())},'
                  f'{p2c_bool(rec.is_sme())},'
                  f'{p2c_bool(rec.is_memop())},'
                  f'{p2c_bool(rec.is_load())},'
                  f'{p2c_bool(rec.is_load_ff())},'
                  f'{p2c_bool(rec.is_load_nf())},'
                  f'{p2c_bool(rec.is_store())},'
                  f'{p2c_bool(rec.is_gather())},'
                  f'{p2c_bool(rec.is_scatter())},'
                  f'{p2c_bool(rec.is_prefetch())},'
                  f'{p2c_bool(rec.is_branch())},'
                  f'{p2c_bool(rec.is_branch_cond())},'
                  f'{p2c_bool(rec.is_branch_indirect())},'
                  f'{p2c_bool(rec.is_branch_call())},'
                  f'{p2c_bool(rec.is_branch_return())},'
                  f'{p2c_bool(rec.is_exception())},'
                  f'{p2c_bool(rec.is_address_translation())},'
                  f'{p2c_bool(rec.is_eret())},'
                  f'{p2c_bool(rec.is_gather_prefetch())},'
                  f'{p2c_bool(rec.is_atomic())},'
                  f'{rec.nregs()},'
                  f'{p2c_bool(rec.is_sparse_unscaled())},'
                  f'{p2c_bool(rec.get_sparse_index_sz()==64)},'
                  f'{p2c_bool(rec.is_sparse_packed())},'
                  f'{p2c_bool(rec.is_sve_agen_reg_reg())},'
                  f'{p2c_bool(rec.is_sve_agen_reg_imm())},'
                  f'{rec.get_memop_esize()},'
                  f'{rec.get_memop_msize()},'
                  f'{p2c_bool(rec.is_divide())},'
                  f'{p2c_bool(rec.is_fp_divide())},'
                  f'{p2c_bool(rec.is_int_divide())},'
                  f'{p2c_bool(rec.is_divide_reversed())},'
                  f'{p2c_bool(rec.is_sqrt())},'
                  f'{p2c_bool(rec.is_rsqrt())},'
                  f'{p2c_bool(rec.is_ic())},'
                  f'{p2c_bool(rec.is_dc())},'
                  f'{p2c_bool(rec.is_tlbi())},'
                  f'{p2c_bool(rec.may_read_flags())},'
                  f'{p2c_bool(rec.may_write_flags())},'
                  f'sys_alias_t::{rec.get_sys_alias_qualifier()},'
                  f'}},',
                  file=f)

        print('};',file=f)

        for return_type, field_nm, _  in methods:
            print(f'''{return_type} {field_nm}(encoding_enum_t r) {{
                assert(r < ADIS_MAX_INST);
                return adis_static_iinfo[r].{field_nm};
            }}''',file=f)
        namespace_end(f)

def _emit_disassembler(r):
    dtables = _uniqueify_tables(r)
    punctuation_strings_set  = _collect_operand_strings(r)
    string_to_strtab_map = _emit_table_functions(r,dtables, punctuation_strings_set)
    _emit_operand_schema(r, string_to_strtab_map)
    _emit_field_extractors(r)
    _emit_per_encoding_field_extractors(r)
    _emit_decoding_actions(r)
    # _emit_disas_field(r)  # a rudimentary field-based disassembler

    nodes = len(r) + 1
    with open('adis-const.h', 'w') as f:
        copyright(f)
        pragma_once(f)
        namespace(f)
        print(f'#define ADIS_MAX_INST {nodes}',file=f)
        namespace_end(f)


def _emit_decoder(ostates: list[brz_dfa.dfa_state_t]):

    # visit the nodes and renumber them to be sequential node 0 is the
    # starting node and also the name of any error node. Accepting
    # nodes are negative numbers.
    nodes = 0
    q = collections.deque()
    q.append(0)
    states_visited = set([])
    while q:
        node = q.pop()
        nodes += 1
        if node != 0:
            # we don't rename node 0, so all node names get one subtracted
            # from the number of nodes else we skip using node 1.
            ostates[node].renumbered_node = nodes-1
        for n in ostates[node].group_next_state:
            if n != 0:
                assert n != -1
                if not ostates[n].final:
                    if n not in states_visited:
                        states_visited.add(n)
                        q.appendleft(n)
    print(f'nodes={nodes}')

    # traverse the graph again, emitting the C code for the state machine

    with open('dectab.h', 'w') as f:
        copyright(f)
        pragma_once(f)
        print("#include <cstdint>",file=f)
        namespace(f)
        print(f'extern const std::int16_t states[{nodes}][256];', file=f)
        namespace_end(f)
    with open('dectab.cc', 'w') as f:
        copyright(f)
        print("#include <cstdint>",file=f)
        namespace(f)
        print(f'extern const std::int16_t states[{nodes}][256] = {{',file=f)
        q = collections.deque()
        q.append(0)
        states_visited = set([])
        while q:
            node = q.pop()
            nn = ostates[node].renumbered_node
            print(f"/*row {node}->{nn} */ {{",file=f, end='')
            for n in ostates[node].group_next_state:
                if n == 0:
                    print("0,", file=f, end='')
                else:
                    ns = ostates[n]
                    if ns.final:
                        print(f"-{ns.iid},",file=f, end='')
                    else:
                        print(f"{ns.renumbered_node},",file=f,end='')
                        if n not in states_visited:
                            states_visited.add(n)
                            q.appendleft(n)
            print('},',file=f) # newline
        print("};",file=f)
        namespace_end(f)


def _parse_args():
    parser = argparse.ArgumentParser()
    parser.add_argument('-v',
                        help="verbose",
                        action='store_true',
                        dest='verbose',
                        default=False)
    parser.add_argument('--aarchmrs',
                        help="input AARCHMRS Instructions.json file (optionally gzip-compressed)",
                        type=Path,
                        required=True)
    args = parser.parse_args()
    return args


def main():
    global gstats
    util.open_dbg_file()

    def _finish_time_delta(start, s='Elapsed time',newline=False):
        delta = time.perf_counter() - start
        if newline:
            print()
        print(f"{s} {delta:0.4f} seconds")

    args = _parse_args()
    start_time = time.perf_counter()
    r = aarchmrs.decorated_encodings(args.aarchmrs)

    #    universal_mask = bit_split.find_universal_mask(r)
    #    print(f'universal_mask={universal_mask:#010x}')
    #    bit_split.bin_nodes(args, r)

    # prepare instruction data and build DFA
    rset = _build_set(r)
    print(f"input_nodes = {len(rset)}")
    ostates, states, edges = brz_dfa.build_dfa(rset)
    print(f"nstates = {len(states)}")
    print(f"nedges = {len(edges)}")
    if args.verbose:
        brz_dfa.print_graph(states, edges)
    _emit_decoder(ostates)
    _emit_disassembler(r)
    _emit_sys_alias_qualifiers(r)
    _emit_static_properties(r)
    _emit_encoding_enum(r)
    _emit_encoding_enum_converter(r)


    gstats.print()
    _finish_time_delta(start_time)

def unicode_check():
    if not sys.stdout.encoding.lower().startswith('utf'):
        msg = ("FAILURE: unicode output NOT supported!\n\n"
               "Please add a setting for LANG to be en_US.UTF-8 to your startup dot files.\n"
               "$HOME/.cshrc:    setenv LANG en_US.UTF-8\n"
               "$HOME/.zshenv:   export LANG=en_US.UTF-8\n"
               "$HOME/.bashrc:   export LANG=en_US.UTF-8\n")
        sys.stderr.write(msg)
        sys.exit(1)

if __name__ == "__main__":
    unicode_check()
    main()
    sys.exit(0)
