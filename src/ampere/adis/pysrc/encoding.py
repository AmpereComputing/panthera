#
# Copyright (c) 2022 Ampere Computing. All rights reserved.
# SPDX-License-Identifier: BSD-3-Clause
#
from __future__ import annotations

import re
import sys

import field
import util


class encoding_t:
    """An instruction encoding. made up of lists of field_t objects"""

    # look for "field<hibit:lobit>"
    _subfield = re.compile(r'(?P<field>\w+)<(?P<hibit>\d+):(?P<lobit>\d+)>')
    _onebit = re.compile(r'(?P<field>\w+)<(?P<bit>\d+)>')

    def __init__(self):
        self.fields: list[field.field_t] = []
    def __str__(self):
        return " ".join([str(x) for x in self.fields])

    def get_rexp(self) -> str:
        s = [ f.get_rexp() for f in self.fields ]
        return "".join(s)

    def add(self, f: field.field_t):
        self.fields.append(f)

    def replace(self, f: field.field_t):
        for i, fld in enumerate(self.fields):
            if fld.hibit == f.hibit:
                if fld.len() != f.len():
                    print(f'WIDTH ERROR new: {f} vs old: {fld}')
                assert fld.len() == f.len()
                assert fld.name == f.name
                self.fields[i] = f
                return
        assert 0  # should not occur

    def merge(self, f: field.field_t):
        """If there are dontcares in the input f, then keep the
        original/current bit from the corresponding pre-existing
        field, otherwise replace current bits with bits from f."""
        for i, fld in enumerate(self.fields):
            if fld.hibit == f.hibit:
                if fld.len() != f.len():
                    print(f'WIDTH ERROR new: {f} vs old: {fld}')
                assert fld.len() == f.len()
                assert fld.name == f.name
                new_value = list(fld.ternbits)  # start with original/old value
                for j, bit in enumerate(f.ternbits):  # look at incoming bits
                    if bit != '-':
                        new_value[j] = bit   # take the bits that are not dontcares

                self.fields[i] = field.field_t(f.name, f.hibit, "".join(new_value), f.forbid)
                return
        assert 0  # should not occur

    def required_mask_str(self):  # return str of binary bits
        b = [x.required_mask_str() for x in self.fields]
        s = "".join(b)
        assert len(s) == 32
        return s

    def set_bit(self, bitpos: int, value: str):
        """Modify the bit in bitpos to be value"""
        for f in self.fields:
            if f.in_range(bitpos):
                f.modify(bitpos, value)
                break

    def get_bit(self, bitpos: int):
        """Extract the bit in bitpos"""
        for f in self.fields:
            if f.in_range(bitpos):
                return f.get_bit(bitpos)
        assert 0
        return 0

    def replace_encoding(self, new_value: str):  # 32b binary string
        """replace whole encoding, bit by bit"""
        for i, bit in enumerate(list(new_value)):
            self.set_bit(31 - i, bit)

    def required_mask_num(self):
        m = self.required_mask_str()  # binary string
        return int(m, 2)

    def required_bits_str(self):  # return str of binary bits
        """1 in any bit position that must be 1/0.
           0 in any dontcare or forbidden (!=) field."""
        b = [x.required_bits_str() for x in self.fields]
        s = "".join(b)
        assert len(s) == 32
        return s

    def required_bits_num(self):
        """1/0 in any bit position that must be 1/0.
           0 in any dontcare or forbidden (!=) field.
        This ultimately gets used under a required-bits-mask."""
        m = self.required_bits_str()  # binary string
        return int(m, 2)

    def apply_required_mask(self, m):
        """m is a mask"""
        return m & self.required_bits_num()

    def requires_forbid_test(self):
        b = {x.is_forbid() for x in self.fields}
        return True in b

    def forbid_mask_str(self):
        b = [x.forbid_mask_str() for x in self.fields]
        s = "".join(b)
        assert len(s) == 32
        return s

    def forbid_bits_str(self):
        b = [x.forbid_bits_str() for x in self.fields]
        s = "".join(b)
        assert len(s) == 32
        return s

    def forbid_mask_num(self):
        m = self.forbid_mask_str()  # binary string
        return int(m, 2)

    def forbid_bits_num(self):
        m = self.forbid_bits_str()  # binary string
        return int(m, 2)

    def pretty_print_fields(self, s=''):
        """Pretty print the field names below the bits"""
        r1 = []  # row1 of output (ternbits)
        r2 = []  # row2 of output (bit names)
        for f in self.fields:
            # skip fields with synthetic names like "field0", "field1", etc.
            if re.match(r'field[0-9]+', f.name):
                w = f.display_len()
                r2.append(' ' * w)
            else:
                w = max(len(f.name), f.display_len())
                r2.append(f'{f.name:{w}}')
            t = f.display_bits()
            r1.append(f'{t:{w}}')

        return [ (s + (" ".join(r1))),
                 (s + (" ".join(r2))) ]

    def print_fields(self, s=''):
        """Pretty print the field names below the bits"""
        t = self.pretty_print_fields(s)

        print(t[0])
        print(t[1])

    def fields_list(self):
        return [f.display_bits() for f in self.fields]

    def print_masks(self, s=''):
        m = self.required_mask_str()  # binary string
        r = self.required_bits_str()  # binary string
        print(s, '  ', m, util.hex32(int(m, 2)), 'required mask')
        print(s, '  ', r, util.hex32(int(r, 2)), 'required bits under mask')

        if self.requires_forbid_test():
            m = self.forbid_mask_str()  # binary string
            r = self.forbid_bits_str()  # binary string
            print(s, '!=', m, util.hex32(int(m, 2)), 'forbid mask')
            print(s, '!=', r, util.hex32(int(r, 2)), 'forbid bits under mask')

    def emit_decode_test(self, varname='v', fp=sys.stdout, s=''): # FIXME: remove, not used
        """varname is the uint32_t variable name with the instruction
        text. s is optional padding."""
        mx = util.hex32(self.required_mask_num())
        rx = util.hex32(self.required_bits_num())
        if self.requires_forbid_test():
            fmx = util.hex32(self.forbid_mask_num())
            frx = util.hex32(self.forbid_bits_num())

            print(s, f"if (({varname} & {mx}) == {rx}) and (({varname} & {fmx}) != {frx}:", file=fp)
        else:
            print(s, f"if ({varname} & {mx}) == {rx}:", file=fp)
    def get_field(self, fname: str) -> field.field_t:
        for f in self.fields:
            if f.name == fname:
                return f
        return None

    def emit_extract_field(self, fname: str, base_input:str = 'x') -> str:
        """Extract the bits corresponding to the named field. It
        assumes the input is named x. Supports subfield or single-bit
        extraction. Some field names include bit-field <...>
        decorations already. For example opc<2:1> and opc<0> are
        common. So we test for those first before trying to extract
        the bits from a larger field."""
        f = self.get_field(fname)
        if f:
            return f.emit_extract(base_input)

        m = encoding_t._subfield.match(fname) # field<hibit:lobit>
        if m:
            f = self.get_field(m.group('field'))

            hibit = int(m.group('hibit'))
            lobit = int(m.group('lobit'))
            nbits = hibit+1-lobit
            shift = lobit
            mask = (1<<nbits)-1
            s = f.emit_extract(base_input)
            return f'({s}>>{shift})&{mask}'

        m = encoding_t._onebit.match(fname)  # field<bit>
        if m:
            f = self.get_field(m.group('field'))

            bit = int(m.group('bit'))
            shift = bit
            mask = 1
            s = f.emit_extract(base_input)
            return f'({s}>>{shift})&{mask}'

        assert 0
