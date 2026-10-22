# Copyright (c) 2019-2025 Ampere Computing. All rights reserved.
# SPDX-License-Identifier: BSD-3-Clause

"""Stackup of uop latencies"""

metrics = [
    "sys.soc_0.core_0.uop_decode2dispatch_cycles",
    "sys.soc_0.core_0.uop_dispatch2ready_cycles",
    "sys.soc_0.core_0.uop_ready2issue_cycles",
    "sys.soc_0.core_0.uop_issue2writeback_cycles",
    "sys.soc_0.core_0.uop_writeback2commit_cycles",
]

latencies = rollup(df, metrics=metrics).reset_index()
latencies.set_index(TRACE, inplace=True)

create_plot(latencies, "uop_latencies.svg", stacked=True)
create_csv(latencies, "uop_latencies.csv")
