#!/usr/bin/env python3
#
# Copyright (c) 2022 Ampere Computing. All rights reserved.
# SPDX-License-Identifier: BSD-3-Clause
#

import json
import argparse
import pprint
import types

def parse_args():
    """Parse command line arguments"""
    parser = argparse.ArgumentParser()
    parser.add_argument("-i",
                        default=None,
                        dest="ifn",
                        help="Input file name")
    parser.add_argument("--sve",
                        default=False,
                        dest="sve",
                        action="store_true",
                        help="Restrictd to SVE* instructions")
    parser.add_argument("--sve-summary",
                        default=False,
                        dest="sve_summary",
                        action="store_true",
                        help="Summarize SVE encodings")

    parser.add_argument("--flags",
                        default=False,
                        dest="flags",
                        action="store_true",
                        help="Print flags r/w info")
    
    parser.add_argument("--nep",
                        default=False,
                        dest="nep",
                        action="store_true",
                        help="Print FPCR.NEP info")

    parser.add_argument("--movprfx",
                        default=False,
                        dest="movprfx",
                        action="store_true",
                        help="Print movprfx info")

    parser.add_argument("--4b-predicate-guard",
                        default=False,
                        dest="predicate_guard_4bit",
                        action="store_true",
                        help="Print instr with 4b predicate guards info")

    parser.add_argument("--movprfx-fields",
                        default=False,
                        dest="movprfx_fields",
                        action="store_true",
                        help="Print movprfix-fusable fields")
    
    parser.add_argument("--program-counter",
                        default=False,
                        dest="program_counter",
                        action="store_true",
                        help="Print instr using program counter")
    args = parser.parse_args()
    assert args.ifn is not None
    return args

SVE_FEATURES = ["FEAT_SVE",
                "FEAT_F32MM",
                "FEAT_F64MM",
                "FEAT_I8MM",
                "FEAT_BF16",
                "FEAT_FP16",
                "FEAT_SVE2",
                "FEAT_SVE2p1",
                "FEAT_SVE_AES",
                "FEAT_SVE_BitPerm",
                "FEAT_SVE_PMULL128",
                "FEAT_SVE_SHA3",
                "FEAT_SVE_SM4",
                ]

def is_sve_class(v):
    """Return true if the feature is some kind of SVE/SVE2/etc. instr"""
    if 'advsimd' in v.instruction_class:
        return False
    if 'float' in v.instruction_class:
        return False
    if 'SVE' in v.feature:
        return True
    if v.feature in SVE_FEATURES:
        return True
    if 'sve' in v.instruction_class:
        return True
    return False

def reads_flags(v):
    """Return true if may read flags"""
    return v.may_read_flags

def writes_flags(v):
    """Return true if may write flags"""
    return v.may_write_flags

def dump_flags(args,data):
    """Dump the flags r/w info"""
    for encoding,v in data.items():
        if not args.sve or  is_sve_class(v):
            r = reads_flags(v)
            w = writes_flags(v)
            if r or w:
                print(f'{encoding:25} {v.feature:15} ',end='')
                if r and w:
                    print('reads/writes', end='')
                elif r:
                    print('reads', end='')
                elif w:
                    print('writes', end='')
                print(' flags')

def dump_movprfx(args,data):
    """Dump the movprfx  info"""
    for encoding,v in data.items():
        if v.movprfx or v.movprfx_pred:
            print(f'{encoding:25} {v.feature:15} {v.instruction_class:15} ',end='')
            if v.movprfx:
                print("movprfx ", end='')
            if v.movprfx_pred:
                print("movprfx_pred ", end='')
            print()


def dump_movprfx_fields(args,data):
    """Dump the movprfx fields  info"""
    def trim_fields(flds:list[str]):
        """Remove the register fields."""
        out = []
        for fld in flds:
            if fld[0] in ['Z','X','R','V','P']:
                continue
            out.append(fld)
        return out

    for encoding,v in data.items():
        if v.movprfx or v.movprfx_pred:
            nf = trim_fields(v.src_fields)
            #print(f'{encoding:25} {v.feature:15} {v.instruction_class:15} {v.dst_fields} {v.src_fields}')
            print(f'{encoding:25} {v.feature:15} {v.instruction_class:15} {nf}')

def dump_nep(args,data):
    """Dump the nep  info"""
    for encoding,v in data.items():
        if v.may_sse_merge:
            print(f'{encoding:25} {v.feature:15} {v.instruction_class:15}')

def dump_4bit_predicates(args,data):
    """Dump instr with 4b predicate guard fields"""
    def predicate_guard_4bit(f):
        if f.startswith('Pg'):
            a = f.split(':')
            return (int(a[1]) - int(a[2]) + 1) > 3
        return False

    for encoding,v in data.items():
        for f in v.src_fields:
            if predicate_guard_4bit(f):
                print(f'{encoding:25} {v.feature:15} {v.instruction_class:15} {v.asm_string}')


def dump_program_counter_uses(args,data):
    for encoding,v in data.items():
        if v.uses_program_counter:
            print(f'{encoding:25} {v.feature:15} {v.instruction_class:15} {v.asm_string}')


def dump_sve_summary(args,data):
    """Dump SVE encodings and sve-class"""
    odict = {}
    for encoding,v in data.items():
        if is_sve_class(v):
            exe_unit = 'int'
            if (encoding.startswith('ld') or
                encoding.startswith('st') or
                encoding.startswith('prf')):
                exe_unit = 'mem'
            else:
                for f in v.src_fields + v.dst_fields:
                    if f.startswith('Z'):
                        exe_unit = 'flt'
                        break

            uimm = ''
            simm = ''
            for opnd in v.operands:
                if opnd['uimm']:
                    uimm='UIMM'
                if opnd['simm']:
                    simm='SIMM'

            print(f'{encoding:25} {uimm:4} {simm:4} {exe_unit:3} {v.feature:15} {v.category:15} {v.instruction_class:15} {v.asm_string}')
            odict[encoding] = dict(feature=v.feature,
                                   exe_unit=exe_unit,
                                   iclass=v.instruction_class,
                                   asm=v.asm_string)
    with open("sve.json","w") as f:
        print(json.dumps(odict, sort_keys=True, indent=4), file=f)
def make_namespace(data):
    """Use types.SimpleNamespace to refer to fields as x.field instead
    of the more verbose x['field']. """
    out = {}
    for encoding,v in data.items():
        out[encoding] = types.SimpleNamespace(**v)
    return out

def work(args):
    """Main function"""
    with open(args.ifn) as f:
        data = json.load(f)
    data = make_namespace(data)
    if args.flags:
        dump_flags(args,data)
    elif args.movprfx:
        dump_movprfx(args,data)
    elif args.movprfx_fields:
        dump_movprfx_fields(args,data)
    elif args.nep:
        dump_nep(args,data)
    elif args.predicate_guard_4bit:
        dump_4bit_predicates(args,data)
    elif args.sve_summary:
        dump_sve_summary(args,data)
    elif args.program_counter:
        dump_program_counter_uses(args,data)
    else:
        for encoding,v in data.items():
            if not args.sve or  is_sve_class(v):
                print(f'{encoding}:')
                pprint.pprint(v)

if __name__ == "__main__":
    args = parse_args()
    work(args)
