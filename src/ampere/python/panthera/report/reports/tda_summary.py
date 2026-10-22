# Copyright (c) 2021-2025 Ampere Computing. All rights reserved.
# SPDX-License-Identifier: BSD-3-Clause

"""Summary of TDA"""

import pandas as pd
from panthera.report.helpers import *

tda_metrics = [
    "sys.soc_0.core_0.ipc",
    "tda.l1.retired",
    "tda.l1.lost",
    "tda.l1.frontend",
    "tda.l1.backend",
    "tda.l2.frontend_latency",
    "tda.l2.frontend_bandwidth",
    "tda.l2.backend_core",
    "tda.l2.retired_pipe_utilization",
    "tda.l2.lost_branch_mispredict",
    "tda.l2.lost_other_clear",
    "tda.l3.frontend_latency_icache",
    "tda.l3.frontend_latency_itlb",
    "tda.l3.frontend_latency_recovery",
    "tda.l3.frontend_latency_bob",
    "tda.l3.backend_memory_dcache",
    "tda.l3.backend_memory_dtlb",
    "tda.l3.backend_memory_l2",
    "tda.l3.backend_core_resource",
    "tda.l3.retired_pipe_utilization_ixu",
    "tda.l3.retired_pipe_utilization_fsu",
    "tda.l4.backend_core_resource_rob",
    "tda.l4.backend_core_resource_ixu",
    "tda.l4.backend_core_resource_fsu",
    "tda.l4.backend_core_resource_lob",
    "tda.l4.backend_core_resource_sob",
]

means = []
for metric in tda_metrics:
    means.append(metric_report(df, metric=metric))

tda_summary = pd.DataFrame(means)
print(tda_summary)
