# Copyright (c) 2022-2025 Ampere Computing. All rights reserved.
# SPDX-License-Identifier: BSD-3-Clause

"""Performance Validation Summary"""

import pandas as pd

ipc = pd.DataFrame(speedup(df, metric="sys.soc_0.core_0.ipc"))
ipc = ipc[[ipc.columns[1]]]
ipc.columns = ["speedup"]

# Create Outlier List
ipc_abs = ipc["speedup"].copy()
ipc_abs[ipc_abs < 1] = 1 / ipc_abs
ipc["abs"] = ipc_abs
ipc.sort_values(by=["abs"], ascending=False, inplace=True)
outliers_path = make_filepath("outliers.csv")
ipc.to_csv(outliers_path)
