#!/usr/bin/env python3
#
# Copyright (c) 2022 Ampere Computing. All rights reserved.
# SPDX-License-Identifier: BSD-3-Clause
#
"""Miscellaneous utilities"""

from __future__ import annotations

import re
import sys
import typing


def msge(s: str):
    sys.stderr.write(s + '\n')

def hex32(n: int) -> str:
    return f'0x{n:08x}'

def bin32(v):
    return f'{v:032b}'

def die(m):
    sys.stdout.write(f'ERROR: {m}\n')
    sys.exit(1)

_dbg_file: typing.TextIO = None
def open_dbg_file():
    global _dbg_file
    _dbg_file = open('dbg.txt','w')

def dbg_file() -> typing.TextIO:
    global _dbg_file
    return _dbg_file


def _splatter(x:str) -> set:
    """Take a input string with 1/0/x create a set of integers
    containing all the possible values"""

    nbits = len(x)

    set_bits = []
    for i,v in enumerate(list(x)):
        if v in ['0','1']:
            set_bits.append((i,v))

    all_vals = set([])
    for i in range(1<<nbits):
        b = list(f'{i:0{nbits}b}')
        for p,v in set_bits:
            b[p]=v
        all_vals.add(int("".join(b),2))
    return all_vals

def splatter_binary(x:str) -> list:
    """Take a input string with 1/0/x create a list of binary strings
       containing all the possible values"""

    out = []
    if x.startswith('0b'):
        x = x[2:]
    nbits = len(x)
    #print(f"SPLIN {x} {nbits}")
    for v in _splatter(x):
        out.append(f'{v:0{nbits}b}')
    return out


def split_merge_tokens(inp:str) -> list[str]:
    """Split colon separated strings into a list. The trick is that
    some of the substrings have colons in in them for bit ranges. The
    bit ranges are <x:y> so we can piece them back together in a
    postpass."""
    if ":" in inp:
        merge = None
        result = []
        for a in inp.split(':'):
            if merge:
                assert '>' in a
                result.append(f'{merge}:{a}')
                merge = None
            elif '<' in a:
                if '>' in a:
                    result.append(a)
                else: # missing right side due to colon range that got split
                    merge = a
            else:
                result.append(a)
        return result

    return [inp]

_subfield = re.compile(r'(?P<field>\w+)<(?P<hibit>\d+):(?P<lobit>\d+)>')
_onebit = re.compile(r'(?P<field>\w+)<(?P<bit>\d+)>')

def search_subfield_specifier(s: str):
    return _subfield.search(s)
def search_onebit_specifier(s: str):
    return _onebit.search(s)

def extract_bits(inp: str) -> str:
    m = _onebit.match(inp)   # field<bit>
    if m:
        shift = m.group('bit')
        if shift == '0':
            return f'({m.group("field")} & 1)'
        return f'(({m.group("field")}>>{shift}) & 1)'

    m = _subfield.match(inp)  # field<hibit:lobit>
    if m:
        shift = m.group('lobit')
        nbits = int(m.group('hibit')) - int(m.group('lobit')) + 1
        mask = (1 << nbits) - 1
        if shift == '0':
            return f'({m.group("field")} & 0x{mask:x})'
        return f'(({m.group("field")}>>{shift}) & 0x{mask:x})'
    return inp

def flatten(lol: list) -> list:
    """Flatten a list of lists into a single flat list."""
    new_list = []
    for item in lol:
        if isinstance(item,list):
            new_list.extend(flatten(item))
        else:
            new_list.append(item)
    return new_list

if __name__ == "__main__":
    print(splatter_binary("1x11"))
    print(splatter_binary("000x"))
    print(flatten([1,[2,3],4,[5,[6,7]]]))
