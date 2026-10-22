# Copyright (c) 2024-2025 Ampere Computing. All rights reserved.
# SPDX-License-Identifier: BSD-3-Clause

core = metric.core

# Core metrics
metric.cycles = core.gpc.cycle
core.ipc = core.gpc.instruction_commit / metric.cycles
