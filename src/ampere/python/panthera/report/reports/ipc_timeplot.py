# Copyright (c) 2019-2025 Ampere Computing. All rights reserved.
# SPDX-License-Identifier: BSD-3-Clause

"""Generate IPC over time"""

ipc = df["sys.soc_0.core_0.gpc.instruction_commit"] / df["sys.soc_0.core_0.gpc.cycle"]
ipc.index = df["stop_time"]

create_plot(ipc, "ipc.svg", kind="line", title="IPC")
