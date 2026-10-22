# Copyright (c) 2018-2026 Ampere Computing. All rights reserved.
# SPDX-License-Identifier: BSD-3-Clause

# noqa: F403, F405
from ampere.xlat import scope, uops


@scope(uops)
def IXUop(uop):
    return uop.unit == 'ixu'


@scope(IXUop, inputs='uopcode')
def is_conflict(uop):
    return uop.conflict == 1
