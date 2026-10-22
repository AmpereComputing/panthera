# Copyright (c) 2019 Ampere Computing. All rights reserved.
# SPDX-License-Identifier: BSD-3-Clause

from ampere.xlat import Signal, scope, decoded_uops


# This defines the argument list of the rtl function that will be
# spit out by xlat. 'inputs' is a dict with each key specifying
# an argument to the fuction
def instruction_decode_inputs(trans):
    insn, uop, index = trans
    inputs = {'Instruction': Signal(insn.pattern),
              'UopNum': Signal(4, index)}
    return inputs


@scope(decoded_uops, inputs=instruction_decode_inputs, do_not_generate=True)
def instruction_decode(trans):
    return True


@scope(instruction_decode, default_output=False)
def f_IxuVld(trans):
    insn, uop, idx = trans
    return uop.unit in ['ixu', 'ld', 'st']
