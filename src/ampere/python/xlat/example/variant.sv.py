# Copyright (c) 2020 Ampere Computing. All rights reserved.
# SPDX-License-Identifier: BSD-3-Clause

from functools import lru_cache
from ampere.xlat import Cat, Signal, scope, decoded_uops


def uop_decode_inputs(trans):
    insn, uop, index = trans
    inputs = {'Instruction0': Signal(insn.pattern)}
    inputs.update(positional_conditions(index))
    return inputs


@lru_cache(maxsize=12)
def positional_conditions(index):
    assert index < 12
    return {'QuadNum': Signal(2, int(index / 4)), 'UopNum': Signal(2, int(index % 4))}


@scope(decoded_uops, inputs=uop_decode_inputs, do_not_generate=True)
def uop_decode(trans):
    return True


def even(trans):
    insn, uop, idx = trans
    return idx % 2 == 0


def odd(trans):
    insn, uop, idx = trans
    return idx % 2 == 1


@scope(uop_decode, default_output=False, variants=[even, odd])
def IxuVld(trans):
    insn, uop, idx = trans
    return uop.unit == 'ixu'


@scope(IxuVld)
def IsAdd(trans):
    insn, uop, idx = trans
    return uop.name == 'adds64'


@scope(IxuVld)
def IsSxt32(trans):
    insn, uop, idx = trans
    return uop.name == 'sxt32'
