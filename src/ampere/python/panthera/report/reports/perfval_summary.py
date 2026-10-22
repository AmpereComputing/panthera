# Copyright (c) 2022-2025 Ampere Computing. All rights reserved.
# SPDX-License-Identifier: BSD-3-Clause

"""Performance Validation Summary"""

import matplotlib.pyplot as plt
import pandas as pd
import seaborn as sns
from scipy.stats import gmean

ipc = pd.DataFrame(speedup(df, metric="sys.soc_0.core_0.ipc"))
ipc = ipc[[ipc.columns[1]]]
ipc.columns = ["speedup"]
print(f"{ipc['speedup'].isna().sum()} of {len(ipc)} Failed")
ipc_geomean = gmean(ipc["speedup"].dropna())
print(f"Geomean: {ipc_geomean:.3f}")

# Create S-Curve
sns.set_style("white")
g = sns.displot(kind="ecdf", data=ipc, y="speedup", height=6, aspect=1.5, legend=False)
g = g.set(title=f"RTL/Panthera Geomean: {ipc_geomean:.3f}", xticklabels=[], xlabel=None, ylabel=None)
scurve_path = make_filepath("perfval_scurve.png")
plt.savefig(scurve_path, bbox_inches="tight")
