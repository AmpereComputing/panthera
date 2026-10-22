# Copyright (c) 2019 Ampere Computing. All rights reserved.
# SPDX-License-Identifier: BSD-3-Clause

from functools import lru_cache
from ampere.xlat import Cat, Fusion, Signal, XlatError, scope, decoded_uops


def uop_decode_inputs(trans):
    insn, uop, index = trans

    if isinstance(insn, Fusion):
        if len(insn.translations) != 2:
            raise XlatError(f'can only handle 2 instruction fusions (was given {len(insn.translations)})')
        inputs = {'Instruction0': Signal(insn.patterns[0]),
                  'Instruction1': Signal(insn.patterns[1]),
                  'Fusion': Signal(True)}
    else:
        inputs = {'Instruction0': Signal(insn.pattern),
                  'Instruction1': Signal(32, mask=0),
                  'Fusion': Signal(False)}

    inputs.update(positional_conditions(index))
    return inputs


@lru_cache(maxsize=12)
def positional_conditions(index):
    assert index < 12
    return {'QuadNum': Signal(2, int(index / 4)), 'UopNum': Signal(2, int(index % 4))}


@scope(decoded_uops, inputs=uop_decode_inputs, do_not_generate=True)
def uop_decode(trans):
    return True


@scope(uop_decode, default_output=False, do_not_generate=True)
def IxuVld(trans):
    insn, uop, idx = trans
    return uop.unit == 'ixu'


@scope(IxuVld)
def IxuOpcode(trans):
    insn, uop, idx = trans
    return uop.opcode


@scope(IxuVld)
def GprDestVld(trans):
    insn, uop, idx = trans
    return hasattr(uop, 'dest')


@scope(GprDestVld)
def GprDestLreg(trans):
    insn, uop, idx = trans
    return uop.dest


@scope(IxuVld)
def ImmVld(trans):
    insn, uop, idx = trans
    return hasattr(uop, 'imm') or hasattr(uop, 'imm_signext')


@scope(ImmVld)
def Imm(trans):
    insn, uop, idx = trans
    if hasattr(uop, 'imm_signext'):
        imm = uop.imm_signext
        signed = True
    else:
        imm = uop.imm
        signed = False
    if type(imm) is tuple:
        imm = Cat.build(*imm)
    imm.signed = signed
    return imm


@scope(IxuVld)
def EndInst(trans):
    insn, uop, idx = trans
    if idx == len(insn.uops) - 1:
        if isinstance(insn, Fusion):
            return Signal(2, len(insn.translations))
        else:
            return Signal(2, 1)
    else:
        return Signal(2, 0)
