# Copyright (c) 2019 Ampere Computing. All rights reserved.
# SPDX-License-Identifier: BSD-3-Clause

import os
from ampere.xlat import Uop


def test_uop_collects_registers_positionally():
    u1 = Uop(1)
    u2 = Uop(1, 2)
    u3 = Uop(1, 2, 3)

    assert u1.dest == 1
    assert not hasattr(u1, 'src1')
    assert not hasattr(u1, 'src2')

    assert u2.dest == 1
    assert u2.src1 == 2
    assert not hasattr(u2, 'src2')

    assert u3.dest == 1
    assert u3.src1 == 2
    assert u3.src2 == 3


def test_uop_collects_registers_by_name():
    u1 = Uop(dest=1)
    u2 = Uop(dest=1, src1=2)
    u3 = Uop(dest=1, src1=2, src2=3)

    assert u1.dest == 1
    assert not hasattr(u1, 'src1')
    assert not hasattr(u1, 'src2')

    assert u2.dest == 1
    assert u2.src1 == 2
    assert not hasattr(u2, 'src2')

    assert u3.dest == 1
    assert u3.src1 == 2
    assert u3.src2 == 3


def test_uop_collects_registers_mixed():
    u = Uop(1, src2=3)

    assert u.dest == 1
    assert not hasattr(u, 'src1')
    assert u.src2 == 3


def test_uop_sets_other_arguments_as_attributes():
    u = Uop(imm=12, set_flags=True)
    assert u.imm == 12
    assert u.set_flags is True


def make_a_uop():
    return Uop(2)


def test_uop_source_info():
    u = Uop(1)
    assert os.path.basename(u.filename) == 'test_uop.py'
    assert u.lineno == 60
    u2 = make_a_uop()
    assert os.path.basename(u2.filename) == 'test_uop.py'
    assert u2.lineno == 56
