# Copyright (c) 2024-2025 Ampere Computing. All rights reserved.
# SPDX-License-Identifier: BSD-3-Clause

"""Summary of performance, IPC"""

import pandas as pd
from panthera.report.helpers import *

metrics = ["sys.soc_0.core_0.ipc"]

means = []
for metric in metrics:
    means.append(metric_report(df, metric=metric))

summary = pd.DataFrame(means)
print(summary)
create_csv(summary, "summary.csv")
ipc_validity_warnings(df)
