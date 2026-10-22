# Copyright (c) 2019-2025 Ampere Computing. All rights reserved.
# SPDX-License-Identifier: BSD-3-Clause

"""Uop allocation stall report"""

stall_metrics = list(
    df.filter(regex=r"^sys\.soc_0\.core_0\.idr\.backend_stall/sys\.soc_0\.core_0\.idr\.backend_stall_cause:.*").columns
)

all_stalls = rollup(df, stall_metrics)
all_stalls.rename(columns=lambda c: c.split(":")[-1], inplace=True)

# Stacked bar chart with all stall causes
create_plot(
    all_stalls,
    "stalls_detailed.svg",
    stacked=True,
    title="Backend Stall Causes",
    y_label="Count of backend stall causes",
)
create_csv(all_stalls, "stalls_detailed.csv")

# Stacked bar chart with IXU partitions summed
all_stalls.insert(0, "SchA", all_stalls.loc[:, "SchA_P0"] + all_stalls.loc[:, "SchA_P1"])
all_stalls.insert(1, "SchB", all_stalls.loc[:, "SchB_P0"] + all_stalls.loc[:, "SchB_P1"])
all_stalls.insert(2, "SchJ", all_stalls.loc[:, "SchJ_P0"] + all_stalls.loc[:, "SchJ_P1"])
all_stalls.insert(3, "SchK", all_stalls.loc[:, "SchK_P0"] + all_stalls.loc[:, "SchK_P1"])
all_stalls.insert(4, "SchM", all_stalls.loc[:, "SchM_P0"] + all_stalls.loc[:, "SchM_P1"])
all_stalls.insert(5, "SchN", all_stalls.loc[:, "SchN_P0"] + all_stalls.loc[:, "SchN_P1"])
stalls = all_stalls.drop(
    columns=[
        "SchA_P0",
        "SchA_P1",
        "SchB_P0",
        "SchB_P1",
        "SchJ_P0",
        "SchJ_P1",
        "SchK_P0",
        "SchK_P1",
        "SchM_P0",
        "SchM_P1",
        "SchN_P0",
        "SchN_P1",
    ],
    inplace=True,
)

create_plot(
    stalls,
    "stalls.svg",
    stacked=True,
    title="Backend Stall Causes by Workload",
    y_label="Count of backend stall causes",
)
create_csv(stalls, "stalls.csv")

print("\n==== Mean Dispatch Stalls per Trace ====")
print(stalls.mean())
