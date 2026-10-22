# Copyright (c) 2021-2025 Ampere Computing. All rights reserved.
# SPDX-License-Identifier: BSD-3-Clause

core = metric.sys.soc_0.core_0

core.branches = metric.df.filter(regex=r"^sys\.soc_0\.core_0\.ixu\.xu\..*?.branch").sum(axis=1)

core.mispredicts = metric.df.filter(regex=r"^sys\.soc_0\.core_0\.ixu\.xu\..*?.branch_mispredict").sum(axis=1)

core.mpki = (core.mispredicts / core.gpc.instruction_commit) * 1000

core.mp_ratio = core.mispredicts / metric.sys.soc_0.core_0.branches
