# Copyright (c) 2020-2025 Ampere Computing. All rights reserved.
# SPDX-License-Identifier: BSD-3-Clause

"""Store forward predictor"""

metric_map = {
    "sys.soc_0.core_0.lsu.uop_ld_st_prediction/sys.soc_0.core_0.lsu.ld_st_prediction:0": "skip",
    "sys.soc_0.core_0.lsu.uop_ld_st_prediction/sys.soc_0.core_0.lsu.ld_st_prediction:1": "wait",
}

fullnames = list(iter(metric_map))
metrics = []
for fullname in fullnames:
    if fullname in df.columns:
        metrics.append(fullname)

breakdown = rollup(df, metrics=metrics).rename(columns=metric_map)

df.loc[:, "ld_st_prediction_per_load"] = (
    df.loc[:, "sys.soc_0.core_0.lsu.uop_ld_st_prediction"] / df.loc[:, "sys.soc_0.core_0.lsu.uop_ld_execute"]
)

pki = rollup_and_pivot(df, "sys.soc_0.core_0.uop_ld_st_prediction.pki")
ratio = rollup_and_pivot(df, "ld_st_prediction_per_load")

create_plot(breakdown, "store_forward_predictor_breakdown.svg", stacked=True)
create_plot(ratio, "store_forward_predictor_ratio.svg")

print("\n==== Prediction Breakdown ====")
print(breakdown.mean(level=1))

print("\n==== Predictions per load executed ====")
print(ratio.mean())

print("\n==== Predictions per 1000 instructions ====")
print(pki.mean())
