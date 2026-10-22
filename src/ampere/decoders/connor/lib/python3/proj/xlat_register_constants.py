#!/usr/bin/env python3
# Copyright (c) 2023 Ampere Computing. All rights reserved.
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

# The first fault register (FFR) is treated as a renamable predicate register
FFR      = Signal('10000')  # 16 (PRED)

# These "scratch" registers can only be used by IXU (they come out of gpr register list)
s0       = Signal('100100') # 36 (GPR)
s1       = Signal('100101') # 37 (GPR)

# This "temp" register can only be used by IXU (it comes out of flag register list)
tmpflag  = Signal('100110') # 38 (FLAG)

# These "temp" registers can only be used by FSU (they come out of vec register list)
T0       = Signal('100000') # 32 (VEC)
T1       = Signal('100001') # 33 (VEC)
T2       = Signal('100010') # 34 (VEC)
T3       = Signal('100011') # 35 (VEC)
T4       = Signal('100100') # 36 (VEC)

# The one architectural flag register
f0       = Signal('0') # 0 (FLAG)

# The "temp" flag register was added to support certain SVE instructions (bug 99349)
f1       = Signal('1') # 1 (FLAG)


# Special SPRs that we write to for DVP/CPP/CFP instructions
#   To check that these values are current:
#   grep localparam BUILD/build_dir_cpu/uarch/reg/CpuSprPkg.sv | grep IMP_ | grep _CLEAR
IMP_BPU_CLEAR_EL3         = Signal('1111010110000101') # hex: f585
IMP_PREFETCH_CLEAR_EL3    = Signal('1111011110110001') # hex: f7b1
IMP_SFP_CLEAR_EL3         = Signal('1111011110110010') # hex: f7b2
IMP_BRBE_IALL_EL3         = Signal('1111011111100010') # hex: f7e2
IMP_BRBE_INJ_EL3          = Signal('1111011111100011') # hex: f7e3
IMP_L2C_LOCAL_TIMEOUT_EL3 = Signal('1111011110011100') # hex: f79c

# Immediate for reg-reg memops
# lower 4b, bits 3:0
NOSHIFT = Signal('0000') 
HSHIFT = Signal('1001')
WSHIFT = Signal('1010')
DSHIFT = Signal('1011')
# next 7b, bits 10:4
SOFF32 = Signal('1000000') # 32b signed offsets for gather/scatter
UOFF32 = Signal('0000000') # 32b unsigned offsets for gather/scatter
UOFF64 = Signal('0000001') # 64b offsets for gather/scatter and normal ld/st

# add64 imm controls
ADD_NOSHIFT = Signal('11000')
ADD_SHIFT1 =  Signal('11001')
ADD_SHIFT2 =  Signal('11010')
ADD_SHIFT3 =  Signal('11011')
ADD_SHIFT4 =  Signal('11100')

#payload in bits 11:10
PULL_WORD0 =  Signal('000000000000')
PULL_WORD1 =  Signal('010000000000') 
PULL_WORD2 =  Signal('100000000000')
PULL_WORD3 =  Signal('110000000000')

PULL_DWORD0 =  Signal('000000000000')
PULL_DWORD1 =  Signal('010000000000')

MEM_B =  Signal('000000000000')
MEM_H =  Signal('010000000000') 
MEM_W =  Signal('100000000000')
MEM_D =  Signal('110000000000')

# for zip1/zip2 and stdv_zip1/stdv_zip2
BD0 = Signal('0001100')
BD1 = Signal('0001110')
