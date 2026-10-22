# Copyright (c) 2019-2025 Ampere Computing. All rights reserved.
# SPDX-License-Identifier: BSD-3-Clause

"""Top Down Accounting"""

metrics = ["tda.l1.retired", "tda.l1.lost", "tda.l1.backend", "tda.l1.frontend"]

idx = df.columns[0]
bd = rollup(df, metrics)
bd.sort_values("tda.l1.retired", inplace=True)
bd.reset_index().set_index([CONFIGURATION, idx], inplace=True)
create_csv(bd, "tda.csv")
create_plot(bd, "tda.svg", stacked=True, title="Top Down Accounting - L1", y_label="% of slots")

print("\n==== Mean breakdown ====")
print(bd.mean())

metrics = [
    "tda.l1.retired",
    "tda.l1.lost",
    "tda.l1.backend",
    "tda.l1.frontend",
    "tda.l2.backend_memory",
    "tda.l3.backend_memory_dtlb",
    "tda.l3.backend_memory_dcache",
    "tda.l3.backend_memory_l2",
    "tda.l2.backend_core",
    "tda.l3.backend_core_resource",
    "tda.l4.backend_core_resource_rob",
    "tda.l4.backend_core_resource_ixu",
    "tda.l4.backend_core_resource_fsu",
    "tda.l4.backend_core_resource_lob",
    "tda.l4.backend_core_resource_sob",
    "tda.l2.frontend_latency",
    "tda.l3.frontend_latency_itlb",
    "tda.l3.frontend_latency_icache",
    "tda.l3.frontend_latency_recovery",
    "tda.l3.frontend_latency_bob",
    "tda.l2.frontend_bandwidth",
    "tda.l2.retired_pipe_utilization",
    "tda.l3.retired_pipe_utilization_ixu",
    "tda.l3.retired_pipe_utilization_fsu",
    "tda.l2.lost_branch_mispredict",
    "tda.l2.lost_other_clear",
]

idx = df.columns[0]
bd = rollup(df, metrics)
bd.sort_values("tda.l1.retired", inplace=True)
bd.reset_index().set_index([CONFIGURATION, idx], inplace=True)
create_csv(bd, "tda_full.csv")

print("\n==== Mean breakdown ====")
print(bd.mean())

metrics = [
    "tda.l2.backend_core",
    "tda.l3.backend_memory_dtlb",
    "tda.l3.backend_memory_dcache",
    "tda.l3.backend_memory_l2",
]
bd = rollup(df, metrics)
bd.sort_values("tda.l2.backend_core", inplace=True)
bd.reset_index().set_index([CONFIGURATION, idx], inplace=True)
create_csv(bd, "tda_backend_memory.csv")
create_plot(
    bd, "tda_backend_memory.svg", stacked=True, title="Top Down Accounting - L2: Backend_Memory", y_label="% of cycles"
)

print("\n==== Mean breakdown ====")
print(bd.mean())

metrics = [
    "tda.l2.backend_memory",
    "tda.l4.backend_core_resource_rob",
    "tda.l4.backend_core_resource_ixu",
    "tda.l4.backend_core_resource_fsu",
    "tda.l4.backend_core_resource_lob",
    "tda.l4.backend_core_resource_sob",
]
bd = rollup(df, metrics)
bd.sort_values("tda.l2.backend_memory", inplace=True)
bd.reset_index().set_index([CONFIGURATION, idx], inplace=True)
create_csv(bd, "tda_backend_core.csv")
create_plot(
    bd, "tda_backend_core.svg", stacked=True, title="Top Down Accounting - L2: Backend_Core", y_label="% of cycles"
)

print("\n==== Mean breakdown ====")
print(bd.mean())

metrics = [
    "tda.l2.frontend_bandwidth",
    "tda.l3.frontend_latency_itlb",
    "tda.l3.frontend_latency_icache",
    "tda.l3.frontend_latency_recovery",
    "tda.l3.frontend_latency_bob",
]
bd = rollup(df, metrics)
bd.sort_values("tda.l2.frontend_bandwidth", inplace=True)
bd.reset_index().set_index([CONFIGURATION, idx], inplace=True)
create_csv(bd, "tda_frontend_latency.csv")
create_plot(
    bd,
    "tda_frontend_latency.svg",
    stacked=True,
    title="Top Down Accounting - L2: Frontend_Latency",
    y_label="% of cycles",
)

print("\n==== Mean breakdown ====")
print(bd.mean())
