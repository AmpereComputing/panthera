# Copyright (c) 2018-2019 Ampere Computing. All rights reserved.
# SPDX-License-Identifier: BSD-3-Clause

from . import XlatError


class IndefiniteValue(XlatError):
    """Raised when a Signal with don't-care bits is converted to integer or boolean."""
    pass


class NotABoolean(XlatError):
    """Raised when a Signal of more than one bit is converted to bool. Signals aren't "truthy" in the same
    way that Python integers are."""
    pass


class SignalType(type):
    signal_store = {}

    TO_MASK = ''.maketrans('01-?xz', '110000')
    TO_VALUE = ''.maketrans('01-?xz', '010000')

    def __call__(cls, arg, value=0, mask=None, signed=False):
        if type(arg) is bool:
            width, value, mask = cls.from_bool(arg)
        elif type(arg) is str:
            width, value, mask = cls.from_string(arg)
        else:
            width, value, mask = cls.from_value(arg, value, mask)
        return cls.memoized_create(super(), width, value, mask, signed)

    @classmethod
    def memoized_create(mcs, sup, width, value, mask, signed):
        sig = SignalType.signal_store.get((width, value, mask, signed), None)
        if sig is None:
            sig = sup.__call__(width, value, mask, signed)
            SignalType.signal_store[(width, value, mask, signed)] = sig
        return sig

    @classmethod
    def from_bool(mcs, value):
        return 1, 1 if value else 0, 1

    @classmethod
    def from_string(mcs, string):
        width = len(string)
        try:
            value = int(string.translate(SignalType.TO_VALUE), 2)
            mask = int(string.translate(SignalType.TO_MASK), 2)
        except ValueError:
            raise XlatError(f'tried to create a signal using an invalid string literal: "{string}"')
        return width, value, mask

    @classmethod
    def from_value(mcs, width, value, mask):
        return int(width), int(value), (1 << width) - 1 if mask is None else int(mask)


class Signal(metaclass=SignalType):
    __slots__ = ['width', 'value', 'mask', 'signed', '_constructed']

    def __init__(self, width, value, mask, signed):
        self._constructed = False
        if width <= 0:
            raise XlatError('signal must have a positive width')
        if value & ~mask:
            raise XlatError(f'signal value 0b{value:b} has set bits which are masked (mask=0b{mask:b})')
        self.width = width
        self.value = value
        self.mask = mask
        self.signed = signed
        self._constructed = True

    def __getitem__(self, item):
        if type(item) is slice:
            val = 0
            msk = 0
            j = 0
            start, stop, step = item.indices(self.width)
            for i in range(start, stop + 1, step):
                val |= ((self.value >> i) & 1) << j
                msk |= ((self.mask >> i) & 1) << j
                j += 1
            return Signal(j, value=val, mask=msk)
        else:
            if 0 <= item < self.width:
                val = (self.value >> item) & 1
                msk = (self.mask >> item) & 1
                return Signal(1, value=val, mask=msk)
            else:
                raise IndexError(f'index {item} out of range for Signal of width {self.width}')

    def __setattr__(self, key, value):
        if key != '_constructed' and hasattr(self, '_constructed') and self._constructed:
            raise RuntimeError(f'attempt to set attribute "{key}" on an immutable Signal')
        else:
            super().__setattr__(key, value)

    def __str__(self):
        return Signal.to_string(self.width, self.value, self.mask)

    def __format__(self, format_spec):
        if format_spec == 'sv':
            return Signal.to_string(self.width, self.value, self.mask, dc='?', prefix=str(self.width) + "'b")
        else:
            return Signal.to_string(self.width, self.value, self.mask)

    def __copy__(self):
        return self

    def __repr__(self):
        return f'Signal(width={self.width}, value={self.value}, mask={self.mask}, signed={self.signed})'

    def __int__(self):
        if self.mask == (1 << self.width) - 1:
            return self.value
        else:
            raise IndefiniteValue(self.width)

    def __index__(self):
        return self.__int__()

    def __bool__(self):
        if self.width == 1:
            return True if self.__int__() == 1 else False
        else:
            raise NotABoolean(self)

    def make_signed(self):
        return Signal(self.width, self.value, self.mask, True)

    @staticmethod
    def to_string(width, value, mask, dc='-', prefix=''):
        return prefix + ''.join([('1' if value & (1 << i) else '0') if mask & (1 << i) else dc
                                 for i in reversed(range(width))])
