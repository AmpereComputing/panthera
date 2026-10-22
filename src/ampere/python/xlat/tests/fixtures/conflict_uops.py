# Copyright (c) 2019 Ampere Computing. All rights reserved.
# SPDX-License-Identifier: BSD-3-Clause

from ampere.xlat import Signal, Uop


class add(Uop):
    unit = 'ixu'
    uopcode = Signal(8, 21)
    conflict = 1


class sub(Uop):
    unit = 'ixu'
    uopcode = Signal(8, 21)
    conflict = 0
