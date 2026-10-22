# Copyright (c) 2019-2025 Ampere Computing. All rights reserved.
# SPDX-License-Identifier: BSD-3-Clause

"""Generate BPU report for a timeseries"""

mispredicts = df.filter(regex=r"sys\.soc_0\.core_0\.ixu\.xu\..*?\.branch_mispredict").sum(axis=1)

instructions = df["sys.soc_0.core_0.gpc.instruction_commit"]

mpki = 1000 * mispredicts / instructions
mpki.index = df["stop_time"]

print(mpki.describe())
create_plot(mpki, "bp_mpki.svg", kind="line", title="Branch Misprediction per 1000 Instructions")
