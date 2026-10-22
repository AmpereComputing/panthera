# Copyright (c) 2018-2019 Ampere Computing. All rights reserved.
# SPDX-License-Identifier: BSD-3-Clause

import traceback


class Uop:
    uop_sequence = []

    def __init__(self, *args, **kwargs):
        caller = traceback.extract_stack(limit=2)[0]
        self.filename = caller.filename
        self.lineno = caller.lineno
        self._args = args
        self._kwargs = kwargs.copy()
        self.collect_attributes(list(args), kwargs)
        Uop.uop_sequence.append(self)

    def collect_attributes(self, args, kwargs):
        for reg in ('dest', 'src1', 'src2', 'src3'):
            try:
                value = kwargs.pop(reg, None)
                if value is None:
                    value = args.pop(0)
                self.set_attribute(reg, value)
            except IndexError:
                pass
        for k, v in kwargs.items():
            self.set_attribute(k, v)

    def set_attribute(self, reg, value):
        if callable(value):
            value = value(self, reg)
        setattr(self, reg, value)


def _all_uop_leaf_classes(cls=Uop):
    subclasses = cls.__subclasses__()
    if not subclasses:
        yield (cls, True)
    else:
        for subcls in subclasses:
            yield from _all_uop_leaf_classes(subcls)


class UopSet:
    def __iter__(self):
        return list(_all_uop_leaf_classes()).__iter__()


uops = UopSet()
