# Copyright (c) 2023-2025 Ampere Computing. All rights reserved.
# SPDX-License-Identifier: BSD-3-Clause

soc = metric.sys.soc_0
core = soc.core_0

# Core metrics
metric.cycles = core.gpc.cycle
metric.seconds = (metric.stop_time - metric.start_time) / 1e12
core.ipc = core.gpc.instruction_commit / metric.cycles
core.ips = core.gpc.instruction_commit / metric.seconds
core.upc = core.gpc.uop_commit / metric.cycles
