# Copyright (c) 2019-2025 Ampere Computing. All rights reserved.
# SPDX-License-Identifier: BSD-3-Clause

"""Branch mispredictions by branch type"""

mp_metrics = [
    "sys.soc_0.core_0.branch.mispredict_cond.pki",
    "sys.soc_0.core_0.branch.mispredict_indirect.pki",
    "sys.soc_0.core_0.branch.mispredict_direct.pki",
    "sys.soc_0.core_0.branch.mispredict_return.pki",
]

mps = rollup(df, mp_metrics)

title = "Mispredicts by Branch Type"
create_plot(mps, "bp_mispredicts_by_type.svg", stacked=True, title=title)
print(mps.mean())
