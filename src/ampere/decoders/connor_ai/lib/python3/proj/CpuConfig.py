# Copyright (c) 2025 Ampere Computing. All rights reserved.
# SPDX-License-Identifier: BSD-3-Clause

# Use this Python class to define variables that are different between AI_CPU mode and non AI_CPU mode.
# To use this package import it:
#
# import sys,os
# sys.path.append (os.environ['VL_PROJECT_ROOT'] + "/rtl/Subsystems/Cpu")
# from CpuConfig import CpuConfig
#
# If used by an epython located in the rtl/Subsystems/Cpu, use:
# <% import sys,os
# sys.path.append(os.path.dirname(sys.argv[1]))
# from CpuConfig import CpuConfig 
# %>
#
# And refer to any variable via the ConfigCpu instace. For example:
#  myvar = CpuConfig.DUMMY_VAR

import os
class CpuConfigClass():
  """
  Container class for capturing Cpu configuration
  """
  def __init__(self):
    self.AI_CPU = 1          if 'AI_CPU' in os.environ else 0
    ## AI_IXU_REDUCE_PRF
    self.CPU_GPR_PREGS            = 166 if 'AI_CPU' in os.environ else 220

    self.CPU_L2C_NUM_BANKS        = 8   if 'AI_CPU' in os.environ else 8
    self.L2C_NUM_4WAY_MACROS      = 3   if 'AI_CPU' in os.environ else 3
    self.L2C_NUM_2WAY_MACROS      = 1   if 'AI_CPU' in os.environ else 1
    self.CPU_L2C_NUM_WAYS         = 14  if 'AI_CPU' in os.environ else 14

    #  AI_L2C_REDUCE_FB
    self.CPU_L2C_NUM_FB           = 6   if 'AI_CPU' in os.environ else 8
    self.AI_L2C_REMOVE_MPAM       = 1   if 'AI_CPU' in os.environ else 0

    self.AI_FSU_REDUCE_CRYPTO     = 1   if 'AI_CPU' in os.environ else 0 
    self.AI_FSU_FEAT_SVE_BFSCALE  = 0 #FIXME: RTL support not yet complete
    self.AI_FSU_FEAT_SVE_BITPERM  = 0 #FIXME: RTL support, but AISLE/COSIM config issues

    self.CPU_L1TLB_ENTRIES        = 32  if 'AI_CPU' in os.environ else 64

    ## AI_LSU_REDUCE_DL1
    self.CPU_DL1_NUM_SETS         = 128 if 'AI_CPU' in os.environ else 256

    ## AI_LSU_REDUCE_LOB
    self.CPU_LOB_ENTRIES          = 64  if 'AI_CPU' in os.environ else 88

    # AI_LSU_REDUCE_SOB
    self.CPU_SOB_ENTRIES          = 42  if 'AI_CPU' in os.environ else 48

    self.AI_LSU_REMOVE_GRETCH     = 1   if 'AI_CPU' in os.environ else 0

    # AI_GPC_REDUCE_ROB
    self.CPU_ROB_ENTRIES         = 192  if 'AI_CPU' in os.environ else 256

    self.FSU_UOPS_YAML            = "fsu_uops.ai_cpu.yaml" if 'AI_CPU' in os.environ else "fsu_uops.yaml"

CpuConfig = CpuConfigClass()

