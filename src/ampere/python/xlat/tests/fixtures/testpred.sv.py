# Copyright (c) 2018-2026 Ampere Computing. All rights reserved.
# SPDX-License-Identifier: BSD-3-Clause

# noqa: F403, F405
from ampere.xlat import scope, uops


@scope(uops)
def IXUop(uop):
    return uop.unit == 'ixu'


@scope(IXUop, inputs='uopcode')
def isPipeA(uop):
    return uop.pipe in ('A', 'AB')


@scope(isPipeA)
def test_predicate(uop):
    return True if getattr(uop, 'test_predicate', False) else False
