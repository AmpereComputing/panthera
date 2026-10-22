# Copyright (c) 2020-2023 Ampere Computing. All rights reserved.
# SPDX-License-Identifier: BSD-3-Clause

import ampere.xlat
from ampere.xlat import translation, uop, Signal

ampere.xlat.translation.enforce_fusion_requirements = True

s0 = Signal('100011')


class cmp(uop.Uop):
    pass


class beq(uop.Uop):
    pass


class subs64(uop.Uop):
    pass


class lsl64_imm(uop.Uop):
    pass


class sbfm64(uop.Uop):
    pass


class cmp_beq(uop.Uop):
    pass


class subs64_bc_eq(uop.Uop):
    pass


class CMP(translation.Translation):
    pattern = '0000000000000000000000 ----- -----'
    pfields = '                        Rn    Rm  '

    def decode(self, f):
        cmp(src1=f.Rn, src2=f.Rm)


class BEQ(translation.Translation):
    pattern = '000001 --------------------------'
    #pfields = '                imm26            '
    fields = {'imm26': [25, 0]}

    def decode(self, f):
        beq(imm=f.imm26)


class CMP_BEQ(translation.Fusion):
    sequence = [CMP, BEQ]

    def decode(self, f, f2):
        cmp_beq(src1=f.Rn, src2=f.Rm, imm=f2.imm26)


class SUBS_64S_addsub_ext(translation.Translation):
    """Subtract (extended register), setting flags"""
    pattern = '1  1  1 01011 00  1 ----- ---    ---  ----- -----'
    pfields = 'sf op S       opt    Rm   option imm3  Rn    Rd  '

    asmtemplate = 'SUBS  <Xd>, <Xn|SP>, <R><m>{, <extend> {#<amount>}}'
    src1_is_stack_pointer = True

    def decode(self, f):
        if f.option.value == 0b000:
          if f.imm3 == 0b000:
            subs64(dest=f.Rd, flag_dest=1, src1=f.Rn, src2=f.Rm, imm=(f.option,Signal("00000000000")))
          else:
            lsl64_imm(dest=s0, src1=f.Rm, imm=f.imm3)
            subs64(dest=f.Rd, flag_dest=1, src1=f.Rn, src2=s0, imm=(f.option,Signal("00000000000")))
        elif f.option.value == 0b001:
          if f.imm3 == 0b000:
            subs64(dest=f.Rd, flag_dest=1, src1=f.Rn, src2=f.Rm, imm=(f.option,Signal("00000000000")))
          else:
            lsl64_imm(dest=s0, src1=f.Rm, imm=f.imm3)
            subs64(dest=f.Rd, flag_dest=1, src1=f.Rn, src2=s0, imm=(f.option,Signal("00000000000")))
        elif f.option.value == 0b010:
          if f.imm3 == 0b000:
            subs64(dest=f.Rd, flag_dest=1, src1=f.Rn, src2=f.Rm, imm=(f.option,Signal("00000000000")))
          else:
            lsl64_imm(dest=s0, src1=f.Rm, imm=f.imm3)
            subs64(dest=f.Rd, flag_dest=1, src1=f.Rn, src2=s0, imm=(f.option,Signal("00000000000")))
        elif f.option.value == 0b011:
          if f.imm3 == 0b000:
            subs64(dest=f.Rd, flag_dest=1, src1=f.Rn, src2=f.Rm, imm=(f.option,Signal("00000000000")))
          else:
            lsl64_imm(dest=s0, src1=f.Rm, imm=f.imm3)
            subs64(dest=f.Rd, flag_dest=1, src1=f.Rn, src2=s0, imm=(f.option,Signal("00000000000")))
        elif f.option.value == 0b100:
            sbfm64(dest=s0, src1=f.Rm, src2=f.Rd, imm=(Signal("000"),f.imm3,Signal("000111")))
            subs64(dest=f.Rd, flag_dest=1, src1=f.Rn, src2=s0, imm=(f.option,Signal("01100000000000")))
        elif f.option.value == 0b101:
            sbfm64(dest=s0, src1=f.Rm, src2=f.Rd, imm=(Signal("000"),f.imm3,Signal("001111")))
            subs64(dest=f.Rd, flag_dest=1, src1=f.Rn, src2=s0, imm=(f.option,Signal("01100000000000")))
        elif f.option.value == 0b110:
            sbfm64(dest=s0, src1=f.Rm, src2=f.Rd, imm=(Signal("000"),f.imm3,Signal("011111")))
            subs64(dest=f.Rd, flag_dest=1, src1=f.Rn, src2=s0, imm=(f.option,Signal("01100000000000")))
        elif f.option.value == 0b111:
          if f.imm3 == 0b000:
            subs64(dest=f.Rd, flag_dest=1, src1=f.Rn, src2=f.Rm, imm=(f.option,Signal("01100000000000")))
          else:
            lsl64_imm(dest=s0, src1=f.Rm, imm=f.imm3)
            subs64(dest=f.Rd, flag_dest=1, src1=f.Rn, src2=s0, imm=(f.option,Signal("01100000000000")))


class ArithS_Bcond(translation.Fusion):
    def no_shift_or_sign_extension(insn):
        return insn.option.value < 4 and insn.imm3.value == 0

    sequence = [SUBS_64S_addsub_ext, BEQ]
    requirements = [no_shift_or_sign_extension, None]

    def decode(self, f, f2):
        subs64_bc_eq(dest=f.Rd, flag_dest=1, src1=f.Rn, src2=f.Rm, imm=(f.option,Signal("00000000000")), pcoffset=f2.imm26)


def find_translation(cls):
    ts = []
    for t, _ in translation.translations:
        if isinstance(t, cls):
            ts.append(t)
    return ts


def test_fusion_was_created():
    assert find_translation(CMP_BEQ)


def test_fusion_can_access_non_pfields():
    fusion = find_translation(CMP_BEQ)[0]
    assert len(fusion.uops) == 1
    assert fusion.uops[0].imm.hi == 25
    assert fusion.uops[0].imm.lo == 0


def test_fusion_obeys_requirements():
    fusions = find_translation(ArithS_Bcond)
    first_opcode_patterns = set([fusion.patterns[0] for fusion in fusions])
    assert first_opcode_patterns == {'11101011001-----000000----------',
                                     '11101011001-----001000----------',
                                     '11101011001-----010000----------',
                                     '11101011001-----011000----------'}

