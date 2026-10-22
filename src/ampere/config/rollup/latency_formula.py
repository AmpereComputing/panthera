# Copyright (c) 2021-2025 Ampere Computing. All rights reserved.
# SPDX-License-Identifier: BSD-3-Clause

# Global metrics
metric.time = metric.stop_time - metric.start_time

# Core metrics
core = metric.sys.soc_0.core_0
core.cycles = core.gpc.cycle
core.period = metric.time / core.cycles

core.cpi = core.cycles / core.gpc.instruction_commit

# Latency metrics
core.uop_decode2dispatch_cycles = (
    metric.uop_decode2dispatch.sum_latency / metric.uop_decode2dispatch.count
) / core.period
core.uop_dispatch2ready_cycles = (metric.uop_dispatch2ready.sum_latency / metric.uop_dispatch2ready.count) / core.period
core.uop_ready2issue_cycles = (metric.uop_ready2issue.sum_latency / metric.uop_ready2issue.count) / core.period
core.uop_issue2writeback_cycles = (
    metric.uop_issue2writeback.sum_latency / metric.uop_issue2writeback.count
) / core.period
core.uop_writeback2commit_cycles = (
    metric.uop_writeback2commit.sum_latency / metric.uop_writeback2commit.count
) / core.period
