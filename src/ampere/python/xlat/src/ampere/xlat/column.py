# Copyright (c) 2018-2019 Ampere Computing. All rights reserved.
# SPDX-License-Identifier: BSD-3-Clause

from . import XlatError
from .signal import Signal


class Column:
    """A series of signals values.

    A Column is a representation of a series of Signal objects. It's generally used to represent alternative
    values, such as an instruction pattern, or a portion of a PLA input/output plane.

    The Signals in a Column must be of equal widths.
    """
    __slots__ = ['_values']

    def __init__(self, values=()):
        self._values = []
        for v in values:
            self.append(v)

    @property
    def width(self):
        return self._values[0].width if self._values else None

    def append(self, signal):
        if isinstance(signal, Signal):
            signal = signal
        elif type(signal) is str or type(signal) is bool:
            signal = Signal(signal)
        else:
            signal = Signal(*signal)

        if self._values and signal.width != self.width:
            raise XlatError('Column requires Signals of equal widths')

        self._values.append(signal)

    class Rows:
        def __init__(self, col):
            self.column = col

        def __getitem__(self, key):
            return self.column._values[key]

    @property
    def rows(self):
        return Column.Rows(self)

    def __len__(self):
        return len(self._values)

    def __getitem__(self, key):
        return Column([v[key] for v in self._values])

    def __iter__(self):
        return self._values.__iter__()

    def __str__(self):
        return '\n'.join([str(s) for s in self._values])

    def __repr__(self):
        return f'Column(values={self._values})'
