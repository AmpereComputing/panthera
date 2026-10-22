#
# Copyright (c) 2022 Ampere Computing. All rights reserved.
# SPDX-License-Identifier: BSD-3-Clause
#
from __future__ import annotations
import re
import util

class field_t:
    """Support for binary strings with dontcare- bits (x)"""

    def __init__(self, name:str, hibit:int, ternbits:str, forbid:bool=False):
        """The parameter forbid is for when there is a "!=" in the
        field description and we want to include such fields in the
        forbidden value mask"""
        self.name: str = name
        self.hibit: int = hibit
        self.ternbits: str = ternbits
        self.forbid: bool = forbid

    def variable(self):
        return '-' in self.ternbits or self.forbid
    def constant(self):
        return  not self.variable()
    def get_name(self):
        return self.name

    def semi_unique_name(self):
        tname = re.sub(r'[\[]', '_', self.name)
        tname = re.sub(r'[]]', '_', tname)
        tname = re.sub(r'[<>]', '', tname)
        tname = re.sub(r':', 'to', tname)
        out = re.sub(r'[_]+', '_', tname)
        return out

    def unique_name(self):
        # used for code gen so need to remove special characters like
        # [x:y] that appear in name field.
        #_subfield = re.compile(r'(?P<field>\w+)<(?P<hibit>\d+):(?P<lobit>\d+)>')
        #_onebit = re.compile(r'(?P<field>\w+)<(?P<bit>\d+)>')

        tname = re.sub(r'[\[]', '_', self.name)
        tname = re.sub(r'[]]', '_', tname)
        tname = re.sub(r'[<>]', '_', tname)
        tname = re.sub(r':', 'to', tname)

        out = f'{tname}_{self.hibit}_{self.len()}'
        out = re.sub(r'[_]+', '_', out)
        return out

    _subfield = re.compile(r'(?P<field>\w+)<(?P<hibit>\d+):(?P<lobit>\d+)>')
    _onebit = re.compile(r'(?P<field>\w+)<(?P<bit>\d+)>')
    def flat_name(self):
        m = field_t._subfield.match(self.name)
        if m:
            return f"{m.group('field')}_{m.group('hibit')}_{m.group('lobit')}"
        m = field_t._onebit.match(self.name)
        if m:
            return f"{m.group('field')}_{m.group('bit')}"
        return self.name

    def is_forbid(self):
        return self.forbid

    def get_rexp(self):
        if self.is_forbid():
            n = len(self.ternbits)
            return f'!{n}:{self.ternbits}'
        return self.ternbits

    def __str__(self):
        if self.forbid:
            s = [self.name, str(self.hibit), "!=", self.ternbits]
        else:
            s = [self.name, str(self.hibit), self.ternbits]
        return " ".join(s)

    def len(self):
        return len(self.ternbits)

    def high_bit_idx(self):
        return self.hibit
    def low_bit_idx(self):
        return self.hibit - self.len() + 1

    def get_bits_for_subfield(self, fld:str) -> (int,int):
        """fld can be name<n> or name<n:m>. Return tuple of
        (hibit,lobit) where the range takes in the location of fld and
        picks off the specified <> qualifer if any."""
        m = util.search_onebit_specifier(fld)
        if m:
             bit = int(m.group('bit'))
             low = self.low_bit_idx()
             return (low+bit,low+bit)
        m = util.search_subfield_specifier(fld)
        if m:
            range_hibit = int(m.group('hibit'))
            range_lobit = int(m.group('lobit'))
            low = self.low_bit_idx()
            return (low+range_hibit, low+range_lobit)
        return (self.high_bit_idx(), self.low_bit_idx())


    def in_range(self, bitpos):
        return self.hibit >= bitpos >= self.low_bit_idx()

    def modify(self, bitpos: int, value: str):
        lst = list(self.ternbits)
        lst[self.hibit - bitpos] = value
        self.ternbits = "".join(lst)

    def get_bit(self, bitpos: int):
        """Returns a ternbit. This does not really make sense for forbid fields..."""
        lst = list(self.ternbits)
        value = lst[self.hibit - bitpos]
        return value

    def display_len(self):
        if self.forbid:
            return self.len() + 2  # plus 2 for "!="
        return self.len()

    def display_bits(self):
        if self.forbid:
            return f'!={self.ternbits}'
        return self.ternbits

    def required_mask(self):
        if self.forbid:
            return 0
        m = 0
        for b in self.ternbits:
            if b == '-':
                m = m << 1
            else:
                m = (m << 1) | 1
        return m

    def required_mask_str(self):
        if self.forbid:
            return '0' * self.len()
        m = []
        for b in self.ternbits:
            if b == '-':
                m.append('0')
            else:
                m.append('1')
        return "".join(m)

    def ignore_mask(self):
        return ~ self.required_mask()

    def required_bits_str(self):  # replace dontcares with 0s
        """1 in any bit position that must be 1/0.
           0 in any dontcare or forbidden (!=) field."""
        if self.forbid:
            return '0' * self.len()

        return re.sub('-', '0', self.ternbits)

    def forbid_mask_str(self):
        if self.forbid:
            return '1' * self.len()
        return '0' * self.len()

    def forbid_bits_str(self):
        if self.forbid:
            return self.ternbits
        return '0' * self.len()

    def constant_value(self):
        assert self.constant()
        return str(int(self.ternbits,2))
    def emit_extract(self, base_input:str='x'):
        if self.constant():
            return str(int(self.ternbits,2))
        ln = self.len()
        shift = self.hibit + 1 - ln
        mask = (1<<ln)-1
        if shift:
            return f'((({base_input})>>{shift})&0x{mask:x})'
        return f'({base_input}&0x{mask:x})'

    def function_name_or_constant(self):
        if self.constant():
            return self.constant_value()
        return self.function_name()
    def function_name(self):
        return f'field_{self.unique_name()}'
    def emit_extract_function(self):
        s = []
        s.append(f'static inline std::uint32_t {self.function_name()}(std::uint32_t x) {{')
        s.append(f'  return {self.emit_extract()};')
        if self.constant():
            s.append('  (void)x;') # avoid compiler warning about unused parameter
        s.append('}')
        return "\n".join(s)
