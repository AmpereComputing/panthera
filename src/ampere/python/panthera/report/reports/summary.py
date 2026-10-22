# Copyright (c) 2019-2025 Ampere Computing. All rights reserved.
# SPDX-License-Identifier: BSD-3-Clause

"""Summary of performance, IPC"""

import pandas as pd
from panthera.report.helpers import *

metrics = [
    "sys.soc_0.core_0.ipc",
    "sys.soc_0.core_0.ips",
    "sys.soc_0.core_0.stalls_frontend_pki",
    "sys.soc_0.core_0.stalls_backend_pki",
    "sys.soc_0.core_0.branch.mispredict.pki",
    "sys.soc_0.core_0.icache.mpki",
    "sys.soc_0.core_0.dcache.mpki",
    "sys.soc_0.core_0.l2cache.mpki",
    "sys.soc_0.core_0.raw_scrub.pki",
]

means = []
for metric in metrics:
    means.append(metric_report(df, metric=metric))

summary = pd.DataFrame(means)
print(summary)
create_csv(summary, "summary.csv")
ipc_validity_warnings(df)
