#!/usr/bin/env python3
# Copyright (c) 2020 Ampere Computing. All rights reserved.
# SPDX-License-Identifier: BSD-3-Clause

"""
xlat_find.py - Find xlat translations matching a pattern.

Overview
========

    xlat_find.py <pattern>

where <pattern> can be:

    1) A binary pattern including "don't cares", e.g.:

           01101011000-----011110----------

    2) An integer opcode in any of the bases recognized by Python, e.g.:

           0x6b03009f                         # hexadecimal opcode
           1795358879                         # decimal opcode
           0b1101011000000110000000010011111  # binary opcode

    3) An (exact) instruction mnemonic from the ARM assembly representation, e.g:

           ADC

Patterns may include spaces, as long as the whole thing is quoted for
the benefit of the shell.

Output
======

The script will output every translation which matches the pattern, along
with the associated sequence of uops. For example "xlat_find.py 0x6b03009f"
might produce something like:

    Pattern matches 1 translations.

    SUBS_32_addsub_shift (/path/to/rtl/MAIN/uarch/xlat/integer.xlat:2694)
    01101011000000110000000010011111
        subs32(dest=Rd [31], flag_dest=1, src1=Rn [4], src2=Rm [3], imm=(010000, imm6 [0])) [line 2704]

Each translation is generated as:

    <translation name> (<xlat file path>:<line number>)
    <matching pattern>
        <uop 1>(<uop arguments...>) [line <uop line #>]

The uop arguments are translated back into field names if applicable, and if
the pattern contains a concrete value for a given field, that value is produced
in square brackets. Thus in the example above, the "Rd" field is 31 for the
specific opcode supplied on the command line.

Caveats/Bugs
============

When given an indefinite pattern (one with don't-cares), it's possible you'll
see many more translations that you expected. This is because translation
patterns are "exploded" based on how the decode function was written. If, for
example, the decode function makes decisions based on the value of a 3-bit
option field, the pattern is exploded into 8 possible variations depending on
the value of that field. Some of those decodes may be identical. Today, this
script will show you all of them anyway. A future enhancement will hopefully
limit output to unique uop sequences.

Fusion candidates are not yet reported.

Loading translations is _slow_. Working on fixing this.
"""

import os
import sys

sys.path.append(os.path.join(os.path.dirname(__file__), "..", "python", "xlat", "src"))

import argparse
import importlib
import re

import ampere.xlat
from ampere.xlat import Field, Fusion
from ampere.xlat.fields import NeedDefiniteValue
from ampere.xlat.pattern import Pattern


def main():
    args = parse_args()
    proj_root = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", args.proj))
    os.environ["PANTHERA_PROJECT_ROOT"] = proj_root
    sys.path.append(os.path.join(proj_root, "lib", "python3"))
    importlib.import_module("proj.xlat.all")
    print_matches(*find(args.opcode))


class PrintDocs(argparse.Action):
    def __init__(self, option_strings, dest, nargs=None, **kwargs):
        super().__init__(option_strings, dest, nargs=0, **kwargs)

    def __call__(self, parser, namespace, values, option_string=None):
        print(__doc__)
        exit(0)


def parse_args():
    parser = argparse.ArgumentParser(description="Find xlat translations matching a pattern.")
    parser.add_argument("opcode", help="Instruction opcode")
    parser.add_argument("--doc", action=PrintDocs, help="Print more documentation")
    parser.add_argument("-p", "--proj", help="Project name", default="furiosa")
    return parser.parse_args()


def find(opcode):
    opcode = opcode.replace(" ", "")

    find_mnemonic = None
    find_pattern = None

    if re.match("^[A-Za-z]", opcode):
        find_mnemonic = opcode
    elif re.match("^[01-]+$", opcode):
        find_pattern = Pattern(32)
        find_pattern.add_pattern(opcode)
    else:
        find_pattern = Pattern(32, val=int(opcode, base=0), mask=0xFFFFFFFF)

    assert find_mnemonic or find_pattern

    matches = []
    fusions = []

    for trans, _ in ampere.xlat.translations:
        if isinstance(trans, Fusion):
            for fused_trans in trans.translations:
                if find_mnemonic:
                    if fused_trans.asmtemplate.split()[0] == find_mnemonic:
                        fusions.append(trans)
                else:
                    if intersects(find_pattern, fused_trans.pattern):
                        fusions.append(trans)
        else:
            if find_mnemonic:
                if trans.asmtemplate.split()[0] == find_mnemonic:
                    matches.append(trans)
            else:
                intersection = intersects(find_pattern, trans.pattern)
                if intersection is not None:
                    trans.pattern = intersection.p_format()[0]
                    matches.append(trans)

    return matches, fusions


def intersects(find_pattern, trans_pattern):
    test_pattern = Pattern(32)
    test_pattern.add_pattern(trans_pattern)

    if find_pattern.p_intersects(test_pattern):
        return find_pattern.p_copy_and(test_pattern)
    else:
        return None


def print_matches(translations, fusions):
    print(f"Pattern matches {len(translations)} translations.\n")
    for trans in translations:
        print(f"{trans.__class__.__name__} ({trans.filename}:{trans.lineno})")
        print(f"{trans.pattern}")
        for uop in trans.uops:
            print(f"\t{uop.name}({build_arguments(trans, uop)}) [line {uop.lineno}]")
        print("")


def build_arguments(trans, uop):
    args = []

    for value in uop._args:
        args.append(arg_value_as_string(trans, value))

    for name, value in uop._kwargs.items():
        args.append(f"{name}={arg_value_as_string(trans, value)}")

    return ", ".join(args)


def arg_value_as_string(trans, value):
    if type(value) is tuple:
        return "(" + ", ".join(arg_value_as_string(trans, x) for x in value) + ")"
    elif isinstance(value, Field):
        value.pattern = trans.pattern
        try:
            numeric_value = value.value
            return f"{value.name} [{numeric_value}]"
        except NeedDefiniteValue:
            return value.name
    else:
        return str(value)


if __name__ == "__main__":
    main()
