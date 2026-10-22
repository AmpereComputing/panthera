#!/usr/bin/env python3
# Copyright (c) 2021 Ampere Computing. All rights reserved.
# SPDX-License-Identifier: BSD-3-Clause

from ampere.xlat import Signal

# Link register
x30      = Signal('011110') # 30 (GPR)

# Two variables for zero register
ZR       = Signal('011111') # 31 (GPR)
xzr      = Signal( '11111') # 31 (GPR)

# Stack pointer for each EL
SP_EL0   = Signal('011111') # 31 (GPR) 
SP_EL1   = Signal('100000') # 32 (GPR)
SP_EL2   = Signal('100001') # 33 (GPR)
SP_EL3   = Signal('100010') # 34 (GPR)

# RGSR_EL1 is special and gets renamed so we assign it its own GPR
RGSR_EL1 = Signal('100011') # 35 (GPR)

# These "scratch" registers can only be used by IXU (they come out of gpr register list)
s0       = Signal('100100') # 36 (GPR)
s1       = Signal('100101') # 37 (GPR)

# These "temp" registers can only be used by FSU (they come out of vec register list)
T0       = Signal('100000') # 32 (VEC)
T1       = Signal('100001') # 33 (VEC)
T2       = Signal('100010') # 34 (VEC)
T3       = Signal('100011') # 35 (VEC)
T4       = Signal('100100') # 36 (VEC)

# Special SPRs that we write to for DVP/CPP/CFP instructions
#   To check that these values are current:
#   grep localparam BUILD/build_dir_cpu/uarch/reg/CpuSprPkg.sv | grep IMP_ | grep _CLEAR
IMP_BPU_CLEAR_EL3      = Signal('1111010110000101') # hex: f585
IMP_PREFETCH_CLEAR_EL3 = Signal('1111011110110001') # hex: f7b1
IMP_SFP_CLEAR_EL3      = Signal('1111011110110010') # hex: f7b2
