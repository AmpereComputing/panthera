#!/usr/bin/env python3

# Copyright (c) 2024 Ampere Computing. All rights reserved.
# SPDX-License-Identifier: BSD-3-Clause

import panthera.streams.aice_stream_pb2 as is_proto


class tinsn:
    def __init__(self, pc, opctype: is_proto.Opcode):
        self.insn = is_proto.Instruction()
        self.insn.program_counter.va = pc
        self.insn.opcode = opctype

    def write(self, trc):
        trc.write(self.insn)

    def gpr_src(self, reg_idx: int, reg_value: int):
        gsrc = self.insn.gpr_src.add()
        gsrc.number = reg_idx
        gsrc.value = reg_value
        return self

    def gpr_dst(self, reg_idx: int, reg_value: int = 0):
        gdst = self.insn.gpr_dst.add()
        gdst.number = reg_idx
        gdst.value = reg_value
        return self

    def imr_src(self, reg_idx: int):
        t = self.insn.imr_src.add()
        t.number = reg_idx
        return self

    def imr_dst(self, reg_idx: int):
        t = self.insn.imr_dst.add()
        t.number = reg_idx
        return self

    def breg_dst(self, reg_idx: int):
        t = self.insn.breg_dst.add()
        t.number = reg_idx
        return self

    def breg_src(self, reg_idx: int):
        t = self.insn.breg_src.add()
        t.number = reg_idx
        return self

    def amr_src(self, reg_idx: int):
        t = self.insn.amr_src.add()
        t.number = reg_idx
        return self

    def amr_dst(self, reg_idx: int):
        t = self.insn.amr_dst.add()
        t.number = reg_idx
        return self

    def imm(self, imm: int):
        self.insn.imm_src.append(imm)
        return self
