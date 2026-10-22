# Copyright (c) 2020-2022 Ampere Computing. All rights reserved.
# SPDX-License-Identifier: BSD-3-Clause

from functools import lru_cache
from ampere.xlat import Cat, Signal, scope, decoded_uops

def uop_decode_inputs(trans):
    insn, uop, index = trans
    inputs = {'Instruction0': Signal(insn.pattern), 'WideMode': Signal(1, insn.mode_bits['WideMode'])}
    inputs.update(positional_conditions(index))
    return inputs

@lru_cache(maxsize=12)
def positional_conditions(index):
    assert index < 12
    return {'QuadNum': Signal(2, int(index / 4)), 'UopNum': Signal(2, int(index % 4))}

@scope(decoded_uops, inputs=uop_decode_inputs, do_not_generate=True)
def uop_decode(trans):
    return True

@scope(uop_decode, default_output=False)
def IxuVld(trans):
    insn, uop, idx = trans
    return uop.unit == 'ixu'

@scope(IxuVld)
def IxuOpcode(trans):
    insn, uop, idx = trans
    return uop.opcode
