# Copyright (c) 2019 Ampere Computing. All rights reserved.
# SPDX-License-Identifier: BSD-3-Clause

from ampere.xlat import Signal, Uop


class IxuUop(Uop):
    unit = 'ixu'
    pipe = 'AB'


class LdUop(Uop):
    unit = 'ld'


class add(IxuUop):
    uopcode = Signal(8, 50)
    test_predicate = 1


class xtnd(IxuUop):
    uopcode = Signal(8, 100)
    test_predicate = 1


class sxtnd(IxuUop):
    uopcode = Signal(8, 101)


class lsl64(IxuUop):
    uopcode = Signal(8, 118)


class ldb(LdUop):
    uopcode = Signal(7, 80)
