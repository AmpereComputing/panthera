# Copyright (c) 2019-2025 Ampere Computing. All rights reserved.
# SPDX-License-Identifier: BSD-3-Clause

"""Generate BPU report"""

import pandas as pd

ratio = metric_report(df, metric="sys.soc_0.core_0.branch.mispredict.ratio")
mpki = metric_report(df, metric="sys.soc_0.core_0.branch.mispredict.pki")

summary = pd.DataFrame([ratio, mpki])
print(summary)
