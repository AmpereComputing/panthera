# Copyright (c) 2019 Ampere Computing. All rights reserved.
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


@scope(uop_decode, default_output=False)
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
def GprSrc1Vld(trans):
    insn, uop, idx = trans
    return hasattr(uop, 'src1')


@scope(GprSrc1Vld)
def GprSrc1Lreg(trans):
    insn, uop, idx = trans
    return uop.src1


@scope(IxuVld)
def GprSrc2Vld(trans):
    insn, uop, idx = trans
    return hasattr(uop, 'src2')


@scope(GprSrc2Vld)
def GprSrc2Lreg(trans):
    insn, uop, idx = trans
    return uop.src2


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
