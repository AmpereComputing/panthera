# Copyright (c) 2019-2024 Ampere Computing. All rights reserved.
# SPDX-License-Identifier: BSD-3-Clause

from . import Field, Signal, XlatError


class Cat:
    def __init__(self, items):
        for i in items:
            self._check_type(i)
        self.items = items

    @property
    def width(self):
        return sum([i.width for i in self.items])

    @staticmethod
    def build(*items):
        items = Cat._fold(list(Cat._flatten(items)))
        if not items:
            raise XlatError('concatenation is empty')
        elif len(items) == 1:
            Cat._check_type(items[0])
            return items[0]
        else:
            return Cat(items)

    @staticmethod
    def _flatten(items):
        for item in items:
            if type(item) is Cat:
                yield from Cat._flatten(item.items)
            else:
                yield item

    @staticmethod
    def _fold(items):
        if not items:
            return items

        folded_items = [items.pop(0)]

        while items:
            if type(folded_items[-1]) is Signal and type(items[0]) is Signal:
                folded_items[-1] = Signal(str(folded_items[-1]) + str(items.pop(0)))
            elif Cat._adjacent_fields(folded_items[-1], items[0]):
                folded_items[-1] = Cat._combine_fields(folded_items[-1], items.pop(0))
            else:
                folded_items.append(items.pop(0))

        return folded_items

    @staticmethod
    def _check_type(item):
        if not (isinstance(item, Field) or isinstance(item, Signal) or isinstance(item, Repl)):
            raise XlatError(f'illegal type {type(item)} given to concatenation; must be Field, Signal, or Repl')

    @staticmethod
    def _adjacent_fields(lhs, rhs):
        if isinstance(lhs, Field) and isinstance(rhs, Field):
            if lhs.source == rhs.source and lhs.lo == rhs.hi + 1:
                return True
        return False

    @staticmethod
    def _combine_fields(lhs, rhs):
        return Field(f'{lhs.name}_{rhs.name}', lhs.source, lhs.pattern, (lhs.hi, rhs.lo))

    def __str__(self):
        return '{' + ', '.join([f'{i:sv}' for i in self.items]) + '}'

    def __format__(self, format_spec):
        return str(self)

    def __eq__(self, other):
        return self.items == other.items


class Repl:
    def __init__(self, count, item):
        self.count = count
        self.item = item
        assert self.item is not None

    @staticmethod
    def build(count, item):
        if type(item) is Signal:
            return Signal(str(item) * count)
        else:
            return Repl(count, item)

    def __str__(self):
        if self.count == 1:
            return f'{self.item:sv}'
        else:
            return f'{{{self.count}{{{self.item:sv}}}}}'

    def __format__(self, format_spec):
        return str(self)
